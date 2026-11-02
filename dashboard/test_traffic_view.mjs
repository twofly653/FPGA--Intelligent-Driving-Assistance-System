import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import vm from "node:vm";
const source=await readFile(new URL("drivesense/traffic-view.js",import.meta.url),"utf8");
const nodes=new Map();
const node=id=>{if(!nodes.has(id))nodes.set(id,{textContent:"",dataset:{}});return nodes.get(id);};
const root={innerHTML:"",querySelector:selector=>node(selector)};
let now=1000,tick;
const env={window:{},performance:{now:()=>now},setInterval:cb=>{tick=cb;}};
vm.runInNewContext(source,env);
const api=env.window.DriveTrafficView;
const p={mode:"LIVE",connected:true,traffic:{frame_seq:12,color:1,valid:1,confidence:95,countdown:28,digit_valid:1,age_ms:10,proc_ms:7,stale:false}};
let s=api.normalize(p);
assert.equal(s.color,"RED");assert.equal(s.digit,28);assert.equal(s.advice,"STOP · 停车等待");
for(const [color,expected]of [[1,"RED"],[2,"AMBER"],[3,"GREEN"]]){
 s=api.normalize({...p,traffic:{...p.traffic,color}});assert.equal(s.color,expected);
}
assert.ok(api.normalize({...p,traffic:{...p.traffic,color:3}}).advice.startsWith("GREEN"));
assert.equal(api.normalize(p,900).digit,28,"no interpolated countdown");
assert.equal(api.normalize(p,1490).valid,true);assert.equal(api.normalize(p,1491).valid,false);
for(const change of [{connected:false},{mode:"DEMO"},{mode:undefined},{traffic:{}},
 {traffic:{...p.traffic,valid:0}},{traffic:{...p.traffic,stale:true}},{traffic:{...p.traffic,color:0}},
 {traffic:{...p.traffic,frame_seq:null}},{traffic:{...p.traffic,age_ms:null}},{traffic:{...p.traffic,age_ms:NaN}},
 {traffic:{...p.traffic,confidence:101}},{traffic:{...p.traffic,color:"1"}}]){
 s=api.normalize({...p,...change});assert.equal(s.color,"UNKNOWN");assert.equal(s.digit,null);
}
for(const countdown of [-1,100,NaN,"28"]){
 s=api.normalize({...p,traffic:{...p.traffic,countdown}});assert.equal(s.color,"RED");assert.equal(s.digit,null);
}
s=api.normalize({...p,mode:"REPLAY"});assert.equal(s.source,"REPLAY · 回放数据");
api.mount(root);assert.ok(root.innerHTML.includes("真实板端结果"));
api.pushPacket(p);assert.equal(node("#cvTrafficCountdown").textContent,"28");
assert.equal(node("#cvTrafficColor").textContent,"红灯");
now+=900;tick();assert.equal(node("#cvTrafficCountdown").textContent,"28");
now+=700;tick();assert.equal(node("#cvTrafficCountdown").textContent,"--");assert.equal(node("#cvTrafficColor").textContent,"未识别");
api.pushPacket(p);assert.equal(node("#cvTrafficColor").textContent,"未识别","duplicate seq cannot renew expired local TTL");
now+=10;api.pushPacket({...p,traffic:{...p.traffic,frame_seq:13,color:3,countdown:9}});assert.equal(node("#cvTrafficCountdown").textContent,"9");
api.disconnect();assert.equal(node("#cvTrafficCountdown").textContent,"--");
assert.equal(node("#cvTrafficSource").textContent,"NO BOARD DATA");
const camera=await readFile(new URL("drivesense/camera-view.js",import.meta.url),"utf8");
assert.ok(camera.includes('id="cvTrafficRoot"'));assert.ok(camera.includes('id="cvObstacleRoot"'));
assert.ok(!camera.includes("SIMULATED FRAME"));
assert.ok(!camera.includes("TRAFFIC SIGNAL / DEMO"));
const html=await readFile(new URL("drivesense/index.html",import.meta.url),"utf8");
assert.ok(html.indexOf("traffic-view.js")<html.indexOf("camera-view.js"));
console.log("PASS traffic: color/digits/unknown, board-age + local expiration, replay/serial disconnect, no invented countdown, DOM clearing, explicit simulation labels");
