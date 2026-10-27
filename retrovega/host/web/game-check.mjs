// game-check.mjs: the native build's page (host/web/site.sh) in headless Chrome: it loads, Fly starts the game, and
// the base works by the mouse: the welcome closes, the bunk menu saves (bunk 1 by its buttons; with PHONE=1 bunk 2
// named with the Keys button as a phone's keyboard types) and closes, the quarters' door leads to the lounge, its
// hatch to the bar, the bar's hatch to the briefing; then the deck, the launch (the ships' sets made by the page as
// the game asks), a flight, C to land (PHONE=1: Fire held, then C by the Keys button), the debrief; and the page
// reloaded keeps the saves.  Screenshots in SHOT_DIR (game-NN-name.png), the game's log as game-RVLOG.TXT.
// Prints "GAME OK" when every step showed.
//   PLAYWRIGHT_DIR=... SHOT_DIR=... [PHONE=1] node host/web/game-check.mjs PAGE_URL
// PHONE=1: a Pixel 7's screen and touch, every input a tap.
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";

const [pageUrl] = process.argv.slice(2);
if (!pageUrl) { console.error("usage: node game-check.mjs PAGE_URL"); process.exit(2); }
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium, devices } = require("playwright");
const phone = !!process.env.PHONE;
const shotDir = process.env.SHOT_DIR || ".";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROME || "/usr/bin/google-chrome",
  args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required"] });
