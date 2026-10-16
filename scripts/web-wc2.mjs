// Headless test of Wing Commander II in the browser build: two Chrome pages
// meet in a lobbylink room, the host picks a mission in the lobby, both click
// "Fly mission" in the barracks and tap Esc through the briefing, and the
// second player's seat is checked: the wingman's ship, a drone (rides behind
// the leader as copilot: its + key sets the leader's speed; "0", "/chase",
// Enter lets it fly free) or the gunner (it starts in the rear turret; fire:
// the shots must reach the host).  Driven by scripts/web-wc2.sh.
//
//   GAME_FILE=wc2.zip node scripts/web-wc2.mjs PAGE_URL LOBBY_URL [series/mission] [drone|gunner|wingman] [door x,y]
//   GAME=so1 GAME_FILE=wc2.zip node scripts/web-wc2.mjs ... 2/0 drone     # Special Operations 1 (so2: 2), from the same directory
//
// The room's code names the program (WC2-, SO1-, SO2-): the pages are given
// the Wing Commander II directory and pick the program by the code.
//
// The door is where "Fly mission" is in that mission's barracks, as fractions
// of the mouse range: the rooms differ (0.48,0.55 the middle door of the
// blue and the Caernarvon rooms, 0.12,0.5 the airlock of the Concordia's).
// Known good: 9/1 drone 0.12,0.5; 9/2 gunner 0.48,0.55; 1/0 wingman 0.48,0.55;
// GAME=so1 1/1 drone 0.48,0.55; GAME=so2 1/0 drone 0.48,0.55.
import { createRequire } from "node:module";
import path from "node:path";
import fs from "node:fs";
const [pageUrl, lobbyUrl, mission = "9/1", expect = "drone", doorArg = "0.12,0.5"] = process.argv.slice(2);
const door = doorArg.split(",").map(Number);
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
// What differs from program to program: its tag and title, how many missions
// its menu has, and where the words the checks read are (src/cpu/wcnet_ds.def:
// setSpeed, cameraMode, mannedTurret).
const game = { wc2: { tag: "WC2", id: "wc2", title: "Wing Commander II", options: 49, speed: 0x5FBE, camera: 0x9389, turret: 0xCD6A },
               so1: { tag: "SO1", id: "wc2so1", title: "Wing Commander II: Special Operations 1", options: 21, speed: 0x5FC8, camera: 0x923B, turret: 0xCC3A },
               so2: { tag: "SO2", id: "wc2so2", title: "Wing Commander II: Special Operations 2", options: 21, speed: 0x6264, camera: 0x94DB, turret: 0xCED0 },
               // Special Operations 3 is SO2's program with the Culloden campaign's files (so3/README.md): one mission in the menu so far
               so3: { tag: "SO3", id: "wc2so3", title: "Wing Commander II: Special Operations 3 (Culloden)", options: 2, menu: /Series 1, mission 1/, speed: 0x6264, camera: 0x94DB, turret: 0xCED0 } }[process.env.GAME || "wc2"];
const room = game.tag + "-" + Math.random().toString(36).slice(2, 8).toUpperCase();
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROME || "/usr/bin/google-chrome",
  args: ["--no-sandbox", "--autoplay-policy=no-user-gesture-required", "--use-fake-device-for-media-stream"] });
const logs = { host: [], wing: [] };
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
  const hiddenBefore = await page.$eval("#firstnameLabel", (el) => el.hidden);
  await page.setInputFiles("#gamefile", process.env.GAME_FILE);
  await page.waitForFunction(() => /^(Ready|Could not)/.test(document.getElementById("sourceStatus").textContent), null, { timeout: 300000 });
  console.log(`[${name}] ` + (await page.$eval("#sourceStatus", (el) => el.textContent)).slice(0, 200));
  if (name === "host") check("the first-name field appears with Wing Commander II", hiddenBefore && !(await page.$eval("#firstnameLabel", (el) => el.hidden)), hiddenBefore);
  check(`${name}: the room's code picks the program`, (await page.$eval("#program", (el) => el.value)) === game.id && (await page.$eval("#sourceStatus", (el) => el.textContent)).startsWith(`Ready: ${game.title}`),
        await page.$eval("#program", (el) => el.value));
  await page.waitForFunction(() => !document.getElementById("lobby").hidden && document.getElementById("roster").children.length > 0, null, { timeout: 60000 });
  if (before) await before(page);
  return page;
}
const flying = (page) => page.evaluate(() => (window.DOSBox ? window.DOSBox._wc_web_in_flight() : -1));
const has = (name, text) => logs[name].some((l) => l.includes(text));
const shotDir = process.env.SHOT_DIR || ".";
const shot = async (page, name) => { const c = await page.$("#canvas"); await c.screenshot({ path: path.join(shotDir, name + ".png") }); };
// The barracks: click the round door in the middle ("Fly mission"), then tap
// Esc through the briefing until the cockpit.
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
  const options = await page.$eval("#mission", (el) => Array.from(el.options).map((o) => o.textContent));
  check(`the host's mission menu is ${game.title}'s`, options.length === game.options && (game.menu || /Series 2, mission 1/).test(options.join("|")), [options.length, options[1], options[5]]);
  await page.$eval("#mission", (el, v) => { el.value = v; el.dispatchEvent(new Event("change")); }, mission);
});
const wing = await open("wing", "WINGMAN", async (page) => {
  await page.waitForFunction(() => /HOST is in the room/.test(document.getElementById("chatLog").textContent), null, { timeout: 30000 });
  await sleep(1);
  const s = await page.$eval("#mission", (el) => ({ value: el.value, disabled: el.disabled, text: el.options[el.selectedIndex] && el.options[el.selectedIndex].textContent }));
  check("the wingman's lobby shows the host's mission", s.value === mission && s.disabled, s);
  console.log("[wing] hint: " + (await page.$eval("#missionHint", (el) => el.textContent)));
});
await host.waitForFunction(() => !document.getElementById("fly").disabled, null, { timeout: 30000 });
await host.click("#fly");               // starts the wingman's page too
await sleep(10);
check("the wingman's page started with the host's", await wing.evaluate(() => !!window.DOSBox), null);
await shot(host, "host-barracks");
const [hf, wf] = await Promise.all([fly(host, "host"), fly(wing, "wing")]);
check("both reached the cockpit", hf && wf, [hf, wf]);
check("the game has the page's first name and callsign", has("host", `WCFIRSTNAME: the game's "FIRSTNAME" is now "Chris"`) && has("wing", `WCCALLSIGN: the game's "CALLSIGN" is now "WINGMAN"`),
      logs.host.filter((l) => l.includes("the game's")));
