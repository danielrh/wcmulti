// Headless test of a three-seat mission in the browser build: three Chrome
// pages meet in a lobbylink room made for three, the host picks a mission in
// the lobby, all three click "Fly mission" in the barracks and tap Esc
// through the briefing, and the seats are checked: both wingmen fly fighters
// of the wing (the host's log says "the wingman" for players 1 and 3), every
// game knows the three humans, frames flow on all three.  Driven by
// scripts/web-wc2.sh trio.
//
//   GAME=so3 GAME_FILE=wc2.zip node scripts/web-trio.mjs PAGE_URL LOBBY_URL [series/mission] [door x,y]
//   GAME=wc1 GAME_FILE=setup_wing_commander.exe node scripts/web-trio.mjs PAGE_URL LOBBY_URL 1/0
//
// The default is SO3's first mission (Shakedown: Maniac's Hellcat and a
// second one the host's game makes), the door at 0.48,0.55 (the Culloden's
// barracks).
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";
const [pageUrl, lobbyUrl, mission = "1/0", doorArg = "0.48,0.55"] = process.argv.slice(2);
const door = doorArg.split(",").map(Number);
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
// GAME=wc1 flies Wing Commander (GAME_FILE: the GOG installer or a zip of the
// folder), GAME=so3 the Culloden campaign, GAME=wc2 Wing Commander II; a
// third pilot in the room makes the host's game spawn a second wingman (or
// seats a second gunner).
const game = { so3: { tag: "SO3", id: "wc2so3", title: "Wing Commander II: Special Operations 3 (Culloden)", options: 21, menu: /Ch 1, mission 1/ },
               wc2: { tag: "WC2", id: "wc2", title: "Wing Commander II", options: 49, menu: /Ch 2, mission 1/ },
               wc1: { tag: "WC1", id: "wc1", title: "Wing Commander", menu: /Enyo/ } }[process.env.GAME || "so3"];
const room = game.tag + "-" + Math.random().toString(36).slice(2, 8).toUpperCase();
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROME || "/usr/bin/google-chrome",
  args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required", "--use-fake-device-for-media-stream"] });
const names = ["host", "wing", "third"];
const logs = { host: [], wing: [], third: [] };
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
const results = [];
const check = (what, ok, detail) => { results.push(ok); console.log(`${ok ? "ok  " : "FAIL"} ${what}: ${JSON.stringify(detail)}`); };
async function open(name, callsign, before) {
  const page = await browser.newPage();
  page.on("console", (m) => logs[name].push(m.text()));
  page.on("pageerror", (e) => console.log(`[${name}] PAGE ERROR: ${e.message}`));
  const u = new URL(pageUrl);
  for (const [k, v] of Object.entries({ room, server: lobbyUrl, callsign, firstname: "Chris", "env.WCNET_LOG": "2" })) u.searchParams.set(k, v);
  await page.goto(u.toString());
  await page.check("#threeSeats");      // (whoever makes the room makes it for three)
  await page.setInputFiles("#gamefile", process.env.GAME_FILE);
  await page.waitForFunction(() => /^(ready|error)$/.test(document.getElementById("sourceStatus").dataset.state || ""), null, { timeout: 300000 });
  console.log(`[${name}] ` + (await page.$eval("#sourceStatus", (el) => `${el.dataset.state}: ${el.textContent || "plays " + document.getElementById("program").value}`)).slice(0, 200));
  check(`${name}: the room's code picks the program`, (await page.$eval("#program", (el) => el.value)) === game.id, await page.$eval("#program", (el) => el.value));
  // (the mission menu is built on joining the room: wait for the roster first)
  try {
    await page.waitForFunction(() => !document.getElementById("lobby").hidden && document.getElementById("roster").children.length > 0, null, { timeout: 60000 });
  } catch (e) {
    console.log(`[${name}] never joined the room: status "${await page.$eval("#status", (el) => el.textContent)}"; chat: ${(await page.$eval("#chatLog", (el) => el.innerText.replace(/\n+/g, " | "))).slice(0, 300)}`);
    throw e;
  }
  if (before) await before(page);
  return page;
}
const flying = (page) => page.evaluate(() => (window.DOSBox ? window.DOSBox._wc_web_in_flight() : -1));
const has = (name, text) => logs[name].some((l) => l.includes(text));
const shotDir = process.env.SHOT_DIR || ".";
const shot = async (page, name) => { const c = await page.$("#canvas"); await c.screenshot({ path: path.join(shotDir, name + ".png") }); };
async function fly(page, name) {
  for (let i = 0; i < 90; i++) {
    if ((await flying(page)) === 1) return true;
    if (has(name, "lost the server") || has(name, "Sorry, an error")) return false;
    await page.evaluate(([i, door]) => { const M = window.DOSBox; if (!M) return;
      if (i % 4 === 0) { M._wc_web_pointer(door[0], door[1]); setTimeout(() => M._wc_web_mouse_button(0, 1), 400); setTimeout(() => M._wc_web_mouse_button(0, 0), 700); }
      else M._wc_web_tap_escape(); }, [i, door]);
    await sleep(2);
  }
  return false;
}

