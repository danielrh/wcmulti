// What may be said in the public lobby (web/hall.js), and the room codes in it.
//
// The lobby is a room of strangers with nobody in charge, so every page
// applies the same rules twice: to what its own player types (who is told
// why a line was not sent) and to what arrives from the others (a page that
// was tampered with gains nothing: the rest drop its lines unseen).
//
//   checkMessage(text)  a line of at most 60 characters, no links, no
//                       profanity -> { ok, text } or { ok: false, why }
//   checkName(name)     the same two rules for a callsign
//   makeBucket()        two lines to start with, then one every 10 seconds
//                       (one a second among fewer than 8 pilots: lineEvery)
//   splitCodes(text)    the room codes in a line, for the page to make
//                       clickable: WC-1234, WC1-1234, WC2-.., SM2-.., SO1-.., SO2-.., SO3-..
//   roomTag(code)       which game a code is for ("WC" = not said)
//   newRoomCode(tag)    a fresh code: the game's prefix and four digits
//   lobbyCode(n)        the lobby's own codes: WC-LOBBY, WC-LOBBY0, WC-LOBBY1, ...
//
// Nothing here touches the page: scripts/web-chatfilter-test.mjs runs it in
// Node.

export const MAX_CHARS = 60;
export const MAX_NAME = 16;

// -- room codes ----------------------------------------------------------------

// A room's code starts with the game it is for, so that a code said in the
// lobby tells who can join: WC1 (Wing Commander and The Secret Missions),
// SM2 (The Secret Missions 2), WC2, SO1 and SO2 (Wing Commander II and its
// Special Operations).  "WC" alone is a code made before any game files
// were loaded, or an older one: it says nothing.
export const GAME_TAGS = ["WC1", "WC2", "SM2", "SO1", "SO2", "SO3", "SO3P"];
const CODE_IN_TEXT = /(^|[^A-Za-z0-9_-])((WC[12]?|SM2|SO3P|SO[123])-[A-Za-z0-9_-]{1,60})/gi;

// The public lobby's own codes are not rooms to fly in: WC-LOBBY, and
// WC-LOBBY0, WC-LOBBY1, ... where pilots land when the ones before are full
// (web/hall.js).
export const LOBBY_CODE = "WC-LOBBY";
export const lobbyCode = (n, base = LOBBY_CODE) => (n <= 0 ? base : base + (n - 1));
export function isLobbyCode(code, base = LOBBY_CODE) {
  const c = String(code || "").toUpperCase(), b = base.toUpperCase();
  return c.startsWith(b) && /^\d*$/.test(c.slice(b.length));
}

export function roomTag(code) {
  const m = /^(WC[12]?|SM2|SO3P|SO[123])-/i.exec(String(code || ""));
  return m ? m[1].toUpperCase() : null;
}
// The prefix is written in capitals whoever typed it, so that the same code
// is the same room for everybody (the lobby server tells cases apart).
export function normalizeCode(code) {
  const m = /^(WC[12]?|SM2|SO3P|SO[123])-(.*)$/i.exec(String(code || ""));
  return m ? m[1].toUpperCase() + "-" + m[2] : String(code || "");
}
// Four digits and nothing else: no letters, so no words.  (1488 is a number
// people use as a slogan.)
export function newRoomCode(tag, random = Math.random) {
  let digits;
  do { digits = String(Math.floor(random() * 10000)).padStart(4, "0"); } while (digits === "1488");
  return `${GAME_TAGS.includes(tag) ? tag : "WC"}-${digits}`;
}
// The same four digits under another game's prefix, when the code is still
// one of ours ("WC-4821" -> "WC2-4821"); any other code is left alone.
export function retagRoomCode(code, tag) {
  const m = /^(WC[12]?|SM2|SO3P|SO[123])-(\d{4})$/.exec(String(code || ""));
  return m ? `${GAME_TAGS.includes(tag) ? tag : "WC"}-${m[2]}` : code;
}
// [{ text }] and [{ code, tag }] in order, for a line of chat.  Whatever
// follows a game's prefix and a hyphen is a code, however short, "WC1-4821",
// "WC-FALCON" or "WC2-x": a pilot who typed a code of their own making must
// see it as one, and a word that only looks like a code costs a click.
export function splitCodes(text, except = []) {
  // (except: codes that are not rooms, a list or a test.)
  const skip = typeof except === "function" ? except : (code) => except.some((e) => e.toUpperCase() === code.toUpperCase());
  const out = [];
  let last = 0;
  CODE_IN_TEXT.lastIndex = 0;
  for (let m; (m = CODE_IN_TEXT.exec(text)); ) {
    const raw = m[2].replace(/[-_]+$/, "");
    const start = m.index + m[1].length;
    const code = normalizeCode(raw);
    const hyphen = raw.indexOf("-");
    CODE_IN_TEXT.lastIndex = start + m[2].length;
    if (hyphen < 0 || hyphen === raw.length - 1) continue;  // the prefix alone
    if (skip(code)) continue;
    if (start > last) out.push({ text: text.slice(last, start) });
    out.push({ code, tag: roomTag(code) });
    last = start + raw.length;
  }
  if (last < text.length) out.push({ text: text.slice(last) });
  return out;
}

