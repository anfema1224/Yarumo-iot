#include "../YarumoPlc.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace yarumo;
static Program p1(Op op,uint32_t param=0){
  Program p; p.count=3;
  p.nodes[0]={Op::DI,0,0,0};p.nodes[1]={Op::DI,1,0,0};
  p.nodes[2]={op,0,1,param};p.output[0]=2;
  return p;
}
static void begin(Engine &e,const Program &p,uint32_t t=0){
  assert(e.install(p));assert(!e.outputs().relay[0]);assert(e.arm(t));
}
static void scan(Engine &e,uint32_t t,const Inputs &in,bool expect){
  assert(e.scan(t,in));assert(e.outputs().relay[0]==expect);
  assert(!e.outputs().relay[1]&&!e.outputs().relay[2]&&!e.outputs().relay[3]);
}
int main(){
  Engine e; Inputs in;
  for(Op op:{Op::AND,Op::OR,Op::NOT,Op::RS}){
    begin(e,p1(op));in={};
    scan(e,0,in,op==Op::NOT);
    in.di[0]=true;scan(e,20,in,op==Op::OR||op==Op::RS);
    in.di[1]=true;scan(e,40,in,op==Op::AND||op==Op::OR);
    in.di[0]=false;in.di[1]=false;scan(e,60,in,op==Op::NOT);
    e.stop();assert(!e.outputs().relay[0]);
  }
  begin(e,p1(Op::RS));in={};scan(e,0,in,false);in.di[0]=true;scan(e,20,in,true);
  in.di[0]=false;scan(e,40,in,true);in.di[1]=true;scan(e,60,in,false);
  begin(e,p1(Op::TON,100));in={};scan(e,0,in,false);
  in.di[0]=true;scan(e,20,in,false);scan(e,119,in,false);scan(e,120,in,true);
  in.di[0]=false;scan(e,140,in,false);
  begin(e,p1(Op::TOF,100));in={};in.di[0]=true;scan(e,0,in,true);
  in.di[0]=false;scan(e,20,in,true);scan(e,119,in,true);scan(e,120,in,false);
  begin(e,p1(Op::TP,100));in={};in.di[0]=true;scan(e,0,in,false);
  in.di[0]=false;scan(e,20,in,false);in.di[0]=true;scan(e,40,in,true);
  in.di[0]=false;scan(e,60,in,true);in.di[0]=true;scan(e,80,in,true);
  scan(e,140,in,false);scan(e,160,in,false);
  for(Op op:{Op::CTU,Op::CTD}){
    begin(e,p1(op,2));in={};scan(e,0,in,false);
    in.di[0]=true;scan(e,20,in,false);scan(e,40,in,false);
    in.di[0]=false;scan(e,60,in,false);in.di[0]=true;scan(e,80,in,true);
    in.di[1]=true;scan(e,100,in,false);in.di[1]=false;scan(e,120,in,false);
  }
  for(Op op:{Op::AI_GE,Op::AI_LE}){
    Program p;p.count=1;p.nodes[0]={op,0,0,2000};p.output[0]=0;
    begin(e,p);in={};in.ai[0]=1999;scan(e,0,in,op==Op::AI_LE);
    in.ai[0]=2000;scan(e,20,in,true);
    in.ai[0]=2001;scan(e,40,in,op==Op::AI_GE);
    in.ai[0]=4096;assert(!e.scan(60,in));assert(e.mode()==Mode::Fault);
    assert(!e.outputs().relay[0]);
  }
  begin(e,p1(Op::TON,100),UINT32_MAX-49);in={};in.di[0]=true;
  scan(e,UINT32_MAX-49,in,false);scan(e,50,in,true);
  assert(!e.scan(301,in));assert(e.error()==Error::ScanGap);
  assert(!e.outputs().relay[0]);assert(!e.arm(302));e.stop();assert(e.arm(303));
  assert(!e.outputs().relay[0]);assert(!e.scan(554,in));
  auto p=p1(Op::AND);p.nodes[2].a=2;assert(!e.install(p));
  assert(!e.arm(0));assert(!e.outputs().relay[0]);
  p=p1(Op::TON,0);assert(!Engine::valid(p));p=p1(Op::CTU,0);assert(!Engine::valid(p));
  p=p1(Op::OR);p.output[1]=3;assert(!Engine::valid(p));
  p=p1(Op::OR);p.nodes[0].a=6;assert(!Engine::valid(p));
  p=p1(Op::OR);p.nodes[0].op=static_cast<Op>(255);assert(!Engine::valid(p));
  p={};assert(!Engine::valid(p));p.count=49;assert(!Engine::valid(p));
  p={};p.count=1;p.nodes[0]={Op::CONST,0,0,1};p.output[3]=0;
  begin(e,p);in={};assert(e.scan(0,in));assert(e.outputs().relay[3]);e.stop();assert(!e.outputs().relay[3]);
  std::puts("PLC: logic, timers, counters, validation, rollover and fail-off OK");
}
