// Game controllers for the browser build.
//
// A controller is presented to the game as its mouse and its keyboard: the
// sticks set the pointer (Wing Commander steers by how far the pointer is
// from a neutral point of the cockpit view), the buttons press keys.  The
// page picks one controller per window, so two windows side by side can each
// take their own (or one a controller and the other the keyboard and mouse).
//
// The mapping is a table of game actions, each bound to a button (or an axis
// used as a button), plus four analog inputs each bound to an axis that can
// be inverted.  It defaults to the standard gamepad layout and is kept in
// local storage; the choice of controller is per window (session storage).
//
// The emulator side is four exports (src/cpu/wcnet_hooks.cpp): wc_web_key,
// wc_web_pointer, wc_web_mouse_button and wc_web_in_flight.
//
// A game that reads a joystick itself (the registry's `joystick`) is offered
// each pad "built in" as well: the pad is then the game port's joystick,
// which the game makes sense of its own way (Wing Commander: button 1 the
// guns, both buttons a missile, button 2 with the stick rolls and changes
// speed, a double click of it the afterburner), and the buttons bound to
// other actions still press their keys.  The game looks for a joystick once,
// when it starts, so the page puts the port in the launch's configuration
// only when a built-in pad is chosen then (host.joystickPort); otherwise the
// port stays empty and a pad picked "built in" later flies as the mouse and
// keyboard until the next launch.  Two more exports: wc_web_joystick (the
// stick on or off the port) and wc_web_joy (where it is, its buttons).

// DOSBox's KBD_KEYS numbering (include/keyboard.h; a static_assert next to
// wc_web_key checks the order).
export const KEY = (() => {
  const k = {};
  let n = 1;
  for (const c of "1234567890" + "qwertyuiop" + "asdfghjklz" + "xcvbnm") k[c] = n++;
  for (let i = 1; i <= 12; i++) k["f" + i] = n++;
  for (const name of ["esc", "tab", "backspace", "enter", "space", "leftalt", "rightalt", "leftctrl", "rightctrl",
                      "leftshift", "rightshift", "capslock", "scrolllock", "numlock", "grave", "minus", "equals",
                      "backslash", "leftbracket", "rightbracket", "semicolon", "quote", "period", "comma", "slash",
                      "extra_lt_gt", "printscreen", "pause", "insert", "home", "pageup", "delete", "end", "pagedown",
                      "left", "up", "down", "right"]) k[name] = n++;
  return k;
})();

// What a button can do: the keys it holds down, and whether it is also the
// pointer's click outside flight (menus, the barracks).
export const ACTIONS = [
  // (A built-in pad's game-port buttons, which the game makes sense of; an
  // action bound to the same button is not pressed with them.)
  { id: "joy1", label: "Joystick button 1 (built in)", keys: [], port: 1, bind: { button: 0 } },
  { id: "joy2", label: "Joystick button 2 (built in)", keys: [], port: 2, bind: { button: 1 } },
  { id: "guns", label: "Fire guns", keys: ["space"], click: true, bind: { button: 0 } },
  { id: "missile", label: "Fire missile", keys: ["enter"], bind: { button: 1 } },
  { id: "target", label: "Change target", keys: ["t"], bind: { button: 2 } },
  { id: "weapon", label: "Change weapon", keys: ["w"], bind: { button: 3 } },
  { id: "faster", label: "Speed up", keys: ["equals"], bind: { button: 4 } },
  { id: "slower", label: "Slow down", keys: ["minus"], bind: { button: 5 } },
  { id: "burner", label: "Afterburner", keys: ["tab"], bind: { button: 6 }, analog: true },
  { id: "nav", label: "Navigation map", keys: ["n"], bind: { button: 8 } },
  { id: "auto", label: "Autopilot", keys: ["a"], bind: { button: 9 } },
  // (A drone riding behind the leader is the leader's copilot: the Up and
  // Down keys shift the leader's shields, Fire guns puts shields into the
  // guns, Fire missile guns into the weakest shield, Speed up and Slow down
  // set the cruising speed.  For a pilot the two arrows are the keyboard's
  // nose up and down.)
  { id: "up", label: "Up (copilot: shields to the rear)", keys: ["up"], bind: { button: 12 } },
  { id: "down", label: "Down (copilot: shields to the front)", keys: ["down"], bind: { button: 13 } },
  { id: "gunsel", label: "Change guns", keys: ["g"], bind: null },
  { id: "lock", label: "Lock target", keys: ["l"], bind: null },
  { id: "comms", label: "Communications", keys: ["c"], bind: null },
  { id: "esc", label: "Esc (skip a scene)", keys: ["esc"], bind: null },
  // (No key: the page's own action, voice push to talk, through host.onAction.)
  { id: "ptt", label: "Push to talk (voice)", keys: [], bind: { button: 10 } },
];
// Analog inputs.  Not inverted means: stick right turns and rolls right, and
// pushing the stick up raises the nose; pitch is inverted by default, the
// way a flight stick works (pull back to climb).
export const AXES = [
  { id: "yaw", label: "Turn left / right", bind: { axis: 0, invert: false } },
  { id: "pitch", label: "Pitch", bind: { axis: 1, invert: true } },
  { id: "pitch2", label: "Pitch, second stick", bind: { axis: 3, invert: true } },
  { id: "roll", label: "Roll", bind: { axis: 2, invert: false } },
];
const ROLL_KEYS = { left: "comma", right: "period" };
const BUTTON_NAMES = ["A", "B", "X", "Y", "L1", "R1", "L2 (left trigger)", "R2 (right trigger)", "Back", "Start",
                      "L3 (left stick)", "R3 (right stick)", "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Home"];
