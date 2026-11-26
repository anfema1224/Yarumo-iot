#pragma once
#include <stdint.h>
#include <stddef.h>

namespace yarumo {
// Pure scan engine: no GPIO, networking, heap allocation or blocking calls.
// All node references must point backwards (topological order).
constexpr uint8_t MAX_NODES=48, DI_COUNT=6, AI_COUNT=4, DO_COUNT=4;
constexpr uint32_t MAX_TIME=600000, MAX_SCAN_GAP=250;
enum class Op : uint8_t {
  DI, AI_GE, AI_LE, CONST, NOT, AND, OR, TON, TOF, TP, RS, CTU, CTD
};
struct Node {
  Op op;
  uint8_t a=0, b=0;
  uint32_t p=0;
};
struct Program {
  uint8_t count=0;
  Node nodes[MAX_NODES]={};
  // -1 = unmapped/off. Otherwise index of a boolean node.
  int8_t output[DO_COUNT]={-1,-1,-1,-1};
};
struct Inputs {
  // Raw GPIO levels; use NOT for active-low contacts.
  bool di[DI_COUNT]={};
  uint16_t ai[AI_COUNT]={};
};
struct Outputs { bool relay[DO_COUNT]={}; };
enum class Mode : uint8_t { Stopped, Running, Fault };
enum class Error : uint8_t { None, InvalidProgram, ScanGap, InvalidInput };

class Engine {
  struct State {
    bool q=false, previous=false, timing=false;
    uint32_t start=0, counter=0;
  };
  Program program_{};
  State state_[MAX_NODES]={};
  Outputs output_{};
  Mode mode_=Mode::Stopped;
  Error error_=Error::None;
  bool installed_=false, scanned_=false;
  uint32_t last_=0;
  void clear() {
    output_=Outputs{};
    for(auto &s:state_) s=State{};
    scanned_=false;
  }
  bool fail(Error e) {
    clear(); mode_=Mode::Fault; error_=e; return false;
  }
  static bool prior(uint8_t n, uint8_t i) { return n<i; }
public:
  static bool valid(const Program &p) {
    if(!p.count || p.count>MAX_NODES) return false;
    for(uint8_t i=0;i<p.count;i++) {
      const auto &n=p.nodes[i];
      switch(n.op) {
        case Op::DI: if(n.a>=DI_COUNT) return false; break;
        case Op::AI_GE: case Op::AI_LE:
          if(n.a>=AI_COUNT || n.p>4095) return false; break;
        case Op::CONST: if(n.p>1) return false; break;
        case Op::NOT: if(!prior(n.a,i)) return false; break;
        case Op::AND: case Op::OR: case Op::RS:
          if(!prior(n.a,i)||!prior(n.b,i)) return false; break;
        case Op::TON: case Op::TOF: case Op::TP:
          if(!prior(n.a,i)||n.p<20||n.p>MAX_TIME) return false; break;
        case Op::CTU: case Op::CTD:
          if(!prior(n.a,i)||!prior(n.b,i)||!n.p||n.p>65535) return false; break;
        default: return false;
      }
    }
    for(int8_t o:p.output) if(o < -1 || o>=p.count) return false;
    return true;
  }
  // Installation never arms the controller. Invalid candidate leaves it faulted/off.
  bool install(const Program &p) {
    stop(); installed_=false;
    if(!valid(p)) return fail(Error::InvalidProgram);
    program_=p; installed_=true; return true;
  }
  void stop() { clear(); mode_=Mode::Stopped; error_=Error::None; }
  // Call only after deliberate operator authorization, never on reboot.
  bool arm(uint32_t now) {
    if(!installed_||mode_!=Mode::Stopped) return false;
    clear(); last_=now;
    for(uint8_t i=0;i<program_.count;i++)
      if(program_.nodes[i].op==Op::CTD) state_[i].counter=program_.nodes[i].p;
    mode_=Mode::Running; error_=Error::None; return true;
  }
  Mode mode() const { return mode_; }
  Error error() const { return error_; }
  const Outputs &outputs() const { return output_; }
  bool value(uint8_t i) const { return i<program_.count && mode_==Mode::Running && state_[i].q; }

  bool scan(uint32_t now,const Inputs &in) {
    if(mode_!=Mode::Running) return false;
    if(uint32_t(now-last_)>MAX_SCAN_GAP) return fail(Error::ScanGap);
    for(uint16_t a:in.ai) if(a>4095) return fail(Error::InvalidInput);
    last_=now;
    for(uint8_t i=0;i<program_.count;i++) {
      const Node &n=program_.nodes[i]; State &s=state_[i];
      const bool a=n.a<i && state_[n.a].q;
      const bool b=n.b<i && state_[n.b].q;
      switch(n.op) {
        case Op::DI: s.q=in.di[n.a]; break;
        case Op::AI_GE: s.q=in.ai[n.a]>=n.p; break;
        case Op::AI_LE: s.q=in.ai[n.a]<=n.p; break;
        case Op::CONST: s.q=n.p!=0; break;
        case Op::NOT: s.q=!a; break;
        case Op::AND: s.q=a&&b; break;
        case Op::OR: s.q=a||b; break;
        case Op::RS: s.q=!b && (a||s.q); break; // reset dominant
        case Op::TON:
          if(!a) {s.timing=false; s.q=false;}
          else {
            if(!s.timing){s.timing=true; s.start=now;}
            s.q=uint32_t(now-s.start)>=n.p;
          }
          break;
        case Op::TOF:
          if(a){s.q=true;s.timing=false;}
          else if(s.q){
            if(!s.timing){s.start=now;s.timing=true;}
            if(uint32_t(now-s.start)>=n.p){s.q=false;s.timing=false;}
          }
          break;
        case Op::TP: {
          // Non-retriggerable, rising edge; no artificial edge on initial scan.
          const bool edge=scanned_ && a && !s.previous;
          if(s.timing){
            if(uint32_t(now-s.start)>=n.p){s.timing=false;s.q=false;}
          } else if(edge){s.start=now;s.timing=true;s.q=true;}
          s.previous=a;
          break;
        }
        case Op::CTU: case Op::CTD: {
          const bool edge=scanned_ && a && !s.previous;
          const bool up=n.op==Op::CTU;
          if(b) s.counter=up?0:n.p;
          else if(edge) {
            if(up && s.counter<n.p) ++s.counter;
            if(!up && s.counter>0) --s.counter;
          }
          s.previous=a;
          s.q=up ? s.counter>=n.p : s.counter==0;
          break;
        }
        default: return fail(Error::InvalidProgram);
      }
    }
    for(uint8_t i=0;i<DO_COUNT;i++){
      const int8_t idx=program_.output[i];
      output_.relay[i]=idx>=0 && state_[idx].q;
    }
    scanned_=true;
    return true;
  }
};
}
