// Firmware body: +X forward, +Y left, +Z up. Model: +X forward, +Y up, +Z right.
// TEL yaw is CLOCKWISE heading, i.e. the negative of right-handed body yaw.
// R_model = Ry(-heading) Rz(-pitch) Rx(roll), the conjugated ZYX body rotation.
export function modelQuaternion(roll, pitch, heading) {
  const h = Math.PI / 360;
  const x = roll * h, y = -heading * h, z = -pitch * h;
  const sx = Math.sin(x), cx = Math.cos(x), sy = Math.sin(y), cy = Math.cos(y), sz = Math.sin(z), cz = Math.cos(z);
  return [cy * cz * sx + sy * sz * cx,
          sy * cz * cx + cy * sz * sx,
          cy * sz * cx - sy * cz * sx,
          cy * cz * cx - sy * sz * sx];
}

export function rotateVector(q, v) {
  const [x, y, z, w] = q, [vx, vy, vz] = v;
  const tx = 2 * (y * vz - z * vy), ty = 2 * (z * vx - x * vz), tz = 2 * (x * vy - y * vx);
  return [vx + w * tx + y * tz - z * ty,
          vy + w * ty + z * tx - x * tz,
          vz + w * tz + x * ty - y * tx];
}
