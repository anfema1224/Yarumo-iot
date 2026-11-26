import http from 'node:http';
import {bootstrap,verifyRuntime} from './bootstrap.mjs';
const port=Number(process.env.PORT||3000);
function setup(status){
 console.log('Yarumo setup:',status);
 http.createServer((req,res)=>{
   const health=req.url==='/api/health';
   res.writeHead(health?503:200,{'Content-Type':health?'application/json; charset=utf-8':'text/html; charset=utf-8','Cache-Control':'no-store','X-Content-Type-Options':'nosniff','Content-Security-Policy':"default-src 'none'; style-src 'none'"});
   res.end(health?JSON.stringify({ok:false,mode:'setup',status}):'<!doctype html><html lang="es"><meta charset="utf-8"><title>Yarumo IoT</title><h1>Yarumo IoT V0.3</h1><p>Configuración de infraestructura pendiente. El acceso y el control remoto están deshabilitados.</p></html>');
 }).listen(port,'0.0.0.0');
}
if(process.env.BOOTSTRAP_DATABASE_URL){
 try{await bootstrap();setup('bootstrap_complete_remove_admin_secret');}catch(e){console.error('Bootstrap failed',e.code||e.name);setup('bootstrap_failed');}
}else if(!process.env.DATABASE_URL||(process.env.REGISTRATION_CODE||'').length<24){setup('missing_configuration');}
else {try{await verifyRuntime();await import('./index.mjs');}catch(e){console.error('Runtime configuration failed',e.code||e.name);setup('runtime_configuration_failed');}}
