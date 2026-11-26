// One-shot migration worker. Never serves database data or accepts HTTP commands.
import http from 'node:http';
import pg from 'pg';
import {readFile} from 'node:fs/promises';
let status='missing_admin_secret';
const port=Number(process.env.PORT||3000);
const secret=process.env.BOOTSTRAP_DATABASE_URL;
function check(ok,code){if(!ok){const e=new Error(code);e.code=code;throw e;}}
async function migrate(){
 const url=new URL(secret);
 check(['postgres:','postgresql:'].includes(url.protocol),'INVALID_PROTOCOL');
 check(url.hostname==='dpg-dc3qr2l6e07s73daifd0-a'&&decodeURIComponent(url.pathname)==='/yarumo_db','WRONG_DATABASE_ENDPOINT');
 check(url.username!=='yarumo_iot'&&url.username!=='yarumo_acceso','ADMIN_REQUIRED');
 const pool=new pg.Pool({connectionString:secret,max:1,connectionTimeoutMillis:8000,statement_timeout:20000});
 let c;
 try{
  c=await pool.connect();
  await c.query('BEGIN');
  await c.query("SET LOCAL lock_timeout='5s'");
  await c.query('SELECT pg_advisory_xact_lock(91273301)');
  const r=await c.query("SELECT current_database()='yarumo_db' AS db, pg_get_userbyid(nspowner)=current_user AS owner FROM pg_namespace WHERE nspname='iot'");
  check(r.rows[0]?.db&&r.rows[0]?.owner,'WRONG_DATABASE_OR_SCHEMA_OWNER');
  const role=await c.query("SELECT rolname,NOT rolsuper AND NOT rolcreaterole AND NOT rolcreatedb AND NOT rolbypassrls AS restricted FROM pg_roles WHERE rolname='yarumo_iot'");
  check(role.rows[0]?.restricted,'RUNTIME_ROLE_INVALID');
  const tables=await c.query("SELECT to_regclass('iot.devices') IS NOT NULL AS devices, to_regclass('iot.commands') IS NOT NULL AS commands");
  check(tables.rows[0]?.devices&&tables.rows[0]?.commands,'BASE_SCHEMA_MISSING');
  const raw=await readFile(new URL('./005-plc.sql',import.meta.url),'utf8');
  // Outer transaction includes both migration and postcondition checks.
  const sql=raw.replace(/^BEGIN;\s*$/m,'').replace(/^COMMIT;\s*$/m,'');
  await c.query(sql);
  for(const [table,columns] of Object.entries({plc_programs:['device_id','revision','graph','program','updated_at'],plc_jobs:['id','device_id','revision','program','state','created_at','expires_at','delivered_at','finished_at']})){
   const q=await c.query("SELECT column_name FROM information_schema.columns WHERE table_schema='iot' AND table_name=$1",[table]);
   check(q.rowCount===columns.length&&columns.every(n=>q.rows.some(r=>r.column_name===n)),'COLUMN_CHECK_FAILED');
   const p=await c.query("SELECT has_table_privilege('yarumo_iot',$1,'SELECT') AND has_table_privilege('yarumo_iot',$1,'INSERT') AND has_table_privilege('yarumo_iot',$1,'UPDATE') AND has_table_privilege('yarumo_iot',$1,'DELETE') AS ok, NOT has_table_privilege('yarumo_acceso',$1,'SELECT') AS isolated",['iot.'+table]);
   check(p.rows[0]?.ok&&p.rows[0]?.isolated,'TABLE_GRANTS_FAILED');
  }
  const isolation=await c.query("SELECT NOT has_schema_privilege('yarumo_iot','iot','CREATE') AND NOT has_schema_privilege('yarumo_iot','acceso','USAGE') AND NOT has_schema_privilege('yarumo_iot','public','CREATE') AND NOT has_schema_privilege('yarumo_acceso','iot','USAGE') AS ok");
  check(isolation.rows[0]?.ok,'SCHEMA_ISOLATION_FAILED');
  await c.query('COMMIT');
  status='migration_005_verified_remove_secret';
 }catch(e){if(c)await c.query('ROLLBACK').catch(()=>{});throw e;}
 finally{if(c)c.release();await pool.end();}
}
if(secret){
 try{await migrate();}catch(e){status='migration_failed';console.error('Migration 005 failed:',typeof e.code==='string'&&/^[A-Z0-9_]{1,80}$/.test(e.code)?e.code:'INVALID_CONFIG_OR_CONNECTION');}
}
console.log('Yarumo migration:',status);
// No admin connection remains open, no database access from requests.
http.createServer((req,res)=>{
 res.writeHead(200,{'Content-Type':'text/plain; charset=utf-8','Cache-Control':'no-store','X-Content-Type-Options':'nosniff'});
 res.end('Yarumo IoT migration worker. Status available in Render logs only.');
}).listen(port,'0.0.0.0');
