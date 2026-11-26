import test from 'node:test';
import assert from 'node:assert/strict';
import {compileGraph,Simulator} from '../public/plc/core.mjs';
function graph(op,p=0){
 const nodes=[{id:'a',op:'DI',p:1},{id:'b',op:'DI',p:2},{id:'logic',op,p},{id:'out',op:'DO',p:1}];
 const edges=[{from:'a',to:'logic',port:0},{from:'logic',to:'out',port:0}];
 if(['AND','OR','RS','CTU','CTD'].includes(op))edges.push({from:'b',to:'logic',port:1});
 return {schema:1,nodes,edges};
}
const di=(a=false,b=false)=>[a,b,false,false,false,false],ai=[0,0,0,0];
function run(op,p){const s=new Simulator(compileGraph(graph(op,p)));s.arm(0);return s;}
function scan(s,t,a,b,q){assert.equal(s.scan(t,di(a,b),ai),true);assert.equal(s.out[0],q);}
test('topological order, channels and output mapping',()=>{
 const g=graph('AND');g.nodes.reverse();const p=compileGraph(g);assert.equal(p.count,3);assert.equal(p.output[0],2);assert.deepEqual(p.output.slice(1),[-1,-1,-1]);assert.ok(p.nodes[2].a<2&&p.nodes[2].b<2);
});
test('reject invalid graph, missing input, cycle, duplicate DO, invalid parameter',()=>{
 for(const mutate of [g=>g.edges.pop(),g=>g.edges[0].from='logic',g=>g.nodes.push({id:'out2',op:'DO',p:1}),g=>g.nodes[0].p=7,g=>g.edges.push({...g.edges[0]}),g=>g.nodes[0].op='__proto__']){
 const g=graph('AND');mutate(g);assert.throws(()=>compileGraph(g));}
 assert.throws(()=>compileGraph(graph('TON',600001)));
 assert.throws(()=>compileGraph(graph('CTU',0)));
});
test('RS reset dominant and stop',()=>{const s=run('RS');scan(s,0,false,false,false);scan(s,20,true,false,true);scan(s,40,false,false,true);scan(s,60,true,true,false);s.stop();assert.deepEqual(s.out,[false,false,false,false]);});
test('TON, TOF, TP and scan fault',()=>{
 let s=run('TON',100);scan(s,0,false,false,false);scan(s,20,true,false,false);scan(s,119,true,false,false);scan(s,120,true,false,true);scan(s,140,false,false,false);
 s=run('TOF',100);scan(s,0,true,false,true);scan(s,20,false,false,true);scan(s,120,false,false,false);
 s=run('TP',100);scan(s,0,true,false,false);scan(s,20,false,false,false);scan(s,40,true,false,true);scan(s,140,true,false,false);
 assert.equal(s.scan(391,di(),ai),false);assert.equal(s.running,false);assert.deepEqual(s.out,[false,false,false,false]);
});
test('CTU and CTD edge counting',()=>{for(const op of ['CTU','CTD']){const s=run(op,2);scan(s,0,false,false,false);scan(s,20,true,false,false);scan(s,40,true,false,false);scan(s,60,false,false,false);scan(s,80,true,false,true);scan(s,100,false,true,false);}});
test('analog comparator and invalid input',()=>{const g={schema:1,nodes:[{id:'a',op:'AI_GE',p:2000,channel:2},{id:'o',op:'DO',p:4}],edges:[{from:'a',to:'o',port:0}]};const s=new Simulator(compileGraph(g));s.arm(0);assert.ok(s.scan(0,di(),[0,1999,0,0]));assert.equal(s.out[3],false);assert.ok(s.scan(20,di(),[0,2000,0,0]));assert.equal(s.out[3],true);assert.equal(s.scan(40,di(),[0,4096,0,0]),false);});
