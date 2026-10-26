// A flight of RetroVega in the browser: the page with no files, Fly, then the
// rooms to the deck by the same hotspots as retrovega/tests/flight.txt, the
// launch, screenshots of the flight, and the engine's log at the end (its
// "flight:" and "ships:" lines, and any FAIL).  scripts/web-rv.mjs stops at
// the quarters: this is the check that the flight itself works in the page.
//   PLAYWRIGHT_DIR=... SHOT_DIR=... node scripts/web-rv-flight.mjs PAGE_URL LOBBY_URL
// CMD="rv /noquit /lasers -1 /flight 30" runs the game with other arguments (the page's ?cmd=; the laser
// benchmark logs its "lasers:" and "bolts:" lines); FLIGHT_S is how long the flight lasts (20); PARAMS adds
// page parameters ("cycles=40000&env.WCNET_PERF=4": the layer's "perf:" lines, emulator speed against the
// wall clock, are printed at the end).
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";

const [pageUrl, lobbyUrl] = process.argv.slice(2);
if (!pageUrl || !lobbyUrl) {
  console.error("usage: node web-rv-flight.mjs PAGE_URL LOBBY_URL");
  process.exit(2);
}
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
const chrome = process.env.CHROME || "/usr/bin/google-chrome";
const shotDir = process.env.SHOT_DIR || ".";
const browser = await chromium.launch({ headless: true, executablePath: chrome,
  args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required"] });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const logs = [];
const context = await browser.newContext();
const page = await context.newPage();
page.on("console", (m) => logs.push(m.text()));
page.on("pageerror", (e) => { logs.push("PAGE ERROR: " + e.message); console.log("PAGE ERROR: " + e.message); });
const u = new URL(pageUrl);
u.searchParams.set("server", lobbyUrl);
u.searchParams.set("hall", "RVHALL-" + Math.random().toString(36).slice(2, 6).toUpperCase());
u.searchParams.set("callsign", "FLIGHT");
u.searchParams.set("retrovega", "");
if (process.env.CMD) u.searchParams.set("cmd", process.env.CMD);
for (const kv of (process.env.PARAMS || "").split("&").filter(Boolean)) {
  const i = kv.indexOf("=");
  u.searchParams.set(i < 0 ? kv : kv.slice(0, i), i < 0 ? "" : kv.slice(i + 1));
}
const flightS = Number(process.env.FLIGHT_S) || 20;
await page.goto(u.toString());
await page.waitForFunction(() => document.getElementById("sourceStatus").dataset.state === "ready", null, { timeout: 60000 });
const rvlog = () => page.evaluate(() => {
  try { return new TextDecoder().decode(window.DOSBox.FS.readFile("/game/RVLOG.TXT")); } catch (e) { return ""; }
});
const waitLog = async (re, ms) => {
  const until = Date.now() + ms;
  for (;;) {
    const t = await rvlog();
    if (re.test(t) || Date.now() > until) return t;
    await sleep(500);
  }
};
await page.waitForFunction(() => window.__wcHall && window.__wcHall.inside(), null, { timeout: 30000 }).catch(() => {});
await page.click("#join");
await page.waitForFunction(() => !document.getElementById("lobby").hidden && !document.getElementById("fly").disabled, null, { timeout: 30000 });
await page.click("#fly");
let t = await waitLog(/input: mouse/, 120000);
console.log(/rvHello: DS is ready/.test(t) ? "ok   the game started" : "FAIL the game did not start");
await sleep(4000);
const box = await page.$eval("#canvas", (c) => { const b = c.getBoundingClientRect(); return { x: b.x, y: b.y, w: b.width, h: b.height }; });
const at = (gx, gy) => ({ x: box.x + gx * box.w / 640, y: box.y + gy * box.h / 400 });   // game pixels to the page
const click = async (gx, gy) => { const p = at(gx, gy); await page.mouse.move(p.x, p.y); await sleep(300); await page.mouse.click(p.x, p.y); };
const shot = async (name) => {
  await page.$eval("#canvas", () => 0);
  await page.screenshot({ path: path.join(shotDir, `rv-flight-${name}.png`), clip: { x: box.x, y: box.y, width: box.w, height: box.h } });
};
await page.focus("#canvas");
await page.keyboard.press("Enter");                     // the welcome
await sleep(1500);
// the pointer by keyboard: Tab puts it on the next hotspot (the engine logs "tab: to x,y"), Enter clicks it.
// (A page's mouse moves the DOS driver's pointer by its motion, so a click at a page position does not land
// on a game position; and Escape in a room quits the game, so a missed hotspot must not be followed by one.)
const lastTab = (t) => { const m = [...t.matchAll(/tab: to (\d+),(\d+)/g)].pop(); return m ? [Number(m[1]), Number(m[2])] : null; };
const hotspot = async (gx, gy, what) => {
  for (let i = 0; i < 10; i++) {
    await page.keyboard.press("Tab");
    await sleep(600);
    const p = lastTab(await rvlog());
    if (p && Math.abs(p[0] - gx) <= 25 && Math.abs(p[1] - gy) <= 25) {
      await page.keyboard.press("Enter");
      console.log(`ok   ${what}: the pointer on ${p[0]},${p[1]}`);
      return true;
    }
  }
  console.log(`FAIL ${what}: no hotspot near ${gx},${gy}`);
  return false;
};
const rooms = async () => (await rvlog()).split("stub: roomEntered").length - 1;
let n = await rooms();
if (!await hotspot(593, 200, "the quarters' door")) process.exit(1);
await waitLog(new RegExp(`(stub: roomEntered[^]*){${n + 1}}`), 8000); await sleep(1500);
if (!await hotspot(431, 170, "the lounge's hatch")) process.exit(1);
await waitLog(new RegExp(`(stub: roomEntered[^]*){${n + 2}}`), 8000); await sleep(1500);
if (!await hotspot(145, 152, "the bar's hatch")) process.exit(1);
t = await waitLog(/briefingStarted/, 8000);
if (!/briefingStarted/.test(t)) { console.log("FAIL no briefing"); process.exit(1); }
await sleep(2000);
for (let i = 0; i < 3; i++) { await page.keyboard.press("Escape"); await sleep(1200); }
await waitLog(new RegExp(`(stub: roomEntered[^]*){${n + 3}}`), 8000); await sleep(2500);
await shot("deck");
if (!await hotspot(478, 176, "the deck's fighter")) process.exit(1);
t = await waitLog(/flight: L1c0|runHangar|flight: slot/, 150000);   // (the page fetches and makes the flight's sets first)
// (the flight's own start line, "flight: MISSION, N ships": the ships' spawn lines are the game's logic's)
const flying = logs.some((l) => /runHangarMission/.test(l)) || /flight: slot|flight: \S+, \d+ ships/.test(t);
console.log(flying ? "ok   the flight started" : "FAIL no flight started (" + (t.split("\n").slice(-3).join(" | ")) + ")");
await sleep(4000); await shot("flight-4s");
await sleep(8000); await shot("flight-12s");
if (!/\/flight /.test(process.env.CMD || "")) {    // (a flight lands when the player calls the carrier: C)
  await sleep(Math.max(0, flightS - 12) * 1000);
  await page.keyboard.press("c");
}
t = await waitLog(/flight: over/, (flightS + 20) * 1000);
console.log(/flight: over/.test(t) ? "ok   the flight ended" : "FAIL the flight did not end");
// the sets the page made for the flight, kept in the browser's cache for the next visit (wc.js rvProvideOne)
const cached = await page.evaluate(async () => {
  try { const n = {}; for (const k of await caches.keys()) if (k.startsWith("rv-")) n[k] = (await (await caches.open(k)).keys()).length; return n; }
  catch (e) { return { error: e.message }; }
});
console.log(Object.keys(cached).some((k) => k.startsWith("rv-sets-") && cached[k] > 0) ? `ok   the browser keeps the sets ${JSON.stringify(cached)}` : `note no cached sets ${JSON.stringify(cached)}`);
await sleep(3000); await shot("after");
const lines = t.split("\n").filter((l) => /^(flight:|ships:|lightfield:|FAIL|cockpit:|stars|lasers:|bolts:)/.test(l));
console.log(lines.join("\n"));
console.log(/FAIL /.test(t) ? "FAIL a stop in the engine's log" : "ok   no stop in the engine's log");
console.log(logs.filter((l) => /perf: |'s views \(|^made SHIP|^ship sets|rv\/ships\.json|sprite worker|could not keep/.test(l)).join("\n"));
console.log(logs.filter((l) => /ended|PAGE ERROR|stack/.test(l)).slice(0, 6).join("\n"));
fs.writeFileSync(path.join(shotDir, "rv-flight-RVLOG.TXT"), t);
fs.writeFileSync(path.join(shotDir, "rv-flight.log"), logs.join("\n") + "\n");
await browser.close();
