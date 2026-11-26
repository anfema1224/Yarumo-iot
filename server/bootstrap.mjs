import pg from 'pg';
import {readFile} from 'node:fs/promises';
const {Pool}=pg;
function diagnostic(code){const e=new Error(code);e.code=code;return e;}
function parseUrl(raw,code){try{return new URL(raw);}catch{throw diagnostic(code);}}

function config(){
 const raw=process.env.DATABASE_URL;
 if(!raw)throw diagnostic('CONFIG_RUNTIME_URL_MISSING');
 const u=parseUrl(raw,'CONFIG_RUNTIME_URL_INVALID');
 if(!['postgres:','postgresql:'].includes(u.protocol))throw diagnostic('CONFIG_RUNTIME_PROTOCOL');
 if(decodeURIComponent(u.username)!=='yarumo_iot')throw diagnostic('CONFIG_RUNTIME_USER');
 if(decodeURIComponent(u.password).length<24)throw diagnostic('CONFIG_RUNTIME_PASSWORD_TOO_SHORT');
 return u;
}
export async function verifyRuntime(){
 config();
 const pool=new Pool({connectionString:process.env.DATABASE_URL,options:'-c search_path=pg_catalog,iot',max:1,connectionTimeoutMillis:6000,statement_timeout:6000});
 try{
   const r=await pool.query(`SELECT current_user='yarumo_iot' AS correct_role,
     has_schema_privilege(current_user,'iot','USAGE') AS iot_usage,
     NOT has_schema_privilege(current_user,'iot','CREATE') AS no_ddl,
     NOT has_schema_privilege(current_user,'acceso','USAGE') AS no_access,
     NOT has_schema_privilege(current_user,'public','CREATE') AS no_public_ddl,
     NOT rolcreaterole AND NOT rolcreatedb AND NOT rolsuper AND NOT rolbypassrls AS restricted_role,
     NOT pg_has_role(current_user,'yarumo_acceso','MEMBER') AS no_other_role
     FROM pg_roles WHERE rolname=current_user`);
   if(r.rowCount!==1)throw diagnostic('ROLE_NOT_FOUND');
   const failed=Object.entries(r.rows[0]).filter(([,v])=>v!==true).map(([k])=>k.toUpperCase());
   if(failed.length)throw diagnostic('ROLE_CHECK_'+failed.join('_'));
   for(const table of ['users','sessions','devices','readings','commands','plc_programs','plc_jobs']){
     const p=await pool.query("SELECT has_table_privilege(current_user,$1,'SELECT') AND has_table_privilege(current_user,$1,'INSERT') AND has_table_privilege(current_user,$1,'UPDATE') AND has_table_privilege(current_user,$1,'DELETE') AS ok",['iot.'+table]);
     if(!p.rows[0]?.ok)throw diagnostic('TABLE_GRANTS_'+table.toUpperCase());
   }
   const s=await pool.query("SELECT has_sequence_privilege(current_user,'iot.readings_id_seq','USAGE') AS ok");
   if(!s.rows[0]?.ok)throw diagnostic('SEQUENCE_GRANT_MISSING');
 }finally{await pool.end();}
}
export async function bootstrap(){
 const runtime=config();
 const admin=parseUrl(process.env.BOOTSTRAP_DATABASE_URL,'CONFIG_ADMIN_URL_INVALID');
 if(!['postgres:','postgresql:'].includes(admin.protocol)||admin.hostname!==runtime.hostname||admin.port!==runtime.port||admin.pathname!==runtime.pathname||decodeURIComponent(admin.username)==='yarumo_iot')throw diagnostic('CONFIG_ENDPOINT_MISMATCH');
 const pool=new Pool({connectionString:admin.toString(),max:1,connectionTimeoutMillis:8000,statement_timeout:15000});
 const c=await pool.connect();
 try{
   await c.query('BEGIN');
   await c.query("SELECT pg_advisory_xact_lock(91273301)");
   const result=await c.query("SELECT current_user AS owner,rolcreaterole AS can_create,rolsuper AS super FROM pg_roles WHERE rolname=current_user");
   const owner=result.rows[0];
   if(!owner?.can_create&&!owner?.super)throw diagnostic('ADMIN_NO_CREATEROLE');
   const existing=await c.query("SELECT rolname FROM pg_roles WHERE rolname IN ('yarumo_iot','yarumo_acceso')");
   if(existing.rowCount===0){
     const ddl=await c.query("SELECT format('CREATE ROLE yarumo_iot LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOINHERIT NOBYPASSRLS PASSWORD %L', $1::text) AS sql",[decodeURIComponent(runtime.password)]);
     await c.query(ddl.rows[0].sql);
     await c.query('CREATE ROLE yarumo_acceso NOLOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOINHERIT NOBYPASSRLS');
   }else if(existing.rowCount!==2)throw diagnostic('PARTIAL_ROLES');
   await c.query('CREATE SCHEMA IF NOT EXISTS iot');
   await c.query('CREATE SCHEMA IF NOT EXISTS acceso');
   const schemas=await c.query("SELECT nspname,pg_get_userbyid(nspowner) AS owner FROM pg_namespace WHERE nspname IN ('iot','acceso')");
   if(schemas.rowCount!==2||schemas.rows.some(x=>x.owner!==owner.owner))throw diagnostic('SCHEMA_OWNER_MISMATCH');
   await c.query('REVOKE ALL ON SCHEMA iot, acceso FROM PUBLIC');
   await c.query('REVOKE CREATE ON SCHEMA public FROM PUBLIC');
   await c.query('REVOKE ALL ON SCHEMA acceso FROM yarumo_iot');
   await c.query('REVOKE ALL ON SCHEMA iot FROM yarumo_acceso');
   await c.query('SET LOCAL search_path TO iot, pg_catalog');
   await c.query(await readFile(new URL('./schema.sql',import.meta.url),'utf8'));
   await c.query('GRANT USAGE ON SCHEMA iot TO yarumo_iot');
   await c.query('GRANT SELECT, INSERT, UPDATE, DELETE ON ALL TABLES IN SCHEMA iot TO yarumo_iot');
   await c.query('GRANT USAGE, SELECT ON ALL SEQUENCES IN SCHEMA iot TO yarumo_iot');
   await c.query('GRANT USAGE ON SCHEMA acceso TO yarumo_acceso');
   await c.query('COMMIT');
 }catch(e){await c.query('ROLLBACK').catch(()=>{});throw e;}finally{c.release();await pool.end();}
 await verifyRuntime();
 console.log('Shared database provisioned; remove BOOTSTRAP_DATABASE_URL before starting application.');
}
