const {test}=require('node:test');
const assert=require('node:assert/strict');
const vm=require('node:vm');
const fs=require('node:fs');
const path=require('node:path');
function setup(){
 const nodes=new Map(); const context2d={clearRect(){},drawImage(){},setLineDash(){},strokeRect(){},putImageData(){},createImageData(w,h){return {data:new Uint8ClampedArray(w*h*4)};}};
 const get=id=>{if(!nodes.has(id)) nodes.set(id,{value:'',checked:false,dataset:{},handlers:{},addEventListener(e,f){this.handlers[e]=f;},reportValidity(){return true;},getContext(){return context2d;}});return nodes.get(id);};
 const context=vm.createContext({document:{getElementById:get,createElement:()=>({getContext:()=>context2d})},atob,AbortController,Date,URLSearchParams,setTimeout:()=>1,clearTimeout(){},fetch:async()=>{throw Error('offline');}});
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../app/html/app.js'),'utf8').replace(/poll\(\);\s*$/,''),context);
 return {get,render(s){context.s=s;vm.runInContext('render(s)',context);},fetch(f){context.fetch=f;},poll(){return vm.runInContext('poll()',context);}};
}
const state={enabled:true,zone:'20,20,60,60',pixelDelta:25,occupiedPercent:10,dwellSeconds:10,fresh:true,state:'Needs calibration',image:Buffer.alloc(14400,100).toString('base64'),width:640,height:360,pitch:640,fps:5,score:0,occupiedSeconds:0,calibrated:false,calibrating:false,calibrationId:0,calibrationMessage:''};
test('settings wait for applied values and ROI survives polling',async()=>{
 const ui=setup();ui.render(state);ui.get('x').value='10';ui.get('settings').handlers.input();ui.render(state);assert.equal(ui.get('x').value,'10');assert.equal(ui.get('calibrate').disabled,true);
 ui.fetch(async(url,options)=>{assert.equal(options.body.get('root.Vdo_inspection_zone.Zone'),'10,20,60,60');return {ok:true,text:async()=> 'OK'};});
 await ui.get('settings').handlers.submit({preventDefault(){},target:ui.get('settings')});
 assert.match(ui.get('save-message').textContent,/Waiting/);ui.render({...state,zone:'10,20,60,60'});assert.match(ui.get('save-message').textContent,/applied/);
});
test('invalid ROI never reaches parameter API',async()=>{
 const ui=setup();ui.render(state);ui.get('w').value=100;ui.fetch(async()=>assert.fail('must not submit'));
 await ui.get('settings').handlers.submit({preventDefault(){},target:ui.get('settings')});assert.match(ui.get('save-message').textContent,/fit/);
});
test('calibration uses POST and waits for camera acknowledgement',async()=>{
 const ui=setup();ui.render(state);
 ui.fetch(async(url,options)=>{assert.equal(url,'calibrate.cgi');assert.equal(options.method,'POST');assert.equal(options.headers['X-Zone-Action'],'calibrate');return {ok:true,text:async()=>'{"accepted":true}'};});
 await ui.get('calibrate').handlers.click();assert.equal(ui.get('calibrate').disabled,true);
 ui.render({...state,calibrationId:1,calibrating:true,calibrationFrames:3});assert.match(ui.get('calibration').textContent,/3\/10/);
 ui.render({...state,calibrationId:1,calibrated:true});assert.match(ui.get('calibration').textContent,/ready/);
});
test('disconnect and stale frames never report clear or allow calibration',async()=>{
 const ui=setup();ui.render({...state,state:'Clear',calibrated:true});await ui.poll();
 assert.equal(ui.get('state').textContent,'Unknown');assert.equal(ui.get('score').textContent,'—');assert.equal(ui.get('calibrate').disabled,true);
 ui.render({...state,fresh:false,state:'Unknown',image:''});assert.equal(ui.get('calibrate').disabled,true);
});
test('VAPIX error response is not a successful save',async()=>{
 const ui=setup();ui.render(state);ui.fetch(async()=>({ok:true,text:async()=> '# Error'}));
 await ui.get('settings').handlers.submit({preventDefault(){},target:ui.get('settings')});assert.match(ui.get('save-message').textContent,/not confirmed/);
});
