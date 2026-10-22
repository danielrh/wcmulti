// Page glue for the browser build of DOSBox + Wing Commander multiplayer.
//
// Flow: the player brings the game files (gamefiles.js), or picks the game
// built into the page (RetroVega: no files to bring), joins a lobbylink
// room from the page (roster + chat over the same WebRTC links the game
// will use), and the host starts the game for everyone.  Pilots who have
// nobody to fly with meet in the public lobby (hall.js), a chat room where
// a room's code can be said and clicked; the page connects to it only once
// a game the hooks can fly is loaded.  DOSBox then runs
// with that live room connection: src/wclobby_web.js adopts Module.lobbyGame
// instead of joining the room a second time.  Lobby chat and presence
// travel as reliable messages tagged with a 4-byte prefix that the game
// protocol never produces, so the transport filters them out.
import { P2PGame } from "./p2p-client.js";
import { initControls } from "./gamepad.js";
import { initVoice } from "./voice.js";
import { loadSo3 } from "./so3.js";
import { readZip, extractInstaller, looksLikeInstaller, identifyGame, installFiles,
         saveGame, loadGames, forgetGame, totalSize, gameById, gameByTag, programsIn, knownBuild, GAMES } from "./gamefiles.js";
import { newRoomCode, retagRoomCode, normalizeCode, roomTag, GAME_TAGS, MAX_CHARS } from "./chatfilter.js";
import { initHall, OTHER_SERVER, serverDown } from "./hall.js";
import { makeSet } from "./rvsprites.js";

const $ = (id) => document.getElementById(id);
const logEl = $("log");
const canvas = $("canvas");

const DEFAULT_SERVER = "https://pqrstuvw.xyz/lobbylink";
const DATA_URL = "wc.tar.gz";
const GAME_ROOT = "/game";
const LOBBY_MAGIC = [0x57, 0x43, 0x4c, 0x01]; // "WCL" + version; never a valid protobuf start
const query = new URLSearchParams(location.search);
// RetroVega, the registry's built-in game, is a preview for now (the user's
// call, 2026-10-20): it is in the "Play" menu, and the game of a page with
// no files, only with ?retrovega (or #retrovega) in the address or a link to
// an RV- room, and its pilots meet in a lobby of their own (RV-LOBBY).
// Later it can be every fileless page's game.
const retrovega = query.has("retrovega") || /retrovega/i.test(location.hash) || /^RV-/i.test(query.get("room") || "");

// The lobby server only introduces the browsers to each other, and any
// lobbylink server does that: should the public one be down, pilots agree on
// another and pass this page's address around with ?server=URL in it (the
// field under Options is the same).  The other server has to allow this
// page's origin, and to be served over https when the page is.  A server
// that is not the public one is named at the top of the page and is in every
// link the page makes, so that those who follow one meet there.
function normalizeServer(v) {
  v = String(v || "").trim();
  return !v ? "" : /^[a-z][a-z0-9+.-]*:\/\//i.test(v) ? v : "https://" + v;
}
$("server").value = normalizeServer(query.get("server")) || DEFAULT_SERVER;
const serverUrl = () => normalizeServer($("server").value) || DEFAULT_SERVER;
const otherServer = () => serverUrl() !== DEFAULT_SERVER;
const withServer = (url) => { if (otherServer()) url.searchParams.set("server", serverUrl()); else url.searchParams.delete("server"); return url; };
// This page's address for a room: what a room code in the lobby links to.
function roomLink(code) {
  const url = new URL(location.href);
  url.search = ""; url.hash = "";
  url.searchParams.set("room", code);
  if (retrovega) url.searchParams.set("retrovega", "");   // (the friend's page offers it too)
  return withServer(url).toString();
}
function showServer() {
  let name = serverUrl();
  try { const u = new URL(name); name = u.host + u.pathname.replace(/\/+$/, ""); } catch (e) { /* as typed */ }
  $("serverTag").hidden = !otherServer();
  $("serverTag").textContent = otherServer() ? `lobby server: ${name}` : "";
}
showServer();
$("server").addEventListener("change", () => {
  $("server").value = serverUrl();
  history.replaceState(null, "", withServer(new URL(location.href)));
  showServer();
  hall.serverChanged();
});

function log(line) {
  console.log(line);
  const atBottom = logEl.scrollTop + logEl.clientHeight >= logEl.scrollHeight - 4;
  logEl.textContent += line + "\n";
  if (logEl.textContent.length > 200000) logEl.textContent = logEl.textContent.slice(-100000);
  if (atBottom) logEl.scrollTop = logEl.scrollHeight;
  noticeGameEnd(String(line));
}
function status(text) { $("status").textContent = text; }

// A game that stops by itself leaves a DOS prompt, or a picture that stands
// still, and a player with nothing to report but "it crashed".  The hooks
// log the game's end with the text it left on the screen and its stack
// (src/cpu/wcnet_game.cpp, game_program_ended), and a trap in the emulator
// itself comes as a page error: both are said in the status line, and
// "Copy log" takes the whole log to the clipboard for a report.
let endNotice = null;
let emulatorStarted = false;   // (declared here: an error can come before the rest of this file has run)
// The game's one line of help, "To transmit comms use the '0' key", is for
// a player who has never chatted: once a message has gone out from this
// browser it is not shown again, and the box in the room form turns it off
// before that.
const HINT_KEY = "wc:hint";
try { if (localStorage.getItem(HINT_KEY) === "off") $("hint").checked = false; } catch (e) { /* shown */ }
$("hint").addEventListener("change", () => { try { localStorage.setItem(HINT_KEY, $("hint").checked ? "on" : "off"); } catch (e) { /* not kept */ } });
function noticeChat(line) {
  if (/^wcnet: comms: message sent$/.test(line)) {
    $("hint").checked = false;
    try { localStorage.setItem(HINT_KEY, "off"); } catch (e) { /* not kept */ }
  }
}
// A pilot who flies is out of the public lobby (start() below).  When the
// game is over on this page, however it ended, the lobby is open to it
// again and a pilot who was in it is back in it.
function flightOver() { setTimeout(() => hall.reopen(), 0); }
function noticeGameEnd(line) {
  noticeChat(line);
  if (/^Exit to error: /.test(line)) {
    // (DOSBox itself gave up: an instruction or a device it does not emulate.)
    setTimeout(() => status(`The emulator stopped: ${line.slice(15, 175)}. Reload the page to fly again; "Copy log" under the picture has the details for a report.`), 0);
    flightOver();
  } else if (/^wcnet: .+ ended$/.test(line)) {
    endNotice = { lines: [] };
    flightOver();
    setTimeout(() => {
      const said = endNotice.lines.join(" / ");
      endNotice = null;
      status(`The game stopped by itself${said ? `: "${said.slice(0, 160)}"` : ""}. Reload the page to fly again; "Copy log" under the picture has the details for a report.`);
    }, 300);
  } else if (endNotice && /^wcnet:\s+screen: /.test(line) && !/^[A-Z]:\\/.test(line.replace(/^wcnet:\s+screen: /, ""))) {
    endNotice.lines.push(line.replace(/^wcnet:\s+screen: /, "").trim());
  }
}
function emulatorFailed(what) {
  if (!emulatorStarted) return;
  log(`the emulator stopped: ${what}`);
  status(`The emulator stopped (${String(what).split("\n")[0].slice(0, 120)}). Reload the page to fly again; "Copy log" under the picture has the details for a report.`);
  flightOver();
}
window.addEventListener("error", (e) => emulatorFailed((e.error && e.error.stack) || e.message));
window.addEventListener("unhandledrejection", (e) => emulatorFailed((e.reason && (e.reason.stack || e.reason.message)) || String(e.reason)));
$("copyLog").addEventListener("click", async () => {
  const text = `${navigator.userAgent}\n${location.href.replace(/([?&]callsign=)[^&]*/, "$1…")}\n\n${logEl.textContent}`;
  try { await navigator.clipboard.writeText(text); $("copyLog").textContent = "Copied"; }
  catch (e) { const r = document.createRange(); r.selectNodeContents(logEl); const sel = getSelection(); sel.removeAllRanges(); sel.addRange(r); $("copyLog").textContent = "Selected: press Ctrl+C"; }
  setTimeout(() => { $("copyLog").textContent = "Copy log"; }, 2500);
});
// The line under the drop zone: progress and trouble.  A game that is simply
// ready says nothing there (the "Play" menu shows it); data-state tells the
// page tests what the text no longer does: "busy", "ready" or "error".
function sourceStatus(text, state) { const el = $("sourceStatus"); el.textContent = text; el.removeAttribute("title"); if (state) el.dataset.state = state; }
const mb = (n) => (n / 1048576).toFixed(1) + " MB";

// Prefill from the URL (?room=CODE&callsign=...) so a link can be shared.
for (const k of ["room", "callsign", "firstname", "lastname"]) if (query.get(k)) $(k).value = query.get(k);
// The pilot's names are remembered in this browser and filled in on the next
// visit.  A name the link brought is the pilot's own (the page's links carry
// no names but the ones typed here) and is kept too; what the pilot types
// over either is what is kept from then on.
const NAME_KEY = (k) => `wc:name:${k}`;
const keepName = (k) => { try { localStorage.setItem(NAME_KEY(k), $(k).value.trim()); } catch (e) { /* not kept */ } };
for (const k of ["callsign", "firstname", "lastname"]) {
  if ($(k).value) keepName(k);
  else { try { $(k).value = localStorage.getItem(NAME_KEY(k)) || ""; } catch (e) { /* none */ } }
  $(k).addEventListener("change", () => keepName(k));
}
if (query.get("relay")) $("relay").checked = true;
if (query.get("verbose")) $("verbose").checked = true;
// A link to a room (?room=CODE) whose room this page has not tried to join
// yet: joinLinked, at the end of this file, says when it does.
let linked = !!query.get("room");
const linkWaits = () => linked && !lobby.game && normalizeCode($("room").value.trim()) === normalizeCode(query.get("room").trim());
// A new room's code is the game's tag and four digits (WC1-4821; "WC-" as
// long as no game is loaded): said in the public lobby it tells who can
// join.  `autoCode` is the code this page made up: it follows the game the
// player loads until the player types another or joins.
let autoCode = "";
if (!$("room").value) { autoCode = newRoomCode(""); $("room").value = autoCode; }
function retagRoom() {
  if (lobby.game || !autoCode || $("room").value !== autoCode) return;
  autoCode = retagRoomCode(autoCode, gameTag());
  $("room").value = autoCode;
}
// The game a code is for, when it says: the tag of a registry game.
const codeGame = (code) => (GAME_TAGS.includes(roomTag(code)) ? gameByTag(roomTag(code)) : null);

// -- game files ----------------------------------------------------------------

