// The public lobby's rules (web/chatfilter.js), checked without a browser:
//
//   node scripts/web-chatfilter-test.mjs
//
// What pilots of this game say must pass (ships, systems, callsigns, file
// names, room codes), and links, the word list and its usual disguises must
// not.
import { checkMessage, checkName, hasLink, isProfane, makeBucket, splitCodes, roomTag, normalizeCode, newRoomCode, retagRoomCode, lobbyCode, isLobbyCode, lineEvery, RATE, MAX_CHARS } from "../web/chatfilter.js";

let failed = 0;
const check = (ok, what) => { if (!ok) { failed++; console.log("FAIL: " + what); } };

// -- language --------------------------------------------------------------------
const clean = [
  "Hellcat or Rapier?", "see you in Hell's Kitchen", "what the hell", "hell yes", "HELLCAT V",
  "my cockpit is on fire", "cocky pilot, that Maniac", "assault on the Ralari", "pass the class, assume nothing", "Jazz and Doomsday",
  "Scimitar, Hornet, Raptor, Rapier", "Dralthi Salthi Krant Gratha Jalthi", "Fralthi Ralari Dorkir Lumbari Snakeir", "Sartha Drakhri Jalkehi Grikath Kamekh Strakha",
  "K'Tithrak Mang", "Ghorah Khar, Gwynedd, Niven", "Enyo 1 then McAuliffe", "Port Hedland, Kurasawa, Rostov", "Tiger's Claw, Concordia", "Bloodfang!",
  "Maverick Spirit Angel Paladin Iceman Bossman Hunter Knight", "Hobbes Stingray Shadow Bear Downtown", "Thrakhath and the Kilrathi",
  "title screen", "Saturn's rings", "my therapist says", "analysis of the mission", "the canal", "a classic", "bass boost", "grasshopper", "wristwatch, saltwater",
  "raccoon", "mustard and custard", "tardy again", "Japan", "spice", "Dickens", "Scunthorpe", "shiitake", "niggle", "debugger", "snigger", "cumin", "document",
  "push it", "is hit", "it's hit", "who are you", "as soon as", "pass", "WC1-8008 anyone?", "WC2-7175 Enyo", "80085", "5 kills", "I am ok", "A B C", "W A S D",
  "wc.exe and sm2.exe", "drop your wc.zip", "ready? go!", "nice shot!!", "kill them all", "die, hairball", "you suck at this", "killed by a Jalthi", "Gimle 2 is hard",
  "2 seats, WC1-4821", "anyone for SO1?", "Secret Missions 2", "$100", "c++", "50%", "#1", "@Maverick hi",
];
for (const t of clean) {
  check(!isProfane(t), `clean line refused: "${t}"`);
  check(checkMessage(t).ok, `clean line not sendable (${checkMessage(t).why}): "${t}"`);
}
const profane = [
  "fuck", "FUCK YOU", "fucking hell", "fuuuuck", "f u c k", "f.u.c.k", "f-u-c-k", "fvck", "f*ck", "f**k", "phuck", "motherfucker", "clusterfuck", "fucker", "fuk", "fck",
  "shit", "sh1t", "sh!t", "$hit", "shiiit", "s h i t", "bullshit", "shithead", "sh*t", "s**t", "shitty",
  "bitch", "b1tch", "b!tch", "bitches", "biatch", "son of a bitch", "cunt", "c*nt", "cvnt", "asshole", "a$$hole", "ass", "a55", "@ss", "a s s", "asses", "arse", "dumbass", "jackass",
  "dick", "d1ck", "dickhead", "cock", "c0ck", "cocksucker", "prick", "pussy", "tits", "t1ts", "boobs", "b00bs", "wanker", "twat", "bastard", "slut", "whore", "wh0re",
  "piss off", "pissed", "crap", "crappy", "damn", "damned", "goddamn", "dammit", "wtf", "stfu", "gtfo", "kys",
  "nigger", "n1gger", "nigga", "faggot", "fag", "retard", "retarded", "tranny", "kike", "spic", "chink", "gook", "coon", "paki", "nazi", "heil hitler",
  "porn", "p0rn", "blowjob", "dildo", "jizz", "cum", "rape", "rapist", "pedo", "hentai", "milf",
  "you fucking idiot", "what a sh1tty game", "youfuckingidiot", "FUCK!", "shit.", "(ass)", "ass!", "\u0430ss", "fu\u0441k",
];
for (const t of profane) {
  check(isProfane(t), `profane line passed: "${t}"`);
  check(checkMessage(t).why === "profane", `profane line: expected "profane", got ${JSON.stringify(checkMessage(t))}: "${t}"`);
}

