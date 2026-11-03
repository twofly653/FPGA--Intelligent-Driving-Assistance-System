import assert from "node:assert/strict";
import {readFile,readdir} from "node:fs/promises";
import vm from "node:vm";
import {spawnSync} from "node:child_process";
const root=new URL("./drivesense/",import.meta.url), sandbox={};
vm.runInNewContext(await readFile(new URL("attitude-adapter.js",root),"utf8"),sandbox);
const {toFrame,toCompass,detailedQuaternion,fallbackQuaternion,rotate}=sandbox.DriveAttitudeAdapter;
const near=(a,b)=>a.forEach((v,i)=>assert.ok(Math.abs(v-b[i])<1e-10,`${a} != ${b}`));
assert.equal(toFrame({pitch:20}).pitch,-20);
assert.equal(toFrame({mode:"REPLAY"}).source,"replay");
assert.equal(toFrame({range_mm:-1}).distance,null);
const compass={heading_x10:1327,age_ms:3,stale:false,valid:1,detected:1,fresh:1,calibrated:1,aligned:1,collecting:0,level:1,direction_verified:1};
const packet={connected:true,imu:0,yaw:359,stale:true,compass};
assert.equal(toCompass(packet).heading_x10,1327);
assert.equal(toCompass(packet).valid,true,"CMP remains independent of missing/stale IMU attitude");
assert.equal(toCompass({connected:true,yaw:359}).heading_x10,null,"never invent magnetic north from TEL relative yaw");
for(const [field,value,status] of [["stale",true,"STALE"],["detected",0,"NO SENSOR"],["fresh",0,"STALE"],["calibrated",0,"CAL REQUIRED"],["aligned",0,"ALIGN REQUIRED"],["collecting",1,"CALIBRATING"],["level",0,"KEEP LEVEL"]]){
  const result=toCompass({...packet,compass:{...compass,[field]:value}});
  assert.equal(result.valid,false,field);assert.equal(result.heading_x10,null,field);assert.equal(result.status,status,field);
}
const unverified=toCompass({...packet,compass:{...compass,direction_verified:0}});
assert.equal(unverified.heading_x10,1327,"candidate value is visible for direction checking");
assert.equal(unverified.valid,false);assert.equal(unverified.status,"VERIFY DIRECTION");
assert.equal(toCompass({...packet,connected:false}).heading_x10,null);
assert.equal(toCompass(packet).selftest_result,0,"legacy frames preserve the old flag gates");
assert.equal(toCompass({...packet,compass:{...compass,selftest_result:1,selftest_status:0x10}}).valid,true);
for(const [selftest_result,status] of [[2,"SELFTEST FAILED"],[3,"SELFTEST INCONCLUSIVE"],[4,"SELFTEST IO ERROR"],[5,"SELFTEST TIMEOUT"]]){
  for(const flags of [{},{detected:0,fresh:0}]){
    const result=toCompass({...packet,compass:{...compass,...flags,selftest_result,selftest_status:0x80}});
    assert.equal(result.valid,false);assert.equal(result.heading_x10,null);assert.equal(result.status,status);
    assert.equal(result.selftest_status,0x80);
  }
}
assert.equal(toCompass({...packet,compass:{...compass,selftest_result:1,calibrated:0}}).status,"CAL REQUIRED","selftest PASS is not a calibration or accuracy certificate");
function rotateBody(v,r,p,h){
  const [x,y,z]=v,cr=Math.cos(r),sr=Math.sin(r),cp=Math.cos(p),sp=Math.sin(p),cy=Math.cos(h),sy=Math.sin(h);
  const a=[x,cr*y-sr*z,sr*y+cr*z],b=[cp*a[0]+sp*a[2],a[1],-sp*a[0]+cp*a[2]];
  return [cy*b[0]-sy*b[1],sy*b[0]+cy*b[1],b[2]];
}
const offset=42*Math.PI/180;
const display=v=>[Math.cos(offset)*v[0]+Math.sin(offset)*v[2],v[1],-Math.sin(offset)*v[0]+Math.cos(offset)*v[2]];
for(let i=0;i<200;i++){
  const r=i*7.3-180,p=i*.83-80,h=i*19.1,body=[.3,.7,-.4];
  const world=rotateBody(body,r*Math.PI/180,p*Math.PI/180,-h*Math.PI/180);
  const frame=toFrame({roll:r,pitch:p,yaw:h});
  near(rotate(detailedQuaternion(frame.roll,frame.pitch,frame.heading),[body[1],body[2],body[0]]),display([world[1],world[2],world[0]]));
  near(rotate(fallbackQuaternion(frame.roll,frame.pitch,frame.heading),[-body[1],body[2],-body[0]]),display([-world[1],world[2],-world[0]]));
}
for(const name of ["app.js","attitude-adapter.js","live-link.js","car3d-source.js","navigation.js","camera-view.js"]){
  const res=spawnSync(process.execPath,["--check",new URL(name,root).pathname.replace(/^\/([A-Z]:)/i,"$1")],{encoding:"utf8"});
  assert.equal(res.status,0,res.stderr);
}
console.log("PASS original DriveSense: single pitch conversion, 200 detailed+fallback rotations, replay/invalid data, JS syntax");

// User-selected R9 relative mode must show the same TEL integer as the LCD,
// even with no magnetometer calibration or a failed independent self-test.
for(const yaw of [0,1,132,180,359]) {
 const rel=toCompass({connected:true,imu:1,stale:false,yaw,heading_mode:"REL",compass:{valid:0,selftest_result:2}});
 assert.equal(rel.heading_x10,yaw*10);assert.equal(rel.valid,true);assert.equal(rel.relative,true);
 assert.equal(rel.status,"RELATIVE HEADING");
}
for(const override of [{stale:true},{connected:false},{imu:0},{yaw:NaN}]) {
 const rel=toCompass({connected:true,imu:1,stale:false,yaw:90,heading_mode:"REL",...override});
 assert.equal(rel.heading_x10,null);assert.equal(rel.valid,false);
}
console.log("PASS shared relative TEL heading, magnetic independence, invalid data clearing");
