// game.js: the page of RetroVega's native build (host/game.sh WEB=1: rv.js, rv.wasm): the game's files from
// game.tar.gz into its file system, its saves kept in the browser (IndexedDB), the ships' sets made as a flight
// asks for them (web/rvsets.js, as the DOSBox page does), the touch controls (web/touch.js) and full screen.
import { createSetProvider, readTarGz } from "./rvsets.js";
import { initTouch } from "./touch.js";

const $ = (id) => document.getElementById(id);
const ROOT = "/game", SAVE = "/save";
const SAVES = /^(BUNK\d+\.SAV|RVSETUP\.CFG)$/;
const lines = [];

function log(line) {
  lines.push(line);
  if (lines.length > 500) lines.shift();
  $("log").textContent = lines.join("\n");
}
function status(text) { $("status").textContent = text; }

// gamepad.js's KEY names (what touch.js sends) as the PC keyboard's scan codes (set 1; 0x100 | code: a grey key)
const PC = (() => {
  const k = {};
  "1234567890".split("").forEach((c, i) => { k[c] = 0x02 + i; });
  const rows = [["qwertyuiop", 0x10], ["asdfghjkl", 0x1E], ["zxcvbnm", 0x2C]];
  for (const [row, at] of rows) row.split("").forEach((c, i) => { k[c] = at + i; });
  for (let i = 1; i <= 10; i++) k["f" + i] = 0x3A + i;
  Object.assign(k, { f11: 0x57, f12: 0x58, esc: 0x01, tab: 0x0F, backspace: 0x0E, enter: 0x1C, space: 0x39,
    leftalt: 0x38, rightalt: 0x138, leftctrl: 0x1D, rightctrl: 0x11D, leftshift: 0x2A, rightshift: 0x36,
    capslock: 0x3A, grave: 0x29, minus: 0x0C, equals: 0x0D, backslash: 0x2B, leftbracket: 0x1A, rightbracket: 0x1B,
    semicolon: 0x27, quote: 0x28, period: 0x34, comma: 0x33, slash: 0x35, insert: 0x152, home: 0x147,
    pageup: 0x149, delete: 0x153, end: 0x14F, pagedown: 0x151, left: 0x14B, up: 0x148, down: 0x150, right: 0x14D });
  return k;
})();

let M = null, running = false;
const sets = createSetProvider({ base: "art/", root: ROOT, status, log });
const touch = initTouch({ log, send: (name, down) => {
  if (!running || PC[name] === undefined) return false;
  M._rv_page_key(PC[name], down ? 1 : 0);
  return true;
} });

// the saves: the game writes them among its files; the page keeps a copy in IndexedDB (SAVE) after each
// (gameSaved: play.c's Module.rvSaved) and when the page is hidden, and puts them back before the game starts
function syncfs(populate) {
  return new Promise((resolve) => M.FS.syncfs(populate, (err) => { if (err) log(`saves: ${err}`); resolve(); }));
}
async function keepSaves() {
  if (!M) return;
  for (const f of M.FS.readdir(ROOT))
    if (SAVES.test(f)) M.FS.writeFile(`${SAVE}/${f}`, M.FS.readFile(`${ROOT}/${f}`));
  await syncfs(false);
}

async function prepare() {
  try {
    status("Fetching the game's files…");
    const t0 = performance.now();
    const [files, mod] = await Promise.all([
      readTarGz("game.tar.gz"),
      RetroVega({ canvas: $("canvas"), print: log, printErr: log,
                  onExit: (code) => { running = false; touch.show(false); status(`The game has ended (${code}): reload the page to fly again.`); } }),
    ]);
    M = mod;
    M.FS.mkdir(ROOT);
    for (const f of files) M.FS.writeFile(`${ROOT}/${f.path}`, f.data);
    M.FS.mkdir(SAVE);
    M.FS.mount(M.FS.filesystems.IDBFS, {}, SAVE);
    await syncfs(true);
    let kept = 0;
    for (const f of M.FS.readdir(SAVE))
      if (SAVES.test(f)) { M.FS.writeFile(`${ROOT}/${f}`, M.FS.readFile(`${SAVE}/${f}`)); kept++; }
    M.rvSaved = () => { keepSaves(); };
    log(`the game's files: ${files.length} in ${Math.round(performance.now() - t0)} ms; ${kept} saved file(s) from this browser`);
    status("Ready.");
    $("fly").textContent = "Fly";
    $("fly").disabled = false;
  } catch (e) {
    status(`Could not load the game: ${e.message}`);
    log(String(e && e.stack || e));
  }
}

function fly() {
  if (running || !M) return;
  running = true;
  $("cover").hidden = true;
  $("sound").disabled = false;
  $("full").disabled = !$("screen").requestFullscreen;
  $("full").hidden = !$("screen").requestFullscreen;   // (an iPhone's page cannot go full screen)
  status("");
  touch.show(true);
  sets.loadManifest().catch((e) => log(`art/ships.json: ${e.message}`));
  setInterval(() => sets.poll(M.FS), 300);
  $("canvas").focus();
  M.callMain(["--data", ROOT, "/noquit"]);   // (returns at the game's first wait: it runs on in the page's turns)
}

// the browser starts sound only for a click or a key: the audio context is resumed at the first
function audio() { return M && M.SDL2 && M.SDL2.audioContext; }
for (const ev of ["pointerdown", "keydown"])
  window.addEventListener(ev, () => { const a = audio(); if (a && a.state === "suspended" && $("sound").textContent === "Sound off") a.resume(); }, true);
$("sound").addEventListener("click", () => {
  const a = audio();
  if (!a) return;
  if (a.state === "running") { a.suspend(); $("sound").textContent = "Sound on"; }
  else { a.resume(); $("sound").textContent = "Sound off"; }
});
$("full").addEventListener("click", () => { $("screen").requestFullscreen().catch((e) => log(`full screen: ${e.message}`)); $("canvas").focus(); });
$("fly").addEventListener("click", fly);
document.addEventListener("visibilitychange", () => { if (document.visibilityState === "hidden" && running) keepSaves(); });
window.addEventListener("error", (e) => log(`page error: ${e.message}`));
// (for the tests and the console: the game's module, its file system's log)
window.RetroVegaPage = { module: () => M, log: () => { try { return new TextDecoder().decode(M.FS.readFile(`${ROOT}/RVLOG.TXT`)); } catch (e) { return ""; } } };
prepare();