// The selected source: { label, game, root, files } with files as
// [{path, data}] relative to the game directory (drive C); a game built into
// the page (`builtin`) has no files until it is flown (start fetches them).
let source = null;
let saved = [];
// The files dropped on this page while a built-in game is picked: still in
// the "Play" menu, as the saved copies are.
let shelved = null;

// A game directory can hold more than one program of the registry (Wing
// Commander's has The Secret Missions 2, Wing Commander II's the two Special
// Operations).  Which one runs: the one the player picks in the menu
// (`want`), else the one the room's code names, else the one picked the last
// time, else the game itself.
const PROGRAM_KEY = (base) => `wc:program:${base}`;
function setSource(s, want) {
  // What the page knows about a game is the registry's, also for a copy this
  // browser saved when the registry said something else.
  if (s && gameById(s.game.id)) {
    const base = gameById(gameById(s.game.id).partOf || s.game.id);
    const programs = programsIn(s.files);
    if (!programs.includes(base)) programs.unshift(base);
    const roomCode = lobby.game ? lobby.game.code : ($("room").value !== autoCode ? $("room").value.trim() : "");
    let kept = null;
    try { kept = localStorage.getItem(PROGRAM_KEY(base.id)); } catch (e) { /* none */ }
    // (In a room the host's program is everyone's: the code only says what
    // the room was made for.)
    const hostsProgram = lobby.game && !isHost() ? lobby.games.get(0) || "" : "";
    const g = programs.find((p) => p.id === want) || programs.find((p) => p.id === hostsProgram) || programs.find((p) => p === codeGame(roomCode)) || programs.find((p) => p.id === kept) || base;
    s.base = base.id; s.programs = programs;
    s.game = { id: g.id, title: g.title, run: g.run, multiplayer: g.multiplayer };
    // (A file of the game's name is not yet the game: the hooks fly one
    // build of each program, and the public lobby is for pilots who have it.
    // A game built into the page is the build the hooks know.)
    s.known = s.builtin ? !!g.builtin : knownBuild(g, s.files);
  } else if (s) {
    s.base = s.game.id; s.programs = []; s.known = false;
  }
  source = s;
  renderPrograms();
  if (s) {
    // Ready: the "Play" menu says which, and the line under it speaks only
    // of what runs single-player.  (Where the files came from and how many
    // is the menu's tooltip: the panel stays a few lines, and Fly in view.)
    sourceStatus(!s.game.multiplayer ? `${s.game.title}: the multiplayer hooks do not know this program, so it runs single-player.`
                 : !s.known ? `${s.game.title}: its ${gameById(s.game.id).detect[0]} is not the build the multiplayer hooks know: it runs single-player, and the public lobby stays closed.` : "", "ready");
    $("programLabel").title = (s.builtin ? `${s.game.title} is built into this page: its files come from the site when you fly.`
                               : `From ${s.label}: ${s.files.length} files, ${mb(totalSize(s.files))}.`)
                              + " The menu has every program of the games this browser holds; in a room everyone plays the host's.";
  }
  $("source").classList.toggle("loaded", !!s);
  // The help under the game is the loaded game's own.
  const hints = s && gameInfo() && gameInfo().campaign && gameInfo().campaign.hints;
  $("helpMission").textContent = (hints && hints.help) || (gameInfo() && gameInfo().help)
    || (s ? "The multiplayer hooks do not know this program: it runs single-player." : "Load the game files (step 1): the help is the game's own.");
  describeSaves();
  // (Wing Commander II's people use a first name; the first game has none.)
  $("firstnameLabel").hidden = !(gameInfo() && gameInfo().firstName);
  retagRoom();
  if (lobby.game) { sayHello(); buildMissionMenu(); renderMission(); }
  hall.gameChanged();
  updateActions();
  joinLinked();
}
// Which files run a program: the loaded ones ("loaded"), the files dropped
// before a built-in game was picked ("shelved"), the newest copy this
// browser saved that has it (its record), the page itself ("builtin"), or
// nothing.
const runs = (s, id) => !!s && (s.game.id === id || (s.programs || []).some((p) => p.id === id));
function holderOf(id) {
  if (!id) return null;
  if (runs(source, id)) return "loaded";
  if (runs(shelved, id)) return "shelved";
  const copy = saved.find((g) => g.id === id || programsIn(g.files).some((p) => p.id === id));
  if (copy) return copy;
  return builtins().some((g) => g.id === id) ? "builtin" : null;
}
// The games built into the page that this page offers.
const builtins = () => (retrovega ? GAMES.filter((g) => g.builtin) : []);
// The game built into the page, picked.
function useBuiltin(id) {
  const g = gameById(id);
  if (source && !source.builtin && !source.saved) shelved = source;
  setSource({ label: "this page", builtin: true, game: { id: g.id, title: g.title, run: g.run, multiplayer: g.multiplayer },
              root: "", files: [] }, id);
}
// Switches to a program wherever this browser has it (holderOf).
function switchTo(holder, id) {
  if (holder === "loaded") setSource(source, id);
  else if (holder === "shelved") { const s = shelved; shelved = null; setSource(s, id); }
  else if (holder === "builtin") useBuiltin(id);
  else if (holder) useSavedCopy(holder, id);
}
// The "Play" menu: every program of every game this browser holds (the
// loaded files and the saved copies), by game, the loaded one selected.
// Picking a program of another saved copy switches to that copy.
function renderPrograms() {
  const sel = $("program");
  sel.innerHTML = "";
  const groups = new Map();  // base game id -> its optgroup
  const seen = new Set();
  const add = (g) => {
    if (seen.has(g.id)) return;
    seen.add(g.id);
    const base = gameById(g.partOf || g.id) || g;
    let grp = groups.get(base.id);
    if (!grp) { grp = document.createElement("optgroup"); grp.label = base.title; groups.set(base.id, grp); sel.appendChild(grp); }
    const o = document.createElement("option"); o.value = g.id; o.textContent = g.title; grp.appendChild(o);
  };
  if (source) {
    for (const g of source.programs || []) add(g);
    if (!gameById(source.game.id)) add({ id: source.game.id, title: source.game.title });
  }
  if (shelved) for (const g of shelved.programs || []) add(g);
  for (const copy of saved) for (const g of (gameById(copy.id) ? programsIn(copy.files) : [{ id: copy.id, title: copy.title }])) add(g);
  for (const g of builtins()) add(g);
  // (One game with one program needs no heading over it.)
  if (groups.size === 1) { const grp = groups.values().next().value; if (grp.children.length === 1) { sel.appendChild(grp.firstChild); grp.remove(); } }
  if (source) sel.value = source.game.id;
  $("programLabel").hidden = !sel.options.length;
}
// The public lobby is for a pilot with a game to fly: the page connects to
// it only while one is loaded that the hooks know.
const lobbyReady = () => !!(source && source.known);
$("program").addEventListener("change", () => {
  if (running) return;
  const id = $("program").value;
  const holder = holderOf(id);
  if (!holder) return;
  const g = gameById(id);
  if (g) { try { localStorage.setItem(PROGRAM_KEY(g.partOf || g.id), id); } catch (e) { /* not kept */ } }
  switchTo(holder, id);  // (a program of another game this browser holds, or the page's own)
});
// The loaded game's tag for a room code ("WC1"), or "" (none, or a program
// the registry does not know).
const gameTag = () => (gameInfo() && gameInfo().tag) || "";

async function useFiles(files, label, { persist = true } = {}) {
  const id = identifyGame(files);
  if (!id.game) {
    $("pickerLabel").hidden = false;
    const sel = $("exePicker");
    sel.innerHTML = "";
    for (const c of id.candidates) { const o = document.createElement("option"); o.value = c; o.textContent = c; sel.appendChild(o); }
    sourceStatus(id.candidates.length
      ? `${label}: no game I recognise (${files.length} files). Pick the executable to run.`
      : `${label}: no DOS executable found among ${files.length} files.`, "error");
    sel.onchange = () => {
      const exe = sel.value;
      const root = exe.includes("/") ? exe.slice(0, exe.lastIndexOf("/")) : "";
      const prefix = root ? root + "/" : "";
      const name = exe.slice(prefix.length);
      const game = { id: "custom-" + name.toLowerCase(), title: name, run: name.replace(/\.(exe|com|bat)$/i, ""), multiplayer: false };
      const sub = files.filter((f) => f.path.startsWith(prefix)).map((f) => ({ path: f.path.slice(prefix.length), data: f.data }));
      setSource({ label, game, root, files: sub });
      if (persist) void saveGame({ id: game.id, title: game.title, run: game.run, multiplayer: false, label, files: sub, when: Date.now() });
    };
    if (id.candidates.length) sel.onchange();
    return;
  }
  $("pickerLabel").hidden = true;
  setSource({ label, game: id.game, root: id.root, files: id.files });
  if (persist) {
    const ok = await saveGame({ id: id.game.id, title: id.game.title, run: id.game.run, multiplayer: id.game.multiplayer, label, files: id.files, when: Date.now() });
    if (ok) { await refreshSaved(); log(`kept ${id.game.title} in this browser for next time`); }
  }
}

async function handleFile(file) {
  try {
    sourceButtons(false);
    sourceStatus(`Reading ${file.name} (${mb(file.size)})…`, "busy");
    const bytes = new Uint8Array(await file.arrayBuffer());
    let files;
    if (bytes[0] === 0x50 && bytes[1] === 0x4b) {
      files = await readZip(bytes);
      log(`read ${files.length} files from ${file.name}`);
    } else if (looksLikeInstaller(bytes)) {
      sourceStatus(`Unpacking the installer ${file.name} in your browser… (innoextract)`);
      files = await extractInstaller(bytes, log);
    } else {
      throw new Error(`${file.name} is neither a .zip nor a Windows installer`);
    }
    await useFiles(files, file.name);
  } catch (e) {
    log("game files: " + (e && e.message ? e.message : e));
    sourceStatus(`Could not use ${file.name}: ${e && e.message ? e.message : e}. ` +
                 `A .zip of the installed game folder always works; for a GOG installer you can also unpack it with innoextract and zip the result.`, "error");
  } finally {
    sourceButtons(true);
  }
}

async function readTarGz(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`cannot fetch ${url}: ${res.status} ${res.statusText}`);
  const stream = res.body.pipeThrough(new DecompressionStream("gzip"));
  const bytes = new Uint8Array(await new Response(stream).arrayBuffer());
  const files = [];
  const text = (off, len) => { let end = off; while (end < off + len && bytes[end] !== 0) end++; return new TextDecoder("latin1").decode(bytes.subarray(off, end)); };
  let off = 0;
  while (off + 512 <= bytes.length) {
    if (bytes[off] === 0) break;
    const name = text(off, 100);
    const size = parseInt(text(off + 124, 12).trim() || "0", 8);
    const typeflag = bytes[off + 156];
    const prefix = text(off + 345, 155);
    off += 512;
    if (typeflag === 0 || typeflag === 0x30) files.push({ path: (prefix ? prefix + "/" : "") + name, data: bytes.slice(off, off + size) });
    off += Math.ceil(size / 512) * 512;
  }
  return files;
}

