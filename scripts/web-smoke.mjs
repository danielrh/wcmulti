// Headless smoke test for the browser build: two Chrome pages meet in a
// lobbylink room, the first hosts, the second joins as the wingman, both
// jump straight into a campaign mission and press through the briefing.
// Driven by scripts/web-smoke.sh, which starts the lobby server and the
// web server this expects.
//
//   node scripts/web-smoke.mjs PAGE_URL LOBBY_URL [seconds] [mission] [series]
//
// Playwright is resolved from PLAYWRIGHT_DIR (a directory with
// node_modules/playwright) and drives the system Google Chrome
// (CHROME=/path/to/chrome).
import { createRequire } from "node:module";
import path from "node:path";

const [pageUrl, lobbyUrl, secondsArg = "90", mission = "1", series = "1"] = process.argv.slice(2);
if (!pageUrl || !lobbyUrl) {
  console.error("usage: node web-smoke.mjs PAGE_URL LOBBY_URL [seconds] [mission] [series]");
  process.exit(2);
}
const seconds = Number(secondsArg);
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
const chrome = process.env.CHROME || "/usr/bin/google-chrome";
// GAME_SOURCE=server (default: the page picks this server's wc.tar.gz),
// zip (GAME_FILE=path to a .zip of the game folder) or gog (GAME_FILE=the
// GOG installer .exe, unpacked in the browser by innoextract).
const gameSource = process.env.GAME_SOURCE || "server";
const gameFile = process.env.GAME_FILE;
if (gameSource !== "server" && !gameFile) { console.error("GAME_FILE is required for GAME_SOURCE=" + gameSource); process.exit(2); }
// ROOM fixes the room code and NO_HOST=1 skips the browser host, for the
// cross-play variant where a native DOSBox hosts (scripts/web-smoke.sh NATIVE_HOST=1).
const room = process.env.ROOM || "SMOKE-" + Math.random().toString(36).slice(2, 8).toUpperCase();
const noHost = process.env.NO_HOST === "1";

const browser = await chromium.launch({ headless: true, executablePath: chrome,
  args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required", "--use-fake-device-for-media-stream"] });
const logs = { host: [], wing: [] };
const seen = { host: new Set(), wing: new Set() };
const milestones = [
  "joined room", "a room of", "connected as player", "connected to ", "mission start", "frame 301", "autopilot",
  "installed", "waiting for", "lost the server", "could not", "failed", "Aborted", "RuntimeError", "unreachable",
];

async function open(name, callsign) {
  const page = await browser.newPage();
  page.on("console", (m) => {
    const t = m.text();
    logs[name].push(t);
    for (const k of milestones) if (t.includes(k) && !seen[name].has(k)) { seen[name].add(k); console.log(`[${name}] ${t.slice(0, 160)}`); }
  });
  page.on("pageerror", (e) => console.log(`[${name}] PAGE ERROR: ${e.message}`));
  const u = new URL(pageUrl);
  u.searchParams.set("room", room);
  u.searchParams.set("server", lobbyUrl);
  u.searchParams.set("callsign", callsign);
  u.searchParams.set("env.MIS", mission);
  u.searchParams.set("env.SERIES", series);
  u.searchParams.set("env.WCNET_AUTOKEYS", "1");
  u.searchParams.set("env.WCNET_LOG", "2");
  await page.goto(u.toString());
  if (gameSource === "server") {
    await page.waitForFunction(() => document.getElementById("sourceStatus").dataset.state === "ready", null, { timeout: 60000 });
  } else {
    await page.setInputFiles("#gamefile", gameFile);
    await page.waitForFunction(() => /^(ready|error)$/.test(document.getElementById("sourceStatus").dataset.state || ""), null, { timeout: 300000 });
  }
  const src = await page.$eval("#sourceStatus", (el) => `${el.dataset.state || "?"}: ${el.textContent || "plays " + document.getElementById("program").value}`);
  console.log(`[${name}] ${src.slice(0, 160)}`);
  if (!src.startsWith("ready")) throw new Error("game files not ready: " + src);
  // The page joins the room on its own (?room= in the URL); wait for the
  // roster, say hello in the lobby chat, then fly.
  await page.waitForFunction(() => !document.getElementById("lobby").hidden && document.getElementById("roster").children.length > 0, null, { timeout: 60000 });
  await page.fill("#chatInput", `hello from ${callsign}`);
  await page.press("#chatInput", "Enter");
  await page.waitForFunction(() => !document.getElementById("fly").disabled, null, { timeout: 30000 });
  await page.click("#fly");
  return page;
}

console.log(`room ${room}, ${seconds}s${noHost ? ", native host" : ""}`);
let host = null;
if (!noHost) {
  host = await open("host", "HOST");
  await host.waitForFunction(() => /Room .*: joining|Share this page|gives friends the way in/.test(document.getElementById("status").textContent), null, { timeout: 120000 });
  // Give the host time to reach the barracks/mission start before the wingman joins.
  await new Promise((r) => setTimeout(r, 8000));
}
const wing = await open("wing", "WINGMAN");
await new Promise((r) => setTimeout(r, seconds * 1000));

const shot = process.env.SHOT_DIR;
if (shot) {
  const fs = await import("node:fs");
  if (host) await host.screenshot({ path: path.join(shot, "host.png") });
  await wing.screenshot({ path: path.join(shot, "wing.png") });
  for (const name of ["host", "wing"]) fs.writeFileSync(path.join(shot, name + ".log"), logs[name].join("\n") + "\n");
}
for (const name of host ? ["host", "wing"] : ["wing"]) {
  console.log(`===== ${name}: last 40 log lines =====`);
  for (const l of logs[name].slice(-40)) console.log(l.slice(0, 200));
}
// Rough throughput: the client logs its health every 300 frames.
const lastFrame = (lines) => lines.reduce((m, l) => { const f = /frame (\d+): own health/.exec(l); return f ? Math.max(m, Number(f[1])) : m; }, 0);
console.log(`frames: host reached ${lastFrame(logs.host)}, wingman reached ${lastFrame(logs.wing)} (logged every 300)`);
// The host is already flying (full screen, page chat out of reach) when the
// wingman arrives, so the lobby check is: the host's page still received the
// wingman's chat line, and the wingman saw the host's presence.
const chatText = async (page) => page ? page.$eval("#chatLog", (el) => el.textContent) : "";
const chatOk = noHost || ((await chatText(host)).includes("hello from WINGMAN") && /HOST is in the room/.test(await chatText(wing)));
console.log(`lobby chat and presence delivered: ${chatOk}`);
for (const [name, page] of [["host", host], ["wing", wing]]) {
  if (page) console.log(`${name} chat log: ` + (await page.$eval("#chatLog", (el) => el.innerText.replace(/\n+/g, " | "))).slice(0, 400));
}
await browser.close();
const ok = (noHost || seen.host.has("connected as player")) && seen.wing.has("connected to ") && (noHost || chatOk);
console.log(ok ? (noHost ? "SMOKE OK: the browser wingman connected to the native host" : "SMOKE OK: host accepted the wingman and the wingman connected") : "SMOKE FAILED");
process.exit(ok ? 0 : 1);
