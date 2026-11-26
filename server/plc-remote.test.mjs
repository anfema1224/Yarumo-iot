import test from 'node:test';import assert from 'node:assert/strict';
import {prepareProgram,canTransfer} from './plc-remote.mjs';
const graph={schema:1,nodes:[{id:'in',op:'DI',p:1,x:0,y:0},{id:'out',op:'DO',p:1,x:100,y:0}],edges:[{from:'in',to:'out',port:0}]};
test('server compiles FBD and ignores untrusted extra data',()=>{const v=prepareProgram({...graph,nodes:graph.nodes.map(n=>({...n,evil:true}))});assert.equal(v.program.count,1);assert.deepEqual(v.program.output,[0,-1,-1,-1]);assert.equal(v.graph.nodes[0].evil,undefined);});
test('reject invalid coordinates and missing connections',()=>{assert.throws(()=>prepareProgram({...graph,nodes:[{...graph.nodes[0],x:-1},graph.nodes[1]]}));assert.throws(()=>prepareProgram({...graph,edges:[]}));});
test('transfer requires compatible STOP and all outputs off',()=>{const s={plc_transfer:true,plc_mode:'STOP',plc_simulation:false,relay_cmd:[0,0,0,0]};assert.ok(canTransfer(s));for(const patch of [{plc_transfer:false},{plc_mode:'RUN'},{plc_mode:'FAULT'},{plc_simulation:true},{relay_cmd:[0,1,0,0]},{relay_cmd:[0]}])assert.equal(canTransfer({...s,...patch}),false);});
