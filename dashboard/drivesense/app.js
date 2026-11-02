(function () {
  "use strict";

  const $ = (id) => document.getElementById(id);
  const canvas = $("carCanvas");
  const stage = $("vehicleStage");
  const ctx = canvas.getContext("2d", { alpha: true });
  let detailedCar = null;
  const rose = $("compassRose");
  const RAD = Math.PI / 180;
  const DEG = 180 / Math.PI;
  const clamp = (v, min, max) => Math.max(min, Math.min(max, v));
  const valid = (v) => typeof v === "number" && Number.isFinite(v);
  const wrap = (v) => ((v % 360) + 360) % 360;
  const signed = (v) => (v < 0 ? "−" : "+") + Math.abs(v).toFixed(1) + "°";

  const state = {
    auto: false,
    visible: true,
    external: false,
    telemetryMode: "DEMO",
    poseValid: true,
    magnetic: false,
    compass: {valid:false,heading_x10:null,status:"WAITING CMP"},
    distanceState: null,
    bodyRates: null,
    zoom: 1,
    target: { roll: 0, pitch: 0, yaw: 42 },
    pose: { roll: 0, pitch: 0, yaw: 42 },
    rate: { roll: 0, pitch: 0, yaw: 0 },
    metrics: { temperature: 26.4, humidity: 48, pressure: 1013, lux: 460, distance: 2.8 },
    drag: null,
    lastTime: 0,
    lastUi: 0,
    sceneWidth: 0,
    sceneHeight: 0
  };

  function svg(tag, attrs, text) {
    const el = document.createElementNS("http://www.w3.org/2000/svg", tag);
    Object.keys(attrs).forEach((key) => el.setAttribute(key, attrs[key]));
    if (text !== undefined) el.textContent = text;
    return el;
  }

  function buildCompass() {
    rose.appendChild(svg("circle", { cx: 120, cy: 120, r: 102, fill: "none", stroke: "#3a5a69", "stroke-width": 1 }));
    for (let d = 0; d < 360; d += 5) {
      const a = d * RAD - Math.PI / 2;
      const major = d % 30 === 0;
      const r0 = major ? 90 : 96;
      const r1 = 102;
      rose.appendChild(svg("line", {
        x1: (120 + Math.cos(a) * r0).toFixed(2), y1: (120 + Math.sin(a) * r0).toFixed(2),
        x2: (120 + Math.cos(a) * r1).toFixed(2), y2: (120 + Math.sin(a) * r1).toFixed(2),
        stroke: major ? "#98cad1" : "#547888", "stroke-width": major ? 1.7 : .8
      }));
    }
    ["N", "E", "S", "W"].forEach((name, i) => {
      const a = i * 90 * RAD - Math.PI / 2;
      rose.appendChild(svg("text", {
        x: (120 + Math.cos(a) * 75).toFixed(2), y: (120 + Math.sin(a) * 75 + 5).toFixed(2),
        fill: i === 0 ? "#f4c777" : "#d7eaf1", "font-size": i === 0 ? 17 : 14,
        "font-weight": 700, "text-anchor": "middle", "font-family": "Segoe UI, Arial"
      }, name));
    });
    rose.appendChild(svg("circle", { cx: 120, cy: 120, r: 67, fill: "none", stroke: "#284a5b", "stroke-width": 1 }));
    rose.appendChild(svg("path", { d: "M120 42 115 56h10Z", fill: "#f4c777" }));
  }

  function cardinal(deg) {
    const labels = ["NORTH", "NORTHEAST", "EAST", "SOUTHEAST", "SOUTH", "SOUTHWEST", "WEST", "NORTHWEST"];
    return labels[Math.round(wrap(deg) / 45) % 8];
  }

  function resizeCanvas() {
    const rect = stage.getBoundingClientRect();
    const dpr = Math.min(window.devicePixelRatio || 1, 2);
    state.sceneWidth = Math.max(1, rect.width);
    state.sceneHeight = Math.max(1, rect.height);
    canvas.width = Math.round(state.sceneWidth * dpr);
    canvas.height = Math.round(state.sceneHeight * dpr);
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    if (detailedCar) detailedCar.resize(state.sceneWidth, state.sceneHeight);
  }

  const eye = normalize([5.7, 5.0, -8.8]);
  const cameraRight = normalize(cross([0, 1, 0], eye));
  const cameraUp = normalize(cross(eye, cameraRight));
  function dot(a, b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
  function cross(a, b) { return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]; }
  function normalize(v) { const l = Math.hypot(v[0], v[1], v[2]); return v.map((x) => x / l); }

  function rotateVehicle(p) {
    // Fallback mesh points toward -Z, unlike the detailed +Z asset.
    return window.DriveAttitudeAdapter.rotate(window.DriveAttitudeAdapter.fallbackQuaternion(state.pose.roll,state.pose.pitch,state.pose.yaw),p);
  }

  function project(p) {
    const w = state.sceneWidth, h = state.sceneHeight;
    const distance = 11;
    const depth = distance - dot(p, eye);
    const factor = distance / depth;
    const scale = Math.min(w / 6.35, h / 5.2) *
                  (w < 700 ? 1.17 : 1.4) * state.zoom;
    return {
      x: w * .5 + dot(p, cameraRight) * scale * factor,
      y: h * .56 - dot(p, cameraUp) * scale * factor,
      depth
    };
  }

  const faces = [];
  function face(points, fill, stroke, glow) {
    const transformed = points.map(rotateVehicle);
    faces.push({
      points: transformed,
      fill,
      stroke: stroke || "rgba(193,248,252,.16)",
      glow: glow || 0,
      depth: transformed.reduce((sum, p) => sum + project(p).depth, 0) / points.length
    });
  }
  function box(x0, x1, y0, y1, z0, z1, material) {
    face([[x0,y1,z0],[x1,y1,z0],[x1,y1,z1],[x0,y1,z1]], material.top);
    face([[x0,y0,z0],[x0,y1,z0],[x0,y1,z1],[x0,y0,z1]], material.left);
    face([[x1,y0,z0],[x1,y1,z0],[x1,y1,z1],[x1,y0,z1]], material.right);
    face([[x0,y0,z0],[x1,y0,z0],[x1,y1,z0],[x0,y1,z0]], material.front);
    face([[x0,y0,z1],[x1,y0,z1],[x1,y1,z1],[x0,y1,z1]], material.back);
  }

  function wheel(side, z) {
    const count = 16;
    const x0 = side * .83, x1 = side * 1.17, cy = .37, radius = .43;
    const ring = (x, r, i) => {
      const a = i * Math.PI * 2 / count;
      return [x, cy + Math.sin(a) * r, z + Math.cos(a) * r];
    };
    for (let i = 0; i < count; i++) {
      const shade = i % 2 ? "#111a22" : "#1b2630";
      face([ring(x0,radius,i), ring(x1,radius,i), ring(x1,radius,i+1), ring(x0,radius,i+1)], shade, "rgba(28,48,57,.3)");
      face([ring(x1 + side*.012,radius,i), ring(x1 + side*.012,radius,i+1),
            ring(x1 + side*.012,.27,i+1), ring(x1 + side*.012,.27,i)], "#25333e", "rgba(73,110,119,.35)");
      face([ring(x1 + side*.018,.27,i), ring(x1 + side*.018,.27,i+1),
            ring(x1 + side*.018,.12,i+1), ring(x1 + side*.018,.12,i)], i % 4 === 0 ? "#89b8bd" : "#5c8992", "rgba(135,194,201,.3)");
    }
    const hub = [];
    for (let i = 0; i < count; i++) hub.push(ring(x1 + side*.022,.12,i));
    face(hub, "#1b404e", "#7cb9bf");
  }

  function buildCarFaces() {
    faces.length = 0;
    wheel(-1, -1.45); wheel(1, -1.45);
    wheel(-1, 1.42); wheel(1, 1.42);
    box(-.91,.91,.25,.77,-2.3,2.18, {
      top:"#7bdce6", left:"#3c899d", right:"#479eb1", front:"#529fb0", back:"#337889"
    });
    face([[-.91,.77,-2.3],[.91,.77,-2.3],[.88,.9,-.98],[-.88,.9,-.98]], "#a6f3f0");
    face([[-.91,.77,1.4],[.91,.77,1.4],[.91,.77,2.18],[-.91,.77,2.18]], "#73ccd6");
    face([[-.9,.5,-2.305],[.9,.5,-2.305],[.9,.62,-2.305],[-.9,.62,-2.305]], "#173342", "#517f8d");
    face([[-.48,.42,-2.314],[.48,.42,-2.314],[.48,.56,-2.314],[-.48,.56,-2.314]], "#0a2230", "#2b6070");
    face([[-.79,.61,-2.319],[-.46,.61,-2.319],[-.46,.72,-2.319],[-.79,.72,-2.319]], "#edffff", "#c9fffd", 10);
    face([[.46,.61,-2.319],[.79,.61,-2.319],[.79,.72,-2.319],[.46,.72,-2.319]], "#edffff", "#c9fffd", 10);
    face([[-.84,.56,2.188],[-.52,.56,2.188],[-.52,.72,2.188],[-.84,.72,2.188]], "#f67c75", "#ffb29b", 6);
    face([[.52,.56,2.188],[.84,.56,2.188],[.84,.72,2.188],[.52,.72,2.188]], "#f67c75", "#ffb29b", 6);

    const bf=-1.03, rf=-.36, rb=.95, bb=1.42;
    const base=.84, roof=1.48, halfBase=.79, halfRoof=.66;
    face([[-halfRoof,roof,rf],[halfRoof,roof,rf],[halfRoof,roof,rb],[-halfRoof,roof,rb]], "#c0ffff");
    face([[-halfBase,base,bf],[halfBase,base,bf],[halfRoof,roof,rf],[-halfRoof,roof,rf]], "#173e51", "#82c8d3");
    face([[-halfRoof,roof,rb],[halfRoof,roof,rb],[halfBase,base,bb],[-halfBase,base,bb]], "#28566a", "#93d6dd");
    face([[-halfBase,base,bf],[-halfRoof,roof,rf],[-halfRoof,roof,rb],[-halfBase,base,bb]], "#4ca4b4");
    face([[halfBase,base,bf],[halfRoof,roof,rf],[halfRoof,roof,rb],[halfBase,base,bb]], "#60bcca");
    face([[-.8,.92,-.91],[-.68,1.4,-.31],[-.68,1.4,.28],[-.8,.92,.22]], "#173d52", "#95d4dc");
    face([[-.8,.92,.38],[-.68,1.4,.37],[-.68,1.4,.87],[-.8,.92,1.28]], "#1b4457", "#95d4dc");
    face([[.8,.92,-.91],[.68,1.4,-.31],[.68,1.4,.28],[.8,.92,.22]], "#204b60", "#a5e1e5");
    face([[.8,.92,.38],[.68,1.4,.37],[.68,1.4,.87],[.8,.92,1.28]], "#234d60", "#a5e1e5");
    face([[-.79,.83,-1.02],[-.74,.83,-1.02],[-.74,.83,1.38],[-.79,.83,1.38]], "#d2ffff");
    face([[.74,.83,-1.02],[.79,.83,-1.02],[.79,.83,1.38],[.74,.83,1.38]], "#d2ffff");
    face([[0,.91,-2.19],[.045,.91,-2.19],[.045,.91,-1.07],[0,.91,-1.07]], "#55b8c4");
    face([[-.9,.63,-2.08],[-.91,.63,2.03],[-.91,.69,2.03],[-.9,.69,-2.08]], "#8ae7eb");
    face([[.9,.63,-2.08],[.91,.63,2.03],[.91,.69,2.03],[.9,.69,-2.08]], "#b1ffff");
  }

  function path(points, fill, stroke, width) {
    if (!points.length) return;
    ctx.beginPath();
    ctx.moveTo(points[0].x, points[0].y);
    for (let i = 1; i < points.length; i++) ctx.lineTo(points[i].x, points[i].y);
    ctx.closePath();
    ctx.fillStyle = fill;
    ctx.fill();
    if (stroke) {
      ctx.strokeStyle = stroke;
      ctx.lineWidth = width || 1;
      ctx.stroke();
    }
  }

  function line3(a, b, color, width, alpha) {
    const p = project(a), q = project(b);
    ctx.globalAlpha = alpha === undefined ? 1 : alpha;
    ctx.beginPath(); ctx.moveTo(p.x,p.y); ctx.lineTo(q.x,q.y);
    ctx.strokeStyle = color; ctx.lineWidth = width; ctx.stroke();
    ctx.globalAlpha = 1;
  }

  function drawGround() {
    const w = state.sceneWidth, h = state.sceneHeight;
    const glow = ctx.createRadialGradient(w*.5,h*.62,30,w*.5,h*.62,w*.4);
    glow.addColorStop(0,"rgba(48,140,151,.16)");
    glow.addColorStop(1,"rgba(14,60,85,0)");
    ctx.fillStyle=glow; ctx.fillRect(0,0,w,h);
    for (let x=-8; x<=8; x++) line3([x,-.11,-8],[x,-.11,8], "#427080", 1, Math.abs(x)%4===0 ? .24 : .11);
    for (let z=-8; z<=8; z++) line3([-8,-.11,z],[8,-.11,z], "#427080", 1, Math.abs(z)%4===0 ? .24 : .11);
    const center=project([0,-.11,0]);
    ctx.save();
    ctx.translate(center.x,center.y+13);
    ctx.rotate(-.33);
    ctx.scale(1,.34);
    const shadow=ctx.createRadialGradient(0,0,20,0,0,215*state.zoom);
    shadow.addColorStop(0,"rgba(0,5,11,.57)");
    shadow.addColorStop(.6,"rgba(0,10,20,.33)");
    shadow.addColorStop(1,"rgba(0,10,20,0)");
    ctx.fillStyle=shadow;
    ctx.beginPath();ctx.arc(0,0,215*state.zoom,0,Math.PI*2);ctx.fill();
    ctx.restore();
    ctx.save();
    ctx.setLineDash([3,9]);
    for (let r=3.2;r<=4.3;r+=1.1) {
      const points=[];
      for(let i=0;i<=90;i++) {
        const a=i*Math.PI*2/90;
        points.push(project([Math.cos(a)*r,-.095,Math.sin(a)*r]));
      }
      ctx.beginPath();ctx.moveTo(points[0].x,points[0].y);
      for(let i=1;i<points.length;i++)ctx.lineTo(points[i].x,points[i].y);
      ctx.strokeStyle="rgba(93,196,206,.20)";ctx.lineWidth=1;ctx.stroke();
    }
    ctx.restore();
  }

  function drawCar() {
    buildCarFaces();
    faces.sort((a,b)=>b.depth-a.depth);
    for (const f of faces) {
      ctx.save();
      if (f.glow) {ctx.shadowBlur=f.glow;ctx.shadowColor=f.fill;}
      path(f.points.map(project), f.fill, f.stroke, .7);
      ctx.restore();
    }
    const origin=rotateVehicle([0,.8,0]);
    const endpoints=[
      [rotateVehicle([1.45,.8,0]),"#6ce8ef","X"],
      [rotateVehicle([0,2.12,0]),"#f4c777","Y"],
      [rotateVehicle([0,.8,-3.2]),"#c2ea85","Z"]
    ];
    ctx.save();ctx.setLineDash([4,5]);
    endpoints.forEach((entry)=>{
      line3(origin,entry[0],entry[1],1.4,.75);
      const p=project(entry[0]);
      ctx.fillStyle=entry[1];ctx.font="700 10px Segoe UI, Arial";ctx.fillText(entry[2],p.x+5,p.y-5);
    });
    ctx.restore();
    const front=project(rotateVehicle([0,.15,-2.75]));
    ctx.fillStyle="rgba(206,246,242,.8)";
    ctx.font="700 9px Segoe UI, Arial";
    ctx.textAlign="center";
    ctx.fillText("FRONT",front.x,front.y+5);
    ctx.textAlign="start";
  }

  function drawScene() {
    const w=state.sceneWidth,h=state.sceneHeight;
    ctx.clearRect(0,0,w,h);
    drawGround();
    if (detailedCar && detailedCar.ready) {
      const live=state.telemetryMode!=="DEMO";
      detailedCar.render(live?state.target:state.pose, state.zoom, w, live);
    } else {
      drawCar();
    }
  }

  function setAuto(on) {
    if (on && state.telemetryMode !== "DEMO") return;
    state.auto=on;
    if (on) state.external=false;
    $("autoLabel").textContent=on ? "PAUSE DEMO" : "PLAY DEMO";
    $("autoIcon").setAttribute("href",on ? "#icon-pause" : "#icon-play");
  }
  function setTarget(next, stopAuto, telemetry) {
    if (state.telemetryMode !== "DEMO" && !telemetry) return;
    if (stopAuto) setAuto(false);
    if (valid(next.roll)) state.target.roll=clamp(next.roll,telemetry?-180:-45,telemetry?180:45);
    if (valid(next.pitch)) state.target.pitch=clamp(next.pitch,telemetry?-90:-35,telemetry?90:35);
    if (valid(next.yaw)) state.target.yaw=wrap(next.yaw);
    syncSliders();
  }
  function syncSliders() {
    $("rollSlider").value=state.target.roll;
    $("pitchSlider").value=state.target.pitch;
    $("yawSlider").value=Math.round(state.target.yaw);
    $("rollOutput").textContent=signed(state.target.roll);
    $("pitchOutput").textContent=signed(state.target.pitch);
    $("yawOutput").textContent=String(Math.round(state.target.yaw)).padStart(3,"0")+"°";
  }

  function updateReadouts(now) {
    const p=state.pose;
    const live=state.telemetryMode !== "DEMO", poseValid=!live || state.poseValid;
    $("rollReadout").textContent=poseValid?signed(p.roll):"—";
    $("pitchReadout").textContent=poseValid?signed(p.pitch):"—";
    $("yawReadout").textContent=poseValid?String(Math.round(wrap(p.yaw))).padStart(3,"0")+"°":"—";
    const rates=live&&state.bodyRates?state.bodyRates:[state.rate.roll,state.rate.pitch,state.rate.yaw];
    ["rollRate","pitchRate","yawRate"].forEach((id,i)=>$(id).textContent=poseValid&&valid(rates[i])?signed(rates[i]).replace("°","°/s"):"—");
    $("rollTrack").style.width=clamp(50+p.roll/45*50,0,100)+"%";
    $("pitchTrack").style.width=clamp(50+p.pitch/35*50,0,100)+"%";
    $("yawTrack").style.width=(p.yaw/359*100)+"%";
    // The adapter explicitly selects relative TEL or magnetic CMP. Readouts
    // use the received integer directly, never the smoothed 3D model pose.
    const c=state.compass, heading=live?(Number.isInteger(c.heading_x10)?Math.floor((c.heading_x10+5)/10)%360:null):wrap(p.yaw);
    $("headingReadout").textContent=heading===null?"—":String(Math.round(heading)%360).padStart(3,"0")+"°";
    $("headingCardinal").textContent=live?(c.relative?c.status:c.valid?cardinal(heading):c.status):cardinal(heading);
    rose.setAttribute("transform","rotate("+(-(heading===null?0:heading))+" 120 120)");
    document.querySelector(".compass-panel").dataset.valid=String(!live||c.valid);
    if (!state.external) {
      state.metrics.temperature=26.4+Math.sin(now*.00032)*.5;
      state.metrics.humidity=48+Math.sin(now*.00021+2)*2;
      state.metrics.pressure=1013+Math.sin(now*.00012)*1.5;
      state.metrics.lux=460+Math.sin(now*.00072+1)*110;
    }
    $("tempReadout").textContent=valid(state.metrics.temperature)?state.metrics.temperature.toFixed(1):"—";
    $("humidityReadout").textContent=valid(state.metrics.humidity)?Math.round(state.metrics.humidity):"—";
    $("pressureReadout").textContent=valid(state.metrics.pressure)?Math.round(state.metrics.pressure):"—";
    $("lightReadout").textContent=valid(state.metrics.lux)?Math.round(state.metrics.lux):"—";
    const distance=state.metrics.distance;
    const distRow=document.querySelector(".distance-row");
    const level=!valid(distance)?"no data":live?(state.distanceState||"NO DATA").toLowerCase():distance<=.30?"danger":distance<=.80?"warning":"safe";
    distRow.classList.toggle("danger",level==="danger");
    distRow.classList.toggle("warning",level==="warning");
    $("distanceReadout").textContent=valid(distance)?distance.toFixed(2)+" m":"—";
    $("distanceState").textContent=level.toUpperCase();
  }

  function animate(now) {
    const dt=state.lastTime ? Math.min((now-state.lastTime)/1000,.1) : .016;
    state.lastTime=now;
    if (state.auto) {
      const t=now*.001;
      state.target.roll=Math.sin(t*.74)*14+Math.sin(t*1.32)*3;
      state.target.pitch=Math.sin(t*.51+.9)*9;
      state.target.yaw=wrap(42+Math.sin(t*.23)*27);
      syncSliders();
    }
    const amount=1-Math.exp(-dt*(state.telemetryMode==="DEMO"?7:1/.018));
    const previous={roll:state.pose.roll,pitch:state.pose.pitch,yaw:state.pose.yaw};
    const rollDelta=wrap(state.target.roll-state.pose.roll+180)-180;
    state.pose.roll=wrap(state.pose.roll+rollDelta*amount+180)-180;
    state.pose.pitch+=(state.target.pitch-state.pose.pitch)*amount;
    const yawDelta=((state.target.yaw-state.pose.yaw+540)%360)-180;
    state.pose.yaw=wrap(state.pose.yaw+yawDelta*amount);
    if (dt > 0) {
      state.rate.roll=clamp((state.pose.roll-previous.roll)/dt,-199.9,199.9);
      state.rate.pitch=clamp((state.pose.pitch-previous.pitch)/dt,-199.9,199.9);
      state.rate.yaw=clamp((((state.pose.yaw-previous.yaw+540)%360)-180)/dt,-199.9,199.9);
    }
    if (state.visible) drawScene();
    if (now-state.lastUi>(state.telemetryMode==="DEMO"?80:16)) {updateReadouts(now);state.lastUi=now;}
    requestAnimationFrame(animate);
  }

  function bindInputs() {
    [["rollSlider","roll"],["pitchSlider","pitch"],["yawSlider","yaw"]].forEach((pair)=>{
      $(pair[0]).addEventListener("input",(event)=>{
        const next={};next[pair[1]]=Number(event.target.value);setTarget(next,true);
      });
    });
    $("autoBtn").addEventListener("click",()=>setAuto(!state.auto));
    $("resetBtn").addEventListener("click",()=>{
      setTarget({roll:0,pitch:0,yaw:0},true);
      state.metrics.distance=2.8;
    });
    document.querySelectorAll("[data-preset]").forEach((button)=>{
      button.addEventListener("click",()=>{
        const name=button.dataset.preset;
        const poses={
          level:{roll:0,pitch:0,yaw:5,distance:2.8},
          corner:{roll:-26,pitch:4,yaw:74,distance:2.3},
          slope:{roll:4,pitch:24,yaw:138,distance:1.8},
          warning:{roll:9,pitch:-8,yaw:312,distance:.25}
        };
        const choice=poses[name];if(!choice)return;
        setTarget(choice,true);state.metrics.distance=choice.distance;
      });
    });
    stage.addEventListener("pointerdown",(event)=>{
      if(state.telemetryMode!=="DEMO")return;
      if(event.button!==0)return;
      stage.setPointerCapture(event.pointerId);
      stage.classList.add("dragging");
      state.drag={x:event.clientX,y:event.clientY};
      setAuto(false);
    });
    stage.addEventListener("pointermove",(event)=>{
      if(!state.drag)return;
      const dx=event.clientX-state.drag.x,dy=event.clientY-state.drag.y;
      setTarget({yaw:state.target.yaw+dx*.33,pitch:state.target.pitch-dy*.18},false);
      state.drag={x:event.clientX,y:event.clientY};
    });
    function endDrag(){state.drag=null;stage.classList.remove("dragging");}
    stage.addEventListener("pointerup",endDrag);
    stage.addEventListener("pointercancel",endDrag);
    stage.addEventListener("wheel",(event)=>{
      event.preventDefault();state.zoom=clamp(state.zoom-event.deltaY*.001,.72,1.36);
    },{passive:false});
    $("fullscreenBtn").addEventListener("click",()=>{
      if(document.fullscreenElement)document.exitFullscreen();
      else document.documentElement.requestFullscreen();
    });
    document.addEventListener("keydown",(event)=>{
      if(state.telemetryMode!=="DEMO")return;
      if(["INPUT","BUTTON"].includes(document.activeElement.tagName))return;
      if(event.key==="ArrowLeft")setTarget({roll:state.target.roll-2},true);
      else if(event.key==="ArrowRight")setTarget({roll:state.target.roll+2},true);
      else if(event.key==="ArrowUp")setTarget({pitch:state.target.pitch+2},true);
      else if(event.key==="ArrowDown")setTarget({pitch:state.target.pitch-2},true);
      else return;
      event.preventDefault();
    });
  }

  function updateClock(){
    $("clock").textContent=new Date().toLocaleTimeString("en-GB",{hour12:false});
  }

  // Future integration hook. Feed fused attitude angles from PS software.
  // Raw gyroscope rates alone must be integrated and corrected before use.
  window.DriveDemo={
    setPose(frame) {
      if (!frame || typeof frame!=="object")return;
      const telemetry=frame.source==="uart"||frame.source==="replay";
      if(state.telemetryMode!=="DEMO"&&!telemetry)return;
      state.external=true;
      setTarget({
        roll:frame.roll,
        pitch:frame.pitch,
        yaw:valid(frame.heading)?frame.heading:frame.yaw
      },true,telemetry);
    },
    pushFrame(frame) {
      if (!frame || typeof frame!=="object")return;
      const telemetry=frame.source==="uart"||frame.source==="replay";
      if(state.telemetryMode!=="DEMO"&&!telemetry)return;
      if(telemetry) {
        state.poseValid=!!frame.poseValid;
        stage.dataset.poseValid=String(state.poseValid);
        stage.dataset.pose=[frame.roll,frame.pitch,frame.heading].join(",");
        state.magnetic=!!frame.mag;
        state.bodyRates=frame.bodyRates||null;
        state.distanceState=frame.distanceState||"NO DATA";
      }
      if ((!telemetry||state.poseValid)&&[frame.roll,frame.pitch,frame.heading,frame.yaw].some(valid)) this.setPose(frame);
      ["temperature","humidity","pressure","lux","distance"].forEach((key)=>{
        if(valid(frame[key])||frame[key]===null)state.metrics[key]=frame[key];
      });
      updateReadouts(performance.now());
    },
    pushCompass(compass) {
      if(state.telemetryMode==="DEMO")return;
      state.compass={...compass};
      updateReadouts(performance.now());
    },
    setTelemetryMode(mode, note) {
      state.telemetryMode=mode;
      state.external=mode!=="DEMO";
      state.poseValid=mode==="DEMO";
      state.bodyRates=null;
      state.compass={valid:false,heading_x10:null,status:"WAITING CMP"};
      if(mode!=="DEMO") {
        setAuto(false);
        Object.keys(state.metrics).forEach(k=>state.metrics[k]=null);
      }
      ["rollSlider","pitchSlider","yawSlider","autoBtn","resetBtn"].forEach(id=>$(id).disabled=mode!=="DEMO");
      document.querySelectorAll("[data-preset]").forEach(button=>button.disabled=mode!=="DEMO");
      stage.dataset.poseValid=String(state.poseValid);
      if(note)$("poseInvalidNote").textContent=note;
      updateReadouts(performance.now());
    },
    invalidate(note) {
      if(state.telemetryMode==="DEMO")return;
      state.poseValid=false;
      state.compass={...state.compass,valid:false,heading_x10:null,status:"STALE"};
      Object.keys(state.metrics).forEach(k=>state.metrics[k]=null);
      stage.dataset.poseValid="false";
      $("poseInvalidNote").textContent=note||"NO VALID POSE";
      updateReadouts(performance.now());
    },
    telemetryState() {
      return {mode:state.telemetryMode,poseValid:state.poseValid,auto:state.auto,
        target:{...state.target},pose:{...state.pose},metrics:{...state.metrics},compass:{...state.compass}};
    },
    pushVisionFrame(frame) {
      if (window.DriveCameraView && typeof window.DriveCameraView.pushFrame === "function") {
        window.DriveCameraView.pushFrame(frame);
      }
    },
    playDemo(){setAuto(true);},
    reset(){setTarget({roll:0,pitch:0,yaw:0},true);},
    setVisible(visible) {
      state.visible = Boolean(visible);
      if (state.visible) {
        resizeCanvas();
        drawScene();
      }
    }
  };

  try {
    if (window.DriveSenseCar3D && $("car3dCanvas")) {
      detailedCar = window.DriveSenseCar3D.create($("car3dCanvas"));
    }
  } catch (error) {
    console.error("High-detail model unavailable; using the built-in fallback", error);
  }

  buildCompass();
  bindInputs();
  syncSliders();
  updateClock();
  setInterval(updateClock,1000);
  new ResizeObserver(resizeCanvas).observe(stage);
  resizeCanvas();
  requestAnimationFrame(animate);
})();
