// The touch screen's controls (a phone's; any screen with touch): KEYS raises the device's own keyboard and types
// into the game, ESC is the game's Esc (a phone has none), FIRE is the trigger held (Space) for flight.  Shown only
// where there is touch, while a game runs: a row under the picture, or its corners in full screen (the page puts
// #screen in full screen, so they come along).
//
// The keyboard is a hidden text field in #screen: focusing it is what makes a phone show its keyboard.  What it
// types reaches the game as key presses through the page's send(name, down), by gamepad.js's KEY names (the
// emulator's queue, wc_web_key, on the DOSBox page; the PC's scan codes on RetroVega's own).  Phone keyboards compose words and correct them, and say little in their key events (Android's are mostly
// "Unidentified"), so the field's value is followed instead: each change is sent as the backspaces and the letters
// that turn what was typed into what is there now.  Enter, Esc, Tab and the arrows come as key events and are sent as
// they are; none of the field's key events reach the emulator's own listener (it would type them twice).
// A character as the key that types it on a US keyboard, and whether Shift is held for it.
const SHIFTED = { "!": "1", "@": "2", "#": "3", "$": "4", "%": "5", "^": "6", "&": "7", "*": "8", "(": "9", ")": "0",
                  "_": "minus", "+": "equals", "?": "slash", ":": "semicolon", '"': "quote", "<": "comma",
                  ">": "period", "{": "leftbracket", "}": "rightbracket", "|": "backslash", "~": "grave" };
const PLAIN = { " ": "space", "-": "minus", "=": "equals", ".": "period", ",": "comma", "/": "slash", ";": "semicolon",
                "'": "quote", "[": "leftbracket", "]": "rightbracket", "\\": "backslash", "`": "grave" };
function keyOf(ch) {
  if (/^[a-z0-9]$/.test(ch)) return { name: ch, shift: false };
  if (/^[A-Z]$/.test(ch)) return { name: ch.toLowerCase(), shift: true };
  if (PLAIN[ch]) return { name: PLAIN[ch], shift: false };
  if (SHIFTED[ch]) return { name: SHIFTED[ch], shift: true };
  return null;                          // (é, emoji: the game's keyboard has none)
}
const NAMED = { Enter: "enter", Escape: "esc", Tab: "tab", ArrowLeft: "left", ArrowRight: "right", ArrowUp: "up",
                ArrowDown: "down", Delete: "delete" };

// send(name, down): a key by its KEY name to the game, false when no game takes keys (what is waiting is dropped)
export function initTouch({ send: sendKey, log }) {
  const bar = document.getElementById("touch");
  const field = document.getElementById("touchType");
  const keysBtn = document.getElementById("touchKeys");
  const touchy = () => navigator.maxTouchPoints > 0 || matchMedia("(pointer: coarse)").matches;

  // the keys go out a few at a time: DOSBox's keyboard holds 32 codes, and a word with capitals is many
  const queue = [];
  let pumping = false;
  function pump() {
    for (let i = 0; i < 6 && queue.length; i++) {
      const [name, down] = queue.shift();
      if (sendKey(name, down) === false) { queue.length = 0; break; }
    }
    if (queue.length) setTimeout(pump, 30);
    else pumping = false;
  }
  function send(name, down) {
    queue.push([name, down ? 1 : 0]);
    if (!pumping) { pumping = true; pump(); }
  }
  function tap(name, shift) {
    if (shift) send("leftshift", true);
    send(name, true);
    send(name, false);
    if (shift) send("leftshift", false);
  }
  function type(text) { for (const ch of text) { const k = keyOf(ch); if (k) tap(k.name, k.shift); } }

  // the field: what has been sent since it was last emptied, against what it holds now
  let sent = "";
  function follow() {
    const now = field.value;
    let p = 0;
    while (p < sent.length && p < now.length && sent[p] === now[p]) p++;
    for (let i = p; i < sent.length; i++) tap("backspace", false);
    type(now.slice(p));
    sent = now;
  }
  function empty() { field.value = ""; sent = ""; }
  field.addEventListener("input", (e) => { e.stopPropagation(); follow(); });
  field.addEventListener("compositionend", () => { if (sent.length > 40) empty(); });
  field.addEventListener("keydown", (e) => {
    e.stopPropagation();
    const named = NAMED[e.key];
    if (named) {
      e.preventDefault();
      if (named === "enter") { follow(); empty(); }
      tap(named, false);
    } else if (e.key === "Backspace" && !field.value) {
      e.preventDefault();               // (nothing in the field to take back: the game's last letter goes)
      tap("backspace", false);
    }
  });
  for (const ev of ["keyup", "keypress"]) field.addEventListener(ev, (e) => e.stopPropagation());
  field.addEventListener("blur", () => { empty(); keysBtn.classList.remove("on"); });
  field.addEventListener("focus", () => keysBtn.classList.add("on"));

  // the buttons keep the focus where it is (a tap on one would close the keyboard)
  for (const b of bar.querySelectorAll("button")) b.addEventListener("pointerdown", (e) => e.preventDefault());
  keysBtn.addEventListener("click", () => {
    if (document.activeElement === field) field.blur();
    else { empty(); field.focus({ preventScroll: true }); }
  });
  document.getElementById("touchEsc").addEventListener("click", () => tap("esc", false));
  const fire = document.getElementById("touchFire");
  let firing = false;
  const fireOn = (e) => { e.preventDefault(); if (!firing) { firing = true; fire.classList.add("on"); send("space", true); } };
  const fireOff = () => { if (firing) { firing = false; fire.classList.remove("on"); send("space", false); } };
  fire.addEventListener("pointerdown", fireOn);
  for (const ev of ["pointerup", "pointercancel", "pointerleave", "lostpointercapture"]) fire.addEventListener(ev, fireOff);
  fire.addEventListener("contextmenu", (e) => e.preventDefault());

  return {
    // while a game runs, on a screen with touch
    show(on) {
      bar.hidden = !(on && touchy());
      if (bar.hidden) { fireOff(); field.blur(); }
      if (on && !bar.hidden) log("touch: KEYS (the keyboard), ESC and FIRE under the picture");
    },
  };
}
