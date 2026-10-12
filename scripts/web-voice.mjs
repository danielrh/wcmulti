// Headless test of voice between two browser pages: both join a room, one
// opts in (the other's control lights up and says so), the other opts in
// (the voice link connects both ways, with a fake microphone), one opts out
// (the link goes), and Fly with a voice on one side only says so in the chat
// of both.  Driven by scripts/web-smoke.sh voice.
//
//   node scripts/web-voice.mjs PAGE_URL LOBBY_URL             # the server's wc.tar.gz
//   GAME_FILE=wc2.zip node scripts/web-voice.mjs PAGE_URL LOBBY_URL   # the pages' own game files
import { createRequire } from "node:module";
import path from "node:path";
const [pageUrl, lobbyUrl] = process.argv.slice(2);
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROME || "/usr/bin/google-chrome",
  args: ["--no-sandbox", "--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream"] });
const results = [];
const check = (what, ok, detail) => { results.push(ok); console.log(`${ok ? "ok  " : "FAIL"} ${what}: ${JSON.stringify(detail)}`); };
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
const room = "VOICE-" + Math.random().toString(36).slice(2, 8).toUpperCase();
const logs = { host: [], wing: [] };
async function open(name, callsign) {
  const context = await browser.newContext();
  const page = await context.newPage();
  page.on("console", (m) => logs[name].push(m.text()));
  page.on("pageerror", (e) => console.log(`[${name}] PAGE ERROR: ${e.message}`));
  const u = new URL(pageUrl);
  for (const [k, v] of Object.entries({ room, server: lobbyUrl, callsign, "env.MIS": "0", "env.SERIES": "1" })) u.searchParams.set(k, v);
  await page.goto(u.toString());
  if (process.env.GAME_FILE) await page.setInputFiles("#gamefile", process.env.GAME_FILE);
  await page.waitForFunction(() => /^(Ready|Could not)/.test(document.getElementById("sourceStatus").textContent), null, { timeout: 300000 });
  // (The page joins the room on its own, ?room= being in the URL.)
  await page.waitForFunction(() => !document.getElementById("lobby").hidden && document.getElementById("roster").children.length > 0, null, { timeout: 60000 });
  return page;
}
const choose = (page, v) => page.selectOption("#voice", v);
const voiceState = (page) => page.$eval("#voiceState", (el) => el.textContent);
const lit = (page) => page.$eval("#voiceLabel", (el) => el.classList.contains("attention"));
const links = (page) => page.evaluate(() => window.__wcVoiceLinks());
const chat = (page) => page.$eval("#chatLog", (el) => el.innerText);

const host = await open("host", "HOST");
const wing = await open("wing", "WINGMAN");
await wing.waitForFunction(() => /HOST is in the room/.test(document.getElementById("chatLog").textContent), null, { timeout: 30000 });
await host.waitForFunction(() => /WINGMAN is in the room/.test(document.getElementById("chatLog").textContent), null, { timeout: 30000 });
check("both start with voice off", (await host.$eval("#voice", (el) => el.value)) === "off" && (await wing.$eval("#voice", (el) => el.value)) === "off", null);

await choose(host, "on");
await sleep(2);
check("the other's control lights up when one opts in", await lit(wing), await voiceState(wing));
check("and says who", /HOST opted in/.test(await voiceState(wing)), await voiceState(wing));
check("the one who opted in waits", /no voice until WINGMAN/.test(await voiceState(host)), await voiceState(host));
check("no voice link yet", Object.keys(await links(host)).length === 0 && Object.keys(await links(wing)).length === 0, null);

await choose(wing, "ptt");
await host.waitForFunction(() => { const l = window.__wcVoiceLinks(); return Object.values(l).some((x) => x.state === "connected"); }, null, { timeout: 30000 }).catch(() => {});
await sleep(2);
const lh = await links(host), lw = await links(wing);
check("voice connects both ways once everybody has opted in", lh[1] && lh[1].state === "connected" && lw[0] && lw[0].state === "connected", [lh, lw]);
check("each has a remote audio track", lh[1] && lh[1].hearing && lw[0] && lw[0].hearing, null);
check("the states say so", /voice connected with WINGMAN/.test(await voiceState(host)) && /voice connected with HOST/.test(await voiceState(wing)) && /hold `/.test(await voiceState(wing)), [await voiceState(host), await voiceState(wing)]);
check("the lights are out", !(await lit(host)) && !(await lit(wing)), null);
check("the choice is remembered", (await wing.evaluate(() => localStorage.getItem("wc:voice"))) === "ptt", null);

// Exercise the previously silent PTT -> Always path, and check decoded audio
// rather than accepting the existence of a remote track as proof of sound.
await choose(wing, "on");
let reports;
const deadline = Date.now() + 15000;
do {
  reports = await Promise.all([host, wing].map((p) => p.evaluate(() => window.__wcVoiceReport())));
  if (reports.every((r) => r.peers.some((p) => p.receivedEnergy > 0 && p.playback === "playing"))) break;
  await sleep(0.2);
} while (Date.now() < deadline);
check("non-silent voice arrives and playback starts both ways", reports.every((r) => r.peers.some((p) => p.receivedEnergy > 0 && p.playback === "playing")), reports);

await choose(host, "off");
await sleep(2);
check("one player off ends the voice for both", Object.keys(await links(host)).length === 0 && Object.keys(await links(wing)).length === 0, [await links(host), await links(wing)]);
check("the one still in is told", /no voice until HOST/.test(await voiceState(wing)), await voiceState(wing));
check("and the one who went off sees the light", await lit(host), await voiceState(host));

// Fly with a voice on one side only: both chats say so.
await host.waitForFunction(() => !document.getElementById("fly").disabled, null, { timeout: 30000 });
await host.click("#fly");
await sleep(6);
check("Fly warns the one who is off", /WINGMAN opted into voice and you are off: no voice this session/.test(await chat(host)), (await chat(host)).slice(-200));
check("Fly warns the one who opted in", /No voice this session: HOST did not opt in/.test(await chat(wing)), (await chat(wing)).slice(-200));
// Opting in while the game runs: voice comes up beside it.
await wing.waitForFunction(() => !!window.DOSBox, null, { timeout: 60000 }).catch(() => {});
await choose(host, "on");
await host.waitForFunction(() => Object.values(window.__wcVoiceLinks()).some((x) => x.state === "connected"), null, { timeout: 30000 }).catch(() => {});
await sleep(1);
const lh2 = await links(host), lw2 = await links(wing);
check("voice comes up while both games run", lh2[1] && lh2[1].state === "connected" && lw2[0] && lw2[0].state === "connected" && (await host.evaluate(() => !!window.DOSBox)), [lh2, lw2]);
await browser.close();
console.log(results.every(Boolean) ? "VOICE OK" : "VOICE FAILED");
process.exit(results.every(Boolean) ? 0 : 1);
