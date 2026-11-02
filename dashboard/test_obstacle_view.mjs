import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import vm from "node:vm";
const source = await readFile(new URL("drivesense/obstacle-view.js", import.meta.url), "utf8");
let now = 1000, tick, checks = 0;
const equal = (actual, expected, note) => { checks++; assert.equal(actual, expected, note); };
class Node {
  constructor() { this.textContent = ""; this.dataset = {}; this.style = {}; this.children = []; this.hidden = false; this.attributes = {}; }
  replaceChildren(...items) { this.children = items; }
  appendChild(item) { this.children.push(item); }
  setAttribute(k,v) { this.attributes[k] = v; }
  querySelector(k) { if (!this.nodes) this.nodes = new Map(); if (!this.nodes.has(k)) this.nodes.set(k,new Node()); return this.nodes.get(k); }
}
const root = new Node();
const env = { window:{}, document:{createElement:()=>new Node()}, performance:{now:()=>now}, setInterval:cb=>{tick=cb;} };
vm.runInNewContext(source, env); const api = env.window.DriveObstacleView;
const box = {id:5,xmin:0,ymin:0,xmax:639,ymax:479};
const packet = { mode:"LIVE", connected:true,
  camera:{kind:"CAMERA",sequence:42,image_mode:0,live:1,configured:1,width:640,height:480,age_ms:20,stale:false,source:"BOARD_UART"},
  obstacles:{kind:"OBSTACLE",result_seq:20,source_frame:98,state:0,available:1,age_ms:40,proc_ms:4,count:1,boxes:[box],stale:false,valid:1,source:"BOARD_UART"} };