// The buttons that change the game files: off while a file is being read
// and once the game runs.
function sourceButtons(on) {
  for (const el of [$("useServer"), $("program"), ...$("savedList").querySelectorAll("button")]) el.disabled = !on;
}
// (Said with its date and its file: a game that is simply there on the
// next visit is the copy this browser kept, not one the site has.)
const useSavedCopy = (g, want) => setSource({ label: `the copy this browser saved${g.when ? " on " + new Date(g.when).toLocaleDateString() : ""} (${g.label})`, saved: true, savedId: g.id,
  game: { id: g.id, title: g.title, run: g.run, multiplayer: g.multiplayer }, root: "", files: g.files }, want);

// The copies this browser kept, side by side on one line: each with its
// size and a "forget" for one that is wrong or damaged.  (Playing one is
// the "Play" menu's business: it lists their programs.)
async function refreshSaved() {
  saved = (await loadGames()).sort((a, b) => (b.when || 0) - (a.when || 0));
  const list = $("savedList");
  list.innerHTML = "";
  if (saved.length) list.appendChild(document.createTextNode("Saved here: "));
  saved.forEach((g, i) => {
    const chip = document.createElement("span");
    chip.className = "saved";
    chip.title = `${g.title}: ${g.files.length} files from ${g.label}${g.when ? ", saved " + new Date(g.when).toLocaleDateString() : ""}. Kept in this browser only.`;
    chip.append(`${g.title} (${mb(totalSize(g.files))}, `);
    const b = document.createElement("button");
    b.type = "button"; b.className = "link"; b.textContent = "forget";
    b.title = `Remove this copy of ${g.title} from the browser; the files on your disk are untouched`;
    b.addEventListener("click", () => void forgetSaved(g));
    chip.append(b, ")");
    list.appendChild(chip);
    if (i < saved.length - 1) list.appendChild(document.createTextNode(" · "));
  });
  renderPrograms();
}
async function forgetSaved(g) {
  if (!confirm(`Forget the saved copy of ${g.title}? Load its files again to play it.`)) return;
  await forgetGame(g.id);
  await refreshSaved();
  if (source && source.savedId === g.id) {
    const own = builtins()[0];
    if (own) useBuiltin(own.id); else setSource(null);
  }
  sourceStatus(`Forgot the saved copy of ${g.title}.`);
}

async function initSources() {
  const drop = $("dropzone");
  for (const ev of ["dragenter", "dragover"]) drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.add("over"); });
  for (const ev of ["dragleave", "drop"]) drop.addEventListener(ev, (e) => { e.preventDefault(); drop.classList.remove("over"); });
  drop.addEventListener("drop", (e) => { const f = e.dataTransfer.files && e.dataTransfer.files[0]; if (f) void handleFile(f); });
  $("gamefile").addEventListener("change", (e) => { const f = e.target.files && e.target.files[0]; if (f) void handleFile(f); });

  $("useServer").addEventListener("click", async () => {
    try {
      sourceStatus("Fetching this server's copy…");
      await useFiles(await readTarGz(DATA_URL), "this server", { persist: false });
    } catch (e) { sourceStatus("Server copy unavailable: " + e.message); }
  });

  await refreshSaved();
  let serverCopy = false;
  try { serverCopy = (await fetch(DATA_URL, { method: "HEAD" })).ok; } catch (e) { /* none */ }
  $("useServer").hidden = !serverCopy;
  // (A link to a room for one game picks that game among the saved copies.)
  // (Nothing saved and no server copy: the game built into the page, so
  // that everybody can fly, and meet in the lobby, with nothing to bring.)
  const wanted = codeGame(lobby.game ? lobby.game.code : $("room").value.trim());
  const fits = wanted && saved.find((g) => programsIn(g.files).includes(wanted));
  if (wanted && builtins().includes(wanted)) useBuiltin(wanted.id);
  else if (saved.length) useSavedCopy(fits || saved[0]);
  else if (serverCopy) $("useServer").click();
  else if (builtins().length) useBuiltin(builtins()[0].id);
}

// -- save games ----------------------------------------------------------------

// A game's registry entry lists the files it keeps its saved games in (Wing
// Commander: one file holding all eight bunks).  This browser keeps a copy in
// local storage, keyed by game: it is put back into the game directory at
// every start and refreshed whenever the running game changes the file.
const gameInfo = () => (source && gameById(source.game.id)) || null;
const saveFiles = () => { const g = gameInfo(); return (g && g.saves) || []; };
const saveKey = (rel) => `wcsave:${source.game.id}:${rel.toUpperCase()}`;
const baseName = (p) => p.slice(p.lastIndexOf("/") + 1);
const sameBytes = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const saveSeen = new Map();  // file -> its bytes as last seen in the running game
function saveStatus(text) { $("saveStatus").textContent = text; }

function storedSave(rel) {
  try {
    const r = JSON.parse(localStorage.getItem(saveKey(rel)) || "null");
    return r && r.b64 ? { when: r.when, data: Uint8Array.from(atob(r.b64), (c) => c.charCodeAt(0)) } : null;
  } catch (e) { return null; }
}
function storeSave(rel, data) {
  try {
    let bin = "";
    for (let i = 0; i < data.length; i += 0x8000) bin += String.fromCharCode(...data.subarray(i, i + 0x8000));
    localStorage.setItem(saveKey(rel), JSON.stringify({ when: Date.now(), b64: btoa(bin) }));
    return true;
  } catch (e) {
    log("save games: this browser would not store the copy: " + (e && e.message ? e.message : e));
    return false;
  }
}
// DOS names ignore case, the emulator's file system does not: find the file
// in whatever case the player's copy of the game uses.
function resolveSave(FS, rel) {
  const parts = rel.split("/");
  let dir = GAME_ROOT;
  for (let i = 0; i < parts.length; i++) {
    let names;
    try { names = FS.readdir(dir); } catch (e) { return null; }
    const hit = names.find((n) => n.toUpperCase() === parts[i].toUpperCase());
    if (i === parts.length - 1) return { path: `${dir}/${hit || parts[i]}`, exists: !!hit };
    if (!hit) return null;
    dir += "/" + hit;
  }
  return null;
}
function readSave(FS, rel) {
  const r = resolveSave(FS, rel);
  if (!r || !r.exists) return null;
  try { return FS.readFile(r.path); } catch (e) { return null; }
}
function writeSave(FS, rel, data) {
  const r = resolveSave(FS, rel);
  if (!r) return false;
  try { FS.writeFile(r.path, data); return true; } catch (e) { return false; }
}
const liveFS = () => (running && window.DOSBox && window.DOSBox.FS) || null;

function describeSaves() {
  const files = saveFiles();
  $("saves").hidden = files.length === 0;
  if (!files.length) return;
  const kept = files.map(storedSave).filter(Boolean);
  saveStatus(kept.length
    ? `This browser keeps your save games (last change ${new Date(Math.max(...kept.map((k) => k.when))).toLocaleString()}).`
    : "No save game of yours is kept here yet: save in a bunk and it will be.");
}
// At start: put the kept copies back, and note what the game starts with.
function restoreSaves(FS) {
  saveSeen.clear();
  for (const rel of saveFiles()) {
    const kept = storedSave(rel);
    if (kept && writeSave(FS, rel, kept.data)) log(`save games: put back ${baseName(rel)} from ${new Date(kept.when).toLocaleString()}`);
    const now = readSave(FS, rel);
    if (now) saveSeen.set(rel, now.slice());
  }
}
// While running: keep a copy whenever the game has changed a save file.
function keepSaves() {
  const FS = liveFS();
  if (!FS) return;
  for (const rel of saveFiles()) {
    const now = readSave(FS, rel);
    const before = saveSeen.get(rel);
    if (!now || (before && sameBytes(before, now))) continue;
    saveSeen.set(rel, now.slice());
    if (storeSave(rel, now)) { log(`save games: kept ${baseName(rel)} in this browser`); describeSaves(); }
  }
}
$("saveDownload").addEventListener("click", () => {
  let count = 0;
  for (const rel of saveFiles()) {
    const FS = liveFS();
    const kept = storedSave(rel);
    const pristine = source.files.find((f) => f.path.toUpperCase() === rel.toUpperCase());
    const data = (FS && readSave(FS, rel)) || (kept && kept.data) || (pristine && pristine.data);
    if (!data) continue;
    const a = document.createElement("a");
    a.href = URL.createObjectURL(new Blob([data], { type: "application/octet-stream" }));
    a.download = baseName(rel);
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 10000);
    count++;
  }
  if (!count) saveStatus("Nothing to download: this copy of the game has no save file yet.");
});
$("saveFile").addEventListener("change", async (e) => {
  const file = e.target.files && e.target.files[0];
  e.target.value = "";
  const files = saveFiles();
  if (!file || !files.length) return;
  const rel = files.find((p) => baseName(p).toUpperCase() === file.name.toUpperCase()) || files[0];
  const data = new Uint8Array(await file.arrayBuffer());
  const pristine = source.files.find((f) => f.path.toUpperCase() === rel.toUpperCase());
  if (!data.length || (pristine && data.length !== pristine.data.length)) {
    saveStatus(`${file.name} is not this game's ${baseName(rel)}: ${data.length} bytes` +
               (pristine ? `, the game's file has ${pristine.data.length}.` : "."));
    return;
  }
  if (!storeSave(rel, data)) return;
  const FS = liveFS();
  if (FS && writeSave(FS, rel, data)) saveSeen.set(rel, data.slice());
  describeSaves();
  log(`save games: restored ${baseName(rel)} from ${file.name}` + (FS ? " (the bunks show it the next time you open them)" : ""));
});

// -- missions ------------------------------------------------------------------