let fails = 0;
const ok = (c, what) => { console.log((c ? "ok   " : "FAIL ") + what); if (!c) fails++; return c; };
const logs = [];
try {
  const context = await browser.newContext(phone ? { ...devices["Pixel 7"] } : { viewport: { width: 1300, height: 1000 } });
  const page = await context.newPage();
  page.on("console", (m) => logs.push(m.text()));
  page.on("pageerror", (e) => logs.push("PAGE ERROR: " + e.message));
  const pre = phone ? "phone" : "game";
  let n = 0;
  const shot = async (name, full) => {
    const file = path.join(shotDir, `${pre}-${String(++n).padStart(2, "0")}-${name}.png`);
    if (full) await page.screenshot({ path: file }); else await page.locator("#canvas").screenshot({ path: file });
  };
  const ready = () => page.waitForFunction(() => !document.getElementById("fly").disabled, null, { timeout: 120000 });
  const t0 = Date.now();
  await page.goto(pageUrl);
  await ready();
  ok(true, `the page is ready (${Date.now() - t0} ms)`);
  if (phone) await page.locator("#fly").tap(); else await page.locator("#fly").click();
  const rvlog = () => page.evaluate(() => window.RetroVegaPage.log());
  const waitLog = async (re, ms) => {
    const until = Date.now() + ms;
    for (;;) { const t = await rvlog(); if (re.test(t) || Date.now() > until) return t; await sleep(250); }
  };
  let t = await waitLog(/input: mouse/, 60000);
  ok(/rvHello: DS is ready/.test(t) && /music: OPL3 at 388h, our bank/.test(t), "the game started and found the OPL3");
  await sleep(2500);
  await page.locator("#canvas").scrollIntoViewIfNeeded();
  const box = await page.$eval("#canvas", (c) => { const b = c.getBoundingClientRect(); return { x: b.x, y: b.y, w: b.width, h: b.height }; });
  ok(Math.abs(box.w / box.h - 1.6) < 0.01, `the picture is 1.6 wide (${Math.round(box.w)} x ${Math.round(box.h)})`);
  // a game pixel (640 x 400) pressed: a tap, or the mouse moved there and clicked
  const press = async (gx, gy) => {
    const x = box.x + (gx + 0.5) * box.w / 640, y = box.y + (gy + 0.5) * box.h / 400;
    if (phone) await page.touchscreen.tap(x, y);
    else {
      await page.mouse.move(x, y, { steps: 4 });
      await sleep(150);
      await page.mouse.down();
      await sleep(80);
      await page.mouse.up();
    }
    await sleep(900);
  };
  await shot("welcome");
  await press(320, 200);                // (the welcome message closes)
  await shot("quarters");
  await press(118, 213);                // the bunk
  await shot("bunk-menu");
  await press(300, 92);                 // bunk 1
  await press(125, 323);                // SAVE (S)
  await press(125, 323);                // SAVE (Enter): no name typed, so "Bunk 1"
  t = await waitLog(/saved bunk 1 'Bunk 1'/, 5000);
  ok(/saved bunk 1 'Bunk 1'/.test(t), "saved to bunk 1 by its buttons");
  if (phone) {
    ok(await page.isVisible("#touchKeys"), "the touch controls show");
    await shot("controls", true);
    await press(300, 118);              // bunk 2
    await press(125, 323);              // SAVE (S): the name
    await page.locator("#touchKeys").tap();
    await sleep(500);
    ok(await page.evaluate(() => document.activeElement && document.activeElement.id === "touchType"), "Keys: the keyboard's field has the focus");
    await page.keyboard.insertText("Cal Brody");   // (as a phone's keyboard: input events, no key codes)
    await sleep(1500);
    await page.keyboard.press("Enter");
    t = await waitLog(/saved bunk 2 'Cal Brody'/, 6000);
    ok(/saved bunk 2 'Cal Brody'/.test(t), "typed a name with the Keys button: bunk 2 'Cal Brody'");
    await page.locator("#touchKeys").tap();      // (the keyboard away)
    await page.locator("#touchEsc").tap();       // Esc: back to the list
    await sleep(800);
  }
  await press(600, 380);                // outside the panel: closed
  await press(593, 200);                // the quarters' door
  t = await waitLog(/cue \d+ \(LOUNGE\.MID\)/, 8000);
  ok(/LOUNGE\.MID/.test(t), "the quarters' door: the lounge");
  await sleep(1500);
  await shot("lounge");
  await press(431, 170);                // the lounge's hatch
  t = await waitLog(/cue \d+ \(BAR\.MID\)/, 8000);
  ok(/BAR\.MID/.test(t), "the lounge's hatch: the bar");
  await sleep(1500);
  await shot("bar");
  await press(145, 152);                // the bar's hatch: the briefing
  t = await waitLog(/cue \d+ \(BRIEF\.MID\)/, 8000);
  ok(/BRIEF\.MID/.test(t), "the bar's hatch: the briefing");
  await sleep(2500);
  await shot("briefing");
  t = await waitLog(/cue \d+ \(LAUNCH\.MID\)/, 150000);   // (the briefing runs its course)
  ok(/LAUNCH\.MID/.test(t), "after the briefing: the deck");
  await sleep(2500);
  await shot("deck");
  await press(201, 172);                // the kit
  await sleep(1500);
  await press(478, 176);                // launch
  t = await waitLog(/flight: L1c0_m1/, 180000);   // (the ships' sets fetched and made by the page first)
  ok(/flight: L1c0_m1/.test(t), "launched: the flight (the ships' sets came from the page)");
  await sleep(4000);
  await shot("flight");
  if (phone) {
    // a finger dragged across the view steers (the pointer follows it) and never holds the button (the guns)
    const cdp = await context.newCDPSession(page);
    const at = (gx, gy) => ({ x: box.x + gx * box.w / 640, y: box.y + gy * box.h / 400 });
    const touch = (type, p) => cdp.send("Input.dispatchTouchEvent", { type, touchPoints: p ? [{ ...p, id: 1 }] : [] });
    await touch("touchStart", at(320, 110));
    let held = 0, xs = [];
    for (let k = 1; k <= 12; k++) {
      await touch("touchMove", at(320 + k * 15, 110 + k * 4));
      await sleep(80);
      const m = await page.evaluate(() => window.RetroVegaPage.module()._rv_page_mouse());
      held |= m & 3;
      xs.push((m >> 8) & 0xFFF);
    }
    await shot("flight-drag");
    await touch("touchEnd");
    ok(!held && xs[xs.length - 1] > xs[0] + 100, `a finger steers without the button held (x ${xs[0]} .. ${xs[xs.length - 1]}, buttons ${held})`);
    // Fire: Space held while the button is (the page's keys into the game, watched)
    await page.evaluate(() => {
      const M = window.RetroVegaPage.module(), f = M._rv_page_key;
      window.__keys = [];
      M._rv_page_key = (k, d) => { window.__keys.push([k, d]); return f(k, d); };
    });
    await page.locator("#touchFire").tap();
    await sleep(500);
    const keys = await page.evaluate(() => window.__keys);
    ok(keys.length === 2 && keys[0][0] === 0x39 && keys[0][1] === 1 && keys[1][1] === 0, `Fire: Space down and up (${JSON.stringify(keys)})`);
    await page.locator("#touchKeys").tap();
    await sleep(300);
    await page.keyboard.insertText("c");         // the carrier: land, typed with the Keys button
    await page.locator("#touchKeys").tap();
  } else {
    await page.keyboard.press("c");     // the carrier: land
  }
  t = await waitLog(/flight: over/, 20000);
  ok(/flight: over/.test(t), "C: landed");
  await sleep(4000);
  await shot("debrief");
  const audio = await page.evaluate(() => { const M = window.RetroVegaPage.module(); return M && M.SDL2 && M.SDL2.audioContext ? M.SDL2.audioContext.state : "none"; });
  ok(audio === "running", `the sound runs (${audio})`);
  fs.writeFileSync(path.join(shotDir, `${pre}-RVLOG.TXT`), await rvlog());
  // the saves outlive the page
  await sleep(1000);
  await page.reload();
  await ready();
  const kept = await page.evaluate(() => document.getElementById("log").textContent);
  const m = /(\d+) saved file\(s\) from this browser/.exec(kept);
  ok(m && +m[1] >= (phone ? 2 : 1), `the page reloaded keeps the saves (${m ? m[1] : "none"})`);
} finally {
  fs.writeFileSync(path.join(shotDir, `${phone ? "phone" : "game"}-console.log`), logs.join("\n") + "\n");
  await browser.close();
}
console.log(fails ? `GAME: ${fails} failed` : "GAME OK");
process.exit(fails ? 1 : 0);