const clone = v => JSON.parse(JSON.stringify(v));
let s = api.normalize(packet); equal(s.valid,true); equal(s.count,1); equal(s.boxes[0].xmax,639); equal(s.procMs,4);
for(let image_mode=0;image_mode<4;image_mode++) { const p=clone(packet);p.camera.image_mode=image_mode;equal(api.normalize(p).imageMode,image_mode); }
equal(api.normalize(packet,460).valid,true,"500 ms boundary is fresh");
equal(api.normalize(packet,461).valid,false,"source age expires at >500 ms");
for(const changes of [ {connected:false},{mode:"DEMO"},{mode:undefined} ]) equal(api.normalize({...packet,...changes}).boxes.length,0);
for(const changes of [ {kind:"NO"},{sequence:"42"},{image_mode:4},{width:320},{height:240},{live:0},{configured:0},{live:"1"},{configured:2},{stale:true},{stale:undefined},{age_ms:NaN},{age_ms:-1},{age_ms:1001},{source:"REPLAY"} ]) {
 const p=clone(packet);Object.assign(p.camera,changes);equal(api.normalize(p).boxes.length,0,JSON.stringify(changes));
}
for(const changes of [ {kind:"NO"},{result_seq:null},{source_frame:-1},{available:0},{valid:0},{valid:"1"},{stale:true},{stale:undefined},{age_ms:-1},{age_ms:null},{age_ms:501},{source:"REPLAY"},{count:9},{count:0},{boxes:[]},{boxes:null} ]) {
 const p=clone(packet);Object.assign(p.obstacles,changes);equal(api.normalize(p).valid,false,JSON.stringify(changes));
}
for(let state=1;state<=5;state++) { const p=clone(packet);p.obstacles.state=state;equal(api.normalize(p).count,null); }
for(const changes of [ {id:0},{id:65536},{id:"5"},{xmin:-1},{xmax:640},{ymin:-1},{ymax:480},{xmin:10,xmax:9},{ymin:12,ymax:11},{xmin:0.5},{xmax:NaN} ]) {
 const p=clone(packet);Object.assign(p.obstacles.boxes[0],changes);equal(api.normalize(p).valid,false,JSON.stringify(changes));
}
const noTarget=clone(packet);noTarget.obstacles.count=0;noTarget.obstacles.boxes=[];
s=api.normalize(noTarget);equal(s.valid,true);equal(s.count,0);equal(s.boxes.length,0);equal(s.status,"当前没有检出运动区域");
const eight=clone(packet);eight.obstacles.count=8; eight.obstacles.boxes=Array.from({length:8},(_,i)=>({id:i+1,xmin:i*70,ymin:i*40,xmax:i*70+60,ymax:i*40+30}));
equal(api.normalize(eight).count,8);
const badIds=clone(eight);badIds.obstacles.boxes[7].id=1;equal(api.normalize(badIds).valid,false,"all IDs unique");
const replay=clone(packet);replay.mode="REPLAY";replay.camera.source="REPLAY";replay.obstacles.source="REPLAY";
equal(api.normalize(replay).valid,true);equal(api.normalize(replay).source,"REPLAY · 回放数据");
// Cached packets received before routing/mounting appear immediately.
api.pushPacket(eight);api.mount(root);equal(root.querySelector("#cvObstacleBoxes").children.length,8);
equal(root.querySelector("#cvObstacleCount").textContent,"8");
equal(root.querySelector("#cvBoardImageMode").textContent,"彩色原画");
api.pushPacket(packet); const fullBox=root.querySelector("#cvObstacleBoxes").children[0];
equal(fullBox.style.width,"100%");equal(fullBox.style.height,"100%");equal(fullBox.style.left,"0%");
now+=300;tick();equal(root.querySelector("#cvObstacleBoxes").children.length,1);
api.pushPacket(packet);now+=161;tick();equal(root.querySelector("#cvObstacleBoxes").children.length,0,"repeated result cannot renew its local lifetime");
now+=50;api.pushPacket({...packet,camera:{...packet.camera,sequence:43},obstacles:{...packet.obstacles,result_seq:21,age_ms:0}});
equal(root.querySelector("#cvObstacleBoxes").children.length,1,"fresh sequence restores data");
now+=100;api.disconnect();equal(root.querySelector("#cvObstacleBoxes").children.length,0);
equal(root.querySelector("#cvObstacleSource").textContent,"NO BOARD DATA");
// Even changing obstacle results cannot extend an expired camera heartbeat.
now+=1000; api.pushPacket({...packet,camera:{...packet.camera,sequence:44,age_ms:0},obstacles:{...packet.obstacles,result_seq:22,age_ms:0}});
now+=1001;api.pushPacket({...packet,camera:{...packet.camera,sequence:44,age_ms:0},obstacles:{...packet.obstacles,result_seq:23,age_ms:0}});
equal(api.current().valid,false);equal(root.querySelector("#cvObstacleBoxes").children.length,0);
// A one-pixel inclusive corner coordinate stays in bounds.
now+=10; const corner=clone(packet); corner.camera.sequence=45;corner.obstacles.result_seq=24;corner.obstacles.boxes=[{id:65535,xmin:639,ymin:479,xmax:639,ymax:479}];
api.pushPacket(corner);equal(api.current().valid,true);equal(root.querySelector("#cvObstacleBoxes").children.length,1);
api.pushPacket({...noTarget,camera:{...noTarget.camera,sequence:46},obstacles:{...noTarget.obstacles,result_seq:25}});
equal(root.querySelector("#cvObstacleCount").textContent,"0");equal(root.querySelector("#cvObstacleBoxes").children.length,0,"explicit zero clears old list");

