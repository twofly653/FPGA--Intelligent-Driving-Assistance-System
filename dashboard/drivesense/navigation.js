(function () {
  "use strict";

  const names = { vehicle: "Dynamics", camera: "Camera", sensors: "Sensors" };
  const buttons = Array.from(document.querySelectorAll(".nav-item[data-view]"));
  const panels = Object.fromEntries(Object.keys(names).map((key) => [key, document.getElementById("view-" + key)]));
  const cameraRoot = document.getElementById("cameraRoot");
  let cameraMounted = false;
  let current = "";

  function selectedFromHash() {
    const key = location.hash.replace(/^#/, "").toLowerCase();
    return Object.prototype.hasOwnProperty.call(names, key) ? key : "vehicle";
  }

  function mountCamera() {
    if (cameraMounted || !window.DriveCameraView || !cameraRoot) return;
    window.DriveCameraView.mount(cameraRoot);
    cameraMounted = true;
  }

  function select(key) {
    if (!panels[key] || key === current) return;
    current = key;
    for (const [name, panel] of Object.entries(panels)) panel.hidden = name !== key;
    for (const button of buttons) {
      const active = button.dataset.view === key;
      button.classList.toggle("active", active);
      if (active) button.setAttribute("aria-current", "page");
      else button.removeAttribute("aria-current");
    }
    if (key === "camera") mountCamera();
    if (window.DriveCameraView && typeof window.DriveCameraView.setActive === "function") {
      window.DriveCameraView.setActive(key === "camera");
    }
    if (window.DriveDemo && typeof window.DriveDemo.setVisible === "function") {
      window.DriveDemo.setVisible(key === "vehicle");
    }
    document.title = "DriveSense — " + names[key] + " Demo";
    window.scrollTo({ top: 0, behavior: "auto" });
  }

  buttons.forEach((button) => button.addEventListener("click", () => {
    const key = button.dataset.view;
    if (location.hash === "#" + key) select(key);
    else location.hash = key;
  }));
  window.addEventListener("hashchange", () => select(selectedFromHash()));
  select(selectedFromHash());
  // Build the Camera workspace on the first paint as well. It used to be created
  // only on the first #camera visit, so the obstacle overlay and the traffic-light
  // card did not exist at all until the tab was opened. select() above has already
  // cleared setActive() for the default page, so no photo polling starts here.
  mountCamera();
})();