// The host picks a mission of its game's campaign for everybody (the game
// registry lists them), or none: then everyone starts in the barracks and the
// host's saved game decides.  A mission is {series, mis} as the game numbers
// them: series 1.., mission 0.. within the series.  The menu and its labels
// are for the host's game, which the others learn from its hello.
const lobbyGameId = () => (isHost() ? (source ? source.game.id : "") : (lobby.games.get(0) || (source ? source.game.id : "")));
const lobbyCampaign = () => { const g = gameById(lobbyGameId()); return (g && g.campaign) || null; };
const missionKey = (m) => (m ? `${m.series}/${m.mis}` : "");
function parseMission(v) {
  const m = /^(\d+)\/(\d+)$/.exec(v || "");
  return m ? { series: Number(m[1]), mis: Number(m[2]) } : null;
}
// A game with no flight yet (RetroVega): no mission to pick, no flight options.
const lobbyFlies = () => { const g = gameById(lobbyGameId()); return !(g && g.noFlight); };
function missionLabel(m) {
  if (!m) return lobbyFlies() ? "the campaign from the barracks" : (gameById(lobbyGameId()) || {}).title || "the game";
  const c = lobbyCampaign();
  const s = c && c.series.find((e) => e.series === m.series);
  if (!s) return `ch ${m.series}, mission ${m.mis + 1}`;
  const note = Array.isArray(s.missions) ? s.missions[m.mis] : "";
  return (s.name ? `${s.name} ${m.mis + 1}` : `ch ${s.series}, mission ${m.mis + 1}`) + (note ? ` (${note})` : "");
}
function buildMissionMenu() {
  const sel = $("mission");
  const c = lobbyCampaign();
  const key = lobbyGameId();
  if (sel.dataset.game === key && sel.options.length) return;
  sel.dataset.game = key;
  sel.innerHTML = "";
  const add = (value, text) => { const o = document.createElement("option"); o.value = value; o.textContent = text; sel.appendChild(o); };
  add("", "Campaign from the barracks (the host's save game decides)");
  if (!c) return;
  for (const s of c.series) {
    const n = Array.isArray(s.missions) ? s.missions.length : s.missions;
    for (let i = 0; i < n; i++) {
      const note = Array.isArray(s.missions) ? s.missions[i] : "";
      // ("Ch" for the game's series, to keep the row short: the user's word.)
      add(`${s.series}/${i}`, s.name ? `${s.name} ${i + 1}  (ch ${s.series}, mission ${i + 1})`
                                     : `Ch ${s.series}, mission ${i + 1}` + (note ? `  (${note})` : ""));
    }
  }
}
function renderMission() {
  $("missionRow").hidden = !lobbyFlies();
  buildMissionMenu();
  const sel = $("mission");
  const c = lobbyCampaign();
  // A mission the host's game does not have (the host changed games) is none.
  if (lobby.mission && isHost() && !Array.from(sel.options).some((o) => o.value === missionKey(lobby.mission))) lobby.mission = null;
  sel.value = missionKey(lobby.mission);
  sel.disabled = !isHost() || running;
  $("collisions").value = `${lobby.rocks}-${lobby.wingBumps}`;
  $("collisions").disabled = !isHost();  // the host may switch them while flying too
  $("latency").value = lobby.latency;
  $("latency").disabled = !isHost();
  const fpsRange = lobbyFpsRange();
  $("fpsLabel").hidden = !fpsRange || !lobbyFlies();
  if (fpsRange) {
    const inp = $("fps");
    inp.min = String(fpsRange.min);
    inp.max = String(fpsRange.max);
    if (document.activeElement !== inp || !isHost() || running) inp.value = String(fpsInForce());
    inp.disabled = !isHost() || running;
  }
  $("capHullLabel").hidden = !lobbyCapHull() || !lobbyFlies();
  $("capHull").checked = lobby.capHull;
  $("capHull").disabled = !isHost() || running;
  const hints = (c && c.hints) || {};
  // (One line, cut with an ellipsis; the whole of it is the tooltip.)
  const hint = !c ? (lobbyGameId() ? "" : "Load the game files to pick a mission.")
    : lobby.mission ? `${missionLabel(lobby.mission)[0].toUpperCase()}${missionLabel(lobby.mission).slice(1)}. ${hints.forced || ""}`
    : (isHost() ? hints.host : hints.wing) || "";
  $("missionHint").textContent = hint;
  $("missionHint").title = hint;
}
// Everyone in a room runs the host's program.  A wingman's page switches
// to it when the loaded files have it (Wing Commander's directory holds
// The Secret Missions 2, Wing Commander II's the Special Operations) or a
// copy this browser saved does; otherwise the status line asks for the
// files (wrongGame).  The room's code only says what the room was made for,
// so the host may switch programs and the room stays together (the
// user's wish of 2026-10-17: pilots found it hard to change games).
function followHost() {
  if (isHost() || !source) return;
  const theirs = lobby.games.get(0);
  if (!theirs || theirs === source.game.id) return;
  const g = gameById(theirs);
  switchTo(holderOf(theirs), theirs);
  const who = esc(lobby.names.get(0) || "The host");
  if (source && source.game.id === theirs) chatLine(`${who} plays ${esc(g ? g.title : theirs)}: switched to it.`, "sys");
  else chatLine(`${who} plays ${esc(g ? g.title : theirs)}, which this browser has no files for: load them (step 1) to fly along.`, "sys");
}

// -- the room ------------------------------------------------------------------

const lobby = { game: null, names: new Map(), links: new Map(), flying: new Set(), unsubscribe: null, mission: null,
                // which game each player has loaded (a registry id), from their hello
                games: new Map(),
                // asteroid and mine fields, "on", "soft" (a rock does a
                // fraction of its damage) or "off": the host's choice, for everybody
                rocks: "on",
                // two wingmen's ships touching: "harmless" (the default) or
                // "game" (the game's collision damage): the host's choice
                wingBumps: "harmless",
                // the exchange mode, "low" (the default) or "high" latency, or
                // "auto" (the host's game measures the connection and picks,
                // which proved disruptive in play): the host's choice
                latency: "low",
                // frames a second in flight, for a game the hooks pace (in its
                // registry's `fps` range; 0: its default): the host's choice
                fps: 0,
                // capital ships with three times their hull: the host's choice
                capHull: false,
                // game-protocol messages that arrive before DOSBox has adopted this
                // connection (a wingman faster to the launch than the host), handed
                // to the transport at adoption so nothing is lost
                backlog: [] };
let running = false;
const myName = () => $("callsign").value.trim() || "Pilot " + (lobby.game ? lobby.game.selfId + 1 : "?");
const isHost = () => lobby.game && lobby.game.selfId === 0;

function encodeLobby(obj) {
  const json = new TextEncoder().encode(JSON.stringify(obj));
  const out = new Uint8Array(4 + json.length);
  out.set(LOBBY_MAGIC); out.set(json, 4);
  return out;
}
function decodeLobby(data) {
  if (data.length < 4 || LOBBY_MAGIC.some((b, i) => data[i] !== b)) return null;
  try { return JSON.parse(new TextDecoder().decode(data.subarray(4))); } catch (e) { return null; }
}

function sendLobby(obj, to) {
  const g = lobby.game;
  if (!g) return;
  const targets = to != null ? [to] : g.players.filter((p) => p.occupied && p.id !== g.selfId).map((p) => p.id);
  for (const id of targets) g.sendReliable(id, encodeLobby(obj)).catch((e) => log(`lobby: message to player ${id} failed: ${e.message}`));
}
const sayHello = (to, reply) => sendLobby({ t: "hello", name: myName(), flying: running, reply: !!reply,
  game: source ? source.game.id : "", voice: voice.choice(),
  mission: isHost() ? missionKey(lobby.mission) : undefined, rocks: isHost() ? lobby.rocks : undefined,
  wingBumps: isHost() ? lobby.wingBumps : undefined,
  latency: isHost() ? lobby.latency : undefined, fps: isHost() ? lobby.fps : undefined,
  capHull: isHost() ? lobby.capHull : undefined }, to);

// Asteroid and mine fields on, soft or off.  The host's page tells the others
// (for their lobby) and its own running game, which tells every wingman's game.
const ROCKS = { off: 0, on: 1, soft: 2 };  // the game's numbers (RocksMode)
const WING_BUMPS = { game: 0, harmless: 1 };  // the game's numbers (WingBumps)
const wingBumpsMode = (v) => (Object.prototype.hasOwnProperty.call(WING_BUMPS, v) ? v : "harmless");
const wingBumpsLabel = (v) => (v === "game" ? "wing bumps to hurt, as the game has it" : "wing bumps to harmless");
function setWingBumps(v) {
  if (!isHost()) return;
  v = wingBumpsMode(v);
  if (lobby.wingBumps === v) return;
  lobby.wingBumps = v;
  renderMission();
  chatLine(`You switched ${wingBumpsLabel(v)}`, "sys");
  sayHello();
  if (running && window.DOSBox && window.DOSBox._wc_web_set_wing_bumps) window.DOSBox._wc_web_set_wing_bumps(WING_BUMPS[v]);
}
const rocksMode = (v) => (Object.prototype.hasOwnProperty.call(ROCKS, v) ? v : "on");
const rocksLabel = (v) => (v === "soft" ? "asteroids soft (one will not kill you)" : `asteroids and mines ${v}`);
function setRocks(v) {
  if (!isHost()) { chatLine("Only the host can switch asteroids and mines.", "sys"); renderMission(); return; }
  v = rocksMode(v);
  if (lobby.rocks === v) return;
  lobby.rocks = v;
  renderMission();
  chatLine(`You switched ${rocksLabel(v)}`, "sys");
  sayHello();
  if (running && window.DOSBox && window.DOSBox._wc_web_set_rocks) window.DOSBox._wc_web_set_rocks(ROCKS[v]);
}

// The exchange mode (src/cpu/wcnet_session.h, ExchangeMode): auto, low
// latency or high latency.  The host's page tells the others (for their
// lobby) and its own running game, whose frames carry the mode in force to
// every wingman's game.
const LATENCY = { auto: 0, low: 1, high: 2 };  // the game's numbers
const latencyMode = (v) => (Object.prototype.hasOwnProperty.call(LATENCY, v) ? v : "low");
const latencyLabel = (v) => (v === "auto" ? "the connection mode to auto (the host's game picks)" : `the connection to ${v} latency`);
function setLatency(v) {
  if (!isHost()) { chatLine("Only the host can set the connection mode.", "sys"); renderMission(); return; }
  v = latencyMode(v);
  if (lobby.latency === v) return;
  lobby.latency = v;
  renderMission();
  chatLine(`You set ${latencyLabel(v)}`, "sys");
  sayHello();
  if (running && window.DOSBox && window.DOSBox._wc_web_set_latency) window.DOSBox._wc_web_set_latency(LATENCY[v]);
}

