const {test}=require('node:test');const assert=require('node:assert/strict');const vm=require('node:vm');const fs=require('node:fs');const path=require('node:path');
function setup(){
 const nodes=new Map();const get=id=>{if(!nodes.has(id))nodes.set(id,{value:'',dataset:{},handlers:{},addEventListener(e,f){this.handlers[e]=f;},reportValidity(){return true;},getContext(){return {clearRect(){},strokeRect(){}};}});return nodes.get(id);};
 const context=vm.createContext({document:{getElementById:get},Date,URLSearchParams,AbortController,setTimeout:()=>1,clearTimeout(){},fetch:async()=>{throw Error('offline');}});
 vm.runInContext(fs.readFileSync(path.join(__dirname,'../app/html/app.js'),'utf8').replace(/poll\(\);\s*$/,''),context);
 return {get,render(s){context.s=s;vm.runInContext('render(s)',context);},fetch(f){context.fetch=f;},poll(){return vm.runInContext('poll()',context);}};
}
const state={enabled:true,rule:'center',zone:[.35,.25,.3,.5],object:[.05,.4,.12,.2],dwellSeconds:3,inside:false,alarm:false,entries:0,insideSeconds:0,drawError:''};
test('edits survive polling and save waits for running values',async()=>{
 const ui=setup();ui.render(state);ui.get('x').value=20;ui.get('settings').handlers.input();ui.render(state);assert.equal(ui.get('x').value,20);
 ui.fetch(async(url,o)=>{assert.equal(o.body.get('root.Bbox_restricted_zone.Zone'),'20,25,30,50');return {ok:true,text:async()=> 'OK'};});
 await ui.get('settings').handlers.submit({preventDefault(){},target:ui.get('settings')});assert.match(ui.get('save-message').textContent,/Waiting/);
 ui.render({...state,zone:[.2,.25,.3,.5]});assert.match(ui.get('save-message').textContent,/applied/);
});
test('invalid zone is blocked and VAPIX errors are not successful saves',async()=>{
 const ui=setup();ui.render(state);ui.get('w').value=100;ui.fetch(async()=>assert.fail('must not submit'));
 await ui.get('settings').handlers.submit({preventDefault(){},target:ui.get('settings')});assert.match(ui.get('save-message').textContent,/fit/);
 ui.render(state);ui.fetch(async()=>({ok:true,text:async()=> '# Error'}));await ui.get('settings').handlers.submit({preventDefault(){},target:ui.get('settings')});assert.match(ui.get('save-message').textContent,/not confirmed/);
});
test('drawing error stays separate from simulated alarm; disconnect becomes unknown',async()=>{
 const ui=setup();ui.render({...state,inside:true,alarm:true,drawError:'BBox failed'});assert.equal(ui.get('state').textContent,'Dwell exceeded');assert.equal(ui.get('draw-status').textContent,'BBox failed');await ui.poll();assert.equal(ui.get('state').textContent,'Unknown');assert.equal(ui.get('fields').disabled,true);
 ui.render({...state,enabled:false});assert.equal(ui.get('state').textContent,'Disabled');
});