// -- cleaning ------------------------------------------------------------------

// One line: invisible characters go (they would hide a word from the
// rules), control characters and line breaks become spaces, single spaces.
export function cleanText(text) {
  return String(text == null ? "" : text)
    .normalize("NFKC")
    .replace(/[\u00ad\u061c\u200b-\u200f\u202a-\u202e\u2060-\u206f\ufeff\ufff9-\ufffb]/g, "")
    .replace(/[\u0000-\u001f\u007f-\u009f\u2028\u2029]/g, " ")
    .replace(/\s+/g, " ")
    .trim();
}
const chars = (s) => Array.from(s).length;

// -- links -----------------------------------------------------------------------

// Nothing in the lobby is a link, and nobody may write one out either: an
// address (with or without http, however the dot is spelled) is what an
// advertisement for somebody's site looks like.  The endings are the ones
// seen in addresses; "it", "is", "no", "me", "to" and their like are left
// out, or a full stop without a space after it would be an address, and so
// are "zip" and "sh": people here talk about wc.zip.
const TLDS = "com|net|org|io|gg|xyz|ru|cn|co|uk|de|fr|tv|ly|info|biz|app|dev|link|site|online|top|club|shop|live|cc|ca|au|nl|se|fi|pl|es|br|jp|kr|ch|eu|edu|gov|icu|win|vip|click|page|fun|pro|tk|ml|ga|cf|gq|onion|art|blog|cloud|store|tech|space|world|today|life|games|game|chat|social|wiki|news|media|video|stream|download|website|mobi|ai|ws|su|ua|cz|sk|hu|ro|bg|gr|tr|il|za|mx|nz|sg|hk|tw|vn|ie|pt|dk";
const SPACED_TLDS = "com|net|org|io|gg|xyz|ru|cn|info|biz";
const LINK_RULES = [
  /h(?:tt|xx)ps?\b/i,                                    // http, https, hxxp
  /:\/\//,                                               // any scheme
  /\bwww\d*\s*\./i,                                      // www.
  new RegExp(`[a-z0-9-]\\.(?:${TLDS})(?![a-z0-9-])`, "i"),      // name.com
  /[a-z0-9-]\.[a-z]{2,}\/\S/i,                           // name.xx/path
  /\b\d{1,3}(?:\.\d{1,3}){3}\b/,                         // 12.34.56.78
];
export function hasLink(text) {
  const t = cleanText(text)
    .replace(/\s*[[({<]\s*(?:dot|\.)\s*[\])}>]\s*/gi, ".")                 // (dot), [.]
    .replace(new RegExp(`\\s+(?:dot|d0t)\\s+(?=(?:${TLDS})\\b)`, "gi"), ".")    // name dot com
    .replace(new RegExp(`\\s*\\.\\s*(?=(?:${SPACED_TLDS})\\b)`, "gi"), ".");  // name . com
  return LINK_RULES.some((re) => re.test(t));
}

// -- language --------------------------------------------------------------------