// The frame rate in flight (WCFPS): Wing Commander's speed, since it moves
// everything a step a frame.  The host picks it before the launch; the
// host's game gives it to the wingmen's with each mission.  0: the game's
// default (its registry's `fps.def`); a number out of range is taken to the
// nearest end of it.
const lobbyFpsRange = () => { const g = gameById(lobbyGameId()); return (g && g.fps) || null; };
const fpsValue = (v) => {
  const r = lobbyFpsRange(); v = Math.round(Number(v));
  return r && v > 0 ? Math.min(r.max, Math.max(r.min, v)) : 0;
};
const fpsInForce = () => { const r = lobbyFpsRange(); return r ? (lobby.fps || r.def) : 0; };
function setFps(v) {
  if (!isHost()) { chatLine("Only the host can set the frame rate.", "sys"); renderMission(); return; }
  if (running) { chatLine("The frame rate is set before the launch.", "sys"); renderMission(); return; }
  v = fpsValue(v);
  if (!v || v === fpsInForce()) { renderMission(); return; }  // (shows what is in force)
  lobby.fps = v;
  renderMission();
  chatLine(`You set ${fpsInForce()} frames a second`, "sys");
  sayHello();
}

// Capital ships with three times their hull (WCCAPHULL=3 in every machine's
// game; the host's decides what is destroyed).  Set before the launch.
const CAP_HULL = 3;
const lobbyCapHull = () => { const g = gameById(lobbyGameId()); return !!(g && g.capHull); };
const capHullLabel = (on) => (on ? `capital ships ${CAP_HULL} times as tough (capship hull on)` : "capital ships as the game has them (capship hull off)");
function setCapHull(on) {
  if (!isHost()) { chatLine("Only the host can set the capital ships' hull.", "sys"); renderMission(); return; }
  if (running) { chatLine("The capital ships' hull is set before the launch.", "sys"); renderMission(); return; }
  on = !!on;
  if (lobby.capHull === on) return;
  lobby.capHull = on;
  renderMission();
  chatLine(`You set ${capHullLabel(on)}`, "sys");
  sayHello();
}

function chatLine(html, cls) {
  const div = document.createElement("div");
  if (cls) div.className = cls;
  div.innerHTML = html;
  const el = $("chatLog");
  el.appendChild(div);
  el.scrollTop = el.scrollHeight;
}
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));

function renderRoster() {
  const g = lobby.game;
  const ul = $("roster");
  ul.innerHTML = "";
  if (!g) return;
  for (const p of g.players) {
    const li = document.createElement("li");
    const me = p.id === g.selfId;
    const link = me ? "up" : (lobby.links.get(p.id) || (p.occupied ? "down" : ""));
    const name = me ? myName() : (lobby.names.get(p.id) || (p.occupied ? "…" : "empty seat"));
    const tags = [];
    if (p.id === 0) tags.push("host");
    if (me) tags.push("you");
    if (p.occupied && !me) tags.push(link === "up" ? "connected" : "connecting…");
    if (lobby.flying.has(p.id) || (me && running)) tags.push("flying");
    li.innerHTML = `<span class="dot ${p.occupied ? link : ""}"></span><span class="who">${esc(name)}</span><span class="tag">${tags.join(" · ")}</span>`;
    ul.appendChild(li);
  }
}

// The host's program is the room's.  A wingman's page follows it when its
// files can (followHost); when they cannot, this is said instead of flying.
// (The room's code is advisory: the game the room was made for.)
function wrongGame() {
  if (!lobby.game || isHost() || !source) return "";
  const theirs = gameById(lobby.games.get(0) || "");
  if (!theirs || theirs.id === source.game.id) return "";
  return `The host plays ${theirs.title}, and you have ${source.game.title} loaded: load ${theirs.title} above (step 1), or leave the room.`;
}
function updateActions() {
  const wrong = wrongGame();
  const ready = !!source && !!lobby.game && !running && !wrong;
  $("fly").disabled = !ready;
  $("advertise").hidden = !lobby.game || running || !lobbyReady() || !lobby.game.players.some((p) => !p.occupied);
  if (!running) {
    if (wrong) status(wrong);
    else if (!source && !lobby.game) status(linkWaits() ? `Load your game files (step 1) and you are in room ${$("room").value.trim()}; "Join room" takes a seat in it without them.` : "Load the game files and join a room first.");
    else if (!source) status(`Join succeeded; now load ${codeGame(lobby.game.code) ? codeGame(lobby.game.code).title : "the game files"} above.`);
    else if (!lobby.game) status("Game files ready; join a room to fly with friends.");
    else if (isHost()) { $("fly").textContent = "Fly (starts everyone)"; status(`You host room ${lobby.game.code}. Press Fly when your wingmen are in.`); }
    else { $("fly").textContent = "Fly"; status(lobby.flying.has(0) ? "The host is already flying: press Fly to join the mission." : "Waiting for the host to start; Fly joins on your own too."); }
  }
  renderTopAction();
}

// The button at the top of the page, in view however the page is scrolled:
// Fly once there is a room to fly from (the same as the Fly button below),
// before that the step that comes next, and the full screen while the game
// runs.
function renderTopAction() {
  const b = $("topAction");
  const step = running ? "screen" : !source ? "files" : !lobby.game ? "room" : "fly";
  b.dataset.step = step;
  b.disabled = step === "fly" && $("fly").disabled;
  b.textContent = { screen: "Full screen", files: "Load game files", room: "Join room", fly: "Fly" }[step];
  b.title = { screen: "The game full screen; holding Esc leaves it",
              files: "Your own Wing Commander or Wing Commander II: a .zip of the game folder, or the GOG installer",
              room: "Take a seat in the room of step 2 (made if nobody has); wingmen join by its code or this page's link",
              fly: $("status").textContent }[step];
}
$("topAction").addEventListener("click", () => {
  const step = $("topAction").dataset.step;
  if (step === "screen") goFullscreen();
  else if (step === "files") $("gamefile").click();
  else if (step === "room") { $("roomPanel").scrollIntoView({ block: "nearest", behavior: "smooth" }); void joinRoom(); }
  else $("fly").click();
});

function onLobbyEvent(ev) {
  switch (ev.type) {
    case "message": {
      if (ev.kind !== "reliable") return;
      const m = decodeLobby(ev.data);
      if (!m) { // game protocol: the wasm transport handles it, once it exists
        if (!lobby.adopted && lobby.backlog.length < 256) lobby.backlog.push({ from: ev.from, data: ev.data });
        return;
      }
      if (m.t === "hello") {
        const fresh = lobby.names.get(ev.from) !== m.name;
        lobby.names.set(ev.from, m.name);
        if (m.flying) lobby.flying.add(ev.from); else lobby.flying.delete(ev.from);
        if (fresh) chatLine(`${esc(m.name)} is in the room${m.flying ? " (flying)" : ""}`, "sys");
        if (typeof m.game === "string" && m.game !== (lobby.games.get(ev.from) || "")) {
          lobby.games.set(ev.from, m.game);
          if (ev.from === 0) { followHost(); renderMission(); }
        }
        if (ev.from === 0 && !isHost() && m.mission !== undefined) {
          const chosen = parseMission(m.mission);
          if (missionKey(chosen) !== missionKey(lobby.mission)) {
            lobby.mission = chosen;
            chatLine(`${esc(m.name)} picked ${esc(missionLabel(chosen))}`, "sys");
            renderMission();
          }
        }
        if (ev.from === 0 && !isHost() && typeof m.rocks === "string" && rocksMode(m.rocks) !== lobby.rocks) {
          lobby.rocks = rocksMode(m.rocks);
          chatLine(`${esc(m.name)} switched ${rocksLabel(lobby.rocks)}`, "sys");
          renderMission();
        }
        if (ev.from === 0 && !isHost() && typeof m.wingBumps === "string" && wingBumpsMode(m.wingBumps) !== lobby.wingBumps) {
          lobby.wingBumps = wingBumpsMode(m.wingBumps);
          chatLine(`${esc(m.name)} switched ${wingBumpsLabel(lobby.wingBumps)}`, "sys");
          renderMission();
        }
        if (ev.from === 0 && !isHost() && typeof m.latency === "string" && latencyMode(m.latency) !== lobby.latency) {
          lobby.latency = latencyMode(m.latency);
          chatLine(`${esc(m.name)} set ${latencyLabel(lobby.latency)}`, "sys");
          renderMission();
        }
        if (ev.from === 0 && !isHost() && typeof m.fps === "number" && fpsValue(m.fps) !== lobby.fps) {
          lobby.fps = fpsValue(m.fps);
          if (fpsInForce()) chatLine(`${esc(m.name)} set ${fpsInForce()} frames a second`, "sys");
          renderMission();
        }
        if (ev.from === 0 && !isHost() && typeof m.capHull === "boolean" && m.capHull !== lobby.capHull) {
          lobby.capHull = m.capHull;
          if (lobbyCapHull()) chatLine(`${esc(m.name)} set ${capHullLabel(lobby.capHull)}`, "sys");
          renderMission();
        }
        voice.remote(ev.from, typeof m.voice === "string" ? m.voice : "off");
        if (!m.reply) sayHello(ev.from, true);
        renderRoster(); updateActions();
      } else if (m.t === "voice") {
        void voice.signal(ev.from, m.data);
      } else if (m.t === "chat") {
        chatLine(`<span class="name">${esc(m.name)}:</span> ${esc(m.text)}`);
      } else if (m.t === "start" && ev.from === 0 && !running) {
        if (typeof m.game === "string" && m.game) lobby.games.set(0, m.game);
        lobby.mission = parseMission(m.mission);
        if (typeof m.rocks === "string") lobby.rocks = rocksMode(m.rocks);
        if (typeof m.wingBumps === "string") lobby.wingBumps = wingBumpsMode(m.wingBumps);
        if (typeof m.latency === "string") lobby.latency = latencyMode(m.latency);
        if (typeof m.fps === "number") lobby.fps = fpsValue(m.fps);
        if (typeof m.capHull === "boolean") lobby.capHull = m.capHull;
        renderMission();
        chatLine(`${esc(lobby.names.get(0) || "The host")} started ${esc(missionLabel(lobby.mission))}`, "sys");
        lobby.flying.add(0); renderRoster();
        followHost();
        const theirs = gameById(lobby.games.get(0) || "");
        if (source && theirs && theirs.id !== source.game.id) status(`The host started ${theirs.title}, and you have ${source.game.title} loaded. Load ${theirs.title} above, then press Fly to join.`);
        else if (source) void start(false);
        else status("The host started the game. Load your game files above, then press Fly to join.");
      }
      break;
    }
    case "player-joined": case "player-rejoined": case "player-replaced":
      lobby.links.set(ev.playerId, "down"); lobby.names.delete(ev.playerId); lobby.flying.delete(ev.playerId); lobby.games.delete(ev.playerId);
      voice.forget(ev.playerId);
      renderRoster(); break;
    case "player-left":
      if (ev.reason === "explicit-leave") {
        chatLine(`${esc(lobby.names.get(ev.playerId) || "Player " + (ev.playerId + 1))} left the room`, "sys");
        lobby.names.delete(ev.playerId); lobby.links.delete(ev.playerId); lobby.flying.delete(ev.playerId); lobby.games.delete(ev.playerId);
      }
      voice.forget(ev.playerId);
      renderRoster(); updateActions(); break;

    case "peer-state":
      lobby.links.set(ev.playerId, ev.state === "connected" ? "up" : (ev.state === "failed" || ev.state === "closed") ? "down" : (lobby.links.get(ev.playerId) || "down"));
      if (ev.state === "connected") sayHello(ev.playerId, false);
      renderRoster(); break;
    case "signaling-closed":
      chatLine(`Lost the lobby server (${esc(ev.code)}): nobody new can join, existing links stay up`, "sys"); break;
    case "lobby-error":
      chatLine(`Lobby error ${esc(ev.code)}: ${esc(ev.message)}`, "sys"); break;
  }
}