const AXIS_NAMES = ["Left stick, sideways", "Left stick, up/down", "Right stick, sideways", "Right stick, up/down"];
const CONFIG_KEY = "wcpad:config";
const CHOICE_KEY = "wcpad:choice";
const NONE = "none";
// A built-in pad's choice is its key after this.
const BUILT_IN = "joy|";
const isBuiltIn = (choice) => !!choice && choice.startsWith(BUILT_IN);
const padOf = (choice) => (isBuiltIn(choice) ? choice.slice(BUILT_IN.length) : choice);

// The stick's sensitivity is the share of the game's full turn that full
// stick asks for: 100%, the stick being analog anyway (it was 70%, which
// stopped at step 5 of the game's 8).  Version 2 of the saved settings:
// a sensitivity saved by version 1 was the old default, not a choice.
// The dead zone is 5% (it was 15%): the system has one of its own before
// the browser sees the stick (Linux: a sixteenth of the axis for most USB
// sticks), and the two add up.  Version 3: a dead zone of 15% saved by an
// earlier version was the old default.
export function defaultConfig() {
  // (pointer: the game's own crosshair stays on the screen in flight.  It
  // is what a mouse steers by; a stick moves it too, for nothing.  curve:
  // the stick's travel past the dead zone squared, half stick a quarter of
  // the full turn: more of the travel for the slow turns.)
  const cfg = { version: 3, buttons: {}, axes: {}, deadzone: 0.05, burner: 0.9, sensitivity: 1.0, pointer: false, curve: false };
  for (const a of ACTIONS) cfg.buttons[a.id] = a.bind ? { ...a.bind } : null;
  for (const a of AXES) cfg.axes[a.id] = { ...a.bind };
  return cfg;
}
function loadConfig() {
  const cfg = defaultConfig();
  try {
    const saved = JSON.parse(localStorage.getItem(CONFIG_KEY) || "null");
    if (saved && saved.version >= 1 && saved.version <= 3) {
      for (const a of ACTIONS) if (a.id in (saved.buttons || {})) cfg.buttons[a.id] = saved.buttons[a.id];
      for (const a of AXES) if (saved.axes && saved.axes[a.id]) cfg.axes[a.id] = saved.axes[a.id];
      for (const k of saved.version === 1 ? ["deadzone", "burner"] : ["deadzone", "burner", "sensitivity"]) if (typeof saved[k] === "number") cfg[k] = saved[k];
      if (saved.version < 3 && saved.deadzone === 0.15) cfg.deadzone = defaultConfig().deadzone;
      for (const k of ["pointer", "curve"]) if (typeof saved[k] === "boolean") cfg[k] = saved[k];
    }
  } catch (e) { /* defaults */ }
  return cfg;
}
function saveConfig(cfg) {
  try { localStorage.setItem(CONFIG_KEY, JSON.stringify(cfg)); } catch (e) { /* not kept, still used */ }
}

