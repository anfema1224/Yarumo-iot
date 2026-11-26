import {OPS,compileGraph,Simulator} from './core.mjs';
const $=id=>document.getElementById(id), el=(tag,txt,cls)=>{const e=document.createElement(tag);if(txt!==undefined)e.textContent=txt;if(cls)e.className=cls;return e;};
const button=(txt,fn,cls)=>{const e=el('button',txt,cls);e.type='button';e.onclick=fn;return e;};
const blank=()=>({schema:1,nodes:[],edges:[]});
let graph=blank(),selected=null,source=null,sim=null,compiled=null,seq=0,di=Array(6).fill(false),ai=Array(4).fill(0),timer=null;
const nodes=new Map(),ports=new Map(),lines=[];
function msg(s){$('msg').textContent=s;}
function find(id){return graph.nodes.find(n=>n.id===id);}
const editing=()=>!sim?.running;
function add(op){if(!editing())return;if(graph.nodes.length>=52)return msg('Límite de bloques.');const spec=OPS[op];if(op==='DO'&&graph.nodes.filter(n=>n.op==='DO').length===4)return msg('Máximo cuatro salidas.');if(op!=='DO'&&graph.nodes.filter(n=>n.op!=='DO').length===48)return msg('Máximo 48 bloques lógicos.');const i=graph.nodes.length;let id;do{id='b'+(++seq);}while(find(id));const n={id,op,x:20+Math.floor(i/6)%7*190,y:20+(i%6)*138,p:0,channel:1};
 if(spec.param)n.p=spec.min;
 if(['TON','TOF','TP'].includes(op))n.p=2000;
 if(op==='CTU'||op==='CTD')n.p=3;
 if(op==='DO')n.p=[1,2,3,4].find(v=>!graph.nodes.some(k=>k.op==='DO'&&k.p===v));
 graph.nodes.push(n);selected=id;redraw();msg('Bloque '+op+' agregado.');
}
function connect(id,port){if(!editing())return;if(!source)return msg('Selecciona primero un puerto Q.');if(source===id)return msg('No se admite una conexión al mismo bloque.');graph.edges=graph.edges.filter(e=>e.to!==id||e.port!==port);graph.edges.push({from:source,to:id,port});source=null;redraw();msg('Conexión creada. Valida el programa antes de RUN.');}
function drawWires(){const svg=$('wires'),board=$('board').getBoundingClientRect();svg.replaceChildren();lines.length=0;
 for(const e of graph.edges){const a=ports.get(e.from+':q'),b=ports.get(e.to+':'+e.port);if(!a||!b)continue;
  const ar=a.getBoundingClientRect(),br=b.getBoundingClientRect(),x=ar.right-board.left,y=(ar.top+ar.bottom)/2-board.top,X=br.left-board.left,Y=(br.top+br.bottom)/2-board.top;
  const path=document.createElementNS('http://www.w3.org/2000/svg','path'),bend=Math.max(36,Math.abs(X-x)*.45);
  path.setAttribute('d','M '+x+' '+y+' C '+(x+bend)+' '+y+', '+(X-bend)+' '+Y+', '+X+' '+Y);path.setAttribute('class','wire');svg.append(path);lines.push({path,from:e.from});
 }
}
function select(id){selected=id;for(const [key,node] of nodes)node.classList.toggle('selected',key===id);properties();}
function redraw(){nodes.clear();ports.clear();$('blocks').replaceChildren();
 for(const n of graph.nodes){const spec=OPS[n.op],box=el('div',undefined,'block');box.style.left=n.x+'px';box.style.top=n.y+'px';box.classList.toggle('selected',selected===n.id);nodes.set(n.id,box);
  const head=el('div',undefined,'head');head.append(el('span',n.op+(n.op==='DI'||n.op==='DO'?' '+n.p:'')),el('small',n.id));box.append(head);
  head.onpointerdown=e=>{select(n.id);if(!editing()||e.button!==0)return;head.setPointerCapture(e.pointerId);const x=e.clientX,y=e.clientY,nx=n.x,ny=n.y;head.onpointermove=v=>{n.x=Math.round(Math.max(0,Math.min(1240,nx+v.clientX-x)));n.y=Math.round(Math.max(0,Math.min(760,ny+v.clientY-y)));box.style.left=n.x+'px';box.style.top=n.y+'px';drawWires();};head.onpointerup=head.onpointercancel=()=>{head.onpointermove=null;};};
  const row=el('div',undefined,'ports'),ins=el('div',undefined,'inputs');spec.ports.forEach((label,i)=>{const p=button(label,()=>connect(n.id,i),'port');p.disabled=!editing();ports.set(n.id+':'+i,p);ins.append(p);});row.append(ins);
  if(n.op!=='DO'){const q=button('Q',()=>{if(!editing())return;source=source===n.id?null:n.id;redraw();if(source)msg('Selecciona una entrada para conectar '+n.id+'.');},'port');q.classList.toggle('chosen',source===n.id);q.disabled=!editing();ports.set(n.id+':q',q);row.append(q);}
  box.append(row);if(spec.param)box.append(el('div',(spec.analog?'AI'+n.channel+' · ':'')+spec.param+': '+n.p,'param'));$('blocks').append(box);
 }
 properties();drawWires();paint();
}
function properties(){const p=$('properties'),c=$('connections');p.replaceChildren();c.replaceChildren();const n=find(selected);
 if(!n)p.textContent='Selecciona un bloque.';
 else{p.append(el('strong',n.op+' · '+n.id));const spec=OPS[n.op];
  const field=(name,key,min,max)=>{const l=el('label',name),inp=el('input');inp.type='number';inp.min=min;inp.max=max;inp.step=1;inp.value=n[key];inp.disabled=!editing();inp.onchange=()=>{const v=Number(inp.value);if(!inp.value||!Number.isInteger(v)||v<min||v>max){inp.value=n[key];msg('Valor fuera de rango.');return;}n[key]=v;redraw();};l.append(inp);p.append(l);};
  if(spec.param)field(spec.param,'p',spec.min,spec.max);if(spec.analog)field('Canal AI','channel',1,4);
  const del=button('Eliminar bloque',()=>{if(!editing())return;graph.nodes=graph.nodes.filter(x=>x.id!==n.id);graph.edges=graph.edges.filter(e=>e.from!==n.id&&e.to!==n.id);selected=null;if(source===n.id)source=null;redraw();});del.disabled=!editing();p.append(del);
 }
 for(const e of graph.edges){if(selected&&e.to!==selected&&e.from!==selected)continue;const b=button(e.from+' → '+e.to+'.'+OPS[find(e.to).op].ports[e.port]+' ×',()=>{if(!editing())return;graph.edges=graph.edges.filter(x=>x!==e);redraw();});b.disabled=!editing();c.append(b);}
 if(!c.childNodes.length)c.textContent='Sin conexiones seleccionadas.';
}
function paint(){const running=!!sim?.running;$('mode').textContent=running?'RUN':'STOP';$('run').disabled=running;$('stop').disabled=!running;$('example').disabled=$('clear').disabled=$('import').disabled=running;$('palette').querySelectorAll('button').forEach(b=>b.disabled=running);
 const vals=new Map(compiled?.order.map((id,i)=>[id,!!(running&&sim.q[i])])||[]);
 for(const [id,box] of nodes){const n=find(id),q=n.op==='DO'?!!(running&&sim.out[n.p-1]):!!vals.get(id);box.classList.toggle('active',q);ports.get(id+':q')?.classList.toggle('on',q);}
 lines.forEach(e=>e.path.classList.toggle('on',!!vals.get(e.from)));
 $('di').querySelectorAll('button').forEach((b,i)=>{b.textContent='DI'+(i+1)+' '+(di[i]?'HIGH':'LOW');b.classList.toggle('on',di[i]);});
 $('outputs').querySelectorAll('.lamp').forEach((b,i)=>{const q=!!(running&&sim.out[i]);b.textContent='DO'+(i+1)+' '+(q?'ON':'OFF');b.classList.toggle('on',q);});
}
function check(){try{const p=compileGraph(graph);msg('Programa válido: '+p.count+' bloques lógicos.');return p;}catch(e){msg(e.message);return null;}}
function stop(message='STOP · salidas virtuales apagadas.'){clearInterval(timer);timer=null;sim?.stop();redraw();msg(message);}
function run(){const p=check();if(!p)return;compiled=p;sim=new Simulator(p);sim.arm(performance.now());source=null;redraw();timer=setInterval(()=>{if(!sim.scan(performance.now(),di,ai)){stop('Fallo de ciclo: simulación detenida.');return;}paint();},20);msg('RUN · solo simulación virtual.');}
function example(){if(!editing())return;graph={schema:1,nodes:[{id:'b1',op:'DI',p:1,x:40,y:100},{id:'b2',op:'TON',p:2000,x:270,y:100},{id:'b3',op:'DO',p:1,x:500,y:100}],edges:[{from:'b1',to:'b2',port:0},{from:'b2',to:'b3',port:0}]};seq=3;selected=null;source=null;compiled=null;sim=null;redraw();}
$('example').onclick=example;$('clear').onclick=()=>{if(!editing())return;graph=blank();selected=source=compiled=sim=null;redraw();msg('Programa nuevo.');};$('validate').onclick=check;$('run').onclick=run;$('stop').onclick=()=>stop();
$('save').onclick=()=>{const p=check();if(!p)return;const a=el('a');const url=URL.createObjectURL(new Blob([JSON.stringify({schema:1,graph,program:p},null,2)],{type:'application/json'}));a.href=url;a.download='yarumo-fbd.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000);};
$('import').onchange=async e=>{try{if(!editing())return;const f=e.target.files[0];if(!f)return;if(f.size>100000)throw Error('Archivo demasiado grande');const g=JSON.parse(await f.text()).graph;if(!editing())throw Error('Detén la simulación antes de importar');compileGraph(g);for(const n of g.nodes){if(!Number.isFinite(n.x)||!Number.isFinite(n.y)||n.x<0||n.x>1240||n.y<0||n.y>760)throw Error('Posición de bloque inválida');}graph=g;seq=0;selected=source=compiled=sim=null;redraw();msg('Programa importado y validado.');}catch(err){msg(err.message);}finally{e.target.value='';}};
for(const op of Object.keys(OPS))$('palette').append(button(op,()=>add(op)));
di.forEach((_,i)=>$('di').append(button('DI'+(i+1)+' LOW',()=>{di[i]=!di[i];paint();})));
ai.forEach((_,i)=>{const l=el('label','AI'+(i+1)+' '),v=el('span','0'),r=el('input');r.type='range';r.min=0;r.max=4095;r.value=0;r.oninput=()=>{ai[i]=Number(r.value);v.textContent=r.value;};l.append(v,r);$('ai').append(l);});
for(let i=0;i<4;i++)$('outputs').append(el('span','DO'+(i+1)+' OFF','lamp'));
example();

let remoteDevices=[],remoteBusy=false,remotePolling=false,remoteData=null;
const remoteMessage=s=>$('remoteMsg').textContent=s;
async function request(path,body){
 const opts={cache:'no-store',signal:AbortSignal.timeout(8000)};
 if(body!==undefined){opts.method='POST';opts.headers={'Content-Type':'application/json','X-Yarumo-UI':'1'};opts.body=JSON.stringify(body);}
 const r=await fetch(path,opts);const data=await r.json().catch(()=>({}));
 if(!r.ok)throw Error(data.error||'HTTP '+r.status);
 return data;
}
function target(){return remoteDevices.find(d=>d.id===$('target').value);}
function route(id){return '/api/devices/'+encodeURIComponent(id)+'/plc';}
function sameGraph(a,b){
 try{
  const normalize=g=>JSON.stringify({schema:1,nodes:g.nodes.map(n=>{const x={id:n.id,op:n.op,x:n.x,y:n.y};if(OPS[n.op].param)x.p=n.p;if(OPS[n.op].analog)x.channel=n.channel;return x;}),edges:g.edges.map(e=>({from:e.from,to:e.to,port:e.port}))});
  return normalize(a)===normalize(b);
 }catch{return false;}
}
function paintRemote(){
 const d=target(),s=d?.snapshot,p=remoteData?.saved,j=remoteData?.jobs?.[0];
 const compatible=s?.plc_transfer===true;
 const ready=compatible&&d?.online&&s.plc_mode==='STOP'&&!s.plc_simulation&&s.relay_cmd?.every(v=>v===0);
 $('remoteState').textContent=d?(d.online?'En línea':'Sin conexión')+' · FW '+(s?.version||'—')+' · PLC '+(s?.plc_mode||'—')+' · Guardado: '+(p?.revision||'ninguno')+' · Instalado: '+(s?.plc_revision||'sin revisión')+' · Última transferencia: '+(j?.state||'—')+(compatible?'':' · Requiere firmware 0.5'):'Sin dispositivos vinculados';
 $('target').disabled=remoteBusy;$('reloadRemote').disabled=remoteBusy;
 $('loadRemote').disabled=remoteBusy||!p||!editing();
 $('saveRemote').disabled=remoteBusy||!d||!editing();
 $('transferRemote').disabled=remoteBusy||!ready||!p||!editing();
 $('stopRemote').disabled=remoteBusy||!d?.online;
}
async function loadState(){
 if(remoteBusy||remotePolling)return;remotePolling=true;
 try{
  const selected=$('target').value;remoteDevices=await request('/api/devices');
  $('target').replaceChildren(...remoteDevices.map(d=>{const o=el('option',d.name+' · '+d.hardware_id);o.value=d.id;return o;}));
  if(remoteDevices.some(d=>d.id===selected))$('target').value=selected;
  const id=$('target').value;remoteData=null;if(id){const result=await request(route(id));if($('target').value===id)remoteData=result;}
 }catch(e){remoteData=null;remoteMessage('No se pudo consultar: '+e.message);}
 finally{remotePolling=false;paintRemote();}
}
async function remoteAction(action){
 if(remoteBusy)return;if(remotePolling&&action!=='stop')return remoteMessage('Actualizando estado, vuelve a pulsar.');const d=target();if(!d)return;
 if(action!=='stop'&&!editing())return remoteMessage('Detén primero la simulación virtual.');
 if(action==='load'){if(!remoteData?.saved)return;if(!confirm('¿Reemplazar el diagrama actual por el programa guardado?'))return;graph=structuredClone(remoteData.saved.graph);seq=0;selected=source=compiled=sim=null;redraw();return remoteMessage('Programa abierto.');}
 if(action==='save'&&!check())return;
 if(action==='transfer'){
  if(!remoteData?.saved||!sameGraph(graph,remoteData.saved.graph))return remoteMessage('Guarda primero los cambios del diagrama.');
  if(!confirm('¿Instalar esta revisión en '+d.name+'? El PLC debe estar en STOP y todas las salidas apagadas.'))return;
 }
 remoteBusy=true;paintRemote();
 try{
  if(action==='save'){const r=await request(route(d.id)+'/save',{graph});remoteMessage('Guardado: '+r.revision);}
  if(action==='transfer'){const r=await request(route(d.id)+'/transfer',{revision:remoteData.saved.revision});remoteMessage('Transferencia en cola: '+r.id+' · consulta la confirmación y revisión instalada.');}
  if(action==='stop'){const r=await request('/api/devices/'+encodeURIComponent(d.id)+'/commands',{action:'all',relay:0,ms:0});remoteMessage('STOP físico en cola: '+r.id+' · verifica que las salidas se apaguen.');}
 }catch(e){remoteMessage(e.message);}finally{remoteBusy=false;await loadState();}
}
$('target').onchange=()=>{remoteData=null;paintRemote();loadState();};
$('reloadRemote').onclick=loadState;$('loadRemote').onclick=()=>remoteAction('load');$('saveRemote').onclick=()=>remoteAction('save');$('transferRemote').onclick=()=>remoteAction('transfer');$('stopRemote').onclick=()=>remoteAction('stop');
paintRemote();loadState();setInterval(()=>{paintRemote();loadState();},3000);