await sleep(12);
await shot(host, "host-flight"); await shot(wing, "wing-flight");
if (expect === "drone") {
  check("the host seats the second player as a drone", has("host", "(a drone,"), logs.host.filter((l) => l.includes("mission start state")));
  check("the wingman's game knows it is a drone", has("wing", "(a drone)"), logs.wing.filter((l) => l.includes("we are player")));
  check("the leader's ship exists on the drone's machine", has("wing", "the leader's ship is slot"), null);
  await wing.evaluate(() => document.fullscreenElement ? document.exitFullscreen() : null);
  await wing.click("#canvas");
  // The drone rides behind the leader as copilot: its + key is the leader's
  // cruising speed (dseg:5FBE, an int32 per slot), and its own gauges show
  // the leader's (the same word on the drone's slot 0).
  const speed = (page, slot) => page.evaluate(([slot, at]) => { const M = window.DOSBox; const b = (o) => M._wc_web_ds_byte(o); const o = at + 4 * slot; return b(o) | (b(o + 1) << 8) | (b(o + 2) << 16); }, [slot, game.speed]);
  const before = await speed(host, 0);
  await wing.keyboard.press("Equal"); await sleep(0.5); await wing.keyboard.press("Equal"); await sleep(3);
  const after = await speed(host, 0), mirrored = await speed(wing, 0);
  check("the drone's + key raises the leader's cruising speed on the host", after > before, [before, after]);
  check("the drone's own speed gauge shows the leader's", mirrored === after, [mirrored, after]);
  check("the host says who did it", has("host", "copilot WINGMAN: cruising speed up"), logs.host.filter((l) => l.includes("copilot")).length);
  // the real way out of the ride: 0 opens the comms prompt, the text, Enter
  await wing.keyboard.press("0"); await sleep(0.5);
  await wing.keyboard.type("/chase", { delay: 80 }); await sleep(0.5);
  await shot(wing, "wing-typing");
  await wing.keyboard.press("Enter");
  await sleep(4);
  await shot(wing, "wing-free");
  check("the drone flies free after /chase", has("wing", "drone: flying free"), null);
} else if (expect === "gunner") {
  check("the host seats the second player as the gunner", has("host", "(the gunner,"), logs.host.filter((l) => l.includes("mission start state")));
  check("the wingman's game knows it is the gunner", has("wing", "(the gunner)"), logs.wing.filter((l) => l.includes("we are player")));
  await wing.evaluate(() => document.fullscreenElement ? document.exitFullscreen() : null);
  await wing.click("#canvas");
  // (WC2.EXE: dseg:9389 is 4 while the player sits in a turret, dseg:CD6A the turret: 0 the rear one.)
  const seat = await wing.evaluate(([camera, turret]) => [window.DOSBox._wc_web_ds_byte(camera), window.DOSBox._wc_web_ds_byte(turret)], [game.camera, game.turret]);
  check("the gunner starts in the rear turret without a key", seat[0] === 4 && seat[1] === 0, seat);
  await shot(wing, "wing-turret");
  await wing.keyboard.down("Space"); await sleep(4);
  await shot(wing, "wing-firing"); await shot(host, "host-firing");
  await wing.keyboard.up("Space");
  check("the gunner's turret shots reach the host", has("host", "the gunner fires a turret"), logs.host.filter((l) => l.includes("gunner fires")).length);
} else {
  check("the host seats the second player as the wingman", has("host", "(the wingman,"), logs.host.filter((l) => l.includes("mission start state")));
  check("the wingman took over the host's campaign record", has("wing", "took over the server's campaign record"), null);
}
const frames = (name) => logs[name].reduce((m, l) => { const f = /frame (\d+): own health/.exec(l); return f ? Math.max(m, Number(f[1])) : m; }, 0);
// (The games log a frame count every 300 frames: on a busy machine, with the
// picture in view of a browser that draws by software, that takes a while.)
for (let i = 0; i < 45 && !(frames("host") >= 301 && frames("wing") >= 301); i++) await sleep(2);
console.log(`frames: host ${frames("host")}, wingman ${frames("wing")}`);
check("frames flow on both", frames("host") >= 301 && frames("wing") >= 301, [frames("host"), frames("wing")]);
await shot(host, "host-end"); await shot(wing, "wing-end");
for (const [name, page] of [["host", host], ["wing", wing]]) {
  console.log(`${name} chat log: ` + (await page.$eval("#chatLog", (el) => el.innerText.replace(/\n+/g, " | "))).slice(0, 500));
  fs.writeFileSync(path.join(shotDir, name + ".log"), logs[name].join("\n") + "\n");
}
await browser.close();
console.log(results.every(Boolean) ? `${game.tag} OK` : `${game.tag} FAILED`);
process.exit(results.every(Boolean) ? 0 : 1);