// -- links -----------------------------------------------------------------------
const links = [
  "http://example.com", "https://example.com/x", "hxxp://bad", "go to www.example.net", "example.com", "EXAMPLE.COM now", "visit my-site.xyz/abc", "discord.gg/abcd",
  "t.me/channel", "bit.ly/3abc", "example . com", "example dot com", "example (dot) com", "example[.]com", "ftp://files", "join 12.34.56.78", "fireflyk64.github.io/dosbox",
  "cheap gold at shop.ru", "a@b.org", "site.online", "x.io",
];
for (const t of links) {
  check(hasLink(t), `link passed: "${t}"`);
  check(checkMessage(t).why === "link", `link: expected "link", got ${JSON.stringify(checkMessage(t))}: "${t}"`);
}
const notLinks = ["wc.exe", "SM2.EXE and SO1.EXE", "wc.zip", "v1.01", "1.5 seconds", "e.g. the Rapier", "i.e. now", "wait...come on", "ok.go", "Mr.Blair", "and/or", "w/o shields", "1/2",
  "see you. Nice shot", "dosbox.conf", "savegame.wld", "Nav 1, Nav 2.", "ready.Set", "WC1-4821."];
for (const t of notLinks) check(!hasLink(t), `not a link, but refused: "${t}"`);

// -- length, cleaning ----------------------------------------------------------------
check(checkMessage("y".repeat(MAX_CHARS)).ok, "a line of exactly the limit");
check(checkMessage("y".repeat(MAX_CHARS + 1)).why === "long", "a line over the limit");
check(checkMessage("   ").why === "empty", "an empty line");
check(checkMessage("  hello \n\t there\u200b ").text === "hello there", "cleaning: spaces, control and invisible characters");
check(checkMessage("f\u200buck").why === "profane", "an invisible character between the letters");
check(checkName("Maverick").ok && checkName("Hellcat").ok, "callsigns");
check(checkName("x".repeat(17)).why === "long" && checkName("shithead").why === "profane" && checkName("me.example.com").why === "link", "bad callsigns");

// -- how often -------------------------------------------------------------------
{
  const b = makeBucket();
  check(b.take(0) && b.take(100) && !b.take(200), "two lines to start with, the third waits");
  check(b.wait(200) > 9000 && b.wait(200) <= 10000, `the wait is about ten seconds (${b.wait(200)})`);
  check(!b.take(9000) && b.take(10100) && !b.take(10200), "one more after ten seconds");
  check(b.take(20200) && !b.take(25000) && b.take(30300), "and one every ten seconds after that");
  check(b.take(60000) && b.take(60001) && !b.take(60002), "a quiet pilot has two again");
  // Among fewer than eight pilots a line a second; the receiving side
  // counts two pilots more before it holds a sender to ten seconds.
  check(lineEvery(7) === 1000 && lineEvery(8) === 10000 && lineEvery(32) === 10000 && lineEvery(1) === 1000, "lineEvery, sending: " + [1, 7, 8, 32].map((n) => lineEvery(n)));
  check(lineEvery(7, true) === 700 && lineEvery(9, true) === 700 && lineEvery(10, true) === 8000, "lineEvery, receiving: " + [7, 9, 10].map((n) => lineEvery(n, true)));
  let pilots = 3;
  const q = makeBucket({ every: () => lineEvery(pilots) });
  check(q.take(0) && q.take(100) && !q.take(200) && q.wait(200) <= 1000 && q.take(1300) && q.take(2400) && !q.take(2500), "three pilots: two lines, then one a second");
  pilots = 8;
  check(!q.take(3400) && q.wait(3400) > 3000 && !q.take(6000) && q.take(8000) && !q.take(17000) && q.take(18000), "an eighth pilot comes: ten seconds to a line again");
  // The receiving side, a little more generous: an honest sender is never
  // dropped, in a lobby of any size, and not while the two pages count a
  // pilot apart.
  for (const [mineCount, theirCount] of [[20, 20], [3, 3], [7, 8], [7, 9], [8, 7]]) {
    const mine = makeBucket({ every: () => lineEvery(mineCount) }), theirs = makeBucket({ every: () => lineEvery(theirCount, true) });
    let now = 0, dropped = 0;
    for (let i = 0; i < 300; i++) {
      now += Math.random() < 0.3 ? 50 : Math.random() * 1.5 * lineEvery(mineCount);
      if (mine.take(now) && !theirs.take(now + (Math.random() - 0.5) * 0.15 * lineEvery(mineCount))) dropped++;
    }
    check(dropped === 0, `the receiver (counting ${theirCount}) dropped ${dropped} lines of an honest sender (counting ${mineCount})`);
  }
}