// A word list of the usual kind: swearing, sexual words, slurs.  "hell" is
// not on it and must stay off: the Hellcat is a ship and Hell's Kitchen a
// system of the Vega campaign.
//
// ROOTS are refused wherever they stand inside a word ("...head", "mother...",
// "...ing"); WORDS only as words of their own, with a plural "s" ("cockpit",
// "assault", "title", "Rapier" and "therapist" are fine), and those marked +
// with the endings -ed, -ing, -er and -y as well.  Either way a letter may be
// doubled, spaced out, or spelled with the usual look-alike signs
// ("f u c k", "sh1t", "a$$", "f*ck").  INNOCENT are words that would trip
// over a root, or over a doubled letter, by accident.
const ROOTS = ("fuck phuck shit cunt bitch nigg faggot whore slut asshole arsehole asswipe asshat jackass dumbass smartass fatass " +
  "bastard cocksuck dickhead dickwad pussy blowjob handjob rimjob jizz dildo porn cumshot gangbang clitoris masturbat " +
  "ejaculat orgasm molest pedoph paedoph retard tranny shemale wetback towelhead raghead jigaboo mongoloid libtard hitler " +
  "bollock douche").split(" ");
const WORDS = ("ass arse cock dick prick tit titty titties boob boobies cum cumming anal anus penis vagina clit boner horny fap milf hentai " +
  "bdsm incest pedo rape raped raping rapist fag homo lesbo dyke spaz tard nig negro coon chink gook jap wop dago kike heeb " +
  "spic beaner honky injun paki darkie darky pikey kkk nazi heil skank thot twat tosser turd xxx " +
  "wtf stfu gtfo omfg lmfao mofo kys fuk fuq fck fcuk phuk biatch beotch " +
  "dammit damnit goddamn goddammit goddamned wank+ piss+ crap+ damn+ bugger+").split(" ");
const INNOCENT = new Set(("scunthorpe shitake shitakes shiitake shiitakes mishit mishits matsushita yamashita kinoshita morishita takeshita " +
  "pussycat pussycats pussyfoot pussyfooting pussywillow retardant retardants niggle niggles niggled niggling snigger sniggers " +
  "sniggered sniggering niggard niggardly rapping annal annals").split(" "));

// The signs a letter is spelled with.
const LOOK = { a: "a4@", b: "b8", e: "e3", g: "g9", i: "i1!|", l: "l1|", o: "o0", s: "s5$z", t: "t7+", u: "uv" };
const esc = (c) => c.replace(/[\\\]^$|+*?.(){}[-]/g, "\\$&");
// f+u+c+k+: every letter one or more times (a doubled letter of the word at
// least twice, or "as" would be "ass"); in a word of four letters or more a
// star may stand for any letter but the first and the last ("f*ck", "s**t").
function pattern(word) {
  let out = "";
  for (let i = 0; i < word.length; ) {
    let n = 1;
    while (word[i + n] === word[i]) n++;
    const inner = i > 0 && i + n < word.length && word.length > 3;
    out += `[${Array.from(LOOK[word[i]] || word[i]).map(esc).join("")}${inner ? "*" : ""}]` + (n > 1 ? `{${n},}` : "+");
    i += n;
  }
  return out;
}
const ROOT_RE = new RegExp(ROOTS.map(pattern).join("|"));
const WORD_RE = new RegExp("^(?:" + WORDS.map((w) => (w.endsWith("+")
  ? pattern(w.slice(0, -1)) + "(?:[s5$z]|[e3][s5$z]|[e3]d|[i1!][n]g?[s5$z]?|[e3]r[s5$z]?|y)?"
  : pattern(w) + "(?:[s5$z]|[e3][s5$z])?")).join("|") + ")$");

// Greek and Cyrillic letters that look like Latin ones.
const LOOKALIKE = { "\u0430": "a", "\u0432": "b", "\u0441": "c", "\u0435": "e", "\u043d": "h", "\u0456": "i", "\u043a": "k", "\u043c": "m", "\u043e": "o", "\u0440": "p", "\u0455": "s", "\u0442": "t", "\u0443": "y", "\u0445": "x",
  "\u03b1": "a", "\u03b2": "b", "\u03b5": "e", "\u03b9": "i", "\u03ba": "k", "\u03bd": "v", "\u03bf": "o", "\u03c1": "p", "\u03c4": "t", "\u03c5": "u", "\u03c7": "x" };
