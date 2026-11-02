(function () {
  "use strict";
  const COLORS = ["UNKNOWN", "RED", "AMBER", "GREEN"];
  const NAMES = { UNKNOWN:"未识别", RED:"红灯", AMBER:"黄灯", GREEN:"绿灯" };
  const ADVICE = { UNKNOWN:"UNKNOWN · 观察实际信号", RED:"STOP · 停车等待", AMBER:"WAIT · 注意信号变化", GREEN:"GREEN · 仅表示检出绿灯" };
  const finite = value => typeof value === "number" && Number.isFinite(value);
  const flag = value => value === 1 || value === true;
  const integer = (v, lo, hi) => Number.isInteger(v) && v >= lo && v <= hi;
  // Guarded so the module also loads inside a node:vm sandbox, where neither
  // Element nor document exists. In a browser the shape check still rejects
  // a wrong argument instead of silently writing to undefined.
  const requireRoot = (element, name) => {
    if (!element || typeof element.querySelector !== "function") throw new TypeError(name + " requires a DOM element");
    return element;
  };
  // Two mount points share one normalize()/packet: the full card on the Camera
  // page and the compact summary on the default page, so the recognition result
  // is never hidden behind a tab the operator has not opened yet.
  let root = null, summary = null, packet = {}, arrival = 0;

  // Public pure function allows protocol/freshness tests without a browser or
  // a board. Board age already includes serial/HTTP elapsed time at snapshot.
  function normalize(value, elapsedMs = 0) {
    value = value && typeof value === "object" ? value : {};
    const t = value.traffic || {}, connected = value.connected === true;
    const replay = value.mode === "REPLAY";
    const live = value.mode === "LIVE";
    const age = finite(t.age_ms) && t.age_ms >= 0 ? t.age_ms + (finite(elapsedMs) ? Math.max(0, elapsedMs) : 0) : null;
    const valid = connected && (live || replay) && flag(t.valid) && !t.stale &&
      integer(t.frame_seq, 0, 0xffffffff) && integer(t.color, 1, 3) && integer(t.confidence, 0, 100) && age !== null && age <= 1500;
    const color = valid ? COLORS[t.color] : "UNKNOWN";
    const digit = valid && flag(t.digit_valid) && integer(t.countdown, 0, 99) ? t.countdown : null;
    let status = !connected ? "串口未连接" : !live && !replay ? "仅演示 · 无板端识别" :
      age === null ? "等待 TRAFFIC 识别结果" : age > 1500 || t.stale ? "识别结果已过期" :
      !valid ? "未检出可靠目标" : digit === null ? "灯色已识别 · 数字不可读" : "灯色与数字已识别";
    return { valid, color, digit, age, status, name:NAMES[color], advice:ADVICE[color],
      confidence:valid ? t.confidence : null, procMs:valid && finite(t.proc_ms) && t.proc_ms >= 0 ? t.proc_ms : null,
      sequence:valid && integer(t.frame_seq, 0, 0xffffffff) ? t.frame_seq : null,
      source:replay ? "REPLAY · 回放数据" : live && connected ? "BOARD UART · 板端识别" : "NO BOARD DATA" };
  }

  function markup() {
    return `<section class="cv-traffic-real" aria-labelledby="cvTrafficHeading" data-color="unknown">
      <div class="cv-traffic-head"><div><span class="cv-kicker">BOARD RECOGNITION / 真实板端结果</span><h3 id="cvTrafficHeading">路口信号识别</h3></div><span id="cvTrafficSource">NO BOARD DATA</span></div>
      <div class="cv-traffic-grid"><div class="cv-traffic-result"><span class="cv-traffic-dot" aria-hidden="true"></span><div><strong id="cvTrafficColor">未识别</strong><span id="cvTrafficStatus" role="status" aria-live="polite">等待板端识别结果</span></div></div>
      <div class="cv-countdown"><span>OBSERVED COUNTDOWN / 识别到的倒计时</span><strong id="cvTrafficCountdown">--</strong><small>秒 · 不推算倒数</small></div>
      <dl class="cv-traffic-metrics"><div><dt>判定分数</dt><dd id="cvTrafficConfidence">--</dd></div><div><dt>结果年龄</dt><dd id="cvTrafficAge">--</dd></div><div><dt>处理耗时</dt><dd id="cvTrafficProc">--</dd></div><div><dt>结果帧号</dt><dd id="cvTrafficSeq">--</dd></div></dl></div>
      <div class="cv-traffic-advice" id="cvTrafficAdvice">UNKNOWN · 观察实际信号</div>
      <p class="cv-traffic-note">本卡读取摄像头在板端产生的识别结果。实时视频通过 HDMI OUT 显示；上方坐标图同步运动目标框，不传输视频。数字不清楚时显示 --，不依据时间补全。绿色提示仅表示检出绿灯。当前规则针对黑壳圆灯与邻近七段数字；小目标、严重雾化、遮挡或多灯歧义时应显示未识别。</p>
    </section>`;
  }
  function summaryMarkup() {
    return `<section class="traffic-summary" aria-labelledby="tsHeading" data-color="unknown">
      <div class="ts-head"><span class="panel-kicker" id="tsHeading">TRAFFIC / 路口信号</span><span id="tsSource">NO BOARD DATA</span></div>
      <div class="ts-body">
        <div class="ts-lamps" aria-hidden="true"><i class="ts-lamp ts-red"></i><i class="ts-lamp ts-amber"></i><i class="ts-lamp ts-green"></i></div>
        <div class="ts-main"><strong id="tsColor">未识别</strong><span id="tsStatus" role="status" aria-live="polite">等待板端识别结果</span></div>
        <div class="ts-digit"><small>COUNTDOWN</small><strong id="tsDigit">--</strong><span>秒</span></div>
      </div>
      <div class="ts-foot"><span id="tsMetrics">判定分数 — · 结果年龄 —</span><a href="#camera">识别详情 ↗</a></div>
    </section>`;
  }

  function render() {
    if (!root && !summary) return;
    const s = normalize(packet, Math.max(0, performance.now() - arrival));
    if (root) {
      const q = id => root.querySelector("#" + id);
      root.querySelector(".cv-traffic-real").dataset.color = s.color.toLowerCase();
      q("cvTrafficSource").textContent = s.source;
      q("cvTrafficColor").textContent = s.name;
      q("cvTrafficStatus").textContent = s.status;
      q("cvTrafficCountdown").textContent = s.digit === null ? "--" : String(s.digit);
      q("cvTrafficConfidence").textContent = s.confidence === null ? "--" : s.confidence + " / 100";
      q("cvTrafficAge").textContent = s.age === null ? "--" : Math.round(s.age) + " ms";
      q("cvTrafficProc").textContent = s.procMs === null ? "--" : s.procMs + " ms";
      q("cvTrafficSeq").textContent = s.sequence === null ? "--" : String(s.sequence);
      q("cvTrafficAdvice").textContent = s.advice;
    }
    if (summary) {
      const q = id => summary.querySelector("#" + id);
      summary.querySelector(".traffic-summary").dataset.color = s.color.toLowerCase();
      q("tsSource").textContent = s.source;
      q("tsColor").textContent = s.name;
      q("tsStatus").textContent = s.status;
      q("tsDigit").textContent = s.digit === null ? "--" : String(s.digit);
      q("tsMetrics").textContent = `判定分数 ${s.confidence === null ? "—" : s.confidence + " / 100"} · 结果年龄 ${s.age === null ? "—" : Math.round(s.age) + " ms"}`;
    }
  }
  function mount(element) {
    root = requireRoot(element, "DriveTrafficView.mount(root)"); root.innerHTML = markup(); render();
  }
  function mountSummary(element) {
    summary = requireRoot(element, "DriveTrafficView.mountSummary(root)"); summary.innerHTML = summaryMarkup(); render();
  }
  function pushPacket(value) {
    const now = performance.now(), next = value || {}, old = packet.traffic || {}, t = next.traffic || {};
    // Repeated HTTP/SSE snapshots cannot restart the lifetime of one observed
    // frame, even if an intermediary repeats a smaller board age.
    const same = next.connected === true && packet.connected === true && next.mode === packet.mode &&
      integer(t.frame_seq, 0, 0xffffffff) && t.frame_seq === old.frame_seq;
    if (same && finite(old.age_ms) && old.age_ms >= 0 && finite(t.age_ms) && t.age_ms >= 0) {
      packet = { ...next, traffic:{ ...t, age_ms:Math.max(t.age_ms, old.age_ms + Math.max(0, now-arrival)) } };
    } else packet = next;
    arrival = now; render();
  }
  function disconnect() { pushPacket({ connected:false }); }
  // Local expiration still runs if the browser stops receiving SSE snapshots.
  setInterval(render, 100);
  // The default page carries a compact summary so the board result is visible
  // without opening the Camera tab first.
  const summaryRoot = typeof document !== "undefined" ? document.getElementById("trafficSummaryRoot") : null;
  if (summaryRoot) mountSummary(summaryRoot);
  window.DriveTrafficView = Object.freeze({ normalize, mount, mountSummary, pushPacket, disconnect });
})();
