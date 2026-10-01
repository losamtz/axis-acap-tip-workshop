'use strict';
const byId = id => document.getElementById(id);
let current = null, dirty = false, saving = false, pending = null, calibrationPending = null, connected = false;
const message = text => { byId('save-message').textContent = text; };
const canvas = byId('preview'), ctx = canvas.getContext('2d');
const sample = document.createElement('canvas'); sample.width = 160; sample.height = 90;
const sampleCtx = sample.getContext('2d');
function populate(s) {
  byId('enabled').checked = s.enabled;
  const zone = s.zone.split(','); ['x','y','w','h'].forEach((key,i) => { byId(key).value = zone[i]; });
  byId('delta').value = s.pixelDelta; byId('percent').value = s.occupiedPercent; byId('dwell').value = s.dwellSeconds;
}
function controls() {
  byId('fields').disabled = !connected || saving;
  byId('calibrate').disabled = !connected || !current?.fresh || !current?.enabled || current?.calibrating || dirty || saving || !!pending || !!calibrationPending;
}
function draw(s) {
  ctx.clearRect(0,0,640,360);
  if (!s.fresh || !s.image) return;
  const bytes = atob(s.image);
  if (bytes.length !== 160*90) throw new Error('Invalid preview');
  const pixels = sampleCtx.createImageData(160,90);
  for (let i=0;i<bytes.length;i++) {
    pixels.data[i*4] = pixels.data[i*4+1] = pixels.data[i*4+2] = bytes.charCodeAt(i); pixels.data[i*4+3]=255;
  }
  sampleCtx.putImageData(pixels,0,0); ctx.drawImage(sample,0,0,640,360);
  const rectangle = (zone,color,dashed) => {
    const [x,y,w,h]=zone; ctx.strokeStyle=color; ctx.lineWidth=3; ctx.setLineDash(dashed ? [8,6] : []);
    ctx.strokeRect(x*6.4,y*3.6,w*6.4,h*3.6);
  };
  rectangle(s.zone.split(',').map(Number),'#ffcf32',false);
  if (dirty) rectangle(['x','y','w','h'].map(k => Number(byId(k).value)),'#28bfff',true);
  ctx.setLineDash([]);
}
function render(s) {
  current=s; connected=true;
  byId('connection').textContent='Connected'; byId('connection').dataset.state='online'; byId('offline').hidden=true;
  byId('state').textContent=s.state;
  byId('state').className=`badge ${s.state==='Clear' ? 'fresh' : 'stale'}`;
  byId('score').textContent=s.fresh && s.calibrated ? `${s.score.toFixed(1)}%` : '—';
  byId('duration').textContent=s.fresh && s.calibrated ? `${s.occupiedSeconds}s` : '—';
  byId('stream').textContent=`NV12 ${s.width} × ${s.height} · row stride ${s.pitch} bytes · ${s.fps.toFixed(1)} frames/s · sampled 160 × 90`;
  byId('error').textContent=s.error || (s.fresh ? 'Receiving camera frames.' : 'Waiting for frames.');
  if (pending && ['enabled','zone','pixelDelta','occupiedPercent','dwellSeconds'].every(k => s[k]===pending[k])) {
    pending=null; dirty=false; message('Settings applied. Clear the zone and calibrate again.');
  } else if (pending && Date.now()>pending.deadline) {
    pending=null; message('Running settings are not confirmed. Use current values before retrying.');
  }
  if (!dirty && !pending && !saving) populate(s);
  if (calibrationPending && s.calibrationId!==calibrationPending.id) calibrationPending=null;
  if (calibrationPending && Date.now()>calibrationPending.deadline) {
    calibrationPending=null; byId('calibration').textContent='Calibration request not confirmed. Check camera status and retry.';
  } else if (!calibrationPending) {
    byId('calibration').textContent=s.calibrating ? `Keep zone empty: ${s.calibrationFrames}/10 frames.` : s.calibrated ? 'Empty reference ready.' : s.calibrationMessage || 'Clear the zone and calibrate before monitoring.';
  }
  draw(s); controls();
}
async function request(url,options={}) {
  const controller=new AbortController(), timer=setTimeout(()=>controller.abort(),5000);
  try {
    const response=await fetch(url,{credentials:'same-origin',cache:'no-store',...options,signal:controller.signal});
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    return await response.text();
  } finally { clearTimeout(timer); }
}
async function poll() {
  try { render(JSON.parse(await request('status.cgi'))); }
  catch (error) {
    connected=false; byId('offline').hidden=false; byId('connection').textContent='Disconnected'; byId('connection').dataset.state='offline';
    byId('state').textContent='Unknown'; byId('score').textContent='—'; byId('duration').textContent='—'; ctx.clearRect(0,0,640,360); controls();
  } finally { setTimeout(poll,500); }
}
byId('settings').addEventListener('input',()=>{dirty=true; pending=null; message('Unsaved changes. Saving requires recalibration.'); if(current) draw(current); controls();});
byId('reset').addEventListener('click',()=>{if(current){dirty=false;pending=null;populate(current);draw(current);message('Form restored to running values.');controls();}});
byId('settings').addEventListener('submit',async event=>{
  event.preventDefault(); if(!connected || saving || !event.target.reportValidity()) return;
  const [x,y,w,h]=['x','y','w','h'].map(k=>Number(byId(k).value));
  if(![x,y,w,h].every(Number.isInteger) || x<0 || y<0 || w<1 || h<2 || x+w>100 || y+h>100){message('The rectangle must fit within 0–100% of the image.');return;}
  const settings={enabled:byId('enabled').checked,zone:[x,y,w,h].join(','),pixelDelta:Number(byId('delta').value),occupiedPercent:Number(byId('percent').value),dwellSeconds:Number(byId('dwell').value)};
  const body=new URLSearchParams({action:'update'}), scope='root.Vdo_inspection_zone.';
  for(const [name,value] of Object.entries({Enabled:settings.enabled?'yes':'no',Zone:settings.zone,PixelDelta:settings.pixelDelta,OccupiedPercent:settings.occupiedPercent,DwellSeconds:settings.dwellSeconds})) body.set(scope+name,value);
  saving=true; controls(); message('Saving…');
  try {
    const result=await request('/axis-cgi/param.cgi',{method:'POST',body});
    if(result.trim()!=='OK') throw new Error('Camera rejected settings');
    pending={...settings,deadline:Date.now()+15000};message('Saved. Waiting for running values…');
  } catch(error){message(`Save not confirmed: ${error.message}. Some settings may have changed.`);}
  finally{saving=false;controls();}
});
byId('calibrate').addEventListener('click',async()=>{
  if(byId('calibrate').disabled) return;
  calibrationPending={id:current.calibrationId,deadline:Date.now()+10000}; controls();
  byId('calibration').textContent='Requesting calibration. Keep the zone empty…';
  try { await request('calibrate.cgi',{method:'POST',headers:{'X-Zone-Action':'calibrate'}}); }
  catch(error){calibrationPending=null;byId('calibration').textContent=`Calibration not confirmed: ${error.message}`;controls();}
});
poll();
