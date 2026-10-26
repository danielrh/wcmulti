// The browser rvsim's check (host/build.sh WEB=1, deployed at fireflyk64.github.io/rv/): the page loads, the flight
// steps (the status line counts them), the keys reach it, Esc ends it, and the log has no error or broken rule.
//   PLAYWRIGHT_DIR=.../playwright node host/web/check.mjs PAGE_URL [SHOT.png]
// CHROME=/path/to/chrome (default /usr/bin/google-chrome).  Prints "RVSIM WEB OK" when all is well.
import { createRequire } from "node:module";
import path from "node:path";

const [pageUrl, shot] = process.argv.slice(2);
if (!pageUrl) {
  console.error("usage: node check.mjs PAGE_URL [SHOT.png]");
  process.exit(2);
}
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROME || "/usr/bin/google-chrome",
  args: ["--no-sandbox"] });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let fails = 0;
const ok = (c, what) => { console.log((c ? "ok   " : "FAIL ") + what); if (!c) fails++; };
const errors = [];
try {
  const page = await browser.newPage({ viewport: { width: 1100, height: 1100 } });
  page.on("pageerror", (e) => errors.push("PAGE ERROR: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push("console: " + m.text()); });
  await page.goto(pageUrl, { waitUntil: "load" });
  await page.waitForFunction(() => /step \d+/.test(document.getElementById("status").textContent), null,
    { timeout: 30000 });
  const step = async () => +(await page.textContent("#status")).match(/step (\d+)/)[1];
  const s0 = await step();
  await sleep(2000);
  const s1 = await step();
  ok(s1 - s0 >= 40 && s1 - s0 <= 80, `the flight steps at about 30 a second (${s1 - s0} in 2 s)`);
  await page.click("#canvas");
  await page.keyboard.press("p");
  await sleep(500);
  const p0 = await step();
  await sleep(1000);
  ok((await step()) === p0 && /paused/.test(await page.textContent("#status")), "P pauses it (the keys reach it)");
  await page.keyboard.press("p");
  const log = await page.textContent("#log");
  ok(/rvsim: mission \d+/.test(log), "the log has the flight's start");
  if (shot) await page.locator("#canvas").screenshot({ path: shot });
  await page.keyboard.press("Escape");
  await page.waitForSelector("#over:not([hidden])", { timeout: 5000 }).catch(() => {});
  ok(/missionStatus 5/.test(await page.textContent("#over")), "Esc ends the flight");
  const end = await page.textContent("#log");
  ok(/rvsim: \d+ steps, missionStatus 5/.test(end), "the log has the flight's end");
  ok(!/RULE:|runtime error|rv_fail/.test(end), "no broken rule or failure in the log");
  ok(errors.length === 0, "no page errors" + (errors.length ? ": " + errors.join(" | ") : ""));
} finally {
  await browser.close();
}
console.log(fails ? `RVSIM WEB: ${fails} failed` : "RVSIM WEB OK");
process.exit(fails ? 1 : 0);