const padKey = (pad) => `${pad.index}:${pad.id}`;
const clamp = (v, lo, hi) => (v < lo ? lo : v > hi ? hi : v);
function pads() {
  try { return Array.from(navigator.getGamepads ? navigator.getGamepads() : []).filter((p) => p && p.connected); }
  catch (e) { return []; }
}

// Wing Commander II turns in steps by how far the pointer is from the middle
// of its view: step 1, 2, ... begins at steps[0], steps[1], ... and the last
// and fastest, worth `top`, is the view's edge (`reach` from the middle,
// taken from `edge` before it; a small view has no room for the later
// steps).  The stick asks for a share v (-1..1) of the full turn and gets
// the step nearest to it, with the pointer in the middle of that step's
// band: a stick at rest is no turn at all in any cockpit, and the same
// stick is the same turn in a cockpit with a large window and a small one.
// A stick that is not at rest is step 1 at least: the dead zone before it
// is the page's (the nearest step alone made another of half a step).
export function stepped(v, steps, reach, edge, top) {
  const inside = steps.filter((from) => from < reach - edge).length;
  const worth = (n) => (n > inside ? top : n);     // 0: no turn, 1..inside, inside + 1: the edge
  const want = Math.abs(v) * top;
  let best = 0;
  for (let n = 1; n <= inside + 1; n++) if (Math.abs(worth(n) - want) < Math.abs(worth(best) - want)) best = n;
  if (best === 0) {
    if (v === 0) return 0;
    best = 1;
  }
  if (best > inside) return Math.sign(v) * reach;
  const from = steps[best - 1];
  const to = Math.min(reach - edge, best < steps.length ? steps[best] : from + 2 * (from - (steps[best - 2] || 0)));
  return Math.sign(v) * (from + to) / 2;
}

