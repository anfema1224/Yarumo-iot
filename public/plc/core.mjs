// FBD schema 1, pure compiler and browser simulator. No network or GPIO access.
export const OPS=Object.freeze({
 DI:{ports:[],param:'canal',min:1,max:6}, AI_GE:{ports:[],param:'umbral ADC',min:0,max:4095,analog:true}, AI_LE:{ports:[],param:'umbral ADC',min:0,max:4095,analog:true},
 CONST:{ports:[],param:'valor',min:0,max:1}, NOT:{ports:['IN']}, AND:{ports:['A','B']}, OR:{ports:['A','B']},
 TON:{ports:['IN'],param:'ms',min:20,max:600000}, TOF:{ports:['IN'],param:'ms',min:20,max:600000}, TP:{ports:['IN'],param:'ms',min:20,max:600000},
 RS:{ports:['S','R']}, CTU:{ports:['CU','R'],param:'PV',min:1,max:65535}, CTD:{ports:['CD','LD'],param:'PV',min:1,max:65535}, DO:{ports:['IN'],param:'canal',min:1,max:4}
});
const bounded=(v,min,max)=>Number.isInteger(v)&&v>=min&&v<=max;
const idOk=id=>typeof id==='string'&&/^[A-Za-z0-9_-]{1,32}$/.test(id);
const fail=s=>{throw new Error(s)};
export function compileGraph(graph){
 if(!graph||graph.schema!==1||!Array.isArray(graph.nodes)||!Array.isArray(graph.edges))fail('Formato FBD inválido');
 if(graph.nodes.length<1||graph.nodes.length>52||graph.edges.length>104)fail('Límite: 48 bloques lógicos y 4 salidas');
 const byId=new Map(),incoming=new Map(),outputs=new Set();
 for(const n of graph.nodes){
  if(!n||!idOk(n.id)||byId.has(n.id)||!Object.hasOwn(OPS,n.op))fail('Bloque o identificador inválido');
  const spec=OPS[n.op];
  if(spec.param&&!bounded(n.p,spec.min,spec.max))fail(n.id+': parámetro fuera de rango');
  if(spec.analog&&!bounded(n.channel,1,4))fail(n.id+': canal analógico inválido');
  if(n.op==='DO'){if(outputs.has(n.p))fail('Salida DO'+n.p+' duplicada');outputs.add(n.p);}
  byId.set(n.id,n);incoming.set(n.id,new Map());
 }
 if(!outputs.size)fail('Agrega al menos una salida DO');
 for(const e of graph.edges){
  const a=byId.get(e?.from),b=byId.get(e?.to);
  if(!a||!b||a.op==='DO'||!bounded(e.port,0,OPS[b.op].ports.length-1))fail('Conexión inválida');
  const ports=incoming.get(b.id);
  if(ports.has(e.port))fail(b.id+': entrada duplicada');
  ports.set(e.port,a.id);
 }
 const ordered=[],mark=new Map();
 function visit(id){
  if(mark.get(id)===1)fail('Ciclo combinacional no permitido');
  if(mark.get(id)===2)return;
  mark.set(id,1);const n=byId.get(id),ports=incoming.get(id);
  for(let i=0;i<OPS[n.op].ports.length;i++){if(!ports.has(i))fail(id+': falta '+OPS[n.op].ports[i]);visit(ports.get(i));}
  mark.set(id,2);if(n.op!=='DO')ordered.push(n);
 }
 for(const id of byId.keys())visit(id);
 if(ordered.length<1||ordered.length>48)fail('Límite de 48 bloques lógicos');
 const index=new Map(ordered.map((n,i)=>[n.id,i]));
 const nodes=ordered.map(n=>{
  const ports=incoming.get(n.id);
  const a=n.op==='DI'?n.p-1:OPS[n.op].analog?n.channel-1:(ports.has(0)?index.get(ports.get(0)):0);
  const b=ports.has(1)?index.get(ports.get(1)):0;
  return {op:n.op,a,b,p:n.op==='DI'||!OPS[n.op].param?0:n.p};
 });
 const output=[-1,-1,-1,-1];for(const n of graph.nodes)if(n.op==='DO')output[n.p-1]=index.get(incoming.get(n.id).get(0));
 return {schema:1,count:nodes.length,nodes,output,order:ordered.map(n=>n.id)};
}
export class Simulator{
 constructor(program){this.program=program;this.stop();}
 stop(){this.running=false;this.scanned=false;this.last=0;this.q=this.program.nodes.map(()=>false);this.state=this.program.nodes.map(n=>({previous:false,timing:false,start:0,counter:n.op==='CTD'?n.p:0}));this.out=[false,false,false,false];}
 arm(now){this.stop();this.running=true;this.last=now;}
 scan(now,di,ai){
  if(!this.running)return false;
  if(!Number.isFinite(now)||now<this.last||now-this.last>250||!Array.isArray(di)||di.length!==6||di.some(x=>typeof x!=='boolean')||!Array.isArray(ai)||ai.length!==4||ai.some(x=>!bounded(x,0,4095))){this.stop();return false;}
  this.last=now;
  for(let i=0;i<this.program.count;i++){
   const n=this.program.nodes[i],s=this.state[i],a=!!this.q[n.a],b=!!this.q[n.b];let q=this.q[i];
   switch(n.op){
    case 'DI':q=di[n.a];break;
    case 'AI_GE':q=ai[n.a]>=n.p;break;
    case 'AI_LE':q=ai[n.a]<=n.p;break;
    case 'CONST':q=n.p===1;break;
    case 'NOT':q=!a;break;
    case 'AND':q=a&&b;break;
    case 'OR':q=a||b;break;
    case 'RS':q=!b&&(a||q);break;
    case 'TON':if(!a){s.timing=false;q=false;}else{if(!s.timing){s.timing=true;s.start=now;}q=now-s.start>=n.p;}break;
    case 'TOF':if(a){q=true;s.timing=false;}else if(q){if(!s.timing){s.start=now;s.timing=true;}if(now-s.start>=n.p){q=false;s.timing=false;}}break;
    case 'TP':if(s.timing){if(now-s.start>=n.p){s.timing=false;q=false;}}else if(this.scanned&&a&&!s.previous){s.start=now;s.timing=true;q=true;}s.previous=a;break;
    case 'CTU':case 'CTD':{const up=n.op==='CTU';if(b)s.counter=up?0:n.p;else if(this.scanned&&a&&!s.previous)s.counter=up?Math.min(n.p,s.counter+1):Math.max(0,s.counter-1);s.previous=a;q=up?s.counter>=n.p:s.counter===0;break;}
    default:this.stop();return false;
   }
   this.q[i]=q;
  }
  this.out=this.program.output.map(i=>i>=0&&this.q[i]);this.scanned=true;return true;
 }
}