// The game a room's code names against what this browser holds.  The code
// is advisory (the host may switch programs in the room, and the pages
// follow the host's hello: followHost), but a browser with no files that
// run the code's game is not let in.  A click on SO1-4821 with Wing
// Commander II loaded switches to Special Operations 1 when it is there,
// or to a saved copy that has it; WC2-4821 with Wing Commander alone is
// refused.
function fitsRoom(code) {
  const theirs = codeGame(code);
  if (!theirs || !source || source.game.id === theirs.id) return true;
  const holder = holderOf(theirs.id);
  if (holder === "loaded") {
    setSource(source, theirs.id);
    chatLine(`Room ${esc(code)} is a ${esc(theirs.title)} room: switched to ${esc(theirs.title)}.`, "sys");
    return true;
  }
  if (holder) {
    switchTo(holder, theirs.id);
    chatLine(`Room ${esc(code)} is a ${esc(theirs.title)} room: switched to ${holder === "builtin" ? "it (built into this page)" : holder === "shelved" ? "the files dropped here" : "the saved copy that has it"}.`, "sys");
    return true;
  }
  status(`${code} is a ${theirs.title} room, and you have ${source.game.title} loaded. Load ${theirs.title} above (step 1) to join it.`);
  return false;
}

// create: false only joins a room that exists (a code clicked in the lobby).
async function joinRoom({ create = true } = {}) {
  if (lobby.game || $("join").disabled) return;
  // (A link's room that waits for the right game goes on waiting for it.)
  const viaLink = linkWaits();
  linked = false;
  const code = normalizeCode($("room").value.trim());
  $("room").value = code;
  if (!/^[A-Za-z0-9_-]{4,64}$/.test(code)) { status("Room codes are 4-64 letters, digits, - or _."); return; }
  if (hall.isLobby(code)) { status(`${code} is the public lobby (enter it above); a room to fly in needs a code of its own.`); return; }
  if (!fitsRoom(code)) { linked = viaLink; return; }
  $("join").disabled = true;
  status(`Joining room ${code}…`);
  try {
    const game = await P2PGame.connect({
      server: serverUrl(),
      code,
      // A room code gets said out loud: nobody who hears it may take a
      // seat somebody is sitting in.  A seat comes back only to the browser
      // tab that held it (its hidden resume token: reloading the page keeps
      // the seat); a tab that is gone leaves its seat taken until the room
      // dies, and the host makes a new room.  Tokenless "claims" would let
      // anyone with the code replace a player after 40 s of lobby silence,
      // which is every player in flight.
      // A room has two seats; "3 players" (beta) gives it a third, never
      // more: a third pilot gets a ship of the mission's wing, or one the
      // host's game makes (a second wingman, a second gunner), when they
      // come.  The box is off by default and not remembered.
      create: !create ? undefined
        : { maxPlayers: $("threeSeats").checked ? 3 : 2, waitUntilFull: false, allowLateJoin: true,
            allowReconnect: true, allowReplacement: false, reconnectPolicy: "token-only" },
      storage: "session",
      storageKey: "wclobby-" + code,
      forceRelay: $("relay").checked,
    });
    lobby.game = game;
    lobby.names.clear(); lobby.links.clear(); lobby.flying.clear(); lobby.games.clear();
    lobby.unsubscribe = game.onEvent(onLobbyEvent);
    // The address keeps no room: a link's code is for the join it brought,
    // not for every reload and bookmark after (rooms die; a pilot stuck in
    // a dead one's link could not get out).  "Copy link" makes the link.
    clearRoomFromAddress();
    $("lobby").hidden = false;
    $("leave").hidden = false;
    $("share").hidden = false;
    if (game.selfId === 0) lobby.mission = parseMission(query.get("mission")) || { series: 1, mis: 0 };
    lobby.rocks = game.selfId === 0 ? rocksMode({ 0: "off", 2: "soft" }[query.get("rocks")] || query.get("rocks")) : "on";
    lobby.wingBumps = game.selfId === 0 ? wingBumpsMode({ 0: "game", 1: "harmless" }[query.get("bumps")] || query.get("bumps")) : "harmless";
    lobby.latency = game.selfId === 0 ? latencyMode(query.get("latency")) : "low";
    lobby.fps = game.selfId === 0 ? fpsValue(query.get("fps")) : 0;
    lobby.capHull = game.selfId === 0 && query.get("caphull") === "1";
    renderMission();
    for (const id of ["room", "threeSeats", "server", "relay"]) $(id).disabled = true;
    chatLine(`You are ${esc(myName())}, player ${game.selfId + 1} of ${game.maxPlayers} in room ${esc(code)}${game.selfId === 0 ? " (host)" : ""}. "Copy link" gives friends the way in.`, "sys");
    log(`lobby: joined room ${code} as player ${game.selfId} of ${game.maxPlayers}`);
    renderRoster(); updateActions();
    $("chatInput").focus();
  } catch (e) {
    // (A link to a full or dead room is spent: the address loses the code.)
    clearRoomFromAddress();
    status(e && e.code === "room-not-found" ? `Room ${code} is not open any more: ask for another room in the lobby, or make one.`
      : e && e.code === "room-full" ? `Room ${code} is full: ask for another room in the lobby, or make one.`
      : `Could not join room ${code}: ${e && e.code ? e.code + ": " : ""}${e && e.message ? e.message : e}.${e && serverDown(e.code) ? " " + OTHER_SERVER : ""}`);
    $("join").disabled = false;
  }
}
function clearRoomFromAddress() {
  const url = new URL(location.href);
  for (const k of ["room", "callsign", "firstname", "lastname"]) url.searchParams.delete(k);
  history.replaceState(null, "", withServer(url));
}

// A room code clicked in the public lobby: the pilot wants to be in that
// room.  One they sit in alone (their own, made by "Join room" or by an
// advertisement) is left for it; one with company is not left behind by a
// click.  What came of it is said under the lobby's chat, where the click
// was, as well as in the status line.
async function joinFromLobby(code) {
  const tell = (text) => { status(text); hall.note(text); };
  if (running) { tell(`Reload the page to join ${code}: this page's game has been started.`); return; }
  if (lobby.game && lobby.game.code === code) { tell(`You are in room ${code} already (step 2).`); return; }
  let left = "";
  if (lobby.game) {
    const company = lobby.game.players.some((p) => p.occupied && p.id !== lobby.game.selfId);
    if (company) { tell(`You are in room ${lobby.game.code} with somebody: leave it (step 2) to join ${code}.`); return; }
    left = lobby.game.code;
    chatLine(`You left room ${esc(left)} for ${esc(code)}`, "sys");
    leaveRoom();
  }
  const typed = $("room").value;
  $("room").value = code;
  await joinRoom({ create: false });
  if (lobby.game) {
    tell(`You are in room ${code} (step 2): the host presses Fly when everyone is in.`);
    $("setup").scrollIntoView({ block: "nearest", behavior: "smooth" });
  } else {
    const why = $("status").textContent;
    $("room").value = typed; retagRoom();
    if (left) { await joinRoom(); if (lobby.game) status(why); }  // (back in the room of one's own)
    hall.note(why);  // (why not, said where the click was)
  }
}

// A room advertised in the public lobby: its code (which names the game),
// the mission and the free seats, "WC1-4821 Gimle 2, 1 seat free".  It is
// one of the pilot's lines there.
function offerText() {
  const g = lobby.game;
  const free = g.players.filter((p) => !p.occupied).length;
  const c = lobbyCampaign();
  const s = lobby.mission && c && c.series.find((e) => e.series === lobby.mission.series);
  const what = !lobbyFlies() ? "" : !lobby.mission ? "campaign" : s && s.name ? `${s.name} ${lobby.mission.mis + 1}` : `ch ${lobby.mission.series} mission ${lobby.mission.mis + 1}`;
  const seats = free ? `${free} seat${free === 1 ? "" : "s"} free` : "full";
  // (The program, when the room's code was made for another: the code is advisory.)
  const prog = gameTag() && roomTag(g.code) !== gameTag() ? ` ${gameTag()}` : "";
  const full = `${g.code}${prog}${what ? " " + what : ""}, ${seats}`;
  return Array.from(full).length <= MAX_CHARS ? full : `${g.code} ${seats}`;
}
// The pilot's own room for the lobby (its "Advertise" and "/room"): the room
// the pilot is in, and otherwise the one of the room form, joined now.
async function myRoom() {
  if (running) return null;
  if (!lobby.game) await joinRoom();
  return lobby.game ? { code: lobby.game.code, offer: offerText() } : null;
}
$("advertise").addEventListener("click", () => { if (lobby.game && !running) void hall.advertise(); });

function leaveRoom() {
  voice.stopAll();
  if (lobby.unsubscribe) lobby.unsubscribe();
  if (lobby.game) { try { lobby.game.close(); } catch (e) { /* gone */ } }
  lobby.game = null; lobby.unsubscribe = null; lobby.adopted = false; lobby.backlog = [];
  lobby.names.clear(); lobby.links.clear(); lobby.flying.clear(); lobby.games.clear();
  $("lobby").hidden = true; $("leave").hidden = true; $("share").hidden = true; $("join").disabled = false;
  for (const id of ["room", "threeSeats", "server", "relay"]) $(id).disabled = false;
  $("roster").innerHTML = "";
  updateActions();
}

