import {randomUUID} from 'node:crypto';
import {compileGraph,OPS} from '../public/plc/core.mjs';
import {isUuid} from './validation.mjs';

export function prepareProgram(graph){
 const p=compileGraph(graph);
 // Persist only known fields; do not trust client-supplied compiled bytecode.
 const nodes=graph.nodes.map(n=>{
  if(!Number.isInteger(n.x)||!Number.isInteger(n.y)||n.x<0||n.x>1240||n.y<0||n.y>760)throw Error('Posición inválida');
  const clean={id:n.id,op:n.op,x:n.x,y:n.y};
  if(OPS[n.op].param)clean.p=n.p;
  if(OPS[n.op].analog)clean.channel=n.channel;
  return clean;
 });
 const edges=graph.edges.map(e=>({from:e.from,to:e.to,port:e.port}));
 const {schema,count,output}=p;const program={schema,count,nodes:p.nodes,output};
 if(Buffer.byteLength(JSON.stringify(program))>4096)throw Error('Programa demasiado grande');
 return {graph:{schema:1,nodes,edges},program};
}
export function canTransfer(s){
 return s?.plc_transfer===true&&s.plc_mode==='STOP'&&s.plc_simulation!==true&&Array.isArray(s.relay_cmd)&&s.relay_cmd.length===4&&s.relay_cmd.every(v=>v===0);
}
export function registerPlcRoutes(app,{pool,auth,csrf,asyncRoute,throttle}){
 app.get('/api/devices/:id/plc',auth,asyncRoute(async(req,res)=>{
  if(!isUuid(req.params.id))return res.sendStatus(400);
  const d=await pool.query('SELECT id FROM devices WHERE id=$1 AND owner_id=$2',[req.params.id,req.user.id]);
  if(!d.rowCount)return res.sendStatus(404);
  const p=await pool.query('SELECT revision,graph,updated_at FROM plc_programs WHERE device_id=$1',[req.params.id]);
  const j=await pool.query('SELECT id,revision,state,created_at,finished_at FROM plc_jobs WHERE device_id=$1 ORDER BY created_at DESC LIMIT 10',[req.params.id]);
  res.json({saved:p.rows[0]||null,jobs:j.rows});
 }));
 app.post('/api/devices/:id/plc/save',csrf,auth,throttle('plcsave:',30,60000),asyncRoute(async(req,res)=>{
  if(!isUuid(req.params.id))return res.sendStatus(400);
  let v;try{v=prepareProgram(req.body?.graph);}catch(e){return res.status(400).json({error:e.message});}
  const c=await pool.connect();try{await c.query('BEGIN');
   const d=await c.query('SELECT id FROM devices WHERE id=$1 AND owner_id=$2 FOR UPDATE',[req.params.id,req.user.id]);
   if(!d.rowCount){await c.query('ROLLBACK');return res.sendStatus(404);}
   const revision=randomUUID();
   await c.query('INSERT INTO plc_programs(device_id,revision,graph,program) VALUES($1,$2,$3,$4) ON CONFLICT(device_id) DO UPDATE SET revision=EXCLUDED.revision,graph=EXCLUDED.graph,program=EXCLUDED.program,updated_at=now()',[req.params.id,revision,JSON.stringify(v.graph),JSON.stringify(v.program)]);
   await c.query("UPDATE plc_jobs SET state='cancelled',finished_at=now() WHERE device_id=$1 AND state='queued'",[req.params.id]);
   await c.query('COMMIT');res.json({revision});
  }catch(e){await c.query('ROLLBACK');throw e;}finally{c.release();}
 }));
 app.post('/api/devices/:id/plc/transfer',csrf,auth,throttle('plcjob:',20,60000),asyncRoute(async(req,res)=>{
  if(!isUuid(req.params.id)||!isUuid(req.body?.revision))return res.sendStatus(400);
  const c=await pool.connect();try{await c.query('BEGIN');
   const d=await c.query("SELECT snapshot,COALESCE(last_seen>now()-interval '15 seconds',false) AS online FROM devices WHERE id=$1 AND owner_id=$2 FOR UPDATE",[req.params.id,req.user.id]);
   if(!d.rowCount){await c.query('ROLLBACK');return res.sendStatus(404);}
   if(!d.rows[0].online||!canTransfer(d.rows[0].snapshot)){await c.query('ROLLBACK');return res.status(409).json({error:'ESP32 compatible, conectado, en STOP y con todas las salidas apagadas requerido'});}
   const p=await c.query('SELECT revision,program FROM plc_programs WHERE device_id=$1 AND revision=$2',[req.params.id,req.body.revision]);
   if(!p.rowCount){await c.query('ROLLBACK');return res.status(409).json({error:'Guarda la revisión actual antes de transferir'});}
   await expirePlc(c,req.params.id);
   const pending=await c.query("SELECT 1 FROM plc_jobs WHERE device_id=$1 AND state IN ('queued','delivered')",[req.params.id]);
   if(pending.rowCount){await c.query('ROLLBACK');return res.status(409).json({error:'Transferencia pendiente'});}
   const manual=await c.query("SELECT 1 FROM commands WHERE device_id=$1 AND state IN ('queued','delivered') AND expires_at>now()",[req.params.id]);
   if(manual.rowCount){await c.query('ROLLBACK');return res.status(409).json({error:'Hay una orden manual pendiente'});}
   const id=randomUUID();await c.query("INSERT INTO plc_jobs(id,device_id,revision,program,state,expires_at) VALUES($1,$2,$3,$4,'queued',now()+interval '10 seconds')",[id,req.params.id,p.rows[0].revision,JSON.stringify(p.rows[0].program)]);
   await c.query('COMMIT');res.status(202).json({id,revision:p.rows[0].revision,state:'queued'});
  }catch(e){await c.query('ROLLBACK');throw e;}finally{c.release();}
 }));
}
export async function expirePlc(c,id){
 await c.query("UPDATE plc_jobs SET state='expired',finished_at=now() WHERE device_id=$1 AND state='queued' AND expires_at<=now()",[id]);
 await c.query("UPDATE plc_jobs SET state='unknown',finished_at=now() WHERE device_id=$1 AND state='delivered' AND delivered_at<now()-interval '30 seconds'",[id]);
}
export async function pollPlc(c,id,s,ack){
 if(ack&&isUuid(ack.id)&&['executed','rejected'].includes(ack.state)){
  // A successful transfer is acknowledged only with the installed revision reported by the device.
  await c.query("UPDATE plc_jobs SET state=CASE WHEN $1='executed' AND revision::text<>$4 THEN 'unknown' ELSE $1 END,finished_at=now() WHERE id=$2 AND device_id=$3 AND state='delivered'",[ack.state,ack.id,id,s.plc_revision||'']);
 }
 await expirePlc(c,id);
 const r=await c.query("SELECT id,revision,program,GREATEST(0,floor(extract(epoch FROM (expires_at-clock_timestamp()))*1000))::int AS ttl_ms FROM plc_jobs WHERE device_id=$1 AND state='queued' AND expires_at>clock_timestamp() ORDER BY created_at LIMIT 1 FOR UPDATE",[id]);
 if(!r.rowCount)return null;
 const j=r.rows[0];
 if(!canTransfer(s)){await c.query("UPDATE plc_jobs SET state='rejected',finished_at=now() WHERE id=$1",[j.id]);return null;}
 await c.query("UPDATE plc_jobs SET state='delivered',delivered_at=now() WHERE id=$1",[j.id]);
 return {id:j.id,action:'plcset',relay:0,ms:0,ttl_ms:j.ttl_ms,revision:j.revision,program:j.program};
}