// initControls wires the controller picker and the mapping table (elements
// by id, see index.html) and starts polling.  `host` supplies:
//   module():  the running emulator's Module, or null
//   pointer(): the game's steering pointer {x, y, rx, ry} as fractions of the
//              mouse range (neutral point and reach), or null for the centre;
//              with stepsX / stepsY / edgeX / edgeY / top for a game that
//              turns in steps (see stepped below)
//   log(line)
//   onAction(id, down): an action of the page's own (no key), such as voice
//              push to talk, pressed or released (optional)
//   builtIn(): whether the game reads a joystick itself (pads are offered
//              "built in" too)
//   joystickPort(): null before the launch, then whether it put the game
//              port in (a built-in pad was chosen then)
export function initControls(host) {
  const $ = (id) => document.getElementById(id);
  let cfg = loadConfig();
  let choice = null;            // padKey of this window's controller, NONE, or null (nothing chosen yet)
  try { choice = sessionStorage.getItem(CHOICE_KEY); } catch (e) { /* per page load then */ }
  let capture = null;           // { kind: "button" | "axis", id, axes: [...], down: Set } while remapping
  let listed = "";              // the controllers the picker currently shows
  const held = new Map();       // action id -> true while its keys are down
  let rollHeld = null;          // "left" | "right" | null
  let clickHeld = false;
  let lastPointer = { x: -1, y: -1, at: 0 };
  let lastJoy = { x: 0, y: 0, b: 0, at: 0 };  // what the game port's stick was last told
  let menu = { x: 0.5, y: 0.5, wasFlight: false, at: 0 };
  const others = new Map();     // padKey -> time another window last claimed it
  let channel = null;
  try {
    channel = new BroadcastChannel("wcpad");
    channel.onmessage = (ev) => {
      const m = ev.data || {};
      if (m.t === "claim" && m.key) others.set(m.key, Date.now());
      else if (m.t === "release" && m.key) others.delete(m.key);
    };
  } catch (e) { /* no coordination between windows */ }
  let lastClaim = 0;
  let told = { M: null, off: false, joyM: null, joy: false };   // what the emulator was last told: the crosshair, the port

  const selected = () => (choice && choice !== NONE ? pads().find((p) => padKey(p) === padOf(choice)) || null : null);
  const offered = () => !!(host.builtIn && host.builtIn());
  // Built in, and the port is there: the pad is the game's joystick.
  const onPort = () => isBuiltIn(choice) && offered() && !!host.joystickPort && host.joystickPort() === true;
  const usedElsewhere = (key) => (Date.now() - (others.get(key) || 0)) < 5000;

  function choose(key, how) {
    if (key === choice) return;
    if (choice && choice !== NONE && channel && padOf(choice) !== padOf(key)) channel.postMessage({ t: "release", key: padOf(choice) });
    releaseAll();
    choice = key;
    try { if (key) sessionStorage.setItem(CHOICE_KEY, key); else sessionStorage.removeItem(CHOICE_KEY); } catch (e) { /* fine */ }
    capture = null;
    lastClaim = 0;
    listed = "";
    if (key && key !== NONE) host.log(`controller: ${padOf(key).slice(padOf(key).indexOf(":") + 1)}${isBuiltIn(key) ? " (built in)" : ""} ${how}`);
    render();
  }

  // -- the picker and the mapping table ----------------------------------------

  function bindingText(b) {
    if (!b) return "not set";
    if (b.button !== undefined) return `${BUTTON_NAMES[b.button] || "Button " + b.button}`;
    return `${AXIS_NAMES[b.axis] || "Axis " + b.axis} ${b.dir < 0 ? "−" : "+"}`;
  }
  const signatureOf = (list) => list.map(padKey).join("|") + "/" + choice + "/" + offered() + "/" + list.map((p) => usedElsewhere(padKey(p))).join("");
  const sameBind = (a, b) => !!a && !!b && (a.button !== undefined ? a.button === b.button : a.axis === b.axis && a.dir === b.dir);
  function render() {
    const sel = $("padSelect");
    const list = pads();
    const signature = signatureOf(list);
    if (signature !== listed) {
      listed = signature;
      sel.innerHTML = "";
      const add = (value, text) => { const o = document.createElement("option"); o.value = value; o.textContent = text; sel.appendChild(o); };
      add(NONE, "Keyboard + mouse");
      for (const p of list) {
        const busy = usedElsewhere(padKey(p)) ? " (in use in another window)" : "";
        add(padKey(p), `${p.index + 1}: ${p.id}${busy}`);
        if (offered()) add(BUILT_IN + padKey(p), `Built in: ${p.index + 1}: ${p.id}${busy}`);
      }
      // (A pad picked built in for a game that has no joystick of its own
      // is that game's mouse and keyboard.)
      const shown = isBuiltIn(choice) && !offered() ? padOf(choice) : choice;
      if (shown && shown !== NONE && !list.some((p) => padKey(p) === padOf(shown))) {
        add(shown, `${isBuiltIn(shown) ? "Built in: " : ""}${padOf(shown).slice(padOf(shown).indexOf(":") + 1)} (not connected)`);
      }
      sel.value = shown || NONE;
    }
    const pad = selected();
    const builtIn = isBuiltIn(choice) && offered();
    $("padHint").textContent = pad
      ? (builtIn && host.joystickPort && host.joystickPort() === false
          ? "Built in: the game looks for a joystick when it starts, so this pad is its joystick from the next launch; until then it is its mouse and keyboard."
          : builtIn
          ? "Built in: the game reads this pad as the joystick on its game port, its own way: button 1 fires the guns, both buttons a missile; hold button 2 and move the stick to roll and change speed, double-click it for the afterburner. The other buttons still press their keys."
          : pad.mapping === "standard" ? "Using the standard gamepad layout; change any button or axis below."
                                       : "This controller does not report the standard layout: check the buttons and axes below.")
      : (list.length ? "Pick a controller, or press one of its buttons while this window has the focus."
                     : "No controller seen yet: plug one in and press one of its buttons.");

    const rows = [];
    for (const a of AXES) {
      const b = cfg.axes[a.id];
      const busy = capture && capture.kind === "axis" && capture.id === a.id;
      rows.push(`<tr><td>${a.label}</td><td>${busy ? "move that stick…" : (AXIS_NAMES[b.axis] || "Axis " + b.axis)}</td>` +
                `<td><label class="check"><input type="checkbox" data-invert="${a.id}"${b.invert ? " checked" : ""}> invert</label></td>` +
                `<td><button type="button" class="secondary" data-axis="${a.id}">${busy ? "Cancel" : "Change"}</button></td></tr>`);
    }
    for (const a of ACTIONS) {
      const b = cfg.buttons[a.id];
      const busy = capture && capture.kind === "button" && capture.id === a.id;
      // Greyed: the joystick's buttons but for a built-in pad, and with
      // one, an action on the same button (the game has it).
      const off = a.port ? !builtIn : builtIn && !a.port && [cfg.buttons.joy1, cfg.buttons.joy2].some((j) => sameBind(j, b));
      const why = a.port ? "Only for a pad picked built in" : "The joystick's button here: the game has it";
      const dis = a.port && off ? " disabled" : "";
      rows.push(`<tr${off ? ` class="off" title="${why}"` : ""}><td>${a.label}</td><td>${busy ? "press a button…" : bindingText(b)}</td><td></td>` +
                `<td><button type="button" class="secondary" data-button="${a.id}"${dis}>${busy ? "Cancel" : "Change"}</button>` +
                (b && !busy ? ` <button type="button" class="secondary" data-clear="${a.id}"${dis}>Clear</button>` : "") + `</td></tr>`);
    }
    $("padRows").innerHTML = rows.join("");
    for (const [id, key, scale] of [["padSensitivity", "sensitivity", 100], ["padDeadzone", "deadzone", 100], ["padBurner", "burner", 100]]) {
      $(id).value = String(Math.round(cfg[key] * scale));
      $(id + "Value").textContent = `${Math.round(cfg[key] * scale)}%`;
    }
    $("padPointer").checked = cfg.pointer;
    $("padCurve").checked = cfg.curve;
  }

  $("padSelect").addEventListener("change", (e) => choose(e.target.value, "selected"));
  $("padRows").addEventListener("click", (e) => {
    const t = e.target;
    if (!(t instanceof HTMLElement)) return;
    const start = (kind, id) => {
      const pad = selected();
      if (capture && capture.kind === kind && capture.id === id) capture = null;
      else if (pad) capture = { kind, id, axes: Array.from(pad.axes), down: new Set(pad.buttons.map((b, i) => (b.pressed ? i : -1)).filter((i) => i >= 0)) };
      render();
    };
    if (t.dataset.axis) start("axis", t.dataset.axis);
    else if (t.dataset.button) start("button", t.dataset.button);
    else if (t.dataset.clear) { cfg.buttons[t.dataset.clear] = null; saveConfig(cfg); releaseAll(); render(); }
  });
  $("padRows").addEventListener("change", (e) => {
    const t = e.target;
    if (t instanceof HTMLInputElement && t.dataset.invert) { cfg.axes[t.dataset.invert].invert = t.checked; saveConfig(cfg); }
  });
  for (const [id, key] of [["padSensitivity", "sensitivity"], ["padDeadzone", "deadzone"], ["padBurner", "burner"]]) {
    $(id).addEventListener("input", (e) => { cfg[key] = Number(e.target.value) / 100; $(id + "Value").textContent = `${e.target.value}%`; saveConfig(cfg); });
  }
  $("padPointer").addEventListener("change", (e) => { cfg.pointer = e.target.checked; saveConfig(cfg); });
  $("padCurve").addEventListener("change", (e) => { cfg.curve = e.target.checked; saveConfig(cfg); });
  $("padReset").addEventListener("click", () => { cfg = defaultConfig(); saveConfig(cfg); capture = null; releaseAll(); render(); });

  // -- reading the controller ---------------------------------------------------

  const shaped = (v) => {                      // dead zone, then the rest of the travel rescaled to 0..1 (and squared)
    const d = cfg.deadzone;
    const m = Math.abs(v);
    if (m <= d) return 0;
    const r = Math.min(1, (m - d) / (1 - d));
    return Math.sign(v) * (cfg.curve ? r * r : r);
  };
  const axisValue = (pad, id) => {
    const b = cfg.axes[id];
    const v = shaped(pad.axes[b.axis] || 0);
    return b.invert ? -v : v;
  };
  function buttonValue(pad, b) {              // 0..1: how far the bound button (or axis) is pressed
    if (!b) return 0;
    if (b.button !== undefined) { const x = pad.buttons[b.button]; return x ? (x.value || (x.pressed ? 1 : 0)) : 0; }
    return Math.max(0, (pad.axes[b.axis] || 0) * (b.dir < 0 ? -1 : 1));
  }

  function sendKeys(M, names, down) { for (const n of names) M._wc_web_key(KEY[n], down ? 1 : 0); }
  function releaseAll() {
    const M = host.module();
    if (M && M._wc_web_key) {
      for (const a of ACTIONS) if (held.get(a.id)) sendKeys(M, a.keys, false);
      if (rollHeld) sendKeys(M, [ROLL_KEYS[rollHeld]], false);
      if (clickHeld) M._wc_web_mouse_button(0, 0);
    }
    held.clear(); rollHeld = null; clickHeld = false;
    lastJoy = { x: 0, y: 0, b: 0, at: 0 };
  }

  function capturing(pad) {
    if (capture.kind === "button") {
      const i = pad.buttons.findIndex((b, n) => b.pressed && !capture.down.has(n));
      if (i >= 0) cfg.buttons[capture.id] = { button: i };
      else {
        const a = pad.axes.findIndex((v, n) => Math.abs(v - (capture.axes[n] || 0)) > 0.6);
        if (a < 0) { for (const n of Array.from(capture.down)) if (!pad.buttons[n] || !pad.buttons[n].pressed) capture.down.delete(n); return; }
        cfg.buttons[capture.id] = { axis: a, dir: pad.axes[a] - (capture.axes[a] || 0) < 0 ? -1 : 1 };
      }
    } else {
      const a = pad.axes.findIndex((v, n) => Math.abs(v - (capture.axes[n] || 0)) > 0.6);
      if (a < 0) return;
      cfg.axes[capture.id].axis = a;
    }
    capture = null;
    saveConfig(cfg);
    render();
  }

  function drive(pad, M, now) {
    const flying = M._wc_web_in_flight() !== 0;
    const port = onPort() && !!M._wc_web_joy;
    const yaw = axisValue(pad, "yaw");
    // Pitch: positive raises the nose.  The game turns towards its pointer,
    // so the nose comes up when the pointer is above the neutral point
    // (checked on screen against the carrier, not just in the game's vectors).
    const pitchOf = (id) => { const b = cfg.axes[id]; const v = shaped(pad.axes[b.axis] || 0); return b.invert ? v : -v; };
    const pitch = clamp(pitchOf("pitch") + pitchOf("pitch2"), -1, 1);
    const roll = axisValue(pad, "roll");
    let x, y, refresh;
    if (port) {
      // The game port's stick, -1..1: right, and back (towards the pilot),
      // which raises the nose, are positive.  The game reads it everywhere,
      // the menus too (its pointer), so the page leaves the mouse alone.
      const jx = clamp(yaw * cfg.sensitivity, -1, 1);
      const jy = clamp(pitch * cfg.sensitivity, -1, 1);
      const jb = (buttonValue(pad, cfg.buttons.joy1) >= 0.5 ? 1 : 0) | (buttonValue(pad, cfg.buttons.joy2) >= 0.5 ? 2 : 0);
      if (Math.abs(jx - lastJoy.x) > 0.001 || Math.abs(jy - lastJoy.y) > 0.001 || jb !== lastJoy.b || now - lastJoy.at > 200) {
        M._wc_web_joy(jx, jy, jb);
        lastJoy = { x: jx, y: jy, b: jb, at: now };
      }
    } else if (flying) {
      const p = host.pointer() || { x: 0.5, y: 0.5, rx: 0.5, ry: 0.5 };
      if (p.stepsX) {
        x = p.x + stepped(yaw * cfg.sensitivity, p.stepsX, p.rx, p.edgeX, p.top);
        y = p.y - stepped(pitch * cfg.sensitivity, p.stepsY, p.ry, p.edgeY, p.top);
      } else {
        x = p.x + yaw * p.rx * cfg.sensitivity;
        y = p.y - pitch * p.ry * cfg.sensitivity;
      }
      refresh = 200;             // the game re-centres the pointer by itself now and then
      menu.wasFlight = true;
    } else {
      // Menus and the barracks: the stick moves the pointer like a mouse.
      if (menu.wasFlight) { menu.x = 0.5; menu.y = 0.5; menu.wasFlight = false; }
      const dt = Math.min(0.1, (now - (menu.at || now)) / 1000);
      const rawX = shaped(pad.axes[cfg.axes.yaw.axis] || 0);
      const rawY = clamp(shaped(pad.axes[cfg.axes.pitch.axis] || 0) + shaped(pad.axes[cfg.axes.pitch2.axis] || 0), -1, 1);
      menu.x = clamp(menu.x + rawX * 0.6 * dt, 0, 1);
      menu.y = clamp(menu.y + rawY * 0.6 * dt, 0, 1);
      x = menu.x; y = menu.y; refresh = 1e9;
    }
    menu.at = now;
    if (!port && (Math.abs(x - lastPointer.x) > 0.001 || Math.abs(y - lastPointer.y) > 0.001 || now - lastPointer.at > refresh)) {
      M._wc_web_pointer(x, y);
      lastPointer = { x, y, at: now };
    }
    const wantRoll = flying && Math.abs(roll) > 0.5 ? (roll < 0 ? "left" : "right") : null;
    if (wantRoll !== rollHeld) {
      if (rollHeld) sendKeys(M, [ROLL_KEYS[rollHeld]], false);
      if (wantRoll) sendKeys(M, [ROLL_KEYS[wantRoll]], true);
      rollHeld = wantRoll;
    }
    const joyButtons = port ? [cfg.buttons.joy1, cfg.buttons.joy2] : [];
    for (const a of ACTIONS) {
      if (a.port) continue;
      const v = joyButtons.some((j) => sameBind(j, cfg.buttons[a.id])) ? 0 : buttonValue(pad, cfg.buttons[a.id]);
      // An analog trigger has to be nearly all the way down (the afterburner
      // drinks fuel); letting go needs a little less, so it does not flutter.
      const on = a.analog ? cfg.burner : 0.5;
      const down = held.get(a.id) ? v > on - 0.1 : v >= on;
      if (down !== !!held.get(a.id)) {
        if (a.keys.length) sendKeys(M, a.keys, down); else if (host.onAction) host.onAction(a.id, down);
        held.set(a.id, down);
      }
      if (a.click) {
        const click = down && !flying && !port;
        if (click !== clickHeld) { M._wc_web_mouse_button(0, click ? 1 : 0); clickHeld = click; }
      }
    }
  }

  function tick() {
    const now = performance.now();
    const list = pads();
    // Nothing chosen in this window yet: the first controller that presses a
    // button while the window has the focus becomes its controller.
    if (choice === null && document.hasFocus()) {
      const p = list.find((q) => !usedElsewhere(padKey(q)) && q.buttons.some((b) => b.pressed));
      if (p) choose(padKey(p), "picked up from its button press");
    }
    const pad = selected();
    if (channel && choice && choice !== NONE && pad && now - lastClaim > 2000) { channel.postMessage({ t: "claim", key: padOf(choice) }); lastClaim = now; }
    const M = host.module();
    // With a controller chosen in this window the cockpit's crosshair is not
    // drawn (src/cpu/wcnet_hooks.cpp, pointer_off), unless the pilot wants it.
    const off = !!choice && choice !== NONE && !cfg.pointer;
    if (M && M._wc_web_pointer_off && (told.M !== M || told.off !== off)) { M._wc_web_pointer_off(off ? 1 : 0); told = { M, off }; }
    // The game port's stick: only when the launch put the port in (a
    // built-in pad was chosen then), on while that pad is chosen and there.
    if (M && M._wc_web_joystick && host.joystickPort && host.joystickPort() === true) {
      const want = !!pad && onPort();
      if (told.joyM !== M || told.joy !== want) { M._wc_web_joystick(want ? 1 : 0); told.joyM = M; told.joy = want; lastJoy.at = 0; }
    }
    if (pad && capture) capturing(pad);
    else if (pad && M && M._wc_web_key && !capture) drive(pad, M, now);
    else if (held.size || rollHeld || clickHeld) releaseAll();
    if (signatureOf(list) !== listed) render();
  }
  window.addEventListener("gamepadconnected", () => render());
  window.addEventListener("gamepaddisconnected", () => { releaseAll(); render(); });
  window.addEventListener("pagehide", () => { if (channel && choice && choice !== NONE) channel.postMessage({ t: "release", key: padOf(choice) }); });
  render();
  // A timer rather than animation frames: a window that is visible but not
  // focused (the other half of a split screen) must keep reading its controller.
  setInterval(tick, 16);
  // builtIn(): a built-in pad is chosen for a game with a joystick of its
  // own (the launch then puts the game port in); render(): after the game
  // changes, which changes what is offered.
  return { config: () => cfg, choice: () => choice, builtIn: () => isBuiltIn(choice) && offered(), render };
}
