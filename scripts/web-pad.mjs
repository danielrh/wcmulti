// Headless test of a controller's stick in the browser build: one Chrome
// page with a simulated gamepad flies a mission and reads back what the game
// makes of the stick: the turn it is asked for (wc_web_steer).  Both games
// park their pointer in the middle of the cockpit's window, which is another
// in every ship, and a stick that rested anywhere else was a constant dive
// in every cockpit but the one it was measured in: at rest the turn must be
// none, and the same stick the same turn in every cockpit.  Driven by
// scripts/web-wc2.sh pad (Wing Commander II, from a zip of the game) and
// scripts/web-smoke.sh pad (Wing Commander, from the server's wc.tar.gz).
//
//   GAME_FILE=wc2.zip node scripts/web-pad.mjs PAGE_URL LOBBY_URL wc2
//   node scripts/web-pad.mjs PAGE_URL LOBBY_URL wc1
import { createRequire } from "node:module";
import path from "node:path";
const [pageUrl, lobbyUrl, game = "wc2"] = process.argv.slice(2);
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROME || "/usr/bin/google-chrome",
  args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required", "--use-fake-device-for-media-stream"] });
const results = [];
const check = (what, ok, detail) => { results.push(ok); console.log(`${ok ? "ok  " : "FAIL"} ${what}: ${JSON.stringify(detail)}`); };
const mock = () => {
  window.__pad = { id: "Mock Pad (STANDARD GAMEPAD)", index: 0, connected: true, mapping: "standard", timestamp: 0,
                   axes: [0, 0, 0, 0], buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })) };
  navigator.getGamepads = () => [window.__pad];
};

