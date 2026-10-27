// RetroVega on a phone, emulated: a Pixel 7's screen and touch, and no Pointer Lock (phones have none), every input
// a tap.  From fireflyk64.github.io/rv/ (or any page URL): one player, Join, Fly, then the base by taps alone: the
// welcome, the bunk menu (its close box; a bunk, SAVE, SAVE: "Bunk 1"; another, SAVE, the Keys button and a name
// typed as a phone's keyboard types (input events, no key codes), Enter; the Esc button; a tap outside), the quarters'
// door, the lounge's hatch, the bar's hatch to the briefing, the deck, and the Fire button (web/touch.js).  A
// screenshot of each step (SHOT_DIR/phone-NN-name.png) and the engine's log.
//   PLAYWRIGHT_DIR=... SHOT_DIR=... node scripts/web-rv-phone.mjs PAGE_URL
// LOCK=1 keeps Pointer Lock (a desktop browser's touch screen); NOFS=1 takes the Fullscreen API away (an iPhone, which
// has it for videos only: the touch controls are then a row under the picture).  Prints "PHONE OK" when every step showed.
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";

const [pageUrl] = process.argv.slice(2);
if (!pageUrl) { console.error("usage: node web-rv-phone.mjs PAGE_URL"); process.exit(2); }
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium, devices } = require("playwright");
const shotDir = process.env.SHOT_DIR || ".";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROME || "/usr/bin/google-chrome",
  args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required"] });
