// Headless test of Wing Commander II's introduction in the browser build:
// one Chrome page starts the game in its campaign (no mission picked),
// clicks "Start New Game" and watches the Emperor's audience.  The scene is
// the game's first digitised speech, and it stood still for ever there when
// the emulated Sound Blaster was not on the IRQ the game's setup names: the
// picture must keep changing well past the first line.  Driven by
// scripts/web-wc2.sh intro.
//
//   GAME_FILE=wc2.zip node scripts/web-wc2-intro.mjs PAGE_URL LOBBY_URL
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";
import crypto from "node:crypto";
const [pageUrl, lobbyUrl] = process.argv.slice(2);
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROME || "/usr/bin/google-chrome",
  args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required", "--use-fake-device-for-media-stream"] });
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
const shotDir = process.env.SHOT_DIR || ".";
const logs = [];
const page = await browser.newPage();
page.on("console", (m) => logs.push(m.text()));
page.on("pageerror", (e) => console.log(`PAGE ERROR: ${e.message}`));
const u = new URL(pageUrl);
// (An empty WCROOM: the game runs without a session, so nobody is waited for.)
for (const [k, v] of Object.entries({ room: "INTRO-" + Math.random().toString(36).slice(2, 8).toUpperCase(), server: lobbyUrl, callsign: "HOST",
                                      "env.WCNET_LOG": "2", "env.WCROOM": "" })) u.searchParams.set(k, v);
await page.goto(u.toString());
await page.setInputFiles("#gamefile", process.env.GAME_FILE);
await page.waitForFunction(() => /^(ready|error)$/.test(document.getElementById("sourceStatus").dataset.state || ""), null, { timeout: 300000 });
await page.waitForFunction(() => !document.getElementById("lobby").hidden && !document.getElementById("fly").disabled, null, { timeout: 60000 });
await page.$eval("#mission", (el) => { el.value = ""; el.dispatchEvent(new Event("change")); });
await page.click("#fly");
await page.waitForFunction(() => !!window.DOSBox, null, { timeout: 60000 });
const picture = async (name) => {
  const c = await page.$("#canvas");
  const png = await c.screenshot({ path: path.join(shotDir, name + ".png") });
  return crypto.createHash("sha1").update(png).digest("hex");
};
// The title menu comes after the logos; "Start New Game" is its upper button.
await sleep(45);
await picture("intro-menu");
for (let i = 0; i < 3; i++) {
  await page.evaluate(() => { const M = window.DOSBox; M._wc_web_pointer(0.5, 0.64); setTimeout(() => M._wc_web_mouse_button(0, 1), 400); setTimeout(() => M._wc_web_mouse_button(0, 0), 700); });
  await sleep(2);
}
// The audience begins some 40 s after the click; a picture every 6 s from
// then on.  Stuck, they are all the Emperor's silhouette.
await sleep(34);
const seen = [];
for (let t = 0; t < 10; t++) { seen.push(await picture("intro-" + String(t).padStart(2, "0"))); await sleep(6); }
const distinct = new Set(seen).size;
console.log(`${distinct} different pictures out of ${seen.length} over the audience`);
fs.writeFileSync(path.join(shotDir, "intro.log"), logs.join("\n") + "\n");
await browser.close();
const ok = distinct >= 5;
console.log(ok ? "WC2 INTRO OK" : "WC2 INTRO FAILED");
process.exit(ok ? 0 : 1);
