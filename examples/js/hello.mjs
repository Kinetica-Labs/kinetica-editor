// The first rung in JavaScript: a box falls on the floor and its height is printed - the same scene as
// examples/python/01_hello.py, the same words. The editor's API is lines of JSON, so any language that starts a program
// and reads its lines speaks it; this is Node.js and its standard library alone, nothing to install:
//
//     node examples/js/hello.mjs
//
// It starts kinetica-core (KE_CORE, else build/kinetica-core of this tree, build/Release on Windows - cmake --build build --target kinetica-core)
// and talks on its stdin and stdout: a request a line {"id", "op", "args"}, an answer a line {"id", "result"}. What the
// operations are and what they take: ops_list; the scene file's lines and keys: schema_get.
import { spawn } from "node:child_process";
import { existsSync } from "node:fs";
import { createInterface } from "node:readline";
import { fileURLToPath } from "node:url";
import path from "node:path";

const here = path.dirname(fileURLToPath(import.meta.url));
const exe = "kinetica-core" + (process.platform === "win32" ? ".exe" : "");
const program = process.env.KE_CORE || [path.join(here, "..", "..", "build", exe), path.join(here, "..", "..", "build", "Release", exe)].find(existsSync);
if (!program) throw new Error(`${exe} is not built: cmake --build build --target kinetica-core, or set KE_CORE`);
const core = spawn(program, [], { stdio: ["pipe", "pipe", "inherit"] });
const answers = createInterface({ input: core.stdout })[Symbol.asyncIterator]();
let next = 0;

// One operation: its answer, or an error that says what was wrong and what was meant.
async function call(op, args = {}) {
  core.stdin.write(JSON.stringify({ id: ++next, op, args }) + "\n");
  const { value } = await answers.next();
  const { result } = JSON.parse(value);
  if (!result.ok) throw new Error(`${op}: ${result.error}`);
  return result;
}

const box = (await call("scene_add", { shape: "box", position: [0, 1, 0], role: "rigid" })).id;
await call("sim_step", { frames: 60 }); // a second: 60 frames of 1/60 s
const { channels } = await call("sim_measure", { objects: [box] });
const height = channels.find((c) => c.id === `${box}/height`).value;
console.log(`after 1 s the box's centre is ${height.toFixed(3)} m high`);
core.stdin.end();