$("setup").addEventListener("submit", (ev) => { ev.preventDefault(); void joinRoom(); });
$("leave").addEventListener("click", leaveRoom);
// The link to this room, to the clipboard (shown in the status line where
// the clipboard is out of reach).
$("share").addEventListener("click", async () => {
  if (!lobby.game) return;
  const link = roomLink(lobby.game.code);
  try { await navigator.clipboard.writeText(link); $("share").textContent = "Copied"; }
  catch (e) { status(`The link to room ${lobby.game.code}: ${link}`); $("share").textContent = "See below"; }
  setTimeout(() => { $("share").textContent = "Copy link"; }, 2500);
});
$("chatForm").addEventListener("submit", (ev) => {
  ev.preventDefault();
  const text = $("chatInput").value.trim();
  if (!text || !lobby.game) return;
  $("chatInput").value = "";
  const cmd = /^\/rocks(?:\s+(on|soft|off))?$/i.exec(text);
  if (cmd) { // a command, not a message
    if (cmd[1]) setRocks(cmd[1].toLowerCase());
    else chatLine(`${rocksLabel(lobby.rocks)[0].toUpperCase()}${rocksLabel(lobby.rocks).slice(1)}; the host switches them with /rocks on, /rocks soft or /rocks off.`, "sys");
    return;
  }
  const bumps = /^\/bumps(?:\s+(harmless|game|on|off))?$/i.exec(text);
  if (bumps) {
    if (bumps[1]) setWingBumps({ on: "game", off: "harmless" }[bumps[1].toLowerCase()] || bumps[1].toLowerCase());
    else chatLine(`${wingBumpsLabel(lobby.wingBumps)[0].toUpperCase()}${wingBumpsLabel(lobby.wingBumps).slice(1)}; the host switches it with /bumps harmless or /bumps game.`, "sys");
    return;
  }
  const lat = /^\/latency(?:\s+(auto|low|high))?$/i.exec(text);
  if (lat) {
    if (lat[1]) setLatency(lat[1].toLowerCase());
    else chatLine(`${linkState() || `The connection mode is set to ${lobby.latency}`}; the host sets it with /latency auto, /latency low or /latency high.`, "sys");
    return;
  }
  chatLine(`<span class="name">${esc(myName())}:</span> ${esc(text)}`);
  sendLobby({ t: "chat", name: myName(), text });
});
$("callsign").addEventListener("change", () => { if (lobby.game) { sayHello(); renderRoster(); } hall.announce(); });
$("collisions").addEventListener("change", () => {
  const [rocks, bumps] = $("collisions").value.split("-");
  setRocks(rocks);
  setWingBumps(bumps);
});
$("latency").addEventListener("change", () => setLatency($("latency").value));
// A terse menu shows a word when closed and the whole of each choice
// (data-long) while open; its width stays the closed one's meanwhile.
for (const sel of document.querySelectorAll("select.terse")) {
  for (const o of sel.options) o.dataset.short = o.textContent;
  const open = () => {
    if (sel.dataset.open) return;
    sel.dataset.open = "1";
    if (sel.offsetWidth) sel.style.width = `${sel.offsetWidth}px`;
    for (const o of sel.options) o.textContent = o.dataset.long || o.dataset.short;
  };
  const close = () => {
    if (!sel.dataset.open) return;
    delete sel.dataset.open;
    for (const o of sel.options) o.textContent = o.dataset.short;
    sel.style.width = "";
  };
  sel.addEventListener("pointerdown", open);
  sel.addEventListener("keydown", open);
  sel.addEventListener("change", close);
  sel.addEventListener("blur", close);
}
// (A number input says "change" on Enter or leaving it; the box may still
// have the focus, which renderMission leaves alone.)
$("fps").addEventListener("change", () => { setFps($("fps").value); $("fps").value = String(fpsInForce()); });
$("capHull").addEventListener("change", () => setCapHull($("capHull").checked));
$("mission").addEventListener("change", () => {
  if (!isHost()) return;
  lobby.mission = parseMission($("mission").value);
  renderMission();
  chatLine(`You picked ${esc(missionLabel(lobby.mission))}`, "sys");
  sayHello();
});
buildMissionMenu();

// -- flying --------------------------------------------------------------------

$("fly").addEventListener("click", () => {
  if (!source || !lobby.game || running) return;
  if (isHost()) sendLobby({ t: "start", game: source.game.id, mission: missionKey(lobby.mission), rocks: lobby.rocks, wingBumps: lobby.wingBumps, latency: lobby.latency, fps: lobby.fps, capHull: lobby.capHull });
  void start(true);
});
$("fullscreen").addEventListener("click", () => goFullscreen());
function goFullscreen() {
  const req = canvas.requestFullscreen || canvas.webkitRequestFullscreen;
  if (!req) return;
  // Where the browser can (Chrome, Edge), ask for Esc in full screen: a tap
  // then goes to the game and only holding it leaves full screen.
  if (navigator.keyboard && navigator.keyboard.lock) navigator.keyboard.lock(["Escape"]).catch(() => { /* not allowed here */ });
  Promise.resolve(req.call(canvas)).catch(() => { /* needs a click: the button stays */ });
  canvas.focus();
}
document.addEventListener("fullscreenchange", () => {
  if (!document.fullscreenElement && navigator.keyboard && navigator.keyboard.unlock) navigator.keyboard.unlock();
});

// Caps Lock is the game's Esc everywhere: browsers keep Esc for leaving full
// screen.  The key never reaches the emulator as Caps Lock; instead a short
// Esc press is injected.  A Mac reports Caps Lock as a key-down when it turns
// on and a lone key-up when it turns off, so a key-up without a recent
// key-down counts as a tap as well.
let capsDownAt = -1e9;
function capsLockAsEscape(e) {
  if (e.code !== "CapsLock" || !running) return;
  if (document.activeElement !== canvas && document.fullscreenElement !== canvas) return;
  e.preventDefault();
  e.stopImmediatePropagation();
  const tap = window.DOSBox && window.DOSBox._wc_web_tap_escape;
  if (!tap) return;
  if (e.type === "keydown") {
    if (e.repeat) return;
    capsDownAt = performance.now();
    tap();
  } else if (performance.now() - capsDownAt > 500) {
    tap();
  }
}
window.addEventListener("keydown", capsLockAsEscape, true);
window.addEventListener("keyup", capsLockAsEscape, true);

async function start(fromGesture) {
  if (running) return;
  running = true;
  emulatorStarted = true;
  // (The public lobby is for finding a flight: this page has one.  The
  // pilot is back in it when the game is over: flightOver.)
  hall.shut("You left the lobby to fly.");
  // Voice on one side only: said here, so that nobody is surprised.
  const voiceWarning = voice.warning();
  if (voiceWarning) { chatLine(esc(voiceWarning), "sys"); log("voice: " + voiceWarning); }
  $("fly").disabled = true;
  renderTopAction();
  renderMission();  // (the mission and the frame rate are set for this flight)
  for (const el of ["gamefile", "exePicker", "callsign", "firstname", "lastname", "verbose", "hint", "leave"]) $(el).disabled = true;
  sourceButtons(false);
  try {
    const cfg = {
      code: lobby.game.code,
      callsign: $("callsign").value.trim(),
      lastname: $("lastname").value.trim(),
      firstname: gameInfo() && gameInfo().firstName ? $("firstname").value.trim() : "",
      players: String(lobby.game.maxPlayers || 2),   // (the game reads the room's own size; this agrees with it)
      server: serverUrl(),
      relay: $("relay").checked,
      verbose: $("verbose").checked,
    };
    status("Loading the emulator…");
    const { default: createDOSBox } = await import("./dosbox.js");
    // (WCNET_EXIT_STACK: should the game end by itself, its stack goes into the log.)
    const env = { WCNET_LOG: cfg.verbose ? "2" : "1", WCNET_EXIT_STACK: "1", SDL_EMSCRIPTEN_KEYBOARD_ELEMENT: "#canvas" };
    if (source.game.multiplayer) {
      env.WCROOM = cfg.code; env.WCLOBBY = cfg.server; env.WCPLAYERS = cfg.players;
      if (cfg.relay) env.WCLOBBY_RELAY = "1";
    } else {
      log(`${source.game.title}: no multiplayer hooks for this game, running single-player`);
    }
    // The host's game decides and tells the wingmen's; theirs start the same.
    if (source.game.multiplayer) env.WCROCKS = String(ROCKS[lobby.rocks]);
    if (source.game.multiplayer) env.WCWINGBUMPS = String(WING_BUMPS[lobby.wingBumps]);
    if (source.game.multiplayer && isHost()) env.WCNET_MODE = lobby.latency;
    // (The wingmen's games take the host's with each mission; theirs agrees.)
    if (fpsInForce()) env.WCFPS = String(fpsInForce());
    if (source.game.multiplayer && lobbyCapHull() && lobby.capHull) env.WCCAPHULL = String(CAP_HULL);
    if (cfg.callsign) env.WCCALLSIGN = cfg.callsign;
    if (cfg.lastname) env.WCLASTNAME = cfg.lastname;
    if (cfg.firstname) env.WCFIRSTNAME = cfg.firstname;
    // A wingman's game calls the leader's ship by the leader's callsign (the
    // targeting computer, the comm menu): the roster has it.
    if (!isHost() && lobby.names.get(0)) env.WCHOSTCALLSIGN = lobby.names.get(0);
    if (!$("hint").checked) env.WCNET_NOHINT = "1";
    // The picked mission: in the hooks' environment, or on the game's own
    // command line (the registry says which).
    const campaign = gameInfo() && gameInfo().campaign;
    let missionArgs = "";
    if (lobby.mission && source.game.multiplayer && campaign) {
      if (campaign.missionArgs) {
        missionArgs = " " + campaign.missionArgs(lobby.mission);
      } else {
        env.MIS = String(lobby.mission.mis);
        env.SERIES = String(lobby.mission.series);
      }
    }
    // Any ?env.NAME=value lands in DOSBox's environment: the same knobs as
    // the native build (MIS, SERIES, WCNET_AUTOKEYS, WCNET_LOG, ...).
    for (const [k, v] of query) if (k.startsWith("env.")) env[k.slice(4)] = v;
    if (env.MIS !== undefined || env.SERIES !== undefined) {
      log(`flying ${missionLabel({ series: Number(env.SERIES || 1), mis: Number(env.MIS || 0) })} straight from the hangar`);
    } else if (missionArgs) {
      log(`${missionLabel(lobby.mission)}: ${source.game.run}${missionArgs}`);
    }

    const config = {
      canvas,
      P2PGame,
      lobbyGame: () => lobby.game,    // the transport adopts the connection the page is in when the game asks
      takeLobbyBacklog: () => { lobby.adopted = true; return lobby.backlog.splice(0); },
      onLobbyClosed: () => { chatLine("The game left the room", "sys"); leaveRoom(); },
      print: log,
      printErr: log,
      locateFile: (path) => path,
      onAbort: (what) => emulatorFailed(`abort: ${what}`),
      preRun: [() => { Object.assign(config.ENV, env); }],
    };
    const Module = await createDOSBox(config);

    // A game built into the page comes from the site now.
    if (source.builtin && !source.files.length) {
      status(`Fetching ${source.game.title}…`);
      source.files = await readTarGz(gameInfo().builtin);
      log(`fetched ${source.files.length} files of ${source.game.title} (${mb(totalSize(source.files))}) from this site`);
    }
    status(`Installing ${source.game.title} (${source.files.length} files)…`);
    installFiles(Module.FS, GAME_ROOT, source.files);
    log(`installed ${source.files.length} files of ${source.game.title} under ${GAME_ROOT}`);
    // RetroVega's sprite sets (SHIPnn.RVS: every view at every size and roll
    // step, retrovega/docs/engine.md "Flight") are made here from the views
    // in SHIPnn.RV rather than downloaded: the same bytes the C tool makes.
    if (gameInfo() && gameInfo().id === "rv") {
      const t0 = performance.now();
      const sets = [];
      for (const f of source.files) {
        const m = /^(SHIP[0-9A-F]{2})\.RV$/i.exec(f.path);
        if (!m) continue;
        status(`Making ${m[1]}'s sprite set…`);
        try { sets.push({ path: f.path + "S", data: makeSet(f.data, 0, 4, 9) }); }
        catch (e) { log(`${f.path}: no sprite set (${e.message})`); }
      }
      installFiles(Module.FS, GAME_ROOT, sets);
      log(`made ${sets.length} sprite sets (${mb(totalSize(sets))}) in ${Math.round(performance.now() - t0)} ms`);
    }
    // A program the page makes from the game (Special Operations 3): its
    // files come sealed from the site and open with the player's own copy.
    if (gameInfo() && gameInfo().addon === "so3") {
      const extra = await loadSo3(source.files, status);
      installFiles(Module.FS, GAME_ROOT, extra);
      log(`installed ${extra.length} files of the Culloden campaign`);
    }
    window.DOSBox = Module;
    restoreSaves(Module.FS);

    sayHello();
    renderRoster(); renderMission();
    status(source.game.multiplayer ? `Room ${cfg.code}: starting…` : `Starting ${source.game.title}…`);
    canvas.focus();
    // ?cmd=... runs another DOS command instead of the game (debugging aid).
    const cmd = query.get("cmd") || source.game.run + missionArgs;
    // The emulated CPU speed: the game's own good value (the registry), or
    // ?cycles=N; Ctrl+F11 / Ctrl+F12 still adjust it while playing.
    const cycles = Number(query.get("cycles")) || (gameInfo() && gameInfo().cycles) || 0;
    const args = ["-c", `mount c ${GAME_ROOT}`, "-c", "c:"];
    if (cycles > 0) args.push("-c", `cycles=${Math.round(cycles)}`);
    // What the game's own setup expects of the machine (the registry), as a
    // configuration file: DOSBox's `config -set` rewrites the list of
    // startup commands it is itself run from, and the game's is lost.
    const conf = gameInfo() && gameInfo().dosbox;
    if (conf) { Module.FS.writeFile("/game.conf", conf); args.unshift("-conf", "/game.conf"); }
    Module.callMain([...args, "-c", cmd]);
    setInterval(keepSaves, 4000);
    setInterval(showPerformance, 1000);
    for (const ev of ["pagehide", "visibilitychange"]) window.addEventListener(ev, keepSaves);
    $("fullscreen").hidden = false;
    if (fromGesture) goFullscreen();
    status(source.game.multiplayer
      ? `Room ${cfg.code}. "Copy link" (step 2) gives friends the way in.`
      : `${source.game.title} is running.`);
  } catch (e) {
    running = false;
    hall.reopen();
    log("failed: " + (e && e.stack ? e.stack : e));
    status("Failed to start: " + (e && e.message ? e.message : e));
    renderTopAction();
  }
}

