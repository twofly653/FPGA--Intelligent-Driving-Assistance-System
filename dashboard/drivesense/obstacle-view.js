(function () {
  "use strict";
  const IMAGE_MODES = ["彩色原画", "Sobel 边缘", "灰度图像", "图像去雾"];
  const STATES = ["READY", "WARMUP", "GLOBAL_CHANGE", "STALE", "DUPLICATE", "INVALID"];
  const integer = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
  const number = v => typeof v === "number" && Number.isFinite(v) && v >= 0;
  const flag = v => v === true || v === 1;
  const off = v => v === false || v === 0;
  const object = v => v !== null && typeof v === "object" && !Array.isArray(v);
  const ageOf = (v, elapsed) => number(v) ? v + elapsed : null;
  let root = null, packet = {}, arrival = 0;
  let cameraClock = null, obstacleClock = null, detectionClock = null;
  let pending = null, commandMessage = "", commandToken = 0;
  let detectorSeen = false, detectorEnabled = null, detectorFloor = null, videoEpoch = null;
  const forward = (next, old) => old === null || ((next - old) >>> 0) > 0 && ((next - old) >>> 0) < 0x80000000;

  // Coordinates are inclusive native HDMI coordinates, never normalized demo
  // boxes. Reject the entire list on malformed metadata, not just one item.
  function normalize(value, elapsedMs = 0) {
    value = object(value) ? value : {};
    const c = object(value.camera) ? value.camera : {};
    const o = object(value.obstacles) ? value.obstacles : {};
    const d = object(value.detection) ? value.detection : {};
    const elapsed = number(elapsedMs) ? elapsedMs : 0;
    const connected = value.connected === true;
    const replay = value.mode === "REPLAY", live = value.mode === "LIVE";
    const expectedSource = replay ? "REPLAY" : "BOARD_UART";
    const cameraAge = ageOf(c.age_ms, elapsed), age = ageOf(o.age_ms, elapsed);
    const cameraValid = connected && (live || replay) && c.kind === "CAMERA" &&
      c.source === expectedSource && integer(c.sequence, 0, 0xffffffff) &&
      integer(c.image_mode, 0, 3) && c.width === 640 && c.height === 480 &&
      (flag(c.live) || off(c.live)) && (flag(c.configured) || off(c.configured)) &&
      off(c.stale) && cameraAge !== null && cameraAge <= 1000;
    const videoLive = cameraValid && flag(c.live) && flag(c.configured);
    // R7 legacy metadata can still be viewed but cannot be controlled. Once
    // DETECT is supported, missing/expired state closes the display gate.
    const detectionSupported = d.supported === true || integer(d.sequence, 0, 0xffffffff);
    const detectionAge = ageOf(d.age_ms, elapsed);
    const detectionValid = connected && (live || replay) && d.kind === "DETECT" &&
      d.source === expectedSource && integer(d.sequence, 0, 0xffffffff) &&
      (flag(d.enabled) || off(d.enabled)) && off(d.stale) && detectionAge !== null && detectionAge <= 1500;
    const detectionEnabled = detectionValid ? flag(d.enabled) : null;
    const controlReady = live && detectionValid && flag(d.can_control);
    const ids = new Set();
    const listValid = integer(o.count, 0, 8) && Array.isArray(o.boxes) && o.boxes.length === o.count && o.boxes.every(b => {
      if (!object(b) || !integer(b.id, 1, 65535) || ids.has(b.id) ||
          !integer(b.xmin, 0, 639) || !integer(b.xmax, b.xmin, 639) ||
          !integer(b.ymin, 0, 479) || !integer(b.ymax, b.ymin, 479)) return false;
      ids.add(b.id); return true;
    });
    const obstacleValid = videoLive && o.kind === "OBSTACLE" && o.source === expectedSource &&
      integer(o.result_seq, 0, 0xffffffff) && integer(o.source_frame, 0, 0xffffffff) &&
      o.state === 0 && flag(o.available) && flag(o.valid) && off(o.stale) &&
      age !== null && age <= 500 && listValid && (!detectionSupported || detectionEnabled === true);
    const boxes = obstacleValid ? o.boxes.map(b => ({ id:b.id, xmin:b.xmin, ymin:b.ymin, xmax:b.xmax, ymax:b.ymax })) : [];
    let status = !connected ? "串口未连接 · 目标框已清空" : detectionSupported && !detectionValid ? "识别开关状态过期 · 目标框已清空" : detectionEnabled === false ? "运动目标识别已关闭 · HDMI 与网页清除绿框" : !live && !replay ? "等待板端硬件数据" :
      !cameraValid ? "摄像头状态缺失或过期 · 目标框已清空" : !flag(c.configured) ? "摄像头尚未配置完成" :
      !flag(c.live) ? "当前没有有效视频输入" : !flag(o.available) ? "等待运动检测模块" :
      o.state === 1 ? "正在学习静止背景" : o.state === 2 ? "画面发生整体变化 · 正在重建背景" :
      !obstacleValid ? "运动结果无效或过期 · 目标框已清空" : boxes.length ? `当前检出 ${boxes.length} 个运动区域` : "当前没有检出运动区域";
    return {
      cameraValid, videoLive, valid:obstacleValid, boxes, count:detectionEnabled === false ? 0 : obstacleValid ? boxes.length : null,
      detectionSupported, detectionValid, detectionEnabled, detectionAge, controlReady,
      detectionSequence:detectionValid ? d.sequence : null,
      detectionName:detectionValid ? detectionEnabled ? "识别已开启" : "识别已关闭" : detectionSupported ? "开关状态过期" : "等待识别开关状态",
      imageMode:cameraValid ? c.image_mode : null, imageModeName:cameraValid ? IMAGE_MODES[c.image_mode] : "等待板端模式",
      status, cameraAge, age, source:replay && connected ? "REPLAY · 回放数据" : live && connected ? "BOARD UART · 真实板端数据" : "NO BOARD DATA",
      sequence:obstacleValid ? o.result_seq : null, sourceFrame:obstacleValid ? o.source_frame : null,
      procMs:obstacleValid && number(o.proc_ms) ? o.proc_ms : null,
      state:integer(o.state, 0, 5) ? STATES[o.state] : "NO DATA", replay
    };
  }

  function markup() {
    return `<section class="cv-obstacle-card" aria-labelledby="cvObstacleHeading" data-link="offline">
      <div class="cv-obstacle-heading"><div><span class="cv-kicker">HARDWARE DETECTION COORDINATES</span><h3 id="cvObstacleHeading">硬件检测坐标</h3><p>实时画面见 HDMI · 此处仅同步已提交的目标框</p></div><span id="cvObstacleSource" class="cv-obstacle-source">NO BOARD DATA</span></div>
      <div class="cv-obstacle-layout"><div class="cv-coordinate-column"><div class="cv-obstacle-plot" id="cvObstaclePlot" role="img" aria-label="640 × 480 硬件检测坐标，等待数据"><div class="cv-coordinate-empty" id="cvObstacleEmpty"><span>HDMI OUT</span><strong>网页不传输视频</strong><small>等待板端运动检测结果</small></div><div class="cv-obstacle-boxes" id="cvObstacleBoxes"></div></div><div class="cv-coordinate-axis"><span>(0, 0) 左上角</span><span>640 × 480 · 4:3 · 原始坐标</span><span>(639, 479)</span></div></div>
      <aside class="cv-obstacle-detail"><div class="cv-detection-control"><div class="cv-detection-state"><span>运动目标识别</span><strong id="cvDetectionState">等待识别开关状态</strong></div><div class="cv-detection-buttons"><button id="cvDetectionOn" type="button" disabled>开启识别</button><button id="cvDetectionOff" type="button" disabled>关闭识别</button></div><p id="cvDetectionCommand" role="status" aria-live="polite">连接新版板卡后，可在网页或扩展屏操作。</p></div><div class="cv-board-mode"><span>硬件当前图像模式</span><strong id="cvBoardImageMode">等待板端模式</strong><small>BTN1 边缘 · BTN2 灰度 · BTN3 去雾<br>再次按同一按键返回彩色原画</small></div><div class="cv-object-count"><span>运动目标数</span><strong id="cvObstacleCount">—</strong><small>最多 8 个独立绿色框</small></div><p id="cvObstacleStatus" class="cv-obstacle-status" role="status" aria-live="polite">串口未连接 · 目标框已清空</p><dl class="cv-obstacle-metrics"><div><dt>结果年龄</dt><dd id="cvObstacleAge">—</dd></div><div><dt>算法处理耗时</dt><dd id="cvObstacleProc">—</dd></div><div><dt>源帧号</dt><dd id="cvObstacleFrame">—</dd></div><div><dt>提交序号</dt><dd id="cvObstacleSequence">—</dd></div></dl><p class="cv-obstacle-timing-note">处理耗时不代表镜头到屏幕的端到端延迟；UART 与 HDMI 刷新存在短暂时间差。</p></aside></div>
      <ul class="cv-obstacle-list" id="cvObstacleList" aria-label="运动目标坐标"></ul>
      <p class="cv-obstacle-note">固定摄像头检测运动区域；不推断物体类别、距离或行驶安全。离场、结果超过 500 ms、摄像头状态超过 1000 ms、识别开关状态超过 1500 ms 或连接中断时，网页自动清空目标框。</p>
    </section>`;
  }
  function current() { return normalize(packet, Math.max(0, performance.now() - arrival)); }
  function render() {
    if (!root) return;
    const s = current(), q = id => root.querySelector("#" + id);
    if (pending && s.detectionValid && forward(s.detectionSequence, pending.sequence) && s.detectionEnabled === pending.enabled) {
      commandMessage = s.detectionEnabled ? "板端已确认开启；等待新的运动检测结果。" : "板端已确认关闭，绿框已清除。";
      pending = null; commandToken++;
    }
    if (pending && (!s.controlReady || performance.now() > pending.deadline)) {
      commandMessage = !s.controlReady ? "连接或板端状态已失效，操作未确认。" : "等待板端确认超时；当前开关以板端状态为准，请重试。";
      pending = null; commandToken++;
    }
    q("cvDetectionState").textContent = s.detectionName;
    q("cvDetectionOn").disabled = !s.controlReady || !!pending || s.detectionEnabled === true;
    q("cvDetectionOff").disabled = !s.controlReady || !!pending || s.detectionEnabled === false;
    q("cvDetectionOn").setAttribute("aria-pressed", String(s.detectionEnabled === true));
    q("cvDetectionOff").setAttribute("aria-pressed", String(s.detectionEnabled === false));
    q("cvDetectionCommand").textContent = pending ? `正在请求${pending.enabled ? "开启" : "关闭"}，等待板端确认…` :
      !s.controlReady ? s.replay ? "回放只读，不能操作真实板卡。" : packet.connected !== true ? "连接真实板卡后，可在网页或扩展屏操作。" : !s.detectionSupported ? "板端尚未提供识别开关状态；旧固件需更新后操作。" : "请连接真实板卡并等待有效开关状态。" : commandMessage || "网页与扩展屏控制同一开关；不会改变视频模式。";
    root.querySelector(".cv-obstacle-card").dataset.link = s.videoLive ? s.replay ? "replay" : "live" : "offline";
    q("cvObstacleSource").textContent = s.source;
    q("cvBoardImageMode").textContent = s.imageModeName;
    q("cvObstacleCount").textContent = s.count === null ? "—" : String(s.count);
    q("cvObstacleStatus").textContent = s.status;
    q("cvObstacleAge").textContent = s.age === null ? "—" : Math.round(s.age) + " ms";
    q("cvObstacleProc").textContent = s.procMs === null ? "—" : s.procMs + " ms";
    q("cvObstacleFrame").textContent = s.sourceFrame === null ? "—" : String(s.sourceFrame);
    q("cvObstacleSequence").textContent = s.sequence === null ? "—" : String(s.sequence);
    q("cvObstacleEmpty").hidden = s.boxes.length > 0;
    q("cvObstacleEmpty").querySelector("small").textContent = s.valid ? "当前没有运动区域" : "等待有效运动检测结果";
    q("cvObstaclePlot").setAttribute("aria-label", `640 × 480 硬件检测坐标，${s.count === null ? "无有效数据" : s.count + " 个运动区域"}，无视频`);
    const layer = q("cvObstacleBoxes"), list = q("cvObstacleList");
    layer.replaceChildren(); list.replaceChildren();
    s.boxes.forEach(b => {
      const box = document.createElement("div"); box.className = "cv-obstacle-box";
      box.style.left = (b.xmin / 640 * 100) + "%"; box.style.top = (b.ymin / 480 * 100) + "%";
      box.style.width = ((b.xmax - b.xmin + 1) / 640 * 100) + "%";
      box.style.height = ((b.ymax - b.ymin + 1) / 480 * 100) + "%";
      const label = document.createElement("span"); label.textContent = "#" + b.id; box.appendChild(label); layer.appendChild(box);
      const item = document.createElement("li"); item.textContent = `#${b.id} · (${b.xmin}, ${b.ymin}) → (${b.xmax}, ${b.ymax})`; list.appendChild(item);
    });
    list.hidden = s.boxes.length === 0;
  }
  // Anchor every sequence to local monotonic time. Repeated SSE snapshots with
  // an artificially smaller age cannot keep the same board observation alive.
  function preserveAge(value, key, clock, now, source) {
    if (!object(value) || !integer(value[key], 0, 0xffffffff) || !number(value.age_ms)) return [value, clock];
    const next = { ...value };
    if (clock && clock.source === source && clock.sequence === value[key]) {
      next.age_ms = Math.max(value.age_ms, clock.age + Math.max(0, now - clock.arrival));
    }
    return [next, { sequence:value[key], source, age:next.age_ms, arrival:now }];
  }
  function pushPacket(value) {
    const now = performance.now(), next = object(value) ? { ...value } : {};
    // POST and SSE can arrive out of order. Bridge seq is a monotonic transport
    // counter (not a uint32 source-frame counter). Never roll a newer observed
    // board state back to a delayed HTTP snapshot or a previous boot epoch.
    const incomingEpoch = integer(next.video_epoch, 0, Number.MAX_SAFE_INTEGER);
    if (incomingEpoch && videoEpoch !== null && next.video_epoch < videoEpoch) return false;
    if (incomingEpoch && next.video_epoch === videoEpoch && next.mode === packet.mode &&
        integer(next.seq, 0, Number.MAX_SAFE_INTEGER) && integer(packet.seq, 0, Number.MAX_SAFE_INTEGER) &&
        next.seq < packet.seq) return false;
    if (integer(next.video_epoch, 0, Number.MAX_SAFE_INTEGER) && next.video_epoch !== videoEpoch) {
      if (videoEpoch !== null && pending) commandMessage = "板卡已重启或重连，之前的操作未确认。";
      videoEpoch = next.video_epoch; cameraClock = obstacleClock = detectionClock = null;
      pending = null; commandToken++;
      detectorSeen = false; detectorEnabled = null; detectorFloor = null;
    }
    if (next.connected !== true || object(next.detection) && next.detection.supported === false) {
      detectorSeen = false; detectorEnabled = null; detectorFloor = null;
    }
    [next.camera, cameraClock] = preserveAge(next.camera, "sequence", cameraClock, now, next.mode);
    [next.obstacles, obstacleClock] = preserveAge(next.obstacles, "result_seq", obstacleClock, now, next.mode);
    [next.detection, detectionClock] = preserveAge(next.detection, "sequence", detectionClock, now, next.mode);
    if (object(next.obstacles) && Array.isArray(next.obstacles.boxes)) next.obstacles.boxes = next.obstacles.boxes.map(b => object(b) ? { ...b } : b);
    const detect = next.detection;
    if (object(detect) && integer(detect.sequence, 0, 0xffffffff) && (flag(detect.enabled) || off(detect.enabled)) && off(detect.stale)) {
      const enabled = flag(detect.enabled);
      if (!enabled || detectorSeen && detectorEnabled === false && enabled) {
        const oldSeq = object(packet.obstacles) ? packet.obstacles.result_seq : null;
        detectorFloor = integer(oldSeq, 0, 0xffffffff) ? oldSeq : detectorFloor;
      }
      detectorSeen = true; detectorEnabled = enabled;
    }
    if (detectorSeen && object(next.obstacles) && detectorFloor !== null &&
        !forward(next.obstacles.result_seq, detectorFloor)) next.obstacles.valid = false;
    packet = next; arrival = now; render();
  }
  async function requestControl(enabled) {
    const s = current();
    if (typeof enabled !== "boolean" || pending || !s.controlReady) return false;
    const token = ++commandToken;
    pending = {enabled, sequence:s.detectionSequence, deadline:performance.now() + 3500};
    commandMessage = ""; render();
    try {
      const response = await fetch("/api/obstacle/control", {method:"POST", headers:{"Content-Type":"application/json"}, body:JSON.stringify({enabled})});
      const value = await response.json();
      if (token !== commandToken) return false;
      if (!response.ok) throw new Error(value.error || `请求失败 (${response.status})`);
      // This response contains observed state, not an optimistic switch value.
      pushPacket(value);
      return true;
    } catch (error) {
      if (token === commandToken) {
        pending = null; commandMessage = `操作未确认：${error.message}`; render();
      }
      return false;
    }
  }
  function mount(element) {
    root = element; root.innerHTML = markup();
    root.querySelector("#cvDetectionOn").onclick = () => requestControl(true);
    root.querySelector("#cvDetectionOff").onclick = () => requestControl(false);
    render();
  }
  function disconnect() { packet = { connected:false }; videoEpoch = null; cameraClock = obstacleClock = detectionClock = null; pending = null; detectorSeen = false; detectorEnabled = null; detectorFloor = null; commandToken++; commandMessage = ""; arrival = performance.now(); render(); }
  setInterval(render, 100);
  window.DriveObstacleView = Object.freeze({ normalize, mount, pushPacket, disconnect, current, requestControl });
})();
