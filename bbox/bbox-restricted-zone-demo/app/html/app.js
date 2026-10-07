'use strict';
const byId=id=>document.getElementById(id);
let current=null,dirty=false,saving=false,pending=null,connected=false;
const ctx=byId('diagram').getContext('2d');
const message=text=>{byId('save-message').textContent=text;};
function populate(s){byId('enabled').checked=s.enabled;['x','y','w','h'].forEach((k,i)=>{byId(k).value=Math.round(s.zone[i]*100);});byId('rule').value=s.rule;byId('dwell').value=s.dwellSeconds;}
function render(s){
 current=s;connected=true;byId('offline').hidden=true;byId('connection').textContent='Connected';byId('connection').dataset.state='online';byId('fields').disabled=saving;
 byId('state').textContent=!s.enabled?'Disabled':s.alarm?'Dwell exceeded':s.inside?'Inside zone':'Outside zone';
 byId('state').className=`badge ${s.inside?'stale':'fresh'}`;byId('duration').textContent=`${s.insideSeconds}s`;byId('entries').textContent=s.entries;
 byId('draw-status').textContent=s.drawError || (s.enabled?'BBox commit succeeded. Check camera preview for actual output.':'BBox clear committed.');
 ctx.clearRect(0,0,640,360);
 if(s.enabled){
  for(const [r,color] of [[s.zone,'#e0a000'],[s.object,s.alarm?'#e62828':s.inside?'#ff9600':'#28be5a']]){ctx.strokeStyle=color;ctx.lineWidth=4;ctx.strokeRect(r[0]*640,r[1]*360,r[2]*640,r[3]*360);}
 }
 if(pending && s.enabled===pending.enabled && s.rule===pending.rule && s.dwellSeconds===pending.dwellSeconds && s.zone.every((v,i)=>Math.abs(v-pending.zone[i])<1e-6)){pending=null;dirty=false;message('Settings applied. Simulation restarted.');}
 else if(pending && Date.now()>pending.deadline){pending=null;message('Running settings not confirmed. Use current values before retrying.');}
 if(!dirty && !saving && !pending)populate(s);
}
async function request(url,options={}){const c=new AbortController(),t=setTimeout(()=>c.abort(),5000);try{const r=await fetch(url,{credentials:'same-origin',cache:'no-store',...options,signal:c.signal});if(!r.ok)throw Error(`HTTP ${r.status}`);return await r.text();}finally{clearTimeout(t);}}
async function poll(){try{render(JSON.parse(await request('status.cgi')));}catch(error){connected=false;byId('offline').hidden=false;byId('connection').textContent='Disconnected';byId('connection').dataset.state='offline';byId('fields').disabled=true;byId('state').textContent='Unknown';byId('duration').textContent='—';byId('entries').textContent='—';ctx.clearRect(0,0,640,360);}finally{setTimeout(poll,500);}}
byId('settings').addEventListener('input',()=>{dirty=true;pending=null;message('Unsaved changes.');});
byId('reset').addEventListener('click',()=>{if(current){dirty=false;pending=null;populate(current);message('Form restored to running settings.');}});
byId('settings').addEventListener('submit',async event=>{
 event.preventDefault();if(!connected || saving || !event.target.reportValidity())return;
 const zone=['x','y','w','h'].map(k=>Number(byId(k).value));
 if(!zone.every(Number.isInteger)||zone[0]<0||zone[1]<0||zone[2]<1||zone[3]<1||zone[0]+zone[2]>100||zone[1]+zone[3]>100){message('Rectangle must fit within 0–100% of the view.');return;}
 const settings={enabled:byId('enabled').checked,rule:byId('rule').value,dwellSeconds:Number(byId('dwell').value),zone:zone.map(v=>v/100)};
 const body=new URLSearchParams({action:'update'}),scope='root.Bbox_restricted_zone.';
 for(const [k,v] of Object.entries({Enabled:settings.enabled?'yes':'no',Zone:zone.join(','),Rule:settings.rule,DwellSeconds:settings.dwellSeconds}))body.set(scope+k,v);
 saving=true;byId('fields').disabled=true;message('Saving…');
 try{if((await request('/axis-cgi/param.cgi',{method:'POST',body})).trim()!=='OK')throw Error('Camera rejected settings');pending={...settings,deadline:Date.now()+15000};message('Saved. Waiting for running values…');}catch(error){message(`Save not confirmed: ${error.message}. Some values may have changed.`);}finally{saving=false;byId('fields').disabled=!connected;}
});
poll();