// Board-confirmed explicit manual control. No UART is opened by these tests.
const controlled=clone(packet);
controlled.camera.sequence=100; controlled.camera.age_ms=0;
controlled.obstacles.result_seq=100;controlled.obstacles.age_ms=0;
controlled.detection={kind:"DETECT",sequence:1,enabled:1,age_ms:0,stale:false,supported:true,can_control:true,source:"BOARD_UART"};
equal(api.normalize(controlled).controlReady,true);
equal(api.normalize(packet).controlReady,false,"R7 legacy is read-only");
for(const change of [{enabled:0},{enabled:null},{sequence:-1},{stale:true},{age_ms:1501},{source:"REPLAY"}]) {
 const p=clone(controlled);Object.assign(p.detection,change);equal(api.normalize(p).boxes.length,0,"manual gate "+JSON.stringify(change));
}
let replayControl=clone(controlled);replayControl.mode="REPLAY";replayControl.camera.source=replayControl.obstacles.source=replayControl.detection.source="REPLAY";
equal(api.normalize(replayControl).controlReady,false,"replay cannot write control");
equal(api.normalize(controlled,1501).detectionValid,false);
now+=2000;api.pushPacket(controlled);equal(root.querySelector("#cvDetectionOff").disabled,false);
equal(root.querySelector("#cvDetectionOn").disabled,true);
let sent=[], responsePacket=controlled;
env.fetch=async (url,options)=>{sent.push({url,options});return {ok:true,json:async()=>responsePacket};};
await api.requestControl(false);
equal(sent.length,1);equal(sent[0].url,"/api/obstacle/control");equal(sent[0].options.body,'{"enabled":false}');
equal(api.current().detectionEnabled,true,"POST does not optimistically turn off");
equal(root.querySelector("#cvObstacleBoxes").children.length,1,"wait for actual OFF before clearing");
equal(root.querySelector("#cvDetectionOn").disabled,true);equal(root.querySelector("#cvDetectionOff").disabled,true);
await api.requestControl(true);equal(sent.length,1,"only one request pending");
now+=10;const confirmedOff=clone(controlled);confirmedOff.detection.sequence=2;confirmedOff.detection.enabled=0;
api.pushPacket(confirmedOff);equal(api.current().detectionEnabled,false);equal(root.querySelector("#cvObstacleBoxes").children.length,0);
equal(root.querySelector("#cvObstacleCount").textContent,"0");equal(root.querySelector("#cvDetectionOn").disabled,false);
responsePacket=confirmedOff;await api.requestControl(true);equal(sent[1].options.body,'{"enabled":true}');
equal(api.current().detectionEnabled,false,"POST does not optimistically turn on");
now+=10;const confirmedOn=clone(controlled);confirmedOn.detection.sequence=3;
api.pushPacket(confirmedOn);equal(api.current().detectionEnabled,true);
equal(root.querySelector("#cvObstacleBoxes").children.length,0,"OFF then ON cannot revive cached boxes");
confirmedOn.obstacles.result_seq=101;api.pushPacket(confirmedOn);equal(root.querySelector("#cvObstacleBoxes").children.length,1,"new result restores boxes after enabling");
// Same heartbeat cannot renew its lifetime even when HTTP age is reset.
now+=1501;api.pushPacket(confirmedOn);equal(api.current().detectionValid,false);equal(root.querySelector("#cvObstacleBoxes").children.length,0);
equal(root.querySelector("#cvDetectionOff").disabled,true);
now+=10;const renewed=clone(controlled);renewed.camera.sequence=101;renewed.obstacles.result_seq=102;renewed.detection.sequence=4;
api.pushPacket(renewed);responsePacket=renewed;await api.requestControl(false);
now+=3501;tick();equal(api.current().detectionEnabled,null,"timeout does not invent requested state");
equal(root.querySelector("#cvDetectionOff").disabled,true);
const recovered=clone(renewed);recovered.camera.sequence=102;recovered.obstacles.result_seq=103;recovered.detection.sequence=5;
api.pushPacket(recovered);env.fetch=async()=>({ok:false,status:400,json:async()=>({error:"port busy"})});
await api.requestControl(false);equal(api.current().detectionEnabled,true);equal(root.querySelector("#cvDetectionCommand").textContent.includes("port busy"),true);
api.disconnect();await api.requestControl(true);equal(root.querySelector("#cvDetectionOn").disabled,true);

// Keep heartbeats fresh while confirmation is absent: timeout is actionable,
// with the actual enabled state unchanged and retry available.
now+=10;const awaiting=clone(controlled);awaiting.video_epoch=20;awaiting.camera.sequence=200;awaiting.obstacles.result_seq=200;awaiting.detection.sequence=200;
api.pushPacket(awaiting);responsePacket=awaiting;env.fetch=async()=>({ok:true,json:async()=>responsePacket});
await api.requestControl(false);
for(let n=1;n<=7;n++){now+=500;const p=clone(awaiting);p.camera.sequence+=n;p.obstacles.result_seq+=n;p.detection.sequence+=n;api.pushPacket(p);}
now+=1;tick();equal(api.current().detectionEnabled,true);equal(root.querySelector("#cvDetectionCommand").textContent.includes("确认超时"),true);
equal(root.querySelector("#cvDetectionOff").disabled,false,"retry enabled after timeout with fresh board state");
// Reboot epoch resets browser sequence-age anchors even if UART restarts at a
// sequence value the browser has previously seen.
const epochOff=clone(awaiting);epochOff.detection.enabled=0;epochOff.detection.sequence=300;epochOff.obstacles.result_seq=300;epochOff.camera.sequence=300;
api.pushPacket(epochOff);now+=5000;
const epochOn=clone(awaiting);epochOn.video_epoch=21;api.pushPacket(epochOn);
equal(api.current().detectionValid,true);equal(api.current().valid,true,"new boot does not inherit old disabled result fence");

