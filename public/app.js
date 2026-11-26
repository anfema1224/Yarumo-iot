'use strict';
const $=id=>document.getElementById(id);let logged=false,busy=false,polling=false,selected='',devices=[],generation=0,lastDetails=0,credential='';
const diPins=[13,14,16,17,19,27],aiPins=[34,35,36,39],doPins=[32,33,25,26];
function notice(m){$('notice').textContent=m;}
async function api(path,body){
 const opt={cache:'no-store',signal:AbortSignal.timeout(8000)};
 if(body!==undefined){opt.method='POST';opt.headers={'Content-Type':'application/json','X-Yarumo-UI':'1'};opt.body=JSON.stringify(body);}
 const r=await fetch(path,opt);const data=await r.json().catch(()=>({}));
 if(!r.ok){const e=new Error(data.error||'HTTP '+r.status);e.status=r.status;throw e;}return data;
}
function textCell(tr,value){const td=document.createElement('td');td.textContent=String(value??'');tr.append(td);}
function rows(id,items){$(id).replaceChildren(...items.map(row=>{const tr=document.createElement('tr');row.forEach(v=>textCell(tr,v));return tr;}));}
function hideCredential(){credential='';$('token').textContent='';$('credential').hidden=true;}
function showCredential(r){credential=r.token;$('token').textContent=credential;$('credential').hidden=false;notice('Credencial generada. Cópiala al firmware local; no la publiques.');}
function setAuth(me){logged=!!me;generation++;$('loginPanel').hidden=logged;$('dashboard').hidden=!logged;$('logout').hidden=!logged;$('account').textContent=me?.email||'';if(!logged){devices=[];selected='';hideCredential();}}
function device(){return devices.find(d=>d.id===selected);}
function updateControls(){const d=device();const enabled=logged&&!busy&&!!d?.online&&!!d.snapshot;doPins.forEach((p,i)=>{$('go'+i).disabled=!enabled||!!d?.snapshot?.relay_cmd[i];$('off'+i).disabled=!enabled;});$('allOff').disabled=!enabled;$('rotate').disabled=!logged||busy||!d;}
function cardHtml(){diPins.forEach((p,i)=>{$('di').insertAdjacentHTML('beforeend','<div class="card">DI'+(i+1)+' · GPIO'+p+'<div class="val" id="d'+i+'">--</div></div>')});aiPins.forEach((p,i)=>{$('ai').insertAdjacentHTML('beforeend','<div class="card">AI'+(i+1)+' · GPIO'+p+'<div class="val" id="a'+i+'">--</div><div class="muted" id="raw'+i+'"></div></div>')});doPins.forEach((p,i)=>{$('relays').insertAdjacentHTML('beforeend','<div class="card">Relé '+(i+1)+' · GPIO'+p+'<div class="val" id="r'+i+'">--</div><p class="muted" id="t'+i+'"></p><label>Tiempo (s)<input id="sec'+i+'" type="number" min=".05" max="10" step=".05" value="2"></label><button id="go'+i+'" disabled>Activar</button> <button class="danger" id="off'+i+'" disabled>Apagar</button></div>');$('go'+i).onclick=()=>{const sec=Number($('sec'+i).value);if(!Number.isFinite(sec)||sec<.05||sec>10)return notice('Tiempo inválido');send('pulse',i+1,Math.round(sec*1000));};$('off'+i).onclick=()=>send('off',i+1,0);});}
function render(){const d=device(),s=d?.snapshot;$('online').textContent=d?(d.online?'Conectado':'Desconectado — datos históricos')+' · '+d.hardware_id+(s?' · V'+s.version:''):'Sin dispositivos vinculados';$('online').className=d?.online?'on':'offline';
diPins.forEach((p,i)=>{$('d'+i).textContent=s?(s.di[i]?'HIGH':'LOW'):'--';});
aiPins.forEach((p,i)=>{$('a'+i).textContent=s?s.ai_mv[i]+' mV':'--';$('raw'+i).textContent=s?'ADC '+s.ai_raw[i]:'';});
doPins.forEach((p,i)=>{$('r'+i).textContent=s?(s.relay_cmd[i]?'ON':'OFF'):'--';$('r'+i).className='val '+(s?.relay_cmd[i]?'on':'');$('t'+i).textContent=s?(s.remaining_ms[i]/1000).toFixed(1)+' s restantes':'';});updateControls();}
async function refresh(force=false){if(!logged||polling||busy)return;polling=true;const g=generation;try{const list=await api('/api/devices');if(g!==generation)return;devices=list;const prev=selected;if(!devices.some(d=>d.id===selected))selected=devices[0]?.id||'';if(prev!==selected)lastDetails=0;
const sel=$('device');sel.replaceChildren(...devices.map(d=>{const o=document.createElement('option');o.value=d.id;o.textContent=d.name;return o;}));sel.value=selected;render();
if(selected&&(force||Date.now()-lastDetails>5000)){const id=selected;const [c,h]=await Promise.all([api('/api/devices/'+id+'/commands'),api('/api/devices/'+id+'/history')]);if(g!==generation||id!==selected)return;rows('commands',c.map(x=>[new Date(x.created_at).toLocaleTimeString(),x.action+' '+(x.relay||'todos')+(x.ms?' · '+x.ms+' ms':''),x.state]));rows('history',h.slice(-30).reverse().map(x=>[new Date(x.at).toLocaleTimeString(),...x.data.ai_mv]));lastDetails=Date.now();}
if(!selected){rows('commands',[]);rows('history',[]);}
}catch(e){if(g!==generation)return;if(e.status===401){setAuth(null);notice('Sesión vencida.');}else{devices=[];render();notice('Error de conexión: '+e.message);}}finally{polling=false;}}
async function send(action,relay,ms){const d=device();if(!d?.online||busy)return;busy=true;updateControls();try{const r=await api('/api/devices/'+d.id+'/commands',{action,relay,ms});notice('Orden '+r.id.slice(0,8)+' en cola. Espera confirmación en el historial.');}catch(e){notice(e.message);}finally{busy=false;lastDetails=0;updateControls();refresh(true);}}
for(const mode of ['login','register'])$(mode).onsubmit=async e=>{e.preventDefault();const data=Object.fromEntries(new FormData(e.target));try{await api('/api/'+mode,data);e.target.reset();setAuth(await api('/api/me'));notice('');refresh(true);}catch(err){notice(err.message);}};
$('enroll').onsubmit=async e=>{e.preventDefault();const data=Object.fromEntries(new FormData(e.target));data.hardware_id=data.hardware_id.toUpperCase();try{const r=await api('/api/devices',data);showCredential(r);e.target.reset();selected=r.id;lastDetails=0;refresh(true);}catch(err){notice(err.message);}};
$('logout').onclick=async()=>{try{await api('/api/logout',{});setAuth(null);notice('');}catch(e){notice(e.message);}};
$('copyToken').onclick=async()=>{try{await navigator.clipboard.writeText(credential);notice('Token copiado');}catch{notice('Copia el token manualmente.');}};
$('hideToken').onclick=hideCredential;
$('rotate').onclick=async()=>{const d=device();if(!d||!confirm('La credencial actual quedará revocada. ¿Continuar?'))return;try{showCredential(await api('/api/devices/'+d.id+'/rotate',{}));refresh(true);}catch(e){notice(e.message);}};
$('device').onchange=e=>{selected=e.target.value;generation++;lastDetails=0;rows('commands',[]);rows('history',[]);render();refresh(true);};
$('allOff').onclick=()=>send('all',0,0);cardHtml();updateControls();
(async()=>{try{setAuth(await api('/api/me'));refresh(true);}catch{setAuth(null);}})();setInterval(refresh,2500);