// -- room codes ------------------------------------------------------------------
const codes = (t, except) => splitCodes(t, except).filter((p) => p.code).map((p) => p.code).join(",");
check(codes("join WC1-4821 now") === "WC1-4821", "a code in a line");
check(codes("wc2-0042!") === "WC2-0042", "a prefix in small letters is the same code");
check(codes("WC-1234, SM2-0001 or SO1-9999/SO2-0000") === "WC-1234,SM2-0001,SO1-9999,SO2-0000", "all the prefixes: " + codes("WC-1234, SM2-0001 or SO1-9999/SO2-0000"));
check(codes("(WC1-4821).") === "WC1-4821" && codes("WC1-4821-") === "WC1-4821", "punctuation around a code");
check(codes("WC-FALCON and WC-Falcon7") === "WC-FALCON,WC-Falcon7", "codes of a pilot's own making");
check(codes("WC2-style, WC1-x and SO1-only are codes") === "WC2-style,WC1-x,SO1-only", "a short code, and talk that looks like one, are codes: " + codes("WC2-style, WC1-x and SO1-only are codes"));
check(codes("XWC1-1234, WC3-1234, WC1-, WC1--, WC1_1234 and WC1 1234 are not") === "", "not a code: " + codes("XWC1-1234, WC3-1234, WC1-, WC1--, WC1_1234 and WC1 1234 are not"));
check(codes("meet in WC-LOBBY or WC1-0007", ["WC-LOBBY"]) === "WC1-0007", "the lobby's own code is not a room");
check(splitCodes("a WC1-4821 b").map((p) => p.text || `<${p.code}>`).join("") === "a <WC1-4821> b", "the text around a code is kept");
// The lobby's own codes: a row of them, none a room.
check([0, 1, 2, 11].map((n) => lobbyCode(n)).join() === "WC-LOBBY,WC-LOBBY0,WC-LOBBY1,WC-LOBBY10" && lobbyCode(2, "HALL-X") === "HALL-X1", "lobbyCode: " + [0, 1, 2, 11].map((n) => lobbyCode(n)));
check(isLobbyCode("WC-LOBBY") && isLobbyCode("wc-lobby0") && isLobbyCode("WC-LOBBY27") && !isLobbyCode("WC-LOBBYX") && !isLobbyCode("WC-LOBB") && !isLobbyCode("WC1-4821") && isLobbyCode("HALL-X3", "HALL-X") && !isLobbyCode("HALL-X3"), "isLobbyCode");
check(codes("in WC-LOBBY, WC-LOBBY0 or WC-LOBBY12; room WC2-0042", (c) => isLobbyCode(c)) === "WC2-0042", "a lobby's code in a line is not a room: " + codes("in WC-LOBBY, WC-LOBBY0 or WC-LOBBY12; room WC2-0042", (c) => isLobbyCode(c)));
check(roomTag("WC1-4821") === "WC1" && roomTag("so2-1") === "SO2" && roomTag("WC-1234") === "WC" && roomTag("FALCON-7") === null, "roomTag");
check(normalizeCode("wc1-AbCd") === "WC1-AbCd" && normalizeCode("Falcon-7") === "Falcon-7", "normalizeCode");
check(retagRoomCode("WC-4821", "WC2") === "WC2-4821" && retagRoomCode("WC1-4821", "") === "WC-4821" && retagRoomCode("FALCON-7", "WC2") === "FALCON-7" && retagRoomCode("WC-FALCON", "WC2") === "WC-FALCON", "retagRoomCode");
for (let i = 0; i < 20000; i++) {
  const c = newRoomCode(["WC1", "WC2", "SM2", "SO1", "SO2", "", "junk"][i % 7]);
  if (!/^(WC|WC1|WC2|SM2|SO1|SO2)-\d{4}$/.test(c) || c.endsWith("-1488") || isProfane(c) || hasLink(c) || codes(c) !== c) { check(false, "a bad new room code: " + c); break; }
}
check(newRoomCode("junk").startsWith("WC-") && newRoomCode("SO1").startsWith("SO1-"), "newRoomCode's prefix");

// The line the room form offers ("WC1-4821 Enyo 1, 1 seat free") must be one
// the lobby takes, for every mission of every game in the registry.
{
  const { GAMES } = await import("../web/gamefiles.js");
  check(GAMES.filter((g) => g.multiplayer).map((g) => g.tag).sort().join() === "SM2,SO1,SO2,WC1,WC2", "the registry's tags: " + GAMES.map((g) => g.tag));
  for (const g of GAMES) {
    for (const s of (g.campaign ? g.campaign.series : [])) {
      const n = Array.isArray(s.missions) ? s.missions.length : s.missions;
      for (let i = 0; i < n; i++) {
        const what = s.name ? `${s.name} ${i + 1}` : `series ${s.series} mission ${i + 1}`;
        const line = `${g.tag}-4821 ${what}, 2 seats free`;
        const r = checkMessage(line);
        if (!r.ok || codes(line) !== `${g.tag}-4821`) { check(false, `an offer the lobby would not take (${r.why}): "${line}"`); }
      }
    }
  }
}

console.log(failed ? `${failed} FAILED` : "chat filter OK");
process.exit(failed ? 1 : 0);