const fold = (text) => String(text).normalize("NFKD").replace(/[\u0300-\u036f]/g, "").toLowerCase()
  .replace(/[\u0370-\u03ff\u0400-\u04ff]/g, (c) => LOOKALIKE[c] || c);

export function isProfane(text) {
  const tokens = [];
  for (const raw of fold(cleanText(text)).split(/[^a-z0-9@$!|+*]+/)) {
    if (!raw) continue;
    const bare = raw.replace(/^[^a-z0-9]+|[^a-z0-9]+$/g, "");
    tokens.push({ raw, bare });
  }
  // Letters spaced out, "f u c k", are a word.
  const words = [];
  let run = "";
  for (const t of [...tokens, { raw: "", bare: "" }]) {
    if (t.bare.length === 1 && /[a-z0-9]/.test(t.bare)) { run += t.bare; continue; }
    if (run.length >= 3) words.push(run);
    run = "";
    if (t.raw) { words.push(t.raw); if (t.bare && t.bare !== t.raw) words.push(t.bare); }
  }
  for (const w of words) {
    if (/^\d+$/.test(w) || INNOCENT.has(w)) continue;   // (a number is a number, never a word)
    if (WORD_RE.test(w) || ROOT_RE.test(w)) return true;
  }
  return false;
}

// -- the two rules together ------------------------------------------------------

const WHY = {
  empty: "nothing to send",
  long: `lines in the lobby are at most ${MAX_CHARS} characters`,
  link: "links and addresses cannot be sent in the lobby",
  profane: "that language is not sent in the lobby",
};
export const whyText = (why) => WHY[why] || "that line cannot be sent";

export function checkMessage(text) {
  const t = cleanText(text);
  if (!t) return { ok: false, why: "empty" };
  if (chars(t) > MAX_CHARS) return { ok: false, why: "long" };
  if (hasLink(t)) return { ok: false, why: "link" };
  if (isProfane(t)) return { ok: false, why: "profane" };
  return { ok: true, text: t };
}
export function checkName(name) {
  const t = cleanText(name);
  if (!t) return { ok: false, why: "empty" };
  if (chars(t) > MAX_NAME) return { ok: false, why: "long" };
  if (hasLink(t)) return { ok: false, why: "link" };
  if (isProfane(t)) return { ok: false, why: "profane" };
  return { ok: true, text: t };
}

// -- how often -------------------------------------------------------------------

// Two lines to start with, then one every ten seconds in a lobby of eight
// pilots or more, and one a second among fewer.  (A full lobby of 32 at a
// line every ten seconds is three lines a second, which can still be read;
// seven pilots can simply talk.)  A bucket of two tokens gets one back every
// `every` milliseconds, a number or a function that says what it is now.
// The receiving side keeps a bucket per sender, a little more generous --
// the network does not deliver at an even pace, and two pages do not count
// the same pilots at the same moment -- so that an honest sender's lines
// are never dropped.
export const RATE = { burst: 2, every: 10000, slack: 2000, quiet: 1000, quietSlack: 300, busyFrom: 8, margin: 2 };
// The time between lines among so many pilots, for the sending side or the
// receiving one.
export function lineEvery(pilots, receiving = false, busyFrom = RATE.busyFrom) {
  const busy = pilots >= busyFrom + (receiving ? RATE.margin : 0);
  return (busy ? RATE.every : RATE.quiet) - (receiving ? (busy ? RATE.slack : RATE.quietSlack) : 0);
}
export function makeBucket({ burst = RATE.burst, every = RATE.every, tokens = burst, at = 0 } = {}) {
  const ms = () => (typeof every === "function" ? every() : every);
  const b = {
    tokens, at,
    fill(now) {
      if (now > b.at) b.tokens = Math.min(burst, b.tokens + (now - b.at) / ms());
      b.at = Math.max(b.at, now);
    },
    // May a line go now?  (Takes its token when it may.)
    take(now) {
      b.fill(now);
      if (b.tokens < 1) return false;
      b.tokens -= 1;
      return true;
    },
    // Milliseconds until the next line may go.
    wait(now) {
      b.fill(now);
      return b.tokens >= 1 ? 0 : Math.ceil((1 - b.tokens) * ms());
    },
  };
  return b;
}