// Delayed POST snapshots must not roll newer SSE state back. Keep the
// pending command unconfirmed first, so the sequence gate itself is tested.
now+=10;const raceStart=clone(controlled);raceStart.video_epoch=30;raceStart.seq=100;
raceStart.camera.sequence=500;raceStart.obstacles.result_seq=500;raceStart.detection.sequence=500;
api.pushPacket(raceStart);
let finishPost;
env.fetch=()=>new Promise(resolve=>{finishPost=resolve;});
const delayedPost=api.requestControl(false);
const newerSse=clone(raceStart);newerSse.seq=102;newerSse.camera.sequence=501;newerSse.camera.image_mode=2;newerSse.detection.sequence=501;
api.pushPacket(newerSse);
const oldPost=clone(raceStart);oldPost.seq=101;
finishPost({ok:true,json:async()=>oldPost});await delayedPost;
equal(api.current().imageMode,2,"older POST cannot overwrite newer SSE video state");
equal(api.current().detectionSequence,501,"older POST cannot overwrite newer DETECT state");
const raceOff=clone(newerSse);raceOff.seq=103;raceOff.detection.sequence=502;raceOff.detection.enabled=0;
api.pushPacket(raceOff);equal(api.current().detectionEnabled,false);
api.pushPacket(oldPost);equal(api.current().detectionEnabled,false,"older transport snapshot cannot restore ON");
// A new epoch accepts a reset transport counter, but old-epoch responses
// cannot override it even when their transport counter is much larger.
const raceNewBoot=clone(raceStart);raceNewBoot.video_epoch=31;raceNewBoot.seq=1;raceNewBoot.detection.sequence=1;
api.pushPacket(raceNewBoot);equal(api.current().detectionEnabled,true,"new epoch allows sequence reset");
const lateOldEpoch=clone(raceOff);lateOldEpoch.seq=10000;
api.pushPacket(lateOldEpoch);equal(api.current().detectionEnabled,true,"old epoch cannot overwrite new epoch");
// A lost HTTP/SSE connection starts a new transport session; a restarted
// bridge may begin its own epoch counter at zero.
api.disconnect();const restartedBridge=clone(raceStart);restartedBridge.video_epoch=0;restartedBridge.seq=0;
api.pushPacket(restartedBridge);equal(api.current().detectionValid,true,"bridge restart accepted after disconnect");
const camera=await readFile(new URL("drivesense/camera-view.js",import.meta.url),"utf8");
const html=await readFile(new URL("drivesense/index.html",import.meta.url),"utf8");
const css=await readFile(new URL("drivesense/camera-view.css",import.meta.url),"utf8");
for(const forbidden of ["SIMULATED FRAME","sceneSvg","data-cv-light","cvDistanceSlider","cv-live-image"])equal(camera.includes(forbidden),false,"no simulated camera "+forbidden);
for(const required of ['id="cvTrafficRoot"','id="cvObstacleRoot"','id="cvPhotoCapture"','id="cvPhotoImage"',"2592 × 1944"])equal(camera.includes(required),true,"preserve "+required);
equal(html.indexOf("obstacle-view.js")<html.indexOf("camera-view.js"),true);
equal(css.includes("aspect-ratio:4 / 3"),true);equal(css.includes("border:2px solid #00ff00"),true);
console.log(`PASS ${checks} hardware frontend checks: strict native coordinates, 8 boxes, mode, replay, source age, local expiration, repeat sequence, disconnect, zero-target clearing, confirmed manual control, POST/SSE ordering, boot epochs, no simulated camera, photo component retained`);
