(function() {
  "use strict";
  // Explicit smoke mode tests the original public API without opening a port.
  if(new URLSearchParams(location.search).get("smoke")==="1")return;
  const $=id=>document.getElementById(id), api=window.DriveDemo;
  let demo=false, mode="WAITING", lastMessage=0, lastSequence=-1;
  const strip=document.querySelector(".hardware-link");
  const source=document.querySelector(".source-pill");
  const sourceText=document.createElement("span");
  const sourceDot=document.createElement("span"); sourceDot.className="pulse-dot";
  source.replaceChildren(sourceDot,sourceText);
  const note=document.querySelector("#view-vehicle .section-heading p");
  const envTitle=document.querySelector(".sensors-panel .panel-kicker");
  const signalNote=document.querySelector(".signal-note");
  const compassTitle=document.querySelector(".compass-panel .panel-kicker");
  const compassLabel=document.querySelector(".compass-center small");
  const compassFoot=document.querySelector(".compass-footer b");
  const invalidNote=$("poseInvalidNote");
  let compassPending=false;
  let lastPacket={};
  const compassControls=["compassBegin","compassEnd","compassCancel","compassAlign","compassFlip","compassConfirm"];

  function setMode(next) {
    if(next===mode)return;
    mode=next;
    api.setTelemetryMode(next,"等待板卡姿态 · IMU 请静止对齐");
  }
  function setLabels(packet, text) {
    const replay=packet.mode==="REPLAY", isDemo=mode==="DEMO";
    strip.dataset.state=isDemo?"demo":replay?"replay":packet.imu&&!packet.stale?"live":"invalid";
    $("linkStatus").textContent=text;
    $("linkMetrics").textContent=isDemo?"SIMULATION / NO HARDWARE":`${Number(packet.rx_hz||0).toFixed(1)} Hz · 接收后 ${packet.age_ms==null?"—":Math.round(packet.age_ms)} ms`;
    sourceText.textContent=isDemo?"SIMULATION MODE":replay?"REPLAY · 非板卡实测":packet.connected?"LIVE UART · BOARD SYNC":"WAITING FOR BOARD";
    note.textContent=isDemo?"Interactive HDMI dashboard concept / sensor feed simulated":replay?"Recorded / synthetic TEL replay — not a board measurement":"Fused board attitude over UART / pitch positive nose up";
    document.querySelector(".side-nav-foot strong").textContent=isDemo?"DESIGN PREVIEW":replay?"REPLAY INPUT":packet.connected?"BOARD TELEMETRY":"BOARD OFFLINE";
    document.querySelector(".side-nav-foot small").textContent=isDemo?"Simulated values":packet.port||"Connect a COM port";
    document.querySelector(".panel-status").textContent=isDemo?"DEMO":packet.imu&&!packet.stale?replay?"REPLAY":"TRACKING":"NO VALID POSE";
    document.querySelector(".stage-bottom > span").textContent=isDemo?"DRAG MODEL TO ADJUST YAW / PITCH":"BOARD FOLLOWS · PITCH NOSE UP +";
    document.querySelector(".control-intro p").textContent=isDemo?"Explicit simulation. Sliders never control the board.":"Hardware pose input: sliders and presets are locked. ‘仅演示’ disconnects the port first.";
    document.querySelector(".control-intro h3").textContent=isDemo?"Test the movement":"Board movement";
    envTitle.textContent=isDemo?"ENVIRONMENT / DEMO":replay?"ENVIRONMENT / REPLAY":"ENVIRONMENT / UART";
    document.querySelector("#view-sensors .section-heading p").textContent=isDemo?"Environmental signals / simulated values":replay?"TEL replay / non-hardware validation":"Cached board measurements / invalid channels show —";
    signalNote.querySelector("span:last-of-type").textContent=isDemo?"SIMULATED INPUT":replay?"REPLAY INPUT":"BOARD UART INPUT";
    signalNote.querySelector("strong").textContent=isDemo?"No values in this mode are board measurements.":replay?"Controlled replay; the real serial port is not open.":"Only valid, recent board data is shown. Camera metadata is synchronized; video stays on HDMI.";
    document.querySelector(".signal-intro").textContent=isDemo?"Preview of sensor channels with simulated readings.":"Live values come from the same TEL frames as the attitude display. Invalid or expired measurements are shown as —.";
    document.querySelector(".distance-caption > span").textContent=isDemo?"FRONT DISTANCE / DEMO":replay?"FRONT DISTANCE / REPLAY":"FRONT DISTANCE / UART";
    const c=window.DriveAttitudeAdapter.toCompass(packet);
    compassTitle.textContent=isDemo?"COMPASS / DEMO":c.relative?"REL HEADING / BOARD":replay?"MAG COMPASS / REPLAY":"MAG COMPASS / BOARD";
    compassLabel.textContent=isDemo?"SIM HEADING":c.relative?"RELATIVE HEADING":c.valid?"MAGNETIC NORTH":c.heading_x10===null?"NO VALID HEADING":"PENDING DIRECTION CHECK";
    compassFoot.textContent=isDemo?"SIMULATED":c.status;
    document.querySelectorAll("#compassRose text").forEach((el,i)=>el.textContent=(c.relative?["0","90","180","270"]:["N","E","S","W"])[i]);
    document.querySelector(".compass-dial").setAttribute("aria-label",isDemo?"Simulated compass":`Board heading: ${c.message}`);
    $("compassCalibration").hidden=!!c.relative;
    document.querySelector(".compass-footer > span").textContent=c.relative?"RELATIVE SOURCE":"MAGNETOMETER";
    $("compassMessage").textContent=isDemo?"仅演示，不能校准真实板卡。":c.message;
    const count=value=>Number.isInteger(value)?value:"—";
    const selftestLabels=["NOT RUN","PASS","SAT FAIL","INCONCLUSIVE","IO ERROR","TIMEOUT"];
    $("compassMetrics").textContent=`覆盖 ${count(c.coverage)} / 8 · 样本 ${count(c.samples)} · X/Y 跨度 ${count(c.span_x)} / ${count(c.span_y)} mG · 参考 ${Number.isInteger(c.reference_x10)?(c.reference_x10/10).toFixed(1):"—"}° · 方向 ${c.direction===1?"+":"−"} · 拟合 ${Number.isInteger(c.fit_error_ppm)&&c.fit_error_ppm>=0?(c.fit_error_ppm/10000).toFixed(1)+"%":"—"} · 倾斜 ${Number.isInteger(c.tilt_deg10)&&c.tilt_deg10>=0?(c.tilt_deg10/10).toFixed(1)+"°":"—"} · 自检 ${selftestLabels[c.selftest_result]||"UNKNOWN"}${c.selftest_result?" / 0x"+c.selftest_status.toString(16).padStart(2,"0").toUpperCase():""}`;
    compassControls.forEach(id=>$(id).disabled=compassPending||isDemo||replay||!packet.connected);
    $("compassReference").disabled=compassPending||isDemo||replay||!packet.connected;
    document.querySelector(".footer span:last-child").textContent=isDemo?"v0.3 · SIMULATION":replay?"v0.3 + UART · REPLAY":"v0.3 + UART · BOARD SYNC";
    ["rollRate","pitchRate","yawRate"].forEach((id,i)=>{
      const line=$(id).parentElement;
      if(line.firstChild.nodeType===Node.TEXT_NODE)line.firstChild.textContent=isDemo?"ANGULAR RATE ":`BODY ω${"xyz"[i]} `;
    });
  }

  function apply(packet) {
    if(demo)return;
    if(window.DriveTrafficView)window.DriveTrafficView.pushPacket(packet);
    if(window.DriveCameraView)window.DriveCameraView.pushPacket(packet);
    lastPacket=packet;
    lastMessage=performance.now();
    setMode(packet.mode==="REPLAY"?"REPLAY":"LIVE");
    const old=lastSequence;
    lastSequence=packet.seq;
    const hasFrame=packet.age_ms!=null, valid=packet.connected&&!packet.stale;
    let status=packet.error||(!packet.connected?"未连接串口 · 选择端口后点击连接":!hasFrame?"串口已连接，等待 TEL · 按 BTN0 启动板卡":packet.stale?"遥测中断 · STALE":!packet.imu?"TEL 已连接 · IMU 等待静止初始化或重新对齐":packet.mode==="REPLAY"?"回放验证 · 非板卡实测":"真实板卡姿态 · 旋转开发板查看实时变化");
    if(!valid) {
      api.invalidate(status);
    } else if(packet.seq!==old || !api.telemetryState().poseValid) {
      api.pushFrame(window.DriveAttitudeAdapter.toFrame(packet));
      if(!packet.imu)invalidNote.textContent=status;
    }
    api.pushCompass(window.DriveAttitudeAdapter.toCompass(packet));
    setLabels(packet,status);
    document.documentElement.dataset.telemetryMode=mode;
    document.documentElement.dataset.telemetrySeq=packet.seq;
    document.documentElement.dataset.telemetryValid=String(!!packet.imu&&valid);
  }

  async function refresh() {
    try {
      const old=$("linkPort").value;
      const obj=await(await fetch("/api/ports",{cache:"no-store"})).json();
      $("linkPort").replaceChildren(...obj.ports.map(p=>new Option(p,p)));
      if(!obj.ports.length)$("linkPort").add(new Option("无串口",""));
      if(obj.ports.includes(old))$("linkPort").value=old;
    } catch(error) { $("linkStatus").textContent=`本机桥接服务不可用：${error.message}`; }
  }
  async function command(name,data={}) {
    const response=await fetch(`/api/${name}`,{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(data)});
    const obj=await response.json();
    if(!response.ok)throw new Error(obj.error||response.status);
    return obj;
  }
  async function compassCommand(action) {
    const data={action};
    if(action==="ref") {
      const field=$("compassReference"),value=Number(field.value);
      if(!field.value.trim()||!field.checkValidity()||!Number.isFinite(value)||value<0||value>=360) {
        $("compassCommandStatus").textContent="请输入手机磁北角 0.0–359.9°。";return;
      }
      data.heading_x10=Math.round(value*10);
    }
    compassPending=true;compassControls.forEach(id=>$(id).disabled=true);
    $("compassCommandStatus").textContent="正在发送校准命令…";
    try {
      const packet=await command("compass",data);
      apply(packet);
      $("compassCommandStatus").textContent="命令已发送；以板端后续状态为准。";
    } catch(error) {$("compassCommandStatus").textContent=error.message;}
    finally {compassPending=false;setLabels(lastPacket,$("linkStatus").textContent);}
  }
  [["compassBegin","begin"],["compassEnd","end"],["compassCancel","cancel"],
   ["compassAlign","ref"],["compassFlip","flip"],["compassConfirm","confirm"]].forEach(([id,action])=>$(id).onclick=()=>compassCommand(action));
  $("linkRefresh").onclick=refresh;
  $("linkConnect").onclick=async()=>{
    demo=false; setMode("WAITING"); lastSequence=-1;
    try { apply(await command("connect",{port:$("linkPort").value})); }
    catch(error) { api.invalidate(error.message); $("linkStatus").textContent=error.message; }
  };
  $("linkDisconnect").onclick=async()=>{
    demo=false;
    try { apply(await command("disconnect")); }
    catch(error) { api.invalidate(error.message); $("linkStatus").textContent=error.message; }
  };
  $("linkDemo").onclick=async()=>{
    try {
      await command("disconnect");
      demo=true; setMode("DEMO");
      if(window.DriveTrafficView)window.DriveTrafficView.disconnect();
      if(window.DriveCameraView)window.DriveCameraView.disconnect();
      api.pushFrame({distance:2.8}); api.playDemo();
      setLabels({},"仅演示 · 已释放串口，所有姿态与环境值均为模拟");
      document.documentElement.dataset.telemetryMode="DEMO";
      document.documentElement.dataset.telemetryValid="false";
    } catch(error) { $("linkStatus").textContent=`未能释放串口，保持实测模式：${error.message}`; }
  };
  api.setTelemetryMode("WAITING","等待板卡姿态 · 连接 COM 后按 BTN0 启动");
  refresh();
  const events=new EventSource("/events");
  events.onmessage=event=>{ try{apply(JSON.parse(event.data));}catch(error){api.invalidate(`遥测处理失败：${error.message}`);} };
  function markStale(note) {
    if(window.DriveTrafficView)window.DriveTrafficView.disconnect();
    if(window.DriveCameraView)window.DriveCameraView.disconnect();
    api.invalidate(note);
    setLabels({...lastPacket,connected:false,stale:true},note);
    document.documentElement.dataset.telemetryValid="false";
  }
  events.onerror=()=>{if(!demo)markStale("本机服务断连 · 正在重连");};
  setInterval(()=>{if(!demo&&lastMessage&&performance.now()-lastMessage>650)markStale("遥测中断 · STALE");},100);
  addEventListener("beforeunload",()=>events.close());
})();
