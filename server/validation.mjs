export const isUuid = v => typeof v === 'string' && /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(v);
const ints=(v,n,min,max)=>Array.isArray(v)&&v.length===n&&v.every(x=>Number.isInteger(x)&&x>=min&&x<=max);
export function snapshot(s, hardwareId) {
 if(!s || s.device!==hardwareId || typeof s.version!=='string' || !/^\d{1,3}\.\d{1,3}\.\d{1,3}$/.test(s.version)
 || !ints(s.di,6,0,1)||!ints(s.ai_raw,4,0,4095)||!ints(s.ai_mv,4,0,5000)||!ints(s.relay_cmd,4,0,1)
 ||!ints(s.remaining_ms,4,0,10000)) return null;
 return {device:s.device,version:s.version,di:s.di,ai_raw:s.ai_raw,ai_mv:s.ai_mv,relay_cmd:s.relay_cmd,remaining_ms:s.remaining_ms,hold_supported:s.hold_supported===true};
}
export function command(body){
 if(!body||typeof body!=='object') return null;
 const {action,relay,ms}=body;
 if(action==='all') return {action,relay:0,ms:0};
 if(!Number.isInteger(relay)||relay<1||relay>4) return null;
 if(action==='off') return {action,relay,ms:0};
 // Store continuous activation using the existing pulse row with ms=0; never accept a zero-length pulse from API.
 if(action==='on'&&ms===0) return {action:'pulse',relay,ms:0};
 if(action==='pulse'&&Number.isInteger(ms)&&ms>=50&&ms<=10000) return {action,relay,ms};
 return null;
}
