// Axis and combined-rotation tests independent of WebGL and the live board.
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { spawnSync } from "node:child_process";
const source = await readFile(new URL("./attitude_math.js", import.meta.url), "utf8");
const { modelQuaternion, rotateVector } = await import("data:text/javascript;base64," + Buffer.from(source).toString("base64"));
const close = (a, b) => a.forEach((v, i) => assert.ok(Math.abs(v - b[i]) < 1e-10, `${a} != ${b}`));
close(rotateVector(modelQuaternion(0, 0, 0), [1, 0, 0]), [1, 0, 0]);
close(rotateVector(modelQuaternion(90, 0, 0), [0, 0, -1]), [0, 1, 0]); // left side rises
close(rotateVector(modelQuaternion(0, 90, 0), [1, 0, 0]), [0, -1, 0]); // positive pitch nose down
close(rotateVector(modelQuaternion(0, 0, 90), [1, 0, 0]), [0, 0, 1]); // clockwise to model right
close(rotateVector(modelQuaternion(0, 0, 359), [1, 0, 0]), rotateVector(modelQuaternion(0, 0, -1), [1, 0, 0]));
// Independently rotate in body coordinates, then remap to model coordinates.
for (let i = 0; i < 200; i++) {
  const roll = i * 7.3 - 180, pitch = i * 0.83 - 80, heading = i * 19.1;
  const r = roll * Math.PI / 180, p = pitch * Math.PI / 180, y = -heading * Math.PI / 180;
  const body = [0.3, 0.7, -0.4];
  const rx = [body[0], Math.cos(r) * body[1] - Math.sin(r) * body[2], Math.sin(r) * body[1] + Math.cos(r) * body[2]];
  const ry = [Math.cos(p) * rx[0] + Math.sin(p) * rx[2], rx[1], -Math.sin(p) * rx[0] + Math.cos(p) * rx[2]];
  const rz = [Math.cos(y) * ry[0] - Math.sin(y) * ry[1], Math.sin(y) * ry[0] + Math.cos(y) * ry[1], ry[2]];
  close(rotateVector(modelQuaternion(roll, pitch, heading), [body[0], body[2], -body[1]]), [rz[0], rz[2], -rz[1]]);
}
const html = await readFile(new URL("./index.html", import.meta.url), "utf8");
const moduleSource = html.match(/<script type="module">([\s\S]*?)<\/script>/)[1];
const syntax = spawnSync(process.execPath, ["--check", "--input-type=module"], {input: moduleSource, encoding:"utf8"});
assert.equal(syntax.status, 0, syntax.stderr);
assert.ok(!html.includes("https://unpkg.com"), "Runtime libraries must work offline");
console.log("PASS: roll/pitch/heading signs, 359/0 wrap, 200 combined rotations, HTML module syntax and local imports");