// -- how it is running -----------------------------------------------------------

// The game's own figures from the flight loop (src/cpu/wcnet_perf.h): frames
// per second, how much of each frame's time the game needed, whether the
// emulator keeps up with the clock, and how long a frame waits for the other
// players.  They answer "why is it slow": a load near 100% means the game
// needs more cycles in flight (?env.WCFLIGHTCYCLES=N), an emulator below 100%
// means this computer cannot deliver the cycles asked for (use fewer), and a
// long wait means the other player's computer or the connection is the brake.
function showPerformance() {
  const M = window.DOSBox;
  const el = $("perf");
  if (!M || !M._wc_web_perf || !M._wc_web_in_flight || !M._wc_web_in_flight() || M._wc_web_perf(0) < 0) { el.textContent = ""; return; }
  const fps = M._wc_web_perf(0), speed = M._wc_web_perf(1), wait = M._wc_web_perf(2), cycles = M._wc_web_perf(3), load = M._wc_web_perf(5);
  // (No load figure for a game that paces itself, as Wing Commander II does.)
  // The host sets the pace and a wingman's game waits for the host's frames
  // as a matter of course; it is the host's wait that says somebody is slow.
  const alone = !lobby.game || lobby.game.players.filter((p) => p.occupied).length < 2;
  const parts = [`${fps.toFixed(1)} frames/s`, load < 0 ? `${cycles} cycles` : `game busy ${Math.round(100 * load)}% of the time at ${cycles} cycles`,
                 `emulator at ${Math.round(100 * speed)}% of real time`];
  if (!alone) parts.push(isHost() ? `${wait.toFixed(0)} ms/frame waiting for the other players` : "following the host's pace");
  const link = linkState();
  if (!alone && link) parts.push(link);
  const warn = speed < 0.93 ? "This computer cannot keep up with the emulated CPU: fewer cycles would run smoother (Ctrl+F11)."
             : load > 0.95 ? "The game needs more CPU than it is given: try ?env.WCFLIGHTCYCLES=20000 in the address."
             : (!alone && isHost() && wait > 10) ? "Another player's computer or connection is holding the game back." : "";
  el.innerHTML = esc(parts.join(" · ")) + (warn ? ` <span class="warn">${esc(warn)}</span>` : "");
}

// The running game's word on the connection (src/cpu/wcnet_hooks.cpp,
// wc_web_link): the exchange mode in force and the round trip, or "".
function linkState() {
  const M = window.DOSBox;
  if (!running || !M || !M._wc_web_link || !lobby.game || lobby.game.players.filter((p) => p.occupied).length < 2) return "";
  const mode = M._wc_web_link(0), setting = M._wc_web_link(1), rtt = M._wc_web_link(2);
  const name = mode === 2 ? "high-latency mode" : "low-latency mode";
  return `${name}${setting === 0 ? " (auto)" : ""}${rtt >= 0 ? `, round trip ${Math.round(rtt)} ms` : ""}`;
}

// -- controllers ---------------------------------------------------------------

// A game controller chosen in this window drives the running game's mouse and
// keyboard (web/gamepad.js); the registry says where the game's steering
// pointer rests and how far it reaches.
// Where a controller's stick rests the game's pointer and how far it moves
// it: the registry's figures (Wing Commander: a fixed point of the screen),
// or the running game's own (`fromGame`, Wing Commander II: the middle of
// the cockpit's window, another in every ship and every turret, and the
// distances at which its steps of turn begin; src/cpu/wcnet_hooks.cpp,
// steer_info).  The game's are in units of the mouse's range, 640 x 200.
function steeringPointer() {
  const p = (gameInfo() && gameInfo().pointer) || null;
  if (!p || !p.fromGame) return p;
  const M = running && window.DOSBox;
  if (!M || !M._wc_web_steer) return null;
  const [left, top, right, bottom] = [0, 1, 2, 3].map((i) => M._wc_web_steer(0, i));
  if (!(right > left && bottom > top && left >= 0 && top >= 0)) return null;
  const steps = (what, range) => { const s = []; for (let i = 0, v; (v = M._wc_web_steer(what, i)) >= 0; i++) s.push(v / range); return s; };
  return { x: (left + right) / 2 / 639, y: (top + bottom) / 2 / 199, rx: (right - left) / 2 / 639, ry: (bottom - top) / 2 / 199,
           stepsX: steps(1, 639), stepsY: steps(2, 199), edgeX: M._wc_web_steer(3, 0) / 639, edgeY: M._wc_web_steer(3, 1) / 199, top: M._wc_web_steer(3, 2) };
}

// The public lobby (web/hall.js): RetroVega's preview pilots have their own;
// ?hall=CODE is another one, for tests.
const lobbyName = query.get("hall") || (retrovega ? "RV-LOBBY" : undefined);
if (lobbyName) for (const el of document.querySelectorAll("code.lobbyName")) el.textContent = lobbyName + (el.dataset.n || "");
if (retrovega) {
  $("hallNeeds").textContent = "Pick a game under Play (RetroVega is built in) or load Wing Commander or Wing Commander II (step 1) to enter: this page connects to the lobby only with a game to fly.";
  for (const el of document.querySelectorAll("#dropzone .loaded")) el.firstChild.textContent = "RetroVega is built in (under Play). Wing Commander or Wing Commander II: drop a ";
}
const hall = initHall({
  code: lobbyName,
  server: serverUrl,
  link: roomLink,
  name: () => $("callsign").value,
  ready: lobbyReady,
  tag: () => gameTag(),
  title: (tag) => (gameByTag(tag) ? gameByTag(tag).title : tag),
  // (A room code is dimmed when nothing here runs its game: the loaded files or a saved copy.)
  fits: (tag) => !source || !gameByTag(tag) || !!holderOf(gameByTag(tag).id),
  onCode: (code) => void joinFromLobby(code),
  room: myRoom,
  log,
});
window.__wcHall = hall;  // (for the page tests)

// The voice control, told of the room by the lobby code above.
const voice = initVoice({
  game: () => lobby.game,
  names: () => lobby.names,
  chatLine: (text, cls) => chatLine(esc(text), cls),
  log,
  announce: () => { if (lobby.game) sayHello(); },
  signal: (id, data) => sendLobby({ t: "voice", data }, id),
});
window.__wcVoiceLinks = voice.links;  // (for the page tests)
window.__wcVoiceReport = voice.report;

initControls({
  module: () => (running && window.DOSBox) || null,
  // (A controller button without a key of its own is the page's: push to talk.)
  onAction: (id, down) => { if (id === "ptt") voice.ptt(down); },
  pointer: steeringPointer,
  log,
});

// -- go ------------------------------------------------------------------------

void initSources();
// A shared link (?room=CODE) puts a pilot with a game to fly straight into
// the room, as soon as the game files are there (setSource).  A page
// without them joins nothing by itself: whoever opens the link, or whatever
// does, has said nothing to the lobby server until a game is loaded or
// "Join room" is pressed.
function joinLinked() { if (linkWaits() && lobbyReady()) void joinRoom(); }
updateActions();
// The public lobby is where a pilot with a game to fly is unless it left:
// hall.auto says when not (?lobby=off stays out this once, for tests).  The
// page enters when the game files are there (setSource), and not before: a
// visitor who has none costs the lobby server nothing.
if (hall.auto({ link: !!query.get("room"), off: query.get("lobby") === "off" })) hall.enterWhenReady();