let fails = 0;
const ok = (c, what) => { console.log((c ? "ok   " : "FAIL ") + what); if (!c) fails++; return c; };
const logs = [];
try {
  const context = await browser.newContext({ ...devices["Pixel 7"] });
  if (!process.env.LOCK)
    await context.addInitScript(() => { Element.prototype.requestPointerLock = undefined; });
  if (process.env.NOFS)
    await context.addInitScript(() => { Element.prototype.requestFullscreen = undefined; Element.prototype.webkitRequestFullscreen = undefined; });
  const page = await context.newPage();
  page.on("console", (m) => logs.push(m.text()));
  page.on("pageerror", (e) => logs.push("PAGE ERROR: " + e.message));
  let n = 0;
  const shot = async (name, full) => {
    const file = path.join(shotDir, `phone-${String(++n).padStart(2, "0")}-${name}.png`);
    if (full) await page.screenshot({ path: file });
    else await page.locator("#canvas").screenshot({ path: file });
  };
  await page.goto(pageUrl);
  await page.waitForFunction(() => document.getElementById("sourceStatus") && document.getElementById("sourceStatus").dataset.state === "ready",
    null, { timeout: 90000 });
  ok(/\/dosbox\/\?retrovega/.test(page.url()), `the address leads to the game (${page.url()})`);
  await shot("page", true);
  await page.locator("#seats").fill("1");
  await page.locator("#join").tap();
  await page.waitForFunction(() => !document.getElementById("lobby").hidden && !document.getElementById("fly").disabled, null, { timeout: 30000 });
  await page.locator("#fly").tap();
  const rvlog = () => page.evaluate(() => {
    try { return new TextDecoder().decode(window.DOSBox.FS.readFile("/game/RVLOG.TXT")); } catch (e) { return ""; }
  });
  const waitLog = async (re, ms) => {
    const until = Date.now() + ms;
    for (;;) { const t = await rvlog(); if (re.test(t) || Date.now() > until) return t; await sleep(500); }
  };
  let t = await waitLog(/input: mouse/, 120000);
  ok(/rvHello: DS is ready/.test(t), "the game started");
  await sleep(5000);
  await page.locator("#canvas").scrollIntoViewIfNeeded();
  const box = await page.$eval("#canvas", (c) => { const b = c.getBoundingClientRect(); return { x: b.x, y: b.y, w: b.width, h: b.height }; });
  // the picture inside the canvas: 640 x 400 shown at 1.6, letterboxed (a phone held upright has a tall canvas)
  const pw = Math.min(box.w, box.h * 1.6), ph = pw / 1.6;
  const pic = { x: box.x + (box.w - pw) / 2, y: box.y + (box.h - ph) / 2, w: pw, h: ph };
  const tap = async (gx, gy) => {     // a game pixel (640 x 400) as a tap on the page
    await page.touchscreen.tap(pic.x + gx * pic.w / 640, pic.y + gy * pic.h / 400);
    await sleep(900);
  };
  await shot("welcome");
  await tap(320, 200);                // (the welcome message closes on a tap)
  await shot("quarters");
  await tap(118, 213);                // the bunk
  await shot("bunk-menu");
  await tap(532, 56);                 // its close box
  await shot("bunk-closed");
  await tap(118, 213);
  await tap(300, 92);                 // bunk 1
  await tap(125, 323);                // SAVE (S)
  await shot("bunk-name");
  await tap(125, 323);                // SAVE (Enter): no name typed, so "Bunk 1"
  t = await waitLog(/saved bunk 1 'Bunk 1'/, 5000);
  ok(/saved bunk 1 'Bunk 1'/.test(t), "saved to bunk 1 by taps alone");
  await shot("bunk-saved");
  ok(await page.isVisible("#touchKeys"), "the touch controls show");
  await shot("controls", true);
  await tap(300, 118);                // bunk 2
  await tap(125, 323);                // SAVE (S): the name
  await page.locator("#touchKeys").tap();
  await sleep(500);
  ok(await page.evaluate(() => document.activeElement && document.activeElement.id === "touchType"), "Keys: the keyboard's field has the focus");
  await page.keyboard.insertText("Cal Brody");   // (as a phone's keyboard: input events, no key codes)
  await sleep(1500);
  await shot("bunk-typing");
  await page.keyboard.press("Enter");
  t = await waitLog(/saved bunk 2 'Cal Brody'/, 6000);
  ok(/saved bunk 2 'Cal Brody'/.test(t), "typed a name with the Keys button: bunk 2 'Cal Brody'");
  await page.locator("#touchKeys").tap();        // (the keyboard away)
  await shot("bunk-typed");
  await page.locator("#touchEsc").tap();         // Esc: back to the list
  await sleep(800);
  await tap(600, 380);                // outside the panel: closed
  await shot("bunk-out");
  // (each room is known by its music's cue in the log: the roomEntered stub logs only its first call)
  await tap(593, 200);                // the quarters' door
  t = await waitLog(/cue \d+ \(LOUNGE\.MID\)/, 8000);
  ok(/LOUNGE\.MID/.test(t), "the quarters' door: the lounge");
  await sleep(1500);
  await shot("lounge");
  await tap(431, 170);                // the lounge's hatch
  t = await waitLog(/cue \d+ \(BAR\.MID\)/, 8000);
  ok(/BAR\.MID/.test(t), "the lounge's hatch: the bar");
  await sleep(1500);
  await shot("bar");
  await tap(145, 152);                // the bar's hatch: the briefing
  t = await waitLog(/briefingStarted/, 8000);
  ok(/briefingStarted/.test(t), "the bar's hatch: the briefing");
  await sleep(3000);
  await shot("briefing");
  t = await waitLog(/cue \d+ \(LAUNCH\.MID\)/, 150000);   // (the briefing runs its course)
  ok(/LAUNCH\.MID/.test(t), "after the briefing: the deck");
  await sleep(2500);
  await shot("deck");
  // Fire: Space held while the button is (the emulator's key queue, watched)
  await page.evaluate(() => {
    const M = window.DOSBox, f = M._wc_web_key;
    window.__keys = [];
    M._wc_web_key = (k, d) => { window.__keys.push([k, d]); return f(k, d); };
  });
  await page.locator("#touchFire").tap();
  await sleep(500);
  const keys = await page.evaluate(() => window.__keys);
  ok(keys.length === 2 && keys[0][1] === 1 && keys[1][1] === 0 && keys[0][0] === keys[1][0], `Fire: a key down and up (${JSON.stringify(keys)})`);
  fs.writeFileSync(path.join(shotDir, "phone-RVLOG.TXT"), await rvlog());
} finally {
  fs.writeFileSync(path.join(shotDir, "phone-console.log"), logs.join("\n") + "\n");
  await browser.close();
}
console.log(fails ? `PHONE: ${fails} failed` : "PHONE OK");
process.exit(fails ? 1 : 0);
