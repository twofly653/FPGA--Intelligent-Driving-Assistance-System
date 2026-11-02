const THREE = window.THREE;
const GLTFLoader = THREE.GLTFLoader;
const RoomEnvironment = THREE.RoomEnvironment;

// A local classic script: the whole demo also runs from a file:// URL.
class VehicleScene {
  constructor(canvas) {
    this.canvas = canvas;
    this.ready = false;
    this.renderer = new THREE.WebGLRenderer({
      canvas,
      alpha: true,
      antialias: true,
      powerPreference: "high-performance"
    });
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
    this.renderer.setClearColor(0x000000, 0);
    this.renderer.outputEncoding = THREE.sRGBEncoding;
    this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
    this.renderer.toneMappingExposure = 0.86;

    this.scene = new THREE.Scene();
    this.camera = new THREE.PerspectiveCamera(34, 1, 0.1, 80);
    // The asset's nose points toward +Z after its root transform.
    this.camera.position.set(6.6, 4.5, 8.8);
    this.camera.lookAt(0, 0.05, 0);

    this.scene.add(new THREE.HemisphereLight(0xe5f8ff, 0x25394b, 0.8));
    const key = new THREE.DirectionalLight(0xf6fbff, 1.25);
    key.position.set(-3, 8, -5);
    this.scene.add(key);
    const edge = new THREE.DirectionalLight(0x62cde3, 0.72);
    edge.position.set(5, 3, 6);
    this.scene.add(edge);
    const warm = new THREE.DirectionalLight(0xffd5aa, 0.32);
    warm.position.set(-7, 2, 3);
    this.scene.add(warm);

    const pmrem = new THREE.PMREMGenerator(this.renderer);
    const room = new RoomEnvironment();
    this.scene.environment = pmrem.fromScene(room, 0.04).texture;
    pmrem.dispose();
    room.dispose();

    this.poseGroup = new THREE.Group();
    this.scene.add(this.poseGroup);
    this.poseTarget = new THREE.Quaternion();
    this.lastRenderTime = 0;
    this.addGroundShadow();
    this.loadCar();
  }

  addGroundShadow() {
    const surface = document.createElement("canvas");
    surface.width = surface.height = 128;
    const c = surface.getContext("2d");
    const gradient = c.createRadialGradient(64, 64, 4, 64, 64, 62);
    gradient.addColorStop(0, "rgba(0,5,12,.67)");
    gradient.addColorStop(0.5, "rgba(0,8,19,.38)");
    gradient.addColorStop(1, "rgba(0,8,19,0)");
    c.fillStyle = gradient;
    c.fillRect(0, 0, 128, 128);
    const shadow = new THREE.Mesh(
      new THREE.PlaneGeometry(7.2, 4.3),
      new THREE.MeshBasicMaterial({
        map: new THREE.CanvasTexture(surface),
        transparent: true,
        depthWrite: false,
        side: THREE.DoubleSide
      })
    );
    shadow.rotation.x = -Math.PI / 2;
    shadow.position.y = -0.86;
    this.scene.add(shadow);
  }

  loadCar() {
    const encoded = window.DRIVESENSE_CAR_DATA;
    if (!encoded) throw new Error("Car model data is missing");
    const decoded = atob(encoded);
    const bytes = new Uint8Array(decoded.length);
    for (let i = 0; i < decoded.length; i++) bytes[i] = decoded.charCodeAt(i);
    delete window.DRIVESENSE_CAR_DATA;

    new GLTFLoader().parse(bytes.buffer, "", (gltf) => {
      const model = gltf.scene;
      // Brand marks are also removed from the bundled GLB itself.
      model.traverse((object) => {
        if (object.name === "License Plate" || object.name === "InteriorSteeringEmblem") {
          object.visible = false;
        }
        if (!object.isMesh) return;
        object.frustumCulled = false;
        const materials = Array.isArray(object.material) ? object.material : [object.material];
        for (const material of materials) {
          if (!material) continue;
          if (/Paint 1 Carmine/i.test(material.name)) {
            material.color.set("#075a78");
            material.metalness = 0.52;
            material.roughness = 0.28;
            material.envMapIntensity = 0.55;
            if ("clearcoat" in material) material.clearcoat = 0.95;
          } else if (/Paint 2 Carmine/i.test(material.name)) {
            material.color.set("#173e54");
            material.metalness = 0.55;
            material.roughness = 0.34;
            material.envMapIntensity = 0.55;
          } else if (material.name === "Glass") {
            material.color.set("#102b3a");
            if ("transmission" in material) material.transmission = 0;
            material.opacity = 1;
            material.transparent = false;
            material.metalness = 0.08;
            material.roughness = 0.18;
            material.envMapIntensity = 0.28;
          }
        }
      });

      const bounds = new THREE.Box3().setFromObject(model);
      const size = bounds.getSize(new THREE.Vector3());
      const center = bounds.getCenter(new THREE.Vector3());
      const scale = 5.25 / Math.max(size.x, size.z);
      const centered = new THREE.Group();
      centered.scale.setScalar(scale);
      centered.position.copy(center).multiplyScalar(-scale);
      centered.add(model);
      this.poseGroup.add(centered);
      this.ready = true;
      this.canvas.classList.add("ready");
      window.dispatchEvent(new CustomEvent("drivesense:model-ready"));
    }, (error) => {
      console.error("High-detail car could not load; retaining the fallback model", error);
    });
  }

  resize(width, height) {
    this.camera.aspect = Math.max(width, 1) / Math.max(height, 1);
    this.camera.updateProjectionMatrix();
    this.renderer.setSize(Math.max(width, 1), Math.max(height, 1), false);
  }

  render(pose, zoom, width, live) {
    if (!this.ready) return;
    this.poseTarget.fromArray(window.DriveAttitudeAdapter.detailedQuaternion(pose.roll,pose.pitch,pose.yaw));
    const now=performance.now(), dt=this.lastRenderTime?Math.min(.1,(now-this.lastRenderTime)/1000):0;
    if(live&&this.lastRenderTime)this.poseGroup.quaternion.slerp(this.poseTarget,1-Math.exp(-dt/.018));
    else this.poseGroup.quaternion.copy(this.poseTarget);
    this.lastRenderTime=now;
    // Scale smoothly with the stage width. The old 700 px step enlarged the
    // model abruptly and cropped the nose/wheels on medium HDMI layouts.
    const cameraZoom = zoom * Math.min(1.24, Math.max(1.05, 0.9 + width / 3000));
    if (this.camera.zoom !== cameraZoom) {
      this.camera.zoom = cameraZoom;
      this.camera.updateProjectionMatrix();
    }
    this.renderer.render(this.scene, this.camera);
  }
}

window.DriveSenseCar3D = {
  create(canvas) {
    return new VehicleScene(canvas);
  }
};
