// The page's sprite-set generator (web/rvsprites.js) against the C tool's
// files: every SHIPnn.RVS and .RVH in retrovega/build/dos must come out byte for byte.
//   node scripts/rvsprites-check.mjs            (after retrovega/build.sh)
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { makeSet } from "../web/rvsprites.js";

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), "..", "retrovega");
const dos = path.join(root, "build", "dos");
let ok = 0, bad = 0;
for (const f of fs.readdirSync(path.join(root, "art", "ships")).sort()) {
  if (!/^SHIP[0-9A-F]{2}\.RV$/.test(f)) continue;
  const rv = new Uint8Array(fs.readFileSync(path.join(root, "art", "ships", f)));
  // the two recipes build.sh makes: the sets (0 4 9) and the block mode's ladder (0 0 0 1 2, SHIPnn.RVH)
  for (const [ext, args] of [["S", [0, 4, 9]], ["H", [0, 0, 0, 1, 2]]]) {
    const want = path.join(dos, f + ext);
    if (!fs.existsSync(want)) continue;
    const t0 = Date.now();
    const set = makeSet(rv, ...args);
    const same = Buffer.compare(Buffer.from(set), fs.readFileSync(want)) === 0;
    console.log(`${same ? "ok  " : "FAIL"} ${f}${ext}: ${set.length >> 10} KB in ${Date.now() - t0} ms${same ? "" : " differs from the C tool's"}`);
    same ? ok++ : bad++;
  }
}
console.log(bad ? `RVSPRITES FAILED (${bad} differ)` : `RVSPRITES OK (${ok} sets identical to the C tool's)`);
process.exit(bad ? 1 : 0);
