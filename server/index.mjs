import express from 'express';
import pg from 'pg';
import {randomBytes,randomUUID,createHash,scrypt,timingSafeEqual} from 'node:crypto';
import {promisify} from 'node:util';
import {fileURLToPath} from 'node:url';
import {command,snapshot,isUuid,maxPulse} from './validation.mjs';
const {Pool}=pg;
const derive=promisify(scrypt);
const hash=v=>createHash('sha256').update(v).digest('hex');
const token=()=>randomBytes(32).toString('hex');
const safeEq=(a,b)=>typeof a==='string'&&typeof b==='string'&&timingSafeEqual(Buffer.from(hash(a),'hex'),Buffer.from(hash(b),'hex'));
const invite=process.env.REGISTRATION_CODE||'';
if(!process.env.DATABASE_URL) throw Error('DATABASE_URL required');
if(invite.length<24) throw Error('REGISTRATION_CODE must have at least 24 characters');
const pool=new Pool({connectionString:process.env.DATABASE_URL,options:'-c search_path=pg_catalog,iot',max:5,connectionTimeoutMillis:5000,statement_timeout:5000});
await pool.query('SELECT 1 FROM iot.users LIMIT 1');
const app=express();app.disable('x-powered-by');app.set('trust proxy',1);
app.use(express.json({limit:'8kb'}));
app.use((req,res,next)=>{res.set({'Cache-Control':'no-store','X-Content-Type-Options':'nosniff','Referrer-Policy':'no-referrer','X-Frame-Options':'DENY','Content-Security-Policy':"default-src 'self'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'"});next();});
const asyncRoute=fn=>(req,res,next)=>Promise.resolve(fn(req,res,next)).catch(next);
const limits=new Map();
function limit(key,max,windowMs){
 const now=Date.now();let x=limits.get(key);
 if(!x||x.end<=now){if(limits.size>=10000&&!limits.has(key))return false;x={n:0,end:now+windowMs};limits.set(key,x);}
 return ++x.n<=max;
}
setInterval(()=>{for(const [k,v] of limits)if(v.end<=Date.now())limits.delete(k);},60000).unref();
function throttle(prefix,max,ms){return (req,res,next)=>limit(prefix+req.ip,max,ms)?next():res.status(429).json({error:'Demasiados intentos'});}
const cookieName='__Host-yarumo';
const sessionCookie=(res,v,maxAge)=>res.cookie(cookieName,v,{secure:true,httpOnly:true,sameSite:'strict',path:'/',maxAge});
const getCookie=req=>(req.headers.cookie||'').split(';').map(x=>x.trim()).find(x=>x.startsWith(cookieName+'='))?.slice(cookieName.length+1)||'';
const auth=asyncRoute(async(req,res,next)=>{
 const t=getCookie(req);if(!/^[0-9a-f]{64}$/.test(t))return res.status(401).json({error:'Inicia sesión'});
 const r=await pool.query('SELECT u.id,u.email FROM sessions s JOIN users u ON u.id=s.user_id WHERE s.token_hash=$1 AND s.expires_at>now()',[hash(t)]);
 if(!r.rowCount)return res.status(401).json({error:'Sesión vencida'});req.user=r.rows[0];next();
});
function csrf(req,res,next){if(req.get('X-Yarumo-UI')!=='1'||req.get('Sec-Fetch-Site')==='cross-site')return res.status(403).json({error:'Origen no autorizado'});next();}
async function newSession(res,userId){const t=token();await pool.query("INSERT INTO sessions(token_hash,user_id,expires_at) VALUES($1,$2,now()+interval '12 hours')",[hash(t),userId]);sessionCookie(res,t,43200000);}
function validLogin(email,password){return typeof email==='string'&&email.length<=254&&/^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(email)&&typeof password==='string'&&password.length>=12&&password.length<=128;}
async function encodePassword(p){const salt=randomBytes(16).toString('hex');const key=await derive(p,salt,64,{N:16384,r:8,p:1});return salt+':'+key.toString('hex');}
async function verifyPassword(p,stored){const [salt,key]=stored.split(':');const result=await derive(p,salt,64,{N:16384,r:8,p:1});return /^[0-9a-f]{128}$/.test(key)&&timingSafeEqual(result,Buffer.from(key,'hex'));}
app.get('/api/health',asyncRoute(async(req,res)=>{await pool.query('SELECT 1');res.json({ok:true,version:'0.3.0'});}));
app.post('/api/register',csrf,throttle('reg:',5,900000),asyncRoute(async(req,res)=>{
 const {email,password,code}=req.body||{};
 if(!validLogin(email,password)||!safeEq(code,invite))return res.status(400).json({error:'Datos o código de invitación inválidos'});
 const id=randomUUID();const r=await pool.query('INSERT INTO users(id,email,password_hash) VALUES($1,$2,$3) ON CONFLICT(email) DO NOTHING RETURNING id',[id,email.trim().toLowerCase(),await encodePassword(password)]);
 if(!r.rowCount)return res.status(409).json({error:'No se pudo registrar'});await newSession(res,id);res.status(201).json({ok:true});
}));
app.post('/api/login',csrf,throttle('login:',10,900000),asyncRoute(async(req,res)=>{
 const {email,password}=req.body||{};
 if(!validLogin(email,password))return res.status(401).json({error:'Credenciales inválidas'});
 const r=await pool.query('SELECT id,password_hash FROM users WHERE email=$1',[email.trim().toLowerCase()]);
 const dummy='00000000000000000000000000000000:'+ '0'.repeat(128);
 const ok=await verifyPassword(password,r.rows[0]?.password_hash||dummy);
 if(!r.rowCount||!ok)return res.status(401).json({error:'Credenciales inválidas'});
 await newSession(res,r.rows[0].id);res.json({ok:true});
}));
app.get('/api/me',auth,(req,res)=>res.json({email:req.user.email}));
app.post('/api/logout',csrf,auth,asyncRoute(async(req,res)=>{await pool.query('DELETE FROM sessions WHERE token_hash=$1',[hash(getCookie(req))]);sessionCookie(res,'',0);res.json({ok:true});}));
app.get('/api/devices',auth,asyncRoute(async(req,res)=>{
 const r=await pool.query("SELECT id,name,hardware_id,last_seen,snapshot,COALESCE(last_seen>now()-interval '15 seconds',false) AS online FROM devices WHERE owner_id=$1 ORDER BY name",[req.user.id]);res.json(r.rows);
}));
app.post('/api/devices',csrf,auth,throttle('enroll:',10,3600000),asyncRoute(async(req,res)=>{
 const {name,hardware_id}=req.body||{};
 if(typeof name!=='string'||!name.trim()||name.length>64||typeof hardware_id!=='string'||!/^[0-9A-F]{12}$/.test(hardware_id))return res.status(400).json({error:'Nombre o MAC inválida'});
 const count=await pool.query('SELECT count(*)::int AS n FROM devices WHERE owner_id=$1',[req.user.id]);if(count.rows[0].n>=20)return res.status(409).json({error:'Límite de dispositivos'});
 const id=randomUUID(),secret=token();const r=await pool.query('INSERT INTO devices(id,owner_id,name,hardware_id,token_hash) VALUES($1,$2,$3,$4,$5) ON CONFLICT(owner_id,hardware_id) DO NOTHING RETURNING id',[id,req.user.id,name.trim(),hardware_id,hash(secret)]);
 if(!r.rowCount)return res.status(409).json({error:'Dispositivo ya registrado'});res.status(201).json({id,token:secret});
}));
app.post('/api/devices/:id/rotate',csrf,auth,throttle('rotate:',10,3600000),asyncRoute(async(req,res)=>{
 if(!isUuid(req.params.id))return res.sendStatus(404);const secret=token();
 const r=await pool.query('UPDATE devices SET token_hash=$1,last_seen=NULL WHERE id=$2 AND owner_id=$3 RETURNING id',[hash(secret),req.params.id,req.user.id]);
 if(!r.rowCount)return res.sendStatus(404);await pool.query("UPDATE commands SET state='cancelled',finished_at=now() WHERE device_id=$1 AND state='queued'",[req.params.id]);res.json({id:req.params.id,token:secret});
}));
app.get('/api/devices/:id/history',auth,asyncRoute(async(req,res)=>{
 if(!isUuid(req.params.id))return res.sendStatus(404);
 const r=await pool.query('SELECT r.at,r.data FROM readings r JOIN devices d ON d.id=r.device_id WHERE d.id=$1 AND d.owner_id=$2 ORDER BY r.at DESC LIMIT 120',[req.params.id,req.user.id]);res.json(r.rows.reverse());
}));
app.get('/api/devices/:id/commands',auth,asyncRoute(async(req,res)=>{
 if(!isUuid(req.params.id))return res.sendStatus(404);
 const r=await pool.query('SELECT c.id,c.action,c.relay,c.ms,c.state,c.created_at FROM commands c JOIN devices d ON d.id=c.device_id WHERE d.id=$1 AND d.owner_id=$2 ORDER BY c.created_at DESC LIMIT 20',[req.params.id,req.user.id]);res.json(r.rows);
}));
app.post('/api/devices/:id/commands',csrf,auth,throttle('cmd:',90,60000),asyncRoute(async(req,res)=>{
 const cmd=command(req.body);if(!isUuid(req.params.id)||!cmd)return res.status(400).json({error:'Orden inválida'});
 const c=await pool.connect();try{await c.query('BEGIN');
 const d=await c.query("SELECT id,snapshot,COALESCE(last_seen>now()-interval '15 seconds',false) AS online FROM devices WHERE id=$1 AND owner_id=$2 FOR UPDATE",[req.params.id,req.user.id]);
 if(!d.rowCount||!d.rows[0].online){await c.query('ROLLBACK');return res.status(409).json({error:'Dispositivo no disponible'});}
 if(cmd.action==='pulse'&&cmd.ms>maxPulse(d.rows[0].snapshot)){await c.query('ROLLBACK');return res.status(409).json({error:'El firmware admite máximo '+maxPulse(d.rows[0].snapshot)/1000+' segundos. Actualízalo.'});}
 if(cmd.action==='pulse'&&cmd.ms===0&&d.rows[0].snapshot?.hold_supported!==true){await c.query('ROLLBACK');return res.status(409).json({error:'Actualiza el firmware para encendido continuo'});}
 await c.query("UPDATE commands SET state='expired',finished_at=now() WHERE device_id=$1 AND state='queued' AND expires_at<=now()",[req.params.id]);
 if(cmd.action==='pulse'){const p=await c.query("SELECT 1 FROM commands WHERE device_id=$1 AND state='queued'",[req.params.id]);if(p.rowCount){await c.query('ROLLBACK');return res.status(409).json({error:'Hay una orden pendiente'});}}
 else await c.query("UPDATE commands SET state='cancelled',finished_at=now() WHERE device_id=$1 AND state='queued'",[req.params.id]);
 const id=randomUUID();await c.query("INSERT INTO commands(id,device_id,action,relay,ms,expires_at) VALUES($1,$2,$3,$4,$5,now()+interval '10 seconds')",[id,req.params.id,cmd.action,cmd.relay,cmd.ms]);await c.query('COMMIT');res.status(202).json({id,state:'queued'});
 }catch(e){await c.query('ROLLBACK');throw e;}finally{c.release();}
}));
app.post('/api/device/poll',throttle('poll:',300,60000),asyncRoute(async(req,res)=>{
 const bearer=req.get('Authorization')||'';const t=bearer.startsWith('Bearer ')?bearer.slice(7):'';
 if(!/^[0-9a-f]{64}$/.test(t))return res.sendStatus(401);
 const c=await pool.connect();try{await c.query('BEGIN');
 const r=await c.query('SELECT id,hardware_id,last_sample FROM devices WHERE token_hash=$1 FOR UPDATE',[hash(t)]);
 if(!r.rowCount){await c.query('ROLLBACK');return res.sendStatus(401);}
 const d=r.rows[0],s=snapshot(req.body?.snapshot,d.hardware_id);
 if(!s){await c.query('ROLLBACK');return res.status(400).json({error:'Telemetría inválida'});}
 const ack=req.body?.ack;
 if(ack&&isUuid(ack.id)&&['executed','rejected'].includes(ack.state))await c.query("UPDATE commands SET state=$1,finished_at=now() WHERE id=$2 AND device_id=$3 AND state='delivered'",[ack.state,ack.id,d.id]);
 await c.query('UPDATE devices SET snapshot=$1,last_seen=now() WHERE id=$2',[JSON.stringify(s),d.id]);
 if(!d.last_sample||Date.now()-new Date(d.last_sample).getTime()>=15000){await c.query('INSERT INTO readings(device_id,data) VALUES($1,$2)',[d.id,JSON.stringify(s)]);await c.query('UPDATE devices SET last_sample=now() WHERE id=$1',[d.id]);}
 await c.query("UPDATE commands SET state='expired',finished_at=now() WHERE device_id=$1 AND state='queued' AND expires_at<=now()",[d.id]);
 await c.query("UPDATE commands SET state='unknown',finished_at=now() WHERE device_id=$1 AND state='delivered' AND delivered_at<now()-interval '30 seconds'",[d.id]);
 const q=await c.query("SELECT id,action,relay,ms,GREATEST(0,floor(extract(epoch FROM (expires_at-clock_timestamp()))*1000))::int AS ttl_ms FROM commands WHERE device_id=$1 AND state='queued' AND expires_at>clock_timestamp() ORDER BY created_at LIMIT 1 FOR UPDATE",[d.id]);
 let outgoing=q.rows[0]||null;
 if(outgoing){
   const hold=outgoing.action==='pulse'&&outgoing.ms===0;
   if((hold&&!s.hold_supported)||(outgoing.action==='pulse'&&outgoing.ms>maxPulse(s))){await c.query("UPDATE commands SET state='rejected',finished_at=now() WHERE id=$1",[outgoing.id]);outgoing=null;}
   else {await c.query("UPDATE commands SET state='delivered',delivered_at=now() WHERE id=$1",[outgoing.id]);if(hold)outgoing={...outgoing,action:'on'};}
 }
 await c.query('COMMIT');res.json({command:outgoing});
 }catch(e){await c.query('ROLLBACK');throw e;}finally{c.release();}
}));
app.use(express.static(fileURLToPath(new URL('../public/',import.meta.url)),{etag:false,maxAge:0}));
app.use((err,req,res,next)=>{console.error('request failed',err.code||err.name);if(!res.headersSent)res.status(err.status===400?400:500).json({error:'Solicitud no completada'});});
const cleanup=async()=>{try{await pool.query("DELETE FROM readings WHERE at<now()-interval '24 hours'");await pool.query("DELETE FROM sessions WHERE expires_at<=now()");await pool.query("UPDATE commands SET state='expired',finished_at=now() WHERE state='queued' AND expires_at<=now()");await pool.query("UPDATE commands SET state='unknown',finished_at=now() WHERE state='delivered' AND delivered_at<now()-interval '30 seconds'");await pool.query("DELETE FROM commands WHERE created_at<now()-interval '7 days'");}catch(e){console.error('cleanup failed',e.code||e.name);}};
setInterval(cleanup,60000).unref();
app.listen(Number(process.env.PORT||3000),'0.0.0.0',()=>console.log('Yarumo IoT listening'));