const host = await open("host", "HOST", async (page) => {
  // (the address lost the room code on joining)
  check("the address keeps no room code", !new URL(page.url()).searchParams.get("room"), page.url());
  const options = await page.$eval("#mission", (el) => Array.from(el.options).map((o) => o.textContent));
  check(`the host's mission menu is ${game.title}'s`, (!game.options || options.length === game.options) && game.menu.test(options.join("|")), [options.length, options[1]]);
  await page.$eval("#mission", (el, v) => { el.value = v; el.dispatchEvent(new Event("change")); }, mission);
});
const joiner = (who) => async (page) => {
  await page.waitForFunction(() => /HOST is in the room/.test(document.getElementById("chatLog").textContent), null, { timeout: 30000 });
  await sleep(1);
  const s = await page.$eval("#mission", (el) => ({ value: el.value, disabled: el.disabled }));
  check(`${who}'s lobby shows the host's mission`, s.value === mission && s.disabled, s);
};
const wing = await open("wing", "WINGMAN", joiner("the wingman"));
const third = await open("third", "THIRD", joiner("the third"));
await host.waitForFunction(() => document.getElementById("roster").children.length === 3, null, { timeout: 30000 });
check("the room has three seats", await host.$eval("#roster", (el) => el.children.length) === 3, await host.$eval("#roster", (el) => el.innerText.replace(/\n+/g, " | ")));
await host.waitForFunction(() => !document.getElementById("fly").disabled, null, { timeout: 30000 });
await host.click("#fly");               // starts the wingmen's pages too
await sleep(10);
check("the wingmen's pages started with the host's", (await wing.evaluate(() => !!window.DOSBox)) && (await third.evaluate(() => !!window.DOSBox)), null);
const [hf, wf, tf] = await Promise.all([fly(host, "host"), fly(wing, "wing"), fly(third, "third")]);
check("all three reached the cockpit", hf && wf && tf, [hf, wf, tf]);
await sleep(12);
await shot(host, "host-flight"); await shot(wing, "wing-flight"); await shot(third, "third-flight");
const starts = logs.host.filter((l) => l.includes("mission start state"));
check("the host seats both as wingmen", starts.filter((l) => l.includes("(the wingman,")).length === 2, starts);
check("the host's game made a seat for the third", has("host", "is spawned again") && has("host", "the seat made for player"), logs.host.filter((l) => l.includes("a seat for player") || l.includes("seat made for")));
check("every game knows the three humans", ["wing", "third"].every((n) => logs[n].some((l) => /humans fly net 0 \d \d/.test(l))),
      ["wing", "third"].map((n) => logs[n].filter((l) => l.includes("humans fly")).slice(-1)[0]));
check("each wingman flies a ship of its own", has("wing", "is us (slot 0)") && has("third", "is us (slot 0)"),
      ["wing", "third"].map((n) => logs[n].filter((l) => l.includes("is us (slot 0)"))[0]));
// the third fires: the host hears it as net 3's (or whichever slot it took), the wingman replays it
await third.evaluate(() => document.fullscreenElement ? document.exitFullscreen() : null);
await third.click("#canvas");
// (a held key: a tap between two frames of a game that waits for the host's
// frames is missed, as the gunner test found)
await third.keyboard.down("Space"); await sleep(3); await third.keyboard.up("Space");
await sleep(4);
const thirdNet = (() => { const m = /we are player (\d+)/.exec(logs.third.find((l) => l.includes("we are player")) || ""); return m ? m[1] : "?"; })();
check("the third's shots reach the host", logs.host.some((l) => l.includes(`local fire: ship ${thirdNet} (net ${thirdNet})`)),
      [thirdNet, logs.host.filter((l) => l.includes("local fire")).slice(-3)]);
const frames = (name) => logs[name].reduce((m, l) => { const f = /frame (\d+): own health/.exec(l); return f ? Math.max(m, Number(f[1])) : m; }, 0);
for (let i = 0; i < 45 && !(frames("host") >= 301 && frames("wing") >= 301 && frames("third") >= 301); i++) await sleep(2);
console.log(`frames: host ${frames("host")}, wingman ${frames("wing")}, third ${frames("third")}`);
check("frames flow on all three", frames("host") >= 301 && frames("wing") >= 301 && frames("third") >= 301, [frames("host"), frames("wing"), frames("third")]);
check("no drift on the host's copies", !logs.host.some((l) => /copy turned up to \d+ deg.*ai RUNNING/.test(l)), logs.host.filter((l) => l.includes("copy turned")).slice(-2));
await shot(host, "host-end"); await shot(wing, "wing-end"); await shot(third, "third-end");
for (const [name, page] of [["host", host], ["wing", wing], ["third", third]]) {
  console.log(`${name} chat log: ` + (await page.$eval("#chatLog", (el) => el.innerText.replace(/\n+/g, " | "))).slice(0, 400));
  fs.writeFileSync(path.join(shotDir, name + ".log"), logs[name].join("\n") + "\n");
}
await browser.close();
console.log(results.every(Boolean) ? `${game.tag} TRIO OK` : `${game.tag} TRIO FAILED`);
process.exit(results.every(Boolean) ? 0 : 1);
