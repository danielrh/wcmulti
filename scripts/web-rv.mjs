// RetroVega in the browser build (headless Chrome pages, a local lobby
// server), as scripts/web-smoke.sh rv runs it:
//
//   1. a page with no game files at all (a fresh browser, a site with no
//      wc.tar.gz) has RetroVega in its "Play" menu, enters the public lobby
//      by itself, joins a room and flies alone: the game starts, finds the
//      OPL3, and the keyboard reaches it (Tab jumps between hotspots, a held
//      arrow key steers the pointer);
//   2. a host and a wingman meet in an RV- room by its link; the host's Fly
//      starts both games.
//
//   node scripts/web-rv.mjs PAGE_URL LOBBY_URL [seconds]
//
// PAGE_URL must serve a page without wc.tar.gz beside it (web-smoke.sh rv
// serves a copy of web/dist without it).  Playwright comes from
// PLAYWRIGHT_DIR; SHOT_DIR gets screenshots, the page logs and each game's
// RVLOG.TXT.
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";

const [pageUrl, lobbyUrl, secondsArg = "30"] = process.argv.slice(2);
if (!pageUrl || !lobbyUrl) {
  console.error("usage: node web-rv.mjs PAGE_URL LOBBY_URL [seconds]");
  process.exit(2);
}
const seconds = Number(secondsArg);
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
const chrome = process.env.CHROME || "/usr/bin/google-chrome";
const shotDir = process.env.SHOT_DIR || "";
const tag = Math.random().toString(36).slice(2, 6).toUpperCase();
const hallCode = "RVHALL-" + tag;
const browser = await chromium.launch({ headless: true, executablePath: chrome,
  args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required"] });
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const checks = [];
const check = (what, ok, detail = "") => { checks.push({ what, ok }); console.log(`${ok ? "ok  " : "FAIL"} ${what}${detail ? ": " + detail : ""}`); };
const logs = {};

async function open(name, query) {
  // (each page its own browser context: nothing saved from another page)
  const context = await browser.newContext();
  const page = await context.newPage();
  logs[name] = [];
  page.on("console", (m) => logs[name].push(m.text()));
  page.on("pageerror", (e) => { logs[name].push("PAGE ERROR: " + e.message); console.log(`[${name}] PAGE ERROR: ${e.message}`); });
  const u = new URL(pageUrl);
  u.searchParams.set("server", lobbyUrl);
  u.searchParams.set("hall", hallCode);
  for (const [k, v] of Object.entries(query)) u.searchParams.set(k, v);
  await page.goto(u.toString());
  await page.waitForFunction(() => document.getElementById("sourceStatus").dataset.state === "ready", null, { timeout: 60000 });
  return page;
}
const rvlog = (page) => page.evaluate(() => {
  try { return new TextDecoder().decode(window.DOSBox.FS.readFile("/game/RVLOG.TXT")); } catch (e) { return ""; }
});
const waitLog = async (page, re, ms) => {
  const until = Date.now() + ms;
  for (;;) {
    const t = await rvlog(page);
    if (re.test(t)) return t;
    if (Date.now() > until) return t;
    await sleep(500);
  }
};
async function keep(name, page) {
  if (!shotDir) return;
  await page.screenshot({ path: path.join(shotDir, `rv-${name}.png`) });
  fs.writeFileSync(path.join(shotDir, `rv-${name}.log`), logs[name].join("\n") + "\n");
  fs.writeFileSync(path.join(shotDir, `rv-${name}-RVLOG.TXT`), await rvlog(page));
}

// -- 1. no files: RetroVega, the lobby, a flight alone ---------------------------
console.log(`lobby ${hallCode} at ${lobbyUrl}`);
const solo = await open("solo", { callsign: "SOLO" });
check("with no files the page plays RetroVega", (await solo.$eval("#program", (el) => el.value)) === "rv",
  await solo.$eval("#program", (el) => [...el.options].map((o) => o.textContent).join(", ")));
await solo.waitForFunction(() => window.__wcHall && window.__wcHall.inside(), null, { timeout: 30000 }).catch(() => {});
check("and is in the public lobby at once", await solo.evaluate(() => !!(window.__wcHall && window.__wcHall.inside())));
const code = await solo.$eval("#room", (el) => el.value);
check("its room code is an RV- code", /^RV-\d{4}$/.test(code), code);
await solo.click("#join");
await solo.waitForFunction(() => !document.getElementById("lobby").hidden && !document.getElementById("fly").disabled, null, { timeout: 30000 });
await solo.click("#fly");
let t = await waitLog(solo, /input: mouse/, 90000);
check("the game started (fetched from the site)", logs.solo.some((l) => /fetched \d+ files of RetroVega/.test(l)) && /rvHello: DS is ready/.test(t));
check("it plays its music on the OPL3", /music: OPL3 at 388h, our bank/.test(t), (t.match(/music: [^\n]*/) || [""])[0]);
check("the hooks know RV.EXE", logs.solo.some((l) => /RetroVega loaded at/.test(l)));
await sleep(4000);
await solo.focus("#canvas");
await solo.keyboard.press("Enter");                     // the welcome message
await sleep(1500);
await solo.keyboard.press("Tab");                       // the pointer on a hotspot (said in RVLOG.TXT)
await sleep(1500);
await solo.keyboard.down("ArrowLeft");                  // then steered left (said when the key goes up)
await sleep(700);
await solo.keyboard.up("ArrowLeft");
await sleep(1500);
t = await rvlog(solo);
const tab = /tab: to (\d+),(\d+)/.exec(t), steered = /keys: steered to (\d+),(\d+)/.exec(t);
check("Tab puts the pointer on a hotspot", !!tab, tab ? tab[0] : "");
check("a held arrow key steers it", !!(tab && steered && Number(steered[1]) < Number(tab[1]) - 20 && steered[2] === tab[2]),
  steered ? steered[0] : "no steering in the log");
await sleep(Math.max(0, seconds * 1000 - 10000));
t = await rvlog(solo);
check("no stop in the game", !/FAIL /.test(t));
await keep("solo", solo);

// -- 2. a host and a wingman ----------------------------------------------------
const room = "RV-" + String(Math.floor(Math.random() * 9000) + 1000);
const host = await open("host", { room, callsign: "HOST" });
await host.waitForFunction(() => !document.getElementById("lobby").hidden && !document.getElementById("fly").disabled, null, { timeout: 30000 });
const wing = await open("wing", { room, callsign: "WINGMAN" });
await wing.waitForFunction(() => !document.getElementById("lobby").hidden && document.getElementById("roster").children.length > 1, null, { timeout: 30000 });
await host.waitForFunction(() => document.getElementById("roster").children.length > 1, null, { timeout: 30000 });
check("both pages play RetroVega in room " + room, (await host.$eval("#program", (el) => el.value)) === "rv" && (await wing.$eval("#program", (el) => el.value)) === "rv");
await host.click("#fly");
const [th, tw] = await Promise.all([waitLog(host, /input: mouse/, 90000), waitLog(wing, /input: mouse/, 90000)]);
check("the host's Fly started the host's game", /rvHello: DS is ready/.test(th) && /music: OPL3/.test(th));
check("and the wingman's", /rvHello: DS is ready/.test(tw) && /music: OPL3/.test(tw));
check("the wingman's page saw the host start", logs.wing.some((l) => /fetched \d+ files of RetroVega/.test(l)));
await sleep(Math.max(0, seconds * 1000 - 10000));
check("no stop in either game", !/FAIL /.test(await rvlog(host)) && !/FAIL /.test(await rvlog(wing)));
await keep("host", host);
await keep("wing", wing);

await browser.close();
const ok = checks.every((c) => c.ok);
console.log(ok ? `RV OK (${checks.length} checks)` : `RV FAILED (${checks.filter((c) => !c.ok).length} of ${checks.length} checks)`);
process.exit(ok ? 0 : 1);
