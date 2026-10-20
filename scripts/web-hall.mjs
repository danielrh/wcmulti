// Headless test of the public lobby (web/hall.js) and of room codes that
// name their game: pages enter the lobby (with a game the hooks know, and
// not without), talk, are held to its rules by
// their own page and by the others', advertise a room, and join it by a
// click on its code.  Driven by scripts/web-smoke.sh hall, which starts the
// lobby server and the web server; the pages use the server's wc.tar.gz
// (Wing Commander, with The Secret Missions 2 in the same directory).
//
//   node scripts/web-hall.mjs PAGE_URL LOBBY_URL
import { createRequire } from "node:module";
import path from "node:path";
import zlib from "node:zlib";
const [pageUrl, lobbyUrl] = process.argv.slice(2);
const require = createRequire(path.join(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
const browser = await chromium.launch({ headless: true, executablePath: process.env.CHROME || "/usr/bin/google-chrome", args: ["--no-sandbox"] });
const results = [];
const check = (what, ok, detail) => { results.push(!!ok); console.log(`${ok ? "ok  " : "FAIL"} ${what}${detail === undefined ? "" : ": " + JSON.stringify(detail)}`); };
const sleep = (s) => new Promise((r) => setTimeout(r, s * 1000));
const run = Math.random().toString(36).slice(2, 8).toUpperCase();
const logs = {};
const sockets = {};   // per page: the WebSockets it opened (the lobby server's are the only ones)

// busy: from how many pilots a lobby is held to a line every ten seconds
// on that page (8; the pages that try the ten seconds say 2).  A page with
// a game is in the lobby by itself; wait: false keeps it out (?lobby=off)
// until the test has set it up and presses "Enter the lobby".
async function open(name, { hall = "HALL-" + run, game = true, query = {}, busy = 0, wait = true } = {}) {
  const context = await browser.newContext();
  const page = await context.newPage();
  logs[name] = [];
  sockets[name] = [];
  page.on("console", (m) => logs[name].push(m.text()));
  page.on("websocket", (ws) => sockets[name].push(ws.url()));
  page.on("pageerror", (e) => console.log(`[${name}] PAGE ERROR: ${e.message}`));
  if (!game) await page.route("**/wc.tar.gz", (r) => r.abort());
  const u = new URL(pageUrl);
  for (const [k, v] of Object.entries({ server: lobbyUrl, callsign: name, hall, ...(wait ? {} : { lobby: "off" }), ...query })) u.searchParams.set(k, v);
  await page.goto(u.toString());
  if (game) await page.waitForFunction(() => document.getElementById("sourceStatus").dataset.state === "ready", null, { timeout: 60000 });
  if (busy) await page.evaluate((n) => window.__wcHall.test.configure({ busyFrom: n }), busy);
  return page;
}
const text = (page, id) => page.$eval("#" + id, (el) => el.innerText);
const value = (page, id) => page.$eval("#" + id, (el) => el.value);
const inside = (page) => page.waitForFunction(() => !document.getElementById("hallBody").hidden, null, { timeout: 30000 }).then(() => true, () => false);
const enter = async (page) => { await page.click("#hallEnter"); await page.waitForFunction(() => !document.getElementById("hallBody").hidden, null, { timeout: 30000 }); };
const pilots = (page, n) => page.waitForFunction((n) => new RegExp(`^${n} in the lobby`).test(document.getElementById("hallRoster").textContent), n, { timeout: 40000 }).then(() => true, () => false);
const say = async (page, line) => { await page.fill("#hallInput", line); await page.press("#hallInput", "Enter"); await sleep(0.4); };
const sees = (page, what, timeout = 8000) => page.waitForFunction((w) => document.getElementById("hallLog").innerText.includes(w), what, { timeout }).then(() => true, () => false);
const refill = (page) => page.evaluate(() => window.__wcHall.test.refill());
const status = (page) => text(page, "status");
// A .zip (stored, not compressed) of one file.
function zipOf(name, data) {
  const n = Buffer.from(name), crc = zlib.crc32(data);
  const local = Buffer.alloc(30), central = Buffer.alloc(46), end = Buffer.alloc(22);
  local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(20, 4); local.writeUInt32LE(crc, 14);
  local.writeUInt32LE(data.length, 18); local.writeUInt32LE(data.length, 22); local.writeUInt16LE(n.length, 26);
  central.writeUInt32LE(0x02014b50, 0); central.writeUInt16LE(20, 4); central.writeUInt16LE(20, 6); central.writeUInt32LE(crc, 16);
  central.writeUInt32LE(data.length, 20); central.writeUInt32LE(data.length, 24); central.writeUInt16LE(n.length, 28);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(1, 8); end.writeUInt16LE(1, 10);
  end.writeUInt32LE(46 + n.length, 12); end.writeUInt32LE(30 + n.length + data.length, 16);
  return Buffer.concat([local, n, data, central, n, end]);
}

// -- room codes follow the game ---------------------------------------------------
// A page without a game: its pilot would be in the lobby, and it has not
// said a word to the lobby server.
const none = await open("NOGAME", { game: false, hall: "GATE-" + run });
check("a room code before any game is WC- and four digits", /^WC-\d{4}$/.test(await value(none, "room")), await value(none, "room"));
await sleep(3);
const closed = (page) => page.evaluate(() => document.getElementById("hallBody").hidden && document.getElementById("hallEnter").disabled && !document.getElementById("hallNeeds").hidden);
check("without a game the lobby is closed, and says what it needs", (await closed(none)) && /Load Wing Commander/.test(await text(none, "hallTop")), await text(none, "hallTop"));
check("nor does the page let itself be told to enter", (await none.evaluate(() => window.__wcHall.enter())) === false, null);
check("and the page has not connected to the lobby server", sockets.NOGAME.length === 0, sockets.NOGAME);
// A file of the game's name that is not the game does not open it either.
const fake = Buffer.alloc(200000, 0x20);
fake.write("MZ", 0, "latin1"); fake.writeUInt16LE(288, 8);
await none.setInputFiles("#gamefile", { name: "fake.zip", mimeType: "application/zip", buffer: zipOf("WC.EXE", fake) });
await none.waitForFunction(() => /^(ready|error)$/.test(document.getElementById("sourceStatus").dataset.state || ""), null, { timeout: 30000 }).catch(() => {});
await sleep(2);
check("a WC.EXE that is not the build the hooks know keeps it closed", /not the build the multiplayer hooks know/.test(await text(none, "sourceStatus")) && (await closed(none)) && sockets.NOGAME.length === 0, [await text(none, "sourceStatus"), sockets.NOGAME]);
// With the game the pilot is in, and nothing had to be pressed.
await none.unroute("**/wc.tar.gz");
await none.evaluate(() => { const b = document.getElementById("useServer"); b.hidden = false; b.click(); });
check("once the game is loaded the pilot is in the lobby", (await inside(none)) && (await pilots(none, 1)) && sockets.NOGAME.length === 1 && /\/ws$/.test(sockets.NOGAME[0]), [await text(none, "hallState"), sockets.NOGAME]);
check("with the game's tag", /NOGAME \(you\)\s*WC1/.test(await text(none, "hallRoster")), await text(none, "hallRoster"));
// ... and out again when the game goes.
await none.setInputFiles("#gamefile", { name: "fake.zip", mimeType: "application/zip", buffer: zipOf("WC.EXE", fake) });
check("a pilot whose game goes is out of the lobby", await none.waitForFunction(() => document.getElementById("hallBody").hidden && /needs a game to fly/.test(document.getElementById("hallState").textContent), null, { timeout: 30000 }).then(() => true, () => false), await text(none, "hallState"));
await none.evaluate(() => document.getElementById("useServer").click());
check("and back in it with the game", await inside(none), await text(none, "hallState"));
await none.context().close();

const a = await open("ALPHA", { busy: 2 });
const codeA = await value(a, "room");
check("with Wing Commander loaded it is WC1- and four digits", /^WC1-\d{4}$/.test(codeA), codeA);
const programs = await a.$$eval("#program option", (os) => os.map((o) => o.value));
check("the directory's programs are offered", programs.join(",") === "wc1,wc1sm2" && !(await a.$eval("#programLabel", (el) => el.hidden)), programs);
await a.selectOption("#program", "wc1sm2");
check("picking The Secret Missions 2 makes it SM2- with the same digits", (await value(a, "room")) === codeA.replace("WC1-", "SM2-"), await value(a, "room"));
await a.selectOption("#program", "wc1");
check("and back", (await value(a, "room")) === codeA, await value(a, "room"));

// -- the lobby --------------------------------------------------------------------
const b = await open("BRAVO", { busy: 2 });
check("a pilot who opens the page is in the lobby", (await inside(a)) && (await inside(b)), [await text(a, "hallState"), await text(b, "hallState")]);
// ... unless a link to a room brought the page: that pilot has a flight.
const k = await open("KILO", { query: { room: "LINK-" + run } });
await k.waitForFunction(() => !document.getElementById("lobby").hidden, null, { timeout: 30000 }).catch(() => {});
await sleep(1.5);
check("a page opened by a room's link is in the room and not in the lobby, which is open to it", (await k.$eval("#hallBody", (el) => el.hidden)) && !(await k.$eval("#hallEnter", (el) => el.hidden || el.disabled)) && !(await k.$eval("#lobby", (el) => el.hidden)), await text(k, "hallState"));
await k.context().close();
// A link to a room joins nothing for a page without a game: its visitor
// presses "Join room", or loads the game.
const l = await open("LIMA", { game: false, query: { room: "WAIT-" + run } });
await sleep(3);
check("a room's link opened without a game connects to nothing", (await l.$eval("#lobby", (el) => el.hidden)) && sockets.LIMA.length === 0 && /Load your game files .* room WAIT-/.test(await status(l)), [await status(l), sockets.LIMA]);
await l.click("#join");
check("\"Join room\" takes the seat without a game", await l.waitForFunction(() => !document.getElementById("lobby").hidden && /player 1 of 2 in room WAIT-/.test(document.getElementById("chatLog").textContent), null, { timeout: 30000 }).then(() => true, () => false), await status(l));
const m = await open("MIKE", { game: false, query: { room: "WAIT-" + run } });
await sleep(3);
check("a second visitor without a game waits as well", (await m.$eval("#lobby", (el) => el.hidden)) && sockets.MIKE.length === 0, sockets.MIKE);
await m.unroute("**/wc.tar.gz");
await m.evaluate(() => { const b = document.getElementById("useServer"); b.hidden = false; b.click(); });
check("and is in the room once the game is loaded", await m.waitForFunction(() => !document.getElementById("lobby").hidden && /player 2 of 2 in room WAIT-/.test(document.getElementById("chatLog").textContent), null, { timeout: 60000 }).then(() => true, () => false), await status(m));
check("and not in the lobby: one socket, the room's", (await m.$eval("#hallBody", (el) => el.hidden)) && sockets.MIKE.length === 1, sockets.MIKE);
await l.context().close();
await m.context().close();
check("two pilots see each other", (await pilots(a, 2)) && (await pilots(b, 2)), [await text(a, "hallRoster"), await text(b, "hallRoster")]);
check("the lobby has 32 seats", (await a.evaluate(() => window.__wcHall.test.net().maxPlayers)) === 32, await a.evaluate(() => window.__wcHall.test.net().maxPlayers));
check("with callsign and game", /BRAVO\s*WC1/.test(await text(a, "hallRoster")) && /ALPHA\s*WC1/.test(await text(b, "hallRoster")), await text(a, "hallRoster"));

await say(a, "hello from ALPHA");
check("a line arrives", await sees(b, "hello from ALPHA"), await text(b, "hallLog"));
check("under the sender's callsign and game", /ALPHA\s*WC1\s*: hello from ALPHA/.test(await text(b, "hallLog")), null);

// The sender's own page keeps the rules.
await say(a, "what the fuck");
check("profanity is not sent", /language is not sent/.test(await text(a, "hallHint")) && (await value(a, "hallInput")) === "what the fuck", await text(a, "hallHint"));
await say(a, "see you in Hell's Kitchen, Hellcat");
check("hell is fine", await sees(b, "Hell's Kitchen, Hellcat"), await text(b, "hallLog"));
await say(a, "third line too soon");
check("the third line has to wait", /You can send again in \d+ s/.test(await text(a, "hallHint")) && (await value(a, "hallInput")) === "third line too soon", await text(a, "hallHint"));
await say(a, "cheap ships at example.com");
check("links are not sent", /links and addresses cannot be sent/.test(await text(a, "hallHint")), await text(a, "hallHint"));
check("a line is 60 characters at most", (await a.$eval("#hallInput", (el) => el.maxLength)) === 60 && !(await a.evaluate(() => window.__wcHall.say("y".repeat(61)))), null);
await sleep(1);
const logB = await text(b, "hallLog");
check("none of it reached the other pilot", !/fuck|third line|example\.com/.test(logB), logB);
console.log("  (waiting for the next line's turn)");
await sleep(10.5);
await say(a, "third line in its turn");
check("ten seconds later the next line goes", await sees(b, "third line in its turn"), await text(a, "hallHint"));

// A page that keeps no rules: the others do.
await sleep(21);
await a.evaluate(() => {
  const raw = window.__wcHall.test.raw;
  raw({ t: "chat", text: "raw fuck" });
  raw({ t: "chat", text: "raw http://example.com" });
  raw({ t: "chat", text: "raw " + "y".repeat(80) });
  raw({ t: "chat", text: "<img src=x onerror=window.__pwned=1> raw markup" });
  for (let i = 1; i <= 6; i++) raw({ t: "chat", text: `raw flood ${i}` });
  raw("x".repeat(5000));
  raw("not json");
});
await sleep(2);
const rawB = await text(b, "hallLog");
check("the others drop profanity, links and long lines", !/raw fuck|raw http|raw yyyy/.test(rawB), rawB.slice(-300));
check("markup is text", rawB.includes("<img src=x onerror=window.__pwned=1> raw markup") && (await b.$$eval("#hallLog img", (e) => e.length)) === 0 && !(await b.evaluate(() => window.__pwned)), null);
const flood = (rawB.match(/raw flood \d/g) || []).length;
check("and all but the lines a pilot may send (two, one was the markup)", flood === 1, flood);

// -- a room said in the lobby -------------------------------------------------------
// "Advertise" says it without a code being typed, and makes the room first
// when the pilot is in none.
const offer = `${codeA} Enyo 1, 2 seats free`;   // (a room of three: "3 players" is ticked)
await sleep(10);
await refill(a);
await a.check("#threeSeats");
check("the pilot is in no room yet", await a.$eval("#lobby", (el) => el.hidden), null);
await a.click("#hallAdvertise");
check("Advertise joins the pilot's room and sends its code, mission and free seats", (await sees(b, offer)) && !(await a.$eval("#lobby", (el) => el.hidden)) && (await value(a, "hallInput")) === "", [await text(a, "hallHint"), await status(a)]);
check("the room has the button too, while a seat is free", !(await a.$eval("#advertise", (el) => el.hidden)), null);
// A pilot whose lines are used up for the moment finds the offer waiting in the box.
await a.click("#hallAdvertise"); await sleep(0.3); await a.click("#hallAdvertise"); await sleep(0.3);
check("an advertisement that has to wait waits in the box", (await value(a, "hallInput")) === offer && /You can send again in \d+ s/.test(await text(a, "hallHint")), [await value(a, "hallInput"), await text(a, "hallHint")]);
await a.fill("#hallInput", "");
check("the code is a link in the other's lobby", await b.waitForFunction((c) => Array.from(document.querySelectorAll("#hallLog a.roomcode")).some((x) => x.textContent === c), codeA, { timeout: 8000 }).then(() => true, () => false), await text(b, "hallLog"));
const href = new URL(await b.$eval("#hallLog a.roomcode", (x) => x.href));
check("that also opens the room from another tab, on the lobby server this page uses", href.searchParams.get("room") === codeA && href.searchParams.get("server") === lobbyUrl, href.toString());
await b.click("#hallLog a.roomcode");
await b.waitForFunction(() => !document.getElementById("lobby").hidden && document.getElementById("roster").children.length > 0, null, { timeout: 30000 }).catch(() => {});
check("a click joins the room", (await value(b, "room")) === codeA && /player 2 of 3 in room/.test(await text(b, "chatLog")), await status(b));
check("the host sees the wingman", await a.waitForFunction(() => /BRAVO is in the room/.test(document.getElementById("chatLog").textContent), null, { timeout: 30000 }).then(() => true, () => false), await text(a, "chatLog"));
check("the button stays while a seat is free", !(await a.$eval("#advertise", (el) => el.hidden)), null);
check("the lobby goes on beside the room", (await pilots(a, 2)) && !(await a.$eval("#hallBody", (el) => el.hidden)), null);

// A pilot who comes later is shown what was last said; the third seat is theirs.
const c = await open("CHARLIE", { busy: 2 });
await inside(c);
check("a third pilot", (await pilots(c, 3)) && (await pilots(a, 3)), await text(c, "hallRoster"));
check("is shown the last line of those who are there", await sees(c, `${codeA} Enyo 1, 2 seats free`), await text(c, "hallLog"));
await c.click("#hallLog a.roomcode");
await c.waitForFunction(() => !document.getElementById("lobby").hidden && document.getElementById("roster").children.length > 0, null, { timeout: 30000 }).catch(() => {});
check("a third click takes the last seat", (await value(c, "room")) === codeA && /player 3 of 3 in room/.test(await text(c, "chatLog")), await status(c));
check("no button in a full room", await a.waitForFunction(() => document.getElementById("advertise").hidden, null, { timeout: 15000 }).then(() => true, () => false), null);
// A fourth is told the room is full, and keeps a code of their own.
const d = await open("DELTA", { busy: 2 });
await inside(d);
await d.waitForFunction((c) => Array.from(document.querySelectorAll("#hallLog a.roomcode")).some((x) => x.textContent === c), codeA, { timeout: 8000 }).catch(() => {});
await d.click("#hallLog a.roomcode");
check("a full room says so", await d.waitForFunction(() => /is full/.test(document.getElementById("status").textContent), null, { timeout: 15000 }).then(() => true, () => false), await status(d));
const roomD = await value(d, "room");
check("and the pilot's own code is back", /^WC1-\d{4}$/.test(roomD) && roomD !== codeA, roomD);
check("and the address keeps no room", !new URL(d.url()).searchParams.get("room"), d.url());
// A click is where the pilot wants to be: a room they sit in alone is left
// for it (and taken again when the clicked one cannot be joined); a room
// with company is not left by a click.
check("the three-player box starts off", !(await d.$eval("#threeSeats", (el) => el.checked)), null);
await d.click("#join");
await d.waitForFunction(() => !document.getElementById("lobby").hidden, null, { timeout: 30000 });
check("a room made without it has two seats", /player 1 of 2 in room/.test(await text(d, "chatLog")) && (await d.$eval("#threeSeats", (el) => el.disabled)), await text(d, "chatLog"));
await d.click("#hallLog a.roomcode");
check("a pilot alone in a room who clicks a full one is told so, and is back in their own", await d.waitForFunction((own) => /is full/.test(document.getElementById("hallHint").textContent) && !document.getElementById("lobby").hidden && document.getElementById("room").value === own, roomD, { timeout: 30000 }).then(() => true, () => false), [await text(d, "hallHint"), await value(d, "room")]);
await say(d, "/room");
check("the other's offer is a link in the host's lobby", await a.waitForFunction((c) => Array.from(document.querySelectorAll("#hallLog a.roomcode")).some((x) => x.textContent === c), roomD, { timeout: 8000 }).then(() => true, () => false), await text(a, "hallLog"));
await a.evaluate((code) => Array.from(document.querySelectorAll("#hallLog a.roomcode")).find((x) => x.textContent === code).click(), roomD);
await sleep(1);
check("a host with a wingman in the room is not moved by a click", /with somebody/.test(await text(a, "hallHint")) && (await value(a, "room")) === codeA && !(await a.$eval("#lobby", (el) => el.hidden)), [await text(a, "hallHint"), await value(a, "room")]);
await d.close();
await c.click("#leave");
await sleep(1);
check("the fourth is gone from the lobby", await a.waitForFunction(() => /^3 in the lobby/.test(document.getElementById("hallRoster").textContent), null, { timeout: 15000 }).then(() => true, () => false), await text(a, "hallRoster"));

// "/room" in a line is the room's code.
await sleep(10);
await refill(a);
await say(a, "who flies with me in /room tonight?");
check("/room in a line is the pilot's room code", (await sees(c, `who flies with me in ${codeA} tonight?`)) && (await c.evaluate((code) => Array.from(document.querySelectorAll("#hallLog a.roomcode")).filter((x) => x.textContent === code).length, codeA)) === 2, await text(c, "hallLog"));

// Codes that name another game.
await sleep(10);
await refill(a);
await say(a, "WC2-9999 or SM2-7777?");
await c.waitForFunction(() => document.querySelectorAll("#hallLog a.roomcode").length >= 4, null, { timeout: 8000 }).catch(() => {});
const click = (page, code) => page.evaluate((code) => Array.from(document.querySelectorAll("#hallLog a.roomcode")).find((x) => x.textContent === code).click(), code);
check("a code for another game is marked", await c.evaluate(() => { const l = Array.from(document.querySelectorAll("#hallLog a.roomcode")); return l.find((x) => x.textContent === "WC2-9999").classList.contains("other") && !l.find((x) => x.textContent.startsWith("WC1-")).classList.contains("other"); }), null);
await click(c, "WC2-9999");
await sleep(1);
check("a room for a game that is not loaded is not joined", /WC2-9999 is a Wing Commander II room, and you have Wing Commander loaded/.test(await status(c)) && (await c.$eval("#lobby", (el) => el.hidden)), await status(c));
await click(c, "SM2-7777");
check("a room for another program of the same directory switches to it", await c.waitForFunction(() => document.getElementById("program").value === "wc1sm2" && /SM2-7777 is not open any more/.test(document.getElementById("status").textContent), null, { timeout: 15000 }).then(() => true, () => false), [await value(c, "program"), await status(c)]);
check("and a room nobody made is not made by the click", await c.$eval("#lobby", (el) => el.hidden), null);

// The lobby's own code is not a room; a code for another game is not joined by hand either.
await c.selectOption("#program", "wc1");
await c.fill("#room", "HALL-" + run);
await c.click("#join");
await sleep(1);
check("the lobby's code is not a room to fly in", /is the public lobby/.test(await status(c)) && (await c.$eval("#lobby", (el) => el.hidden)), await status(c));
await c.fill("#room", "wc-lobby7");
await c.click("#join");
await sleep(1);
check("nor are the lobbies after it", /WC-LOBBY7 is the public lobby/i.test(await status(c)) && (await c.$eval("#lobby", (el) => el.hidden)), await status(c));
await c.fill("#room", "wc2-1234");
await c.click("#join");
await sleep(1);
check("a WC2 code typed with Wing Commander loaded is refused", /WC2-1234 is a Wing Commander II room/.test(await status(c)) && (await value(c, "room")) === "WC2-1234", await status(c));

// A reload keeps the pilot in the lobby; leaving does not.  The names typed
// are remembered by the browser (the link gives the callsign here, not the
// last name).
await c.fill("#lastname", "Blair");
await c.$eval("#lastname", (el) => el.dispatchEvent(new Event("change", { bubbles: true })));
await c.reload();
check("a reload comes back into the lobby", await c.waitForFunction(() => !document.getElementById("hallBody").hidden, null, { timeout: 40000 }).then(() => true, () => false), null);
check("and the names typed are back", (await value(c, "lastname")) === "Blair" && (await value(c, "callsign")) === "CHARLIE", [await value(c, "callsign"), await value(c, "lastname")]);
check("and the others still count three", (await pilots(a, 3)) && (await pilots(c, 3)), await text(a, "hallRoster"));
await c.click("#hallLeave");
check("leaving is seen by the others", (await pilots(a, 2)) && (await c.$eval("#hallBody", (el) => el.hidden)), await text(a, "hallRoster"));
await c.reload();
await sleep(3);
check("and a reload stays out", await c.$eval("#hallBody", (el) => el.hidden), null);
await c.context().close();

// Flying leaves the lobby.
await a.click("#fly");
check("the host's Fly takes it out of the lobby", await a.waitForFunction(() => /left the lobby to fly/.test(document.getElementById("hallState").textContent) && document.getElementById("hallEnter").disabled, null, { timeout: 30000 }).then(() => true, () => false), await text(a, "hallState"));
check("and the wingman, whose game it starts", await b.waitForFunction(() => /left the lobby to fly/.test(document.getElementById("hallState").textContent), null, { timeout: 30000 }).then(() => true, () => false), await text(b, "hallState"));
// When the game is over the pilot is back (the line is what the hooks log
// when the game's program ends).
await a.waitForFunction(() => !!window.DOSBox, null, { timeout: 60000 }).catch(() => {});
await sleep(2);
check("nobody is in the lobby while flying", (await a.$eval("#hallBody", (el) => el.hidden)) && (await b.$eval("#hallBody", (el) => el.hidden)), null);
await a.evaluate(() => window.DOSBox.print("wcnet: Wing Commander ended"));
check("a pilot whose game is over is back in the lobby", await a.waitForFunction(() => !document.getElementById("hallBody").hidden && /^1 in the lobby/.test(document.getElementById("hallRoster").textContent), null, { timeout: 30000 }).then(() => true, () => false), [await text(a, "hallState"), await text(a, "hallRoster")]);
check("and the one who still flies is not", await b.$eval("#hallBody", (el) => el.hidden), null);
// (The page's "stopped by itself" status lands 300 ms after the ended line: let it, or it overwrites the click's answer.)
await sleep(1);
await click(a, codeA);
await sleep(0.5);
check("a code clicked there says the page has to be loaded again", /Reload the page to join/.test(await status(a)), await status(a));
await a.context().close();
await b.context().close();

// -- another lobby server -----------------------------------------------------------
// Should the public server be down, pilots meet on another: ?server=URL in
// the page's address, or the field under Options.  (Every page of this
// test uses ?server=; the public server is what a page uses without.)
const pub = await open("PAPA", { game: false, wait: false, query: { server: "https://pqrstuvw.xyz/lobbylink" } });
check("the public lobby server is not named on the page", await pub.$eval("#serverTag", (el) => el.hidden), await text(pub, "top"));
await pub.context().close();
const setServer = (page, v) => page.$eval("#server", (el, v) => { el.value = v; el.dispatchEvent(new Event("change", { bubbles: true })); }, v);
const n = await open("NOVEMBER", { hall: "MOVE-" + run, query: { server: "http://127.0.0.1:59999" } });
check("a lobby server that does not answer is said, with the way to another", await n.waitForFunction(() => { const t = document.getElementById("hallState").textContent; return /Could not enter the lobby/.test(t) && /\?server=URL/.test(t); }, null, { timeout: 30000 }).then(() => true, () => false), await text(n, "hallState"));
check("the page names the server it uses", (await text(n, "serverTag")) === "lobby server: 127.0.0.1:59999", await text(n, "serverTag"));
await setServer(n, "127.0.0.1:59999/lobbylink");
check("an address typed without its scheme is https", (await value(n, "server")) === "https://127.0.0.1:59999/lobbylink", await value(n, "server"));
await setServer(n, lobbyUrl);
check("with another server set under Options the pilot is in its lobby", (await inside(n)) && (await pilots(n, 1)), await text(n, "hallState"));
check("and the page's address and its name say which", new URL(n.url()).searchParams.get("server") === lobbyUrl && (await text(n, "serverTag")) === "lobby server: " + new URL(lobbyUrl).host, [n.url(), await text(n, "serverTag")]);
await n.context().close();

// -- a full lobby -------------------------------------------------------------------
// Two seats to a lobby, and a seat is claimable after four seconds of silence.
const small = { hall: "FULL-" + run, wait: false };
const setup = (page, more = {}) => page.evaluate((more) => window.__wcHall.test.configure({ seats: 2, claimAfterMs: 4000, heartbeatMs: 1000, ...more }), more);
const lobbyOf = (page) => page.evaluate(() => { const n = window.__wcHall.test.net(); return n && n.code; });
const e = await open("ECHO", small), f = await open("FOX", small), g = await open("GOLF", small);
for (const p of [e, f, g]) await setup(p);
await enter(e);
await enter(f);
check("a lobby of two seats has two pilots", (await pilots(e, 2)) && (await pilots(f, 2)), await text(e, "hallRoster"));
check("each with its game's tag", /^2 in the lobby: ECHO \(you\)\s*WC1 · FOX\s*WC1$/.test((await text(e, "hallRoster")).trim()), await text(e, "hallRoster"));
await sleep(6);
await enter(g);
check("a third lands in the next lobby, however long the two have been there", (await pilots(g, 1)) && (await lobbyOf(g)) === `FULL-${run}0` && (await lobbyOf(e)) === `FULL-${run}`, await lobbyOf(g));
check("and is told which", (await text(g, "hallRoster")).trim().replace(/\s+/g, " ") === `1 in the lobby FULL-${run}0: GOLF (you) WC1` && (await text(g, "hallLog")).includes(`You are in the lobby FULL-${run}0 (the ones before it are full) as GOLF`), await text(g, "hallRoster"));
// A page that knows of one lobby only is told that it is full.
const i = await open("INDIA", small);
await setup(i, { lobbies: 1 });
await i.click("#hallEnter");
check("with no lobby left a pilot is told so", await i.waitForFunction(() => /Every lobby is full/.test(document.getElementById("hallState").textContent), null, { timeout: 30000 }).then(() => true, () => false), await text(i, "hallState"));
await i.context().close();
// FOX's page falls silent (a closed laptop): its seat in the first lobby can be taken.
await g.click("#hallLeave");
await f.evaluate(() => window.__wcHall.test.net().stopHeartbeat());
await sleep(6);
await enter(g);
check("a silent pilot's seat in the first lobby goes to the newcomer", (await pilots(g, 2)) && /ECHO/.test(await text(g, "hallRoster")) && (await lobbyOf(g)) === `FULL-${run}`, await text(g, "hallState") + " / " + await text(g, "hallRoster"));
check("and the one who lost it lands in the next when it wakes", await f.waitForFunction((want) => document.getElementById("hallRoster").textContent.trim() === want, `1 in the lobby FULL-${run}0: FOX (you) WC1`, { timeout: 30000 }).then(() => true, () => false), [await text(f, "hallRoster"), await text(f, "hallState")]);
await say(g, "hello from GOLF");
check("the newcomer talks to the one who stayed", await sees(e, "hello from GOLF"), await text(e, "hallLog"));
// Two pilots are fewer than eight: a line a second.
await sleep(2.5);
const three = await g.evaluate(() => ["one", "two", "three"].map((t) => window.__wcHall.say(t)));
await sleep(0.2);
check("among fewer than eight pilots the third line waits a second, not ten", three.join() === "true,true,false" && /You can send again in 1 s/.test(await text(g, "hallHint")), [three, await text(g, "hallHint")]);
await sleep(1.2);
check("and goes", (await g.evaluate(() => window.__wcHall.say("three"))) && (await sees(e, "GOLF WC1: three")) && (await sees(e, "GOLF WC1: two")), await text(e, "hallLog"));
await sleep(1.5);
await say(g, `are you in FULL-${run}0 or WC-LOBBY3?`);
check("a lobby's code in a line is not a room", (await sees(e, "or WC-LOBBY3?")) && (await e.$$eval("#hallLog a.roomcode", (l) => l.length)) === 0, await text(e, "hallLog"));

// A page that asks for more seats than the server gives a room is told how
// many there are, and takes that (scripts/web-smoke.sh's server gives 256).
const h = await open("HOTEL", { hall: "BIG-" + run, wait: false });
await h.evaluate(() => window.__wcHall.test.configure({ seats: 1000 }));
await enter(h).catch(() => {});
check("a lobby is made with the seats the server allows", (await h.evaluate(() => { const n = window.__wcHall.test.net(); return n && n.maxPlayers; })) === 256, await text(h, "hallState"));

await browser.close();
console.log(results.every(Boolean) ? "HALL OK" : "HALL FAILED");
process.exit(results.every(Boolean) ? 0 : 1);