// Cockpits with windows of different heights: WC2's series 1 is flown in a
// Ferret and series 2 mission 0 in a Broadsword (the mission test, cockpit
// within seconds); Wing Commander's Enyo 1 in a Hornet and Gimle 2 in a
// Rapier (the forced mission, after the briefing).
const COCKPITS = {
  wc2: [["Ferret", { cmd: "loadfix -34 wc2 Origin -k l s1 m0" }], ["Broadsword", { cmd: "loadfix -34 wc2 Origin -k l s2 m0" }]],
  wc1: [["Hornet", { "env.MIS": "0", "env.SERIES": "1" }], ["Rapier", { "env.MIS": "1", "env.SERIES": "4" }]],
};
for (const [ship, how] of COCKPITS[game]) {
  const context = await browser.newContext();
  await context.addInitScript(mock);
  const page = await context.newPage();
  page.on("pageerror", (e) => console.log(`PAGE ERROR: ${e.message}`));
  const u = new URL(pageUrl);
  // (An empty WCROOM: the game runs without a session, so nobody is waited for.)
  // (WC1's own ship is made invulnerable for the test, the briefing is tapped through, and no missile is fired.)
  const wc1 = game === "wc1" ? { "env.WCNET_AUTOKEYS": "1", "env.WCNET_KEYSCRIPT": "0.5:!poke=00BA:00" } : {};
  for (const [k, v] of Object.entries({ room: "PAD-" + Math.random().toString(36).slice(2, 8).toUpperCase(), server: lobbyUrl, callsign: "HOST",
                                        "env.WCROOM": "", ...how, ...wc1 })) u.searchParams.set(k, v);
  await page.goto(u.toString());
  const axis = (i, v) => page.evaluate(([i, v]) => { window.__pad.axes[i] = v; }, [i, v]);
  const button = (i, v) => page.evaluate(([i, v]) => { window.__pad.buttons[i] = { pressed: v > 0.1, touched: v > 0, value: v }; }, [i, v]);
  if (process.env.GAME_FILE) await page.setInputFiles("#gamefile", process.env.GAME_FILE);
  await page.waitForFunction(() => /^(ready|error)$/.test(document.getElementById("sourceStatus").dataset.state || ""), null, { timeout: 300000 });
  await button(0, 1); await page.waitForTimeout(300); await button(0, 0);
  check(`${ship}: a button press picks the controller up`, (await page.inputValue("#padSelect")).startsWith("0:Mock Pad"), await page.inputValue("#padSelect"));
  await page.waitForFunction(() => !document.getElementById("lobby").hidden && !document.getElementById("fly").disabled, null, { timeout: 60000 });
  await page.click("#fly");
  await page.waitForFunction(() => window.DOSBox && window.DOSBox._wc_web_in_flight && window.DOSBox._wc_web_in_flight() === 1, null, { timeout: 240000 });
  await page.waitForTimeout(4000);
  const view = await page.evaluate(() => [0, 1, 2, 3].map((i) => window.DOSBox._wc_web_steer(0, i)));
  const turn = () => page.evaluate(() => [window.DOSBox._wc_web_steer(4, 0), window.DOSBox._wc_web_steer(4, 1)]);
  // What the game asks for with the stick held there (across, down), and at rest again.
  const held = async (a, v) => { await axis(a, v); await page.waitForTimeout(700); const t = await turn(); await axis(a, 0); await page.waitForTimeout(500); return t; };
  console.log(`${ship}: the view is ${view.join(",")} of 640x200`);

  let t = await turn();
  check(`${ship}: the stick at rest asks for no turn`, t[0] === 0 && t[1] === 0, t);
  // (The default layout has the pitch inverted: pulling back raises the nose,
  // and the game's "down" is then negative.  At the default sensitivity of
  // 100% full stick is the game's fastest turn, the view's edge.)
  const back = await held(1, 1), forward = await held(1, -1), right = await held(0, 1), left = await held(0, -1);
  check(`${ship}: full stick back and forward are the fastest turn up and down`, back[1] === -8 && forward[1] === 8 && back[0] === 0 && forward[0] === 0, [back, forward]);
  check(`${ship}: full stick right and left are the fastest turn`, right[0] === 8 && left[0] === -8 && right[1] === 0 && left[1] === 0, [right, left]);
  const half = await held(0, 0.5), little = await held(1, 0.25);
  check(`${ship}: less stick is less turn`, half[0] >= 2 && half[0] <= 5 && little[1] <= -1 && little[1] >= -2, [half, little]);
  t = await turn();
  check(`${ship}: and let go it is none again`, t[0] === 0 && t[1] === 0, t);
  // At 70% the stick stops at step 5 where the window has room for it (a
  // window too short for steps 4 and 5, the Rapier's, has only the edge
  // beyond step 3, and full stick goes there).
  await page.$eval("#padSensitivity", (el) => { el.value = "70"; el.dispatchEvent(new Event("input")); el.dispatchEvent(new Event("change")); });
  const r70 = await held(0, 1), u70 = await held(1, 1);
  check(`${ship}: at 70% full stick is step 5 (or the edge in a short window)`, r70[0] === 5 && (u70[1] === -5 || u70[1] === -8), [r70, u70]);
  // The crosshair the game steers by with a mouse is not drawn for a pilot
  // with a controller (the emulator steps over the cockpit's call that draws
  // it), unless the pilot ticks the box; with keyboard and mouse it is.
  const pointerOff = () => page.evaluate(() => window.DOSBox._wc_web_pointer_off(-1));
  const tick = async (on) => { await page.$eval("#padPointer", (el, on) => { el.checked = on; el.dispatchEvent(new Event("change")); }, on); await page.waitForTimeout(200); };
  const off1 = await pointerOff();
  await tick(true);
  const off2 = await pointerOff();
  await tick(false);
  const off3 = await pointerOff();
  await page.selectOption("#padSelect", "none");
  await page.waitForTimeout(200);
  const off4 = await pointerOff();
  check(`${ship}: with a controller the cockpit's crosshair is off, the box brings it back, and keyboard and mouse have it`, off1 === 1 && off2 === 0 && off3 === 1 && off4 === 0, [off1, off2, off3, off4]);
  await context.close();
}
await browser.close();
console.log(results.every(Boolean) ? `${game.toUpperCase()} PAD OK` : `${game.toUpperCase()} PAD FAILED`);
process.exit(results.every(Boolean) ? 0 : 1);
