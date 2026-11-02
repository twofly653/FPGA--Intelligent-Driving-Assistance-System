(function () {
  "use strict";

  const ICONS = {
    camera: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M3.5 7.5h4l1.7-2h5.6l1.7 2h4v11h-17z"/><circle cx="12" cy="13" r="3.2"/><path d="M17.5 10h1"/></svg>',
    signal: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="7" y="2" width="10" height="20" rx="3"/><circle cx="12" cy="6.4" r="1.1"/><circle cx="12" cy="12" r="1.1"/><circle cx="12" cy="17.6" r="1.1"/></svg>',
    range: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><circle cx="12" cy="12" r="2"/><path d="M4.5 7.4a9 9 0 0 0 0 9.2M19.5 7.4a9 9 0 0 1 0 9.2M7.7 9.3a5.2 5.2 0 0 0 0 5.4M16.3 9.3a5.2 5.2 0 0 1 0 5.4"/></svg>',
    frame: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><path d="M8 3H4a1 1 0 0 0-1 1v4M16 3h4a1 1 0 0 1 1 1v4M3 16v4a1 1 0 0 0 1 1h4M21 16v4a1 1 0 0 1-1 1h-4"/><path d="M8 12h8M12 8v8"/></svg>',
    chip: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 1v5M15 1v5M9 18v5M15 18v5M1 9h5M1 15h5M18 9h5M18 15h5"/><path d="M10 10h4v4h-4z"/></svg>',
    monitor: '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round" aria-hidden="true"><rect x="2" y="3" width="20" height="15" rx="2"/><path d="M8 22h8M12 18v4"/></svg>'
  };

  let root = null, refs = {};
  const state = { active: true };
  // Video remains on HDMI. This component accepts metadata only and never
  // presents an illustrative scene as the board camera's live picture.
  const photoState = { job: { phase: "idle", busy: false }, connected: false,
    known: false, postPending: false, serviceError: "", photo: null, imageError: "" };
  let photoTimer = null, photoController = null, photoPostController = null;
  let photoPolling = false, photoGeneration = 0, photoJobRevision = 0;


  function template() {
    return `<div class="cv-page" data-source="metadata">
      <div class="cv-heading"><div class="cv-heading-title"><span class="cv-kicker">02 / BOARD PERCEPTION</span><h2>摄像头与硬件同步</h2><p>实体按键切换图像模式 · 网页同步板端结果 · 实时视频见 HDMI OUT</p><div class="cv-sensor-spec" aria-label="摄像头规格"><strong>OV5640 · 500 万像素（标称）</strong><span>预览采集 640 × 480 RGB565 · 原生照片 2592 × 1944</span></div></div></div>
      <div id="cvObstacleRoot"></div>
      <div id="cvTrafficRoot"></div>
      <p class="cv-test-pattern-link"><a href="/traffic-test.html" target="_blank" rel="noopener">打开人工交通灯与七段数字测试图 ↗</a><span>仅供摄像头拍摄验收 · 不生成识别结果</span></p>
      <section class="cv-photo-card" aria-labelledby="cvPhotoHeading" data-photo-phase="idle">
        <div class="cv-card-head"><div class="cv-card-title"><span class="cv-icon">${ICONS.camera}</span><div><span class="cv-kicker">NATIVE STILL / 真实板卡照片</span><h3 id="cvPhotoHeading">500 万像素拍照</h3></div></div><span class="cv-photo-badge" id="cvPhotoBadge">等待连接</span></div>
        <div class="cv-photo-body">
          <div class="cv-photo-preview" id="cvPhotoPreview">
            <div class="cv-photo-empty" id="cvPhotoEmpty"><span class="cv-photo-empty-icon">${ICONS.frame}</span><strong>等待真实照片</strong><span>仅显示通过原生尺寸与 JPEG 完整校验的板卡照片</span></div>
            <img id="cvPhotoImage" alt="OV5640 板卡采集的原生 2592 × 1944 照片" width="2592" height="1944" hidden>
            <span class="cv-photo-image-tag" id="cvPhotoImageTag" hidden>VERIFIED JPEG · 原始尺寸</span>
          </div>
          <div class="cv-photo-detail">
            <div class="cv-photo-title"><h4>原生静态照片 · 2592 × 1944</h4><span>原生照片来自 OV5640；实时视频继续通过 HDMI 显示。</span></div>
            <div class="cv-photo-actions"><button id="cvPhotoCapture" type="button" disabled>${ICONS.camera}<span>拍摄 500 万像素照片</span></button><a id="cvPhotoDownload" hidden>下载原图</a></div>
            <div class="cv-photo-feedback" role="status" aria-live="polite" aria-atomic="true"><strong id="cvPhotoStatus">正在检查板卡连接</strong><p id="cvPhotoMessage">连接真实板卡后即可拍摄，模拟与回放不提供拍照。</p></div>
            <ol class="cv-photo-stages" aria-label="照片处理阶段"><li data-photo-stage="capturing">拍摄</li><li data-photo-stage="transferring">取图</li><li data-photo-stage="verifying">校验</li><li data-photo-stage="ready">完成</li></ol>
            <div class="cv-photo-progress" id="cvPhotoProgress" role="progressbar" aria-label="照片任务进行中" hidden><i></i></div>
            <dl class="cv-photo-meta" id="cvPhotoMeta" hidden><div><dt>已验证像素</dt><dd id="cvPhotoDimensions">—</dd></div><div><dt>原图文件</dt><dd id="cvPhotoSize">—</dd></div><div><dt>照片编号</dt><dd id="cvPhotoId">—</dd></div><div><dt>校验完成时间</dt><dd id="cvPhotoTime">—</dd></div></dl>
            <p class="cv-photo-note">HDMI OUT 实时预览；切换拍摄时画面与姿态会短暂停顿，进入取图阶段后恢复预览。</p>
          </div>
        </div>
      </section>
      <section class="cv-pipeline-card cv-sync-pipeline" aria-label="Board data path"><div class="cv-bottom-title"><div><span class="cv-kicker">BOARD DATA PATH</span><h3>视频与结果同步路径</h3></div><span class="cv-planned-badge">VIDEO → HDMI / METADATA → UART</span></div><div class="cv-pipeline"><div class="cv-pipe-step"><span class="cv-pipe-icon">${ICONS.camera}</span><strong>OV5640 DVP</strong><small>640 × 480 RGB565</small></div><div class="cv-pipe-step"><span class="cv-pipe-icon">${ICONS.chip}</span><strong>PS / PL</strong><small>图像处理与运动检测</small></div><div class="cv-pipe-step"><span class="cv-pipe-icon">${ICONS.monitor}</span><strong>HDMI OUT</strong><small>实时画面与绿色框</small></div><div class="cv-pipe-step"><span class="cv-pipe-icon">${ICONS.frame}</span><strong>UART → WEB</strong><small>模式、目标框与交通结果</small></div></div></section>
    </div>`;
  }

  function mount(container) {
    if (!(container instanceof Element)) throw new TypeError("DriveCameraView.mount(root) requires a DOM element");
    stopPhotoPolling(); root = container; root.innerHTML = template();
    if (window.DriveObstacleView) window.DriveObstacleView.mount(root.querySelector("#cvObstacleRoot"));
    if (window.DriveTrafficView) window.DriveTrafficView.mount(root.querySelector("#cvTrafficRoot"));
    const q = selector => root.querySelector(selector);
    refs = {
      photoCard: q(".cv-photo-card"), photoBadge: q("#cvPhotoBadge"), photoCapture: q("#cvPhotoCapture"),
      photoDownload: q("#cvPhotoDownload"), photoImage: q("#cvPhotoImage"), photoEmpty: q("#cvPhotoEmpty"),
      photoImageTag: q("#cvPhotoImageTag"), photoStatus: q("#cvPhotoStatus"), photoMessage: q("#cvPhotoMessage"),
      photoProgress: q("#cvPhotoProgress"), photoMeta: q("#cvPhotoMeta"), photoDimensions: q("#cvPhotoDimensions"),
      photoSize: q("#cvPhotoSize"), photoId: q("#cvPhotoId"), photoTime: q("#cvPhotoTime"),
      photoStages: Array.from(root.querySelectorAll("[data-photo-stage]"))
    };
    refs.photoCapture.addEventListener("click", capturePhoto);
    refs.photoImage.addEventListener("load", () => {
      if (!photoState.photo) return;
      if (refs.photoImage.naturalWidth !== 2592 || refs.photoImage.naturalHeight !== 1944) {
        photoState.imageError = "原图预览尺寸不符合 2592 × 1944，请重新拍摄。";
        refs.photoImage.hidden = true;
        refs.photoEmpty.hidden = false;
        refs.photoImageTag.hidden = true;
      } else {
        photoState.imageError = "";
        refs.photoImage.hidden = false;
        refs.photoEmpty.hidden = true;
        refs.photoImageTag.hidden = false;
      }
      renderPhoto();
    });
    refs.photoImage.addEventListener("error", () => {
      photoState.imageError = "原图预览加载失败，可下载已校验的 JPEG，或重新拍摄。";
      refs.photoImage.hidden = true; refs.photoEmpty.hidden = false; refs.photoImageTag.hidden = true;
      renderPhoto();
    });
    renderPhoto();
    if (state.active && !document.hidden) startPhotoPolling();
    return { pushFrame, pushPacket, setActive };
  }

  // Compatibility with the original public API: metadata now comes only from
  // pushPacket. Browser example images and boxes cannot become hardware data.
  function pushFrame() {}
  function pushPacket(packet) {
    if (window.DriveObstacleView) window.DriveObstacleView.pushPacket(packet);
  }
  function disconnect() {
    if (window.DriveObstacleView) window.DriveObstacleView.disconnect();
  }
  function setActive(active) {
    state.active = !!active;
    if (state.active && !document.hidden) startPhotoPolling(); else stopPhotoPolling();
  }

  function verifiedPhoto(value) {
    if (!value || value.verified !== true || value.width !== 2592 || value.height !== 1944) return false;
    if (!Number.isInteger(value.size) || value.size < 4 || value.size > 2 * 1024 * 1024) return false;
    if (typeof value.id !== "string" || !/^[a-zA-Z0-9-]{1,80}$/.test(value.id)) return false;
    return value.url === `/photos/${value.id}.jpg`;
  }

  function acceptPhotoJob(job) {
    if (!job || typeof job !== "object" || typeof job.phase !== "string") throw new Error("照片状态格式无效");
    photoState.job = job;
    if (job.phase === "ready") {
      if (!verifiedPhoto(job.photo)) throw new Error("照片未通过原生尺寸与完整性校验，未显示图像");
      if (!photoState.photo || photoState.photo.id !== job.photo.id) photoState.imageError = "";
      photoState.photo = job.photo;
    }
  }

  function renderPhoto() {
    if (!root || !refs.photoCard) return;
    const job = photoState.job, busy = photoState.postPending || job.busy === true;
    const phase = photoState.postPending ? "arming" : job.phase;
    const titles = { idle: "准备拍摄", arming: "正在准备拍摄", capturing: "正在采集原生照片",
      transferring: "正在取回 JPEG · HDMI 预览已恢复", verifying: "正在校验原图", ready: "真实照片已校验", error: "拍照未完成，可重试" };
    refs.photoCard.dataset.photoPhase = phase;
    refs.photoCapture.disabled = !photoState.known || !photoState.connected || busy || !photoPolling;
    refs.photoCapture.querySelector("span").textContent = busy ? "照片任务进行中" : phase === "error" ? "重新拍摄" : "拍摄 500 万像素照片";
    refs.photoCard.setAttribute("aria-busy", String(busy));
    refs.photoBadge.textContent = busy ? "真实照片任务" : !photoState.known ? "连接待确认" : photoState.connected ? "真实板卡已连接" : "等待真实板卡";
    refs.photoStatus.textContent = photoState.serviceError ? "照片服务暂不可用" : titles[phase] || "照片状态";
    let message = photoState.serviceError || photoState.imageError || job.message || "准备拍摄原生照片。";
    if (!busy && !photoState.connected && !photoState.serviceError) {
      message = photoState.known ? "请先连接真实板卡；模拟与回放不能拍摄照片。" : "正在检查板卡与照片服务连接。";
    }
    refs.photoMessage.textContent = message;
    refs.photoProgress.hidden = !busy;
    const stageOrder = ["capturing", "transferring", "verifying", "ready"];
    const current = phase === "arming" ? 0 : stageOrder.indexOf(phase);
    refs.photoStages.forEach((item, index) => {
      item.classList.toggle("is-active", index === current);
      item.classList.toggle("is-complete", current > index || phase === "ready");
      if (index === current) item.setAttribute("aria-current", "step"); else item.removeAttribute("aria-current");
    });
    const photo = photoState.photo;
    refs.photoMeta.hidden = !photo;
    refs.photoDownload.hidden = !photo;
    if (photo) {
      refs.photoImageTag.textContent = job.phase === "ready" && job.photo && job.photo.id === photo.id ? "VERIFIED JPEG · 原始尺寸" : "上次成功照片 · VERIFIED JPEG";
      refs.photoDimensions.textContent = "2592 × 1944 · 5,038,848 像素";
      refs.photoSize.textContent = `${(photo.size / 1024).toFixed(1)} KiB · JPEG`;
      refs.photoId.textContent = photo.id;
      const time = Number(photo.verified_at_ms);
      refs.photoTime.textContent = Number.isFinite(time) && time > 0 ? new Date(time).toLocaleString("zh-CN", { hour12: false }) : "服务未提供时间";
      refs.photoDownload.href = photo.url;
      refs.photoDownload.download = `OV5640_2592x1944_${photo.id}.jpg`;
      if (refs.photoImage.getAttribute("src") !== photo.url) {
        refs.photoImage.hidden = true; refs.photoEmpty.hidden = false; refs.photoImageTag.hidden = true;
        refs.photoImage.src = photo.url;
      }
    }
  }

  async function jsonRequest(url, options) {
    const response = await fetch(url, { cache: "no-store", credentials: "same-origin", ...options });
    const data = await response.json();
    if (!response.ok) throw new Error(data.error || data.message || `照片请求失败 (${response.status})`);
    return data;
  }

  function stopPhotoPolling() {
    photoPolling = false; photoGeneration += 1;
    if (photoTimer !== null) clearTimeout(photoTimer);
    photoTimer = null;
    if (photoController) photoController.abort();
    if (photoPostController) photoPostController.abort();
    photoController = null; photoPostController = null;
    photoState.postPending = false; photoState.known = false;
    renderPhoto();
  }

  function startPhotoPolling() {
    if (!root || photoPolling || document.hidden || !state.active) return;
    photoPolling = true; photoState.known = false;
    renderPhoto();
    pollPhoto();
  }

  async function pollPhoto() {
    if (!photoPolling || photoController) return;
    const generation = photoGeneration;
    const revision = photoJobRevision;
    const controller = new AbortController(); photoController = controller;
    const deadline = setTimeout(() => controller.abort(), 5000);
    try {
      const [job, connection] = await Promise.all([
        jsonRequest("/api/photo", { signal: controller.signal }),
        jsonRequest("/api/status", { signal: controller.signal })
      ]);
      if (generation !== photoGeneration || !photoPolling || revision !== photoJobRevision || photoState.postPending) return;
      acceptPhotoJob(job);
      photoState.connected = connection.connected === true && connection.mode === "LIVE";
      photoState.known = true; photoState.serviceError = "";
    } catch (error) {
      if (generation !== photoGeneration || !photoPolling) return;
      photoState.known = false;
      photoState.serviceError = error.name === "AbortError" ? "连接照片服务超时，正在重新检查。" : String(error.message || "无法连接照片服务");
    } finally {
      clearTimeout(deadline);
      if (generation === photoGeneration && photoPolling) {
        photoController = null;
        renderPhoto();
        photoTimer = setTimeout(() => { photoTimer = null; pollPhoto(); }, photoState.job.busy ? 650 : 1500);
      }
    }
  }

  async function capturePhoto() {
    if (!photoPolling || !photoState.known || !photoState.connected || photoState.job.busy || photoState.postPending) return;
    const generation = photoGeneration;
    photoJobRevision += 1;
    photoState.postPending = true; photoState.serviceError = "";
    const controller = new AbortController(); photoPostController = controller;
    const deadline = setTimeout(() => controller.abort(), 6000);
    renderPhoto();
    try {
      const job = await jsonRequest("/api/photo", { method: "POST", signal: controller.signal,
        headers: { "Content-Type": "application/json" }, body: "{}" });
      if (generation !== photoGeneration || !photoPolling) return;
      acceptPhotoJob(job);
    } catch (error) {
      if (generation !== photoGeneration || !photoPolling) return;
      photoState.job = { phase: "error", busy: false,
        message: error.name === "AbortError" ? "拍照请求尚未确认，正在查询任务状态，请稍候。" : String(error.message || "拍照请求失败") };
      // Reconcile with the server before enabling another POST after ambiguity.
      photoState.known = false;
    } finally {
      clearTimeout(deadline);
      if (generation === photoGeneration && photoPolling) {
        photoJobRevision += 1;
        photoState.postPending = false; photoPostController = null;
        renderPhoto();
        if (photoTimer !== null) clearTimeout(photoTimer);
        photoTimer = null;
        if (!photoController) pollPhoto();
      }
    }
  }

  document.addEventListener("visibilitychange", () => {
    if (document.hidden) stopPhotoPolling(); else if (state.active) startPhotoPolling();
  });
  window.addEventListener("pagehide", stopPhotoPolling);
  window.addEventListener("pageshow", () => { if (state.active) startPhotoPolling(); });

  window.DriveCameraView = Object.freeze({ mount, pushFrame, pushPacket, disconnect, setActive });
})();
