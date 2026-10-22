// The public lobby: one room of the lobby server, WC-LOBBY, where pilots who
// do not know each other yet say which room they fly in.
//
// It is an ordinary lobbylink room of 32 seats (or as many as the server
// gives a room: it says so, and the page asks again for that many), and its
// chat goes from browser to browser like a game's messages do; the lobby
// server only introduces the browsers to each other.  There is nobody in
// charge of it, so every page keeps the rules itself, for what its own
// player types and for what arrives (web/chatfilter.js): lines of 60
// characters, two to start with and then one every ten seconds (one a
// second while fewer than eight pilots are there), no profanity, no links.
// A room code in a line (WC1-4821) is shown as a link that joins that room;
// the page decides whether the game fits (web/wc.js).  Nobody has to type a
// code: "Advertise" sends the pilot's room, its mission and its free seats,
// and "/room" in a line is the room's code.
//
// A pilot whose page has a game to fly is in the lobby (unless a link to a
// room brought it, or it left the lobby on an earlier visit), and out of it
// while it flies.  A page without a game does not connect at all: the lobby
// server and the pilots in the lobby hear only of visitors who brought the
// game (opts.ready; web/wc.js says what counts).
//
// The connection is this file's own and not the lobbylink client's
// (p2p-client.js), which serves a game: a room of strangers wants other
// habits.
//   * Browsers connect directly, and through the server's TURN relay only
//     when that has failed: the relay has ports for a few dozen links, and
//     the games need them.
//   * A seat whose page went away without leaving (a closed laptop) stays
//     taken as far as the server knows.  The room is made so that a seat
//     silent for two and a half minutes may be claimed by a newcomer when
//     all are taken, and every page says something to the server once a
//     minute so that its own is not.  (The server counts any message as a
//     sign of life; it has no ping, so it answers "unknown message type".)
//   * The room ends when the server says so (a day after it was made, or
//     five minutes after the last pilot left): the next page makes it again.
//   * There is a row of lobbies: WC-LOBBY, then WC-LOBBY0, WC-LOBBY1, ...
//     A page takes a seat in the first that has one (a free seat, or a
//     silent pilot's), so pilots gather in the first and spill into the
//     next only when it is full.  That is also what keeps one bad page
//     from being the end of it: whoever makes a room decides its size and
//     its rules, and a page that keeps none could make WC-LOBBY with one
//     seat, or fill it.  The others then land in the next.
//   * Every pilot is linked to every other, so a newcomer to a full lobby
//     gets hundreds of offers.  They are spread over a few seconds: the
//     server drops a socket that has more than a hundred messages waiting.
//
// "Join open game" asks the lobby without a line of chat: the page asks
// every pilot in it for the room they sit in ({t: "rooms?", n}), and each
// page answers with its room's code, the program it plays and its free
// seats, or with nothing when it has no room with a free seat or its pilot
// keeps the room private ({t: "room", n, code, game, free}; opts.openRoom
// says).  The asking page picks one it can fly in (web/wc.js).
import { checkMessage, checkName, makeBucket, lineEvery, splitCodes, whyText, lobbyCode, isLobbyCode, LOBBY_CODE, RATE, MAX_CHARS, MAX_NAME, GAME_TAGS } from "./chatfilter.js";

const SEATS = 32;                 // asked for (what the public server gives a room; 32 pilots at a line every ten seconds are three lines a second); a server that gives fewer says how many
const CLAIM_SEATS = 256;          // a full lobby is asked for seats up to here: another page may have made it bigger
const LOBBIES = 32;               // WC-LOBBY, WC-LOBBY0 ... WC-LOBBY30: how far a page goes for a seat
const CLAIM_BATCH = 32;           // seats of a full lobby asked for in one go
const OFFER_SPREAD_MS = 25;       // per pilot in the lobby: over how long the offers to a newcomer are spread
const OFFER_SPREAD_MAX_MS = 6000;
const ROSTER_NAMES = 24;          // callsigns shown; the rest are counted
const CLAIM_AFTER_MS = 150000;
const HEARTBEAT_MS = 25000;
const CONNECT_TIMEOUT_MS = 20000;
const DIRECT_WAIT_MS = 12000;     // how long a direct connection gets before the relay is tried
const RELAY_WAIT_MS = 30000;
const MAX_ATTEMPTS = 3;           // per pair: direct, relayed, relayed
const MAX_WIRE = 600;             // characters of one message between pages
const REPLAY_AGE_MS = 15 * 60000; // a newcomer is shown each pilot's last line when it is younger than this
const REPLAY_EVERY_MS = 60000;    // ... and takes one such line a minute from a seat
const ROOMS_WAIT_MS = 2500;       // how long "Join open game" waits for the pilots' answers
const LINKS_WAIT_MS = 8000;       // ... and, just after entering, for the links to those already there

// -- the connection --------------------------------------------------------------

function signalingUrl(server) {
  const u = new URL(server);
  if (u.protocol === "http:") u.protocol = "ws:";
  else if (u.protocol === "https:") u.protocol = "wss:";
  else if (u.protocol !== "ws:" && u.protocol !== "wss:") throw new Error(`unsupported scheme ${u.protocol} in the lobby server's address`);
  let path = u.pathname.replace(/\/+$/, "");
  if (!path.endsWith("/ws")) path += "/ws";
  u.pathname = path; u.search = ""; u.hash = "";
  return u.toString();
}
const fail = (code, message) => Object.assign(new Error(message), { code });
// The lobby server did not answer (these are the lobbylink client's codes
// too), and what a pilot is told then: no page depends on that one server.
export const serverDown = (code) => ["connection-failed", "connection-closed", "connection-lost", "connect-timeout", "invalid-server-url"].includes(code);
export const OTHER_SERVER = "If the lobby server is down, pilots can meet on another: set it under Options (step 2), or add ?server=URL to this page's address.";
const shuffled = (n) => { const a = Array.from({ length: n }, (_, i) => i); for (let i = n - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [a[i], a[j]] = [a[j], a[i]]; } return a; };

// One seat in the room and a data channel to everybody else in it.
//   on.roster()            somebody came or went
//   on.link(id, open)      the channel to a pilot opened or closed
//   on.message(id, text)   a message from a pilot
//   on.lost(code, message) the seat is gone (the room ended, the seat was taken, the server went away)
class HallNet {
  // code: the first lobby's; the others are code + "0", code + "1", ...
  constructor({ server, code, lobbies = LOBBIES, seats = SEATS, claimAfterMs = CLAIM_AFTER_MS, heartbeatMs = HEARTBEAT_MS, on, log }) {
    Object.assign(this, { server, base: code, code, lobbies, seats, claimAfterMs, heartbeatMs, on, log });
    this.ws = null; this.selfId = -1; this.maxPlayers = 0; this.players = []; this.token = "";
    this.links = new Map(); this.soon = new Map(); this.closed = false; this.beat = null; this.iceAll = []; this.iceDirect = [];
  }

  // Takes a seat: the one this page had (its token), or a seat in the first
  // lobby of the row that has one -- a free seat, or, when all are taken,
  // the seat of a pilot who has been silent too long.
  connect() {
    const url = signalingUrl(this.server);
    return new Promise((resolve, reject) => {
      const ws = new WebSocket(url);
      let waiting = null, settled = false;
      const done = (err, joined) => {
        if (settled) return;
        settled = true;
        clearTimeout(timer);
        // (The page left while the server was answering: the seat goes back.)
        if (!err && this.closed) { err = fail("closed", "left the lobby"); try { ws.send('{"type":"leave"}'); } catch (e) { /* gone */ } }
        if (err) { try { ws.close(); } catch (e) { /* gone */ } reject(err); return; }
        this.adopt(ws, joined);
        resolve();
      };
      // (The time is for one round of answers: the row is asked lobby by lobby.)
      let timer = null;
      const arm = () => { clearTimeout(timer); timer = setTimeout(() => done(fail("connect-timeout", "the lobby server did not answer")), CONNECT_TIMEOUT_MS); };
      arm();
      // The server answers every question with "joined" or an error, in
      // order: several can be asked at once.  With a "joined" among the
      // answers the rest are "already joined" and nobody waits for them.
      const askAll = (msgs) => new Promise((answer) => {
        const got = [];
        waiting = (m) => { got.push(m); if (m.type === "joined" || got.length === msgs.length) { waiting = null; answer(got); } };
        arm();
        for (const m of msgs) ws.send(JSON.stringify(m));
      });
      const ask = (msg) => askAll([msg]).then((got) => got[got.length - 1]);
      const create = () => ({ maxPlayers: this.seats, waitUntilFull: false, allowLateJoin: true, allowReconnect: true,
                              allowReplacement: true, reconnectPolicy: "token-or-claim-after-timeout", claimAfterMs: this.claimAfterMs });
      ws.onmessage = (ev) => {
        let m;
        try { m = JSON.parse(ev.data); } catch (e) { return; }
        if (waiting && (m.type === "joined" || m.type === "error")) waiting(m);
      };
      ws.onerror = () => done(fail("connection-failed", "no connection to the lobby server"));
      ws.onclose = () => done(fail("connection-closed", "the lobby server closed the connection"));
      const full = { type: "error", code: "room-full", message: "every seat is taken" };
      const join = (code, token) => ask({ type: "join", code, create: create(), ...(token ? { resumeToken: token } : {}) });
      // A full lobby: the seat of a pilot who has been silent too long, if
      // there is one.  (The seats are asked for a batch at a time, in no
      // order; the first answer names how many seats there are.)
      const claim = async (code) => {
        let seats = CLAIM_SEATS;
        const ids = shuffled(CLAIM_SEATS);
        for (let i = 0; i < ids.length; i += CLAIM_BATCH) {
          const batch = ids.slice(i, i + CLAIM_BATCH).filter((id) => id < seats);
          if (!batch.length) continue;
          const answers = await askAll(batch.map((id) => ({ type: "claim-slot", code, playerId: id })));
          const won = answers.find((m) => m.type === "joined");
          if (won) return won;
          for (const m of answers) {
            const range = m.code === "invalid-target" && /out of range 0\.\.(\d+)/.exec(m.message || "");
            if (range) seats = Math.min(seats, Number(range[1]) + 1);
            else if (m.code === "room-not-found") return join(code);   // (it ended meanwhile)
            else if (m.code !== "slot-not-claimable") return full;     // (a room that lets nobody claim)
          }
        }
        return full;
      };
      const seatIn = async (code, token) => {
        let r = await join(code, token);
        // (A server set up with fewer seats to a room says how many.)
        const limit = r.type === "error" && r.code === "invalid-create" && /exceeds limit (\d+)/.exec(r.message || "");
        if (limit && Number(limit[1]) >= 2) { this.seats = Number(limit[1]); r = await join(code, token); }
        return r.type === "error" && r.code === "room-full" ? claim(code) : r;
      };
      ws.onopen = async () => {
        try {
          // The lobby this page was in, while its seat there may still be its own ...
          let r = this.token ? await seatIn(this.code, this.token) : full;
          // ... or the first of the row with a seat.
          for (let n = 0; n < this.lobbies && r.type !== "joined" && r.code === "room-full"; n++) r = await seatIn(lobbyCode(n, this.base), "");
          if (r.type === "joined") done(null, r);
          else done(fail(r.code || "error", r.code === "room-full" ? "every lobby is full" : r.message || "the lobby server refused"));
        } catch (e) { done(fail("connection-failed", String(e && e.message ? e.message : e))); }
      };
    });
  }

  adopt(ws, joined) {
    this.ws = ws;
    this.code = joined.code || this.code;
    this.selfId = joined.selfId; this.maxPlayers = joined.maxPlayers; this.token = joined.resumeToken || "";
    this.players = (joined.players || []).map((p) => ({ id: p.id, occupied: !!p.occupied, connected: !!p.connected }));
    this.iceAll = (joined.iceServers || []).map((s) => ({ urls: s.urls, username: s.username, credential: s.credential }));
    const urls = (s) => (Array.isArray(s.urls) ? s.urls : [s.urls]);
    this.iceDirect = this.iceAll.filter((s) => urls(s).every((u) => /^stuns?:/.test(u)));
    if (!this.iceDirect.length) {
      // (A TURN server answers STUN on the same port.)
      const stun = this.iceAll.flatMap(urls).filter((u) => /^turn:/.test(u)).map((u) => u.replace(/^turn:/, "stun:").replace(/\?.*$/, ""));
      if (stun.length) this.iceDirect = [{ urls: Array.from(new Set(stun)) }];
    }
    ws.onmessage = (ev) => { if (this.ws === ws) { let m; try { m = JSON.parse(ev.data); } catch (e) { return; } this.serverSays(m); } };
    ws.onerror = () => { /* onclose follows */ };
    ws.onclose = () => { if (this.ws === ws && !this.closed) this.lost("connection-lost", "the connection to the lobby server was lost"); };
    this.startHeartbeat();
    // The lower seat offers: to everybody above who is there now; those
    // below offer to us when the server tells them we came.
    for (const [id] of this.links) this.dropLink(id);
    for (const p of this.players) if (p.id > this.selfId && p.occupied && p.connected) this.offerSoon(p.id);
  }

  // An offer after a moment of its own, longer in a fuller lobby, so that
  // the offers a newcomer gets (and the answers to its own) do not all go
  // through the server at once.
  offerSoon(id) {
    clearTimeout(this.soon.get(id));
    const crowd = this.players.filter((p) => p.occupied).length;
    this.soon.set(id, setTimeout(() => {
      this.soon.delete(id);
      const p = this.players[id];
      if (!this.closed && this.ws && p && p.occupied && p.connected && !this.links.has(id)) void this.offer(id, 0);
    }, Math.random() * Math.min(OFFER_SPREAD_MAX_MS, OFFER_SPREAD_MS * crowd)));
  }
  forgetOffers() { for (const t of this.soon.values()) clearTimeout(t); this.soon.clear(); }

  startHeartbeat() {
    this.stopHeartbeat();
    this.beat = setInterval(() => { try { if (this.ws && this.ws.readyState === WebSocket.OPEN) this.ws.send('{"type":"ping"}'); } catch (e) { /* onclose reports */ } }, this.heartbeatMs);
  }
  stopHeartbeat() { if (this.beat) clearInterval(this.beat); this.beat = null; }

  // The seat is gone, or the socket is: the links to the others stay up
  // unless the server ended the room or gave the seat away.
  lost(code, message) {
    const gone = code === "replaced" || code === "session-superseded" || code === "room-expired";
    this.stopHeartbeat();
    const ws = this.ws;
    this.ws = null;
    try { if (ws) ws.close(); } catch (e) { /* gone */ }
    this.forgetOffers();
    if (gone) { this.token = ""; for (const [id] of this.links) this.dropLink(id); }
    this.on.lost(code, message, gone);
  }

  serverSays(m) {
    const slot = (id) => this.players[id];
    switch (m.type) {
      case "player-joined":
        if (Array.isArray(m.players)) this.players = m.players.map((p) => ({ id: p.id, occupied: !!p.occupied, connected: !!p.connected }));
        this.peerCame(m.playerId);
        break;
      case "player-rejoined": case "player-replaced":
        if (slot(m.playerId)) { slot(m.playerId).occupied = true; slot(m.playerId).connected = true; }
        this.peerCame(m.playerId);
        break;
      case "player-left":
        if (slot(m.playerId)) { slot(m.playerId).connected = false; if (m.reason === "explicit-leave") slot(m.playerId).occupied = false; }
        // (Only its socket to the server may be gone: an open channel stays.)
        if (m.reason === "explicit-leave") { clearTimeout(this.soon.get(m.playerId)); this.soon.delete(m.playerId); this.dropLink(m.playerId); }
        this.on.roster();
        break;
      case "signal":
        void this.signalFrom(m.from, m.payload || {});
        break;
      case "error":
        if (m.code === "invalid-message") break;   // the answer to our sign of life
        if (m.code === "already-joined") break;    // ... and to the seats asked for after the one we got
        if (["replaced", "session-superseded", "room-expired", "slow-consumer"].includes(m.code)) this.lost(m.code, m.message || m.code);
        else this.log(`lobby: the server says ${m.code}: ${m.message}`);
        break;
    }
  }

  peerCame(id) {
    if (id === this.selfId) return;
    this.dropLink(id);
    this.on.roster();
    if (this.selfId < id) this.offerSoon(id);
  }

  signal(to, payload) {
    try { if (this.ws && this.ws.readyState === WebSocket.OPEN) this.ws.send(JSON.stringify({ type: "signal", to, payload })); } catch (e) { /* onclose reports */ }
  }

  makeLink(id, initiator, relay) {
    this.dropLink(id);
    const pc = new RTCPeerConnection({ iceServers: relay ? this.iceAll : this.iceDirect });
    const dc = pc.createDataChannel("hall", { negotiated: true, id: 1, ordered: true });
    const link = { id, pc, dc, initiator, attempt: 0, open: false, timer: null, pending: [] };
    this.links.set(id, link);
    const current = () => this.links.get(id) === link;
    dc.onopen = () => { if (!current()) return; link.open = true; clearTimeout(link.timer); this.on.link(id, true); };
    dc.onclose = () => { if (!current()) return; const was = link.open; link.open = false; if (was) this.on.link(id, false); };
    dc.onmessage = (ev) => { if (current() && typeof ev.data === "string") this.on.message(id, ev.data); };
    pc.onicecandidate = (ev) => { if (current() && ev.candidate) this.signal(id, { kind: "ice", candidate: ev.candidate.toJSON() }); };
    pc.onconnectionstatechange = () => { if (current() && pc.connectionState === "failed") this.linkFailed(link); };
    return link;
  }
  dropLink(id) {
    const link = this.links.get(id);
    if (!link) return;
    this.links.delete(id);
    clearTimeout(link.timer);
    try { link.dc.close(); } catch (e) { /* closed */ }
    try { link.pc.close(); } catch (e) { /* closed */ }
    if (link.open) { link.open = false; this.on.link(id, false); }
  }

  async offer(id, attempt) {
    if (this.closed) return;
    const link = this.makeLink(id, true, attempt > 0);
    link.attempt = attempt;
    link.timer = setTimeout(() => this.linkFailed(link), attempt > 0 ? RELAY_WAIT_MS : DIRECT_WAIT_MS);
    try {
      const offer = await link.pc.createOffer();
      if (this.links.get(id) !== link) return;
      await link.pc.setLocalDescription(offer);
      if (this.links.get(id) !== link) return;
      this.signal(id, { kind: "offer", sdp: link.pc.localDescription.sdp, relay: attempt > 0 });
    } catch (e) { this.log(`lobby: no offer to pilot ${id + 1}: ${e && e.message ? e.message : e}`); }
  }
  // The offering side tries again, the next time with the relay.
  linkFailed(link) {
    if (this.links.get(link.id) !== link || this.closed) return;
    const id = link.id, next = link.attempt + 1, offers = link.initiator;
    this.dropLink(id);
    this.on.roster();
    if (!offers || next >= MAX_ATTEMPTS) return;
    const there = () => !this.closed && this.ws && !this.links.has(id) && this.players[id] && this.players[id].occupied && this.players[id].connected;
    setTimeout(() => { if (there()) void this.offer(id, next); }, 500 * next);
  }

  async signalFrom(from, payload) {
    if (this.closed || from === this.selfId || !Number.isInteger(from)) return;
    try {
      if (payload.kind === "offer") {
        if (this.selfId < from) return;   // (we offer to those above us)
        const link = this.makeLink(from, false, !!payload.relay);
        await link.pc.setRemoteDescription({ type: "offer", sdp: payload.sdp });
        if (this.links.get(from) !== link) return;
        this.flush(link);
        const answer = await link.pc.createAnswer();
        if (this.links.get(from) !== link) return;
        await link.pc.setLocalDescription(answer);
        if (this.links.get(from) !== link) return;
        this.signal(from, { kind: "answer", sdp: link.pc.localDescription.sdp });
      } else if (payload.kind === "answer") {
        const link = this.links.get(from);
        if (!link || link.pc.signalingState !== "have-local-offer") return;
        await link.pc.setRemoteDescription({ type: "answer", sdp: payload.sdp });
        this.flush(link);
      } else if (payload.kind === "ice") {
        const link = this.links.get(from);
        if (!link) return;
        if (link.pending) link.pending.push(payload.candidate);
        else await link.pc.addIceCandidate(payload.candidate || undefined).catch(() => { /* a candidate of an older attempt */ });
      }
    } catch (e) { this.log(`lobby: connecting to pilot ${from + 1}: ${e && e.message ? e.message : e}`); }
  }
  flush(link) {
    const queued = link.pending || [];
    link.pending = null;
    for (const c of queued) link.pc.addIceCandidate(c || undefined).catch(() => { /* stale */ });
  }

  open(id) { const l = this.links.get(id); return !!(l && l.open); }
  send(id, text) {
    const l = this.links.get(id);
    if (!l || !l.open) return false;
    try { l.dc.send(text); return true; } catch (e) { return false; }
  }

  // Gives the seat back.
  close() {
    if (this.closed) return;
    this.closed = true;
    this.stopHeartbeat();
    this.forgetOffers();
    const ws = this.ws;
    this.ws = null;
    try { if (ws && ws.readyState === WebSocket.OPEN) ws.send('{"type":"leave"}'); } catch (e) { /* gone */ }
    try { if (ws) ws.close(1000, "left"); } catch (e) { /* gone */ }
    for (const [id] of this.links) this.dropLink(id);
  }
}

// -- the lobby on the page -------------------------------------------------------

const $ = (id) => document.getElementById(id);
const el = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text != null) e.textContent = text; return e; };
const stored = (store, key) => { try { return store.getItem(key); } catch (e) { return null; } };
const store = (s, key, value) => { try { if (value == null) s.removeItem(key); else s.setItem(key, value); } catch (e) { /* not kept */ } };

// opts: server() the lobby server's address, link(code) this page's address
// for a room, name() the callsign typed,
// ready() whether a game is loaded that can be flown with others (the lobby
// is closed without one), tag() the loaded game's ("WC1", ... or ""),
// title(tag) a game's name,
// onCode(code) a room code was clicked, room() the pilot's own room for an
// advertisement, { code, offer } or null (the page joins the room of its
// room form first when the pilot is in none), openRoom() the room offered to
// a pilot who asks for an open game, { code, game, free } or null (none, or
// private), log(line), code (another row of lobbies, for tests).
export function initHall(opts) {
  const code = opts.code || LOBBY_CODE;
  // The lobbies' own codes, this row's and the public one's, are not rooms.
  const isLobby = (c) => isLobbyCode(c, code) || isLobbyCode(c);
  const log = opts.log || (() => {});
  const ready = () => !opts.ready || !!opts.ready();
  // wanted: the pilot belongs in the lobby and is not in it for the moment
  // (no game loaded yet, or flying): it enters as soon as it can.
  let net = null, entering = false, attempt = null, shut = false, wanted = false, retry = null, retries = 0, settings = {};
  const pilots = new Map();      // seat -> { name, tag } from its hello
  const buckets = new Map();     // seat -> what it may still say (kept when a pilot leaves: a seat is not a fresh start)
  const hellos = new Map();      // seat -> how often it may introduce itself
  const replays = new Map();     // seat -> when an old line was last taken from it
  const shown = new Map();       // seat -> its last line shown
  const askers = new Map();      // seat -> how often it may ask for open rooms
  let asking = null;             // { n, asked, answers: seat -> answer, finish }: this page's question for open rooms
  let lastLine = null;           // { text, at }: this pilot's last line, for those who come later
  let noted = "";                // the hint line's own text (a refusal), until the next key

  // The pilots in this lobby, this one included (the roster's count), and
  // how long a pilot waits between lines among so many: ten seconds from
  // eight pilots up, a second among fewer.
  const others = () => (net ? net.players.filter((p) => p.id !== net.selfId && p.occupied && net.open(p.id)) : []);
  const every = (receiving) => lineEvery(others().length + 1, receiving, settings.busyFrom || RATE.busyFrom);

  // What this page may still send: kept over a reload.
  const SENT_KEY = "wc:hall:sent";
  const mine = (() => {
    let saved = null;
    try { saved = JSON.parse(stored(localStorage, SENT_KEY) || "null"); } catch (e) { /* fresh */ }
    const kept = saved && typeof saved.tokens === "number" && typeof saved.at === "number" && saved.at <= Date.now();
    return makeBucket({ every: () => every(false), ...(kept ? { tokens: Math.max(0, Math.min(RATE.burst, saved.tokens)), at: saved.at } : { at: Date.now() }) });
  })();
  const keepMine = () => store(localStorage, SENT_KEY, JSON.stringify({ tokens: mine.tokens, at: mine.at }));

  const inside = () => !!net && net.selfId >= 0 && !entering;
  const fallbackName = (id) => `Pilot ${id + 1}`;
  // The callsign as the lobby shows it: the page's, cut to length, or
  // "Pilot N" when there is none or it breaks the lobby's rules.
  const typedName = () => checkName(Array.from(String(opts.name() || "").trim()).slice(0, MAX_NAME).join(""));
  function myName() {
    const r = typedName();
    return r.ok ? r.text : fallbackName(net ? net.selfId : 0);
  }
  const myTag = () => (GAME_TAGS.includes(opts.tag()) ? opts.tag() : "");

  function line(cls, ...parts) {
    const div = el("div", cls);
    for (const p of parts) div.append(p);
    const box = $("hallLog");
    const atBottom = box.scrollTop + box.clientHeight >= box.scrollHeight - 6;
    box.appendChild(div);
    while (box.children.length > 200) box.firstChild.remove();
    if (atBottom) box.scrollTop = box.scrollHeight;
  }
  const sys = (text) => line("sys", text);

  // A line of chat: the room codes in it are links.  A code's link is the
  // page's own address with ?room=, so it also opens in another tab; a click
  // joins here.
  function chatLine(name, tag, text, { me = false, old = 0 } = {}) {
    const parts = [el("span", "name" + (me ? " me" : ""), name)];
    if (tag) parts.push(" ", el("span", "game", tag));
    parts.push(": ");
    for (const p of splitCodes(text, isLobby)) {
      if (!p.code) { parts.push(p.text); continue; }
      const a = el("a", "roomcode", p.code);
      const url = new URL(location.href);
      url.search = ""; url.searchParams.set("room", p.code);
      a.href = opts.link ? opts.link(p.code) : url.toString();
      // (Dimmed when the pilot cannot fly in it: fits(tag) says, else the loaded game's tag must be the code's.)
      const fits = !GAME_TAGS.includes(p.tag) || (opts.fits ? opts.fits(p.tag) : !myTag() || p.tag === myTag());
      if (!fits) a.classList.add("other");
      a.title = (GAME_TAGS.includes(p.tag) ? `A room for ${opts.title ? opts.title(p.tag) : p.tag}${fits ? "" : " (no files for it in this browser)"}. ` : "") + "Click to join it.";
      a.addEventListener("click", (ev) => { ev.preventDefault(); opts.onCode(p.code); });
      parts.push(a);
    }
    if (old >= 60000) parts.push(" ", el("span", "age", `(${Math.round(old / 60000)} min ago)`));
    line(me ? "mine" : "", ...parts);
  }

  function renderRoster() {
    const box = $("hallRoster");
    box.textContent = "";
    if (!inside()) return;
    const there = others();
    // (Named when it is not the first: pilots in another do not hear this one.)
    box.append(el("span", "count", `${there.length + 1} in the lobby${net.code === code ? "" : " " + net.code}: `));
    const entry = (name, tag, cls) => { const s = el("span", "pilot " + cls, name); if (tag) s.append(" ", el("span", "game", tag)); return s; };
    box.append(entry(myName() + " (you)", myTag(), "me"));
    for (const p of there.slice(0, ROSTER_NAMES)) {
      const who = pilots.get(p.id) || { name: fallbackName(p.id), tag: "" };
      box.append(" · ", entry(who.name, who.tag, ""));
    }
    if (there.length > ROSTER_NAMES) box.append(` · and ${there.length - ROSTER_NAMES} more`);
  }

  function hint() {
    const box = $("hallHint");
    if (!inside()) { box.textContent = ""; return; }
    const left = MAX_CHARS - Array.from($("hallInput").value).length;
    const wait = mine.wait(Date.now());
    box.textContent = noted || (wait > 0 ? `You can send again in ${Math.ceil(wait / 1000)} s.` : left <= 15 ? `${left} characters left.` : "");
    box.classList.toggle("warn", !!noted);
  }
  const note = (text) => { noted = text; hint(); };

  const hello = (to, reply) => {
    const m = JSON.stringify({ t: "hello", name: myName(), game: myTag(), reply: !!reply });
    if (to != null) net.send(to, m);
    else for (const p of net.players) if (p.id !== net.selfId) net.send(p.id, m);
  };

  function fromPilot(id, raw) {
    if (!inside() || raw.length > MAX_WIRE) return;
    let m;
    try { m = JSON.parse(raw); } catch (e) { return; }
    if (!m || typeof m !== "object") return;
    const now = Date.now();
    if (m.t === "hello") {
      if (!hellos.has(id)) hellos.set(id, makeBucket({ burst: 4, every: 2000, at: now }));
      if (!hellos.get(id).take(now)) return;
      const name = checkName(typeof m.name === "string" ? m.name : "");
      pilots.set(id, { name: name.ok ? name.text : fallbackName(id), tag: GAME_TAGS.includes(m.game) ? m.game : "" });
      if (!m.reply) hello(id, true);
      renderRoster();
    } else if (m.t === "chat") {
      const r = checkMessage(typeof m.text === "string" ? m.text : "");
      if (!r.ok) { log(`lobby: dropped a line from pilot ${id + 1} (${r.why})`); return; }
      const who = pilots.get(id) || { name: fallbackName(id), tag: "" };
      if (m.old !== undefined) {
        // A line said before we came: one a minute from a seat, and not
        // the one we have already.
        const old = Number(m.old);
        if (!(old >= 0 && old <= REPLAY_AGE_MS) || now - (replays.get(id) || 0) < REPLAY_EVERY_MS || shown.get(id) === r.text) return;
        replays.set(id, now);
        shown.set(id, r.text);
        chatLine(who.name, who.tag, r.text, { old });
        return;
      }
      if (!buckets.has(id)) buckets.set(id, makeBucket({ every: () => every(true), at: now }));
      if (!buckets.get(id).take(now)) { log(`lobby: dropped a line from pilot ${id + 1} (too many)`); return; }
      shown.set(id, r.text);
      chatLine(who.name, who.tag, r.text);
    } else if (m.t === "rooms?") {
      // A pilot looks for an open game: this page's room, when it has a
      // free seat and is not private (opts.openRoom), or a plain answer.
      if (!askers.has(id)) askers.set(id, makeBucket({ burst: 3, every: 5000, at: now }));
      if (typeof m.n !== "string" || m.n.length > 16 || !askers.get(id).take(now) || settings.mute) return;
      const r = settings.room || (opts.openRoom ? opts.openRoom() : null);
      net.send(id, JSON.stringify({ t: "room", n: m.n, ...(r ? { code: r.code, game: r.game, free: r.free } : {}) }));
    } else if (m.t === "room") {
      if (!asking || m.n !== asking.n || asking.answers.has(id)) return;
      asking.answers.set(id, m);
      if (asking.answers.size >= asking.asked) asking.finish();
    }
  }

  // "Join open game": the rooms with a free seat that the pilots in the
  // lobby sit in, { rooms: [{ code, game, free }], asked, answered } (answers
  // that break the rules are left out; a page older than the question does
  // not answer), or null when this page is not in the lobby.  A page that
  // has only just entered first gives its links to the others a few seconds.
  async function findRooms() {
    const until = (cond, ms) => new Promise((resolve) => {
      const end = Date.now() + ms;
      const look = () => { if (cond() || Date.now() >= end) resolve(); else setTimeout(look, 200); };
      look();
    });
    if (entering) await until(() => !entering, CONNECT_TIMEOUT_MS * 2);
    if (!inside()) return null;
    const there = () => net.players.filter((p) => p.id !== net.selfId && p.occupied && p.connected).length;
    await until(() => !inside() || others().length >= there(), LINKS_WAIT_MS);
    if (!inside()) return null;
    if (asking) return asking.result;   // (one question at a time)
    const ask = { n: Math.random().toString(36).slice(2, 10), asked: others().length, answers: new Map() };
    asking = ask;
    ask.result = new Promise((resolve) => {
      const timer = setTimeout(() => ask.finish(), ROOMS_WAIT_MS);
      ask.finish = () => {
        clearTimeout(timer);
        if (asking === ask) asking = null;
        const rooms = new Map();
        for (const a of ask.answers.values()) {
          const code = typeof a.code === "string" ? a.code : "";
          const free = Number(a.free);
          if (!/^[A-Za-z0-9_-]{4,64}$/.test(code) || isLobby(code) || !checkMessage(code).ok) continue;
          if (typeof a.game !== "string" || a.game.length > 16 || !Number.isInteger(free) || free < 1 || free > 255) continue;
          rooms.set(code, { code, game: a.game, free });
        }
        log(`lobby: asked ${ask.asked} pilots for an open game: ${ask.answers.size} answered, ${rooms.size} room${rooms.size === 1 ? "" : "s"} with a free seat`);
        resolve({ rooms: Array.from(rooms.values()), asked: ask.asked, answered: ask.answers.size });
      };
      if (!ask.asked) { ask.finish(); return; }
      const m = JSON.stringify({ t: "rooms?", n: ask.n });
      for (const p of others()) net.send(p.id, m);
    });
    return ask.result;
  }

  function linkChanged(id, open) {
    if (!net) return;
    if (open) {
      hello(id, false);
      if (lastLine && Date.now() - lastLine.at < REPLAY_AGE_MS) net.send(id, JSON.stringify({ t: "chat", text: lastLine.text, old: Date.now() - lastLine.at }));
    } else {
      pilots.delete(id);
    }
    renderRoster();
  }

  function seatLost(lostCode, message, gone) {
    if (!net) return;
    log(`lobby: ${lostCode}: ${message}`);
    if (lostCode === "session-superseded") {
      // (This seat was taken up from somewhere else with our own token.)
      leave({ keep: true, why: "The lobby is open in another window of yours: this one left it." });
      return;
    }
    if (gone) { pilots.clear(); renderRoster(); }
    // The room ended, the seat was given away while this page slept, or the
    // server went away: take a seat again, soon and then less and less often.
    const delay = lostCode === "room-expired" ? 500 + Math.random() * 2500 : Math.min(30000, 2000 * 2 ** retries) * (0.7 + 0.6 * Math.random());
    retries++;
    $("hallState").textContent = "Reconnecting to the lobby…" + (retries > 2 && serverDown(lostCode) ? " " + OTHER_SERVER : "");
    clearTimeout(retry);
    retry = setTimeout(() => { retry = null; if (net) void reconnect(); }, delay);
  }
  async function reconnect() {
    const n = net;
    try {
      const before = n.code;
      await n.connect();
      if (net !== n) return;
      retries = 0;
      $("hallState").textContent = "";
      if (n.code !== before) sys(`You are in the lobby ${n.code} now.`);
      renderRoster();
    } catch (e) {
      if (net !== n) return;
      if (e.code === "room-full") { leave({ keep: true, why: "You lost your seat in the lobby while this page was away, and every lobby is full now. Try again in a while." }); return; }
      seatLost(e.code || "error", e.message || String(e), false);
    }
  }

  function show() {
    const on = !!net && !entering;
    $("hallTop").hidden = on;       // the way in, and what entering means
    $("hallEnter").disabled = entering || shut || !ready();
    $("hallNeeds").hidden = ready();
    $("hallBody").hidden = !on;
    if (entering) $("hallState").textContent = "Entering the lobby…";
    renderRoster(); hint();
  }

  async function enter() {
    if (shut || !ready()) return false;
    wanted = false;
    if (net || entering) return inside();
    entering = true;
    $("hallState").textContent = "";
    show();
    const n = new HallNet({ server: opts.server(), code, log, ...settings,
      on: { roster: renderRoster, link: linkChanged, message: fromPilot, lost: seatLost } });
    attempt = n;
    try {
      await n.connect();
      if (attempt !== n) { n.close(); return false; }   // (left, or flying, meanwhile)
    } catch (e) {
      if (attempt !== n) return false;
      entering = false;
      wanted = true;    // (the next occasion tries again: another server, another game, the end of a flight)
      n.close();
      $("hallState").textContent = e.code === "room-full" ? "Every lobby is full (every seat is taken). Try again in a while."
        : `Could not enter the lobby: ${e.message || e}.${serverDown(e.code) ? " " + OTHER_SERVER : ""}`;
      show();
      return false;
    }
    net = n; entering = false; retries = 0;
    store(localStorage, "wc:hall", "in");
    store(sessionStorage, "wc:hall", "in");
    $("hallState").textContent = "";
    show();
    sys(`You are in the lobby${net.code === code ? "" : ` ${net.code} (the ones before it are full)`} as ${myName()}. Advertise sends your room, and /room in a line is its code; a code like WC1-4821 can be clicked to join. ` +
        `Lines are ${MAX_CHARS} characters at most, two to start with and then one every ${RATE.every / 1000} seconds ` +
        `(one a second while fewer than ${settings.busyFrom || RATE.busyFrom} pilots are here); no links.`);
    if (!typedName().ok && typedName().why !== "empty") sys(`Your callsign is not shown here (${whyText(typedName().why)}): you are ${myName()}.`);
    log(`lobby: in ${net.code} as pilot ${net.selfId + 1} of ${net.maxPlayers}`);
    return true;
  }

  // keep: the pilot did not ask to leave (the page went away, or the pilot
  // flies).  A pilot who did ask stays out on the next visit too.
  function leave({ keep = false, why = "" } = {}) {
    clearTimeout(retry); retry = null;
    const was = !!net;
    if (net) net.close();
    if (attempt && attempt !== net) attempt.close();
    net = null; entering = false; attempt = null;
    pilots.clear();
    if (!keep) { wanted = false; store(localStorage, "wc:hall", "out"); store(sessionStorage, "wc:hall", "out"); }
    $("hallState").textContent = why;
    show();
    if (was) log("lobby: left");
  }

  // Sends a line.  Returns whether it went.
  function say(text) {
    if (!inside()) return false;
    const r = checkMessage(text);
    if (!r.ok) { if (r.why !== "empty") note(`Not sent: ${whyText(r.why)}.`); return false; }
    const now = Date.now();
    const wait = mine.wait(now);
    if (wait > 0) { note(""); return false; }   // (the hint line counts the seconds down)
    mine.take(now);
    keepMine();
    lastLine = { text: r.text, at: now };
    chatLine(myName(), myTag(), r.text, { me: true });
    const m = JSON.stringify({ t: "chat", text: r.text });
    for (const p of net.players) if (p.id !== net.selfId) net.send(p.id, m);
    note("");
    return true;
  }

  // The pilot's room, said without typing its code.  "Advertise" sends the
  // whole offer (code, mission, free seats); when the pilot's lines are
  // used up for the moment it waits in the box.
  async function advertise() {
    if (shut) return false;
    const mine = await opts.room();
    if (!mine) { if (inside()) note("Nothing to advertise: you are in no room (step 2 says why)."); return false; }
    if (!inside() && !(await enter())) return false;
    if (say(mine.offer)) { $("hallInput").value = ""; hint(); return true; }
    $("hallInput").value = mine.offer;
    hint();
    return false;
  }
  // "/room" in a line is the room's code, and "/room" alone the whole offer.
  const ROOM_WORD = /(^|\s)\/room(?=\s|$)/gi;
  async function submit() {
    let text = $("hallInput").value;
    if (text.trim().toLowerCase() === "/room") { await advertise(); return; }
    ROOM_WORD.lastIndex = 0;
    if (ROOM_WORD.test(text)) {
      const mine = await opts.room();
      if (!mine) { note("Not sent: /room is your room's code, and you are in no room (step 2 says why)."); return; }
      text = text.replace(ROOM_WORD, `$1${mine.code}`);
      $("hallInput").value = text;    // (what goes, should it have to wait)
    }
    if (say(text)) $("hallInput").value = "";
    hint();
  }

  $("hallEnter").addEventListener("click", () => void enter());
  $("hallLeave").addEventListener("click", () => leave());
  $("hallAdvertise").addEventListener("click", () => void advertise());
  $("hallForm").addEventListener("submit", (ev) => { ev.preventDefault(); void submit(); });
  $("hallInput").addEventListener("input", () => {
    // (Said while typing, so that nobody composes a line that cannot go.)
    const r = checkMessage($("hallInput").value);
    noted = !r.ok && (r.why === "link" || r.why === "profane" || r.why === "long") ? `This cannot be sent: ${whyText(r.why)}.` : "";
    hint();
  });
  setInterval(() => { if (inside() && !noted) hint(); }, 1000);
  // A page that goes away gives its seat back; one that comes back from the
  // browser's page cache takes one again.
  window.addEventListener("pagehide", () => { if (net) leave({ keep: true }); });
  window.addEventListener("pageshow", (ev) => { if (ev.persisted && stored(sessionStorage, "wc:hall") === "in") void enter(); });

  show();

  const api = {
    // Is this one of the lobbies' own codes (and so no room to fly in)?
    isLobby,
    enter, leave, inside, say,
    // The page's callsign changed.
    announce() { if (inside()) { hello(null, false); renderRoster(); } },
    // A page that has just been loaded and whose pilot belongs in the lobby
    // (auto): in at once with a game loaded, and otherwise when one is.
    enterWhenReady() { wanted = true; if (ready()) void enter(); else show(); },
    // The page's game files changed.  With a game to fly the lobby is open,
    // and a pilot it was waiting for enters; without one the pilot is out of
    // it until there is a game again.
    gameChanged() {
      if (!ready()) {
        const was = !!net || entering;
        wanted = wanted || was;
        if (was) leave({ keep: true, why: "You left the lobby: it needs a game to fly (step 1)." });
        else show();
        return;
      }
      show();
      if (wanted) void enter();
      else api.announce();
    },
    // The lobby server's address changed: a pilot who is in the lobby, or
    // could not get into it, takes a seat on the new server.
    serverChanged() {
      const was = !!net || entering;
      if (was) leave({ keep: true });
      else { $("hallState").textContent = ""; show(); }
      if (was || wanted) void enter();
    },
    // The pilot's room, as a line in the lobby (the buttons that say so).
    advertise,
    // The open rooms the pilots in the lobby offer ("Join open game").
    findRooms,
    // A word under the chat, until the pilot types: what came of a click on
    // a code (the page's status line is far below).
    note,
    // While the game runs there is no lobby: the pilot has a flight, and a
    // lobby full of pilots who are away would be no use to those looking
    // for one.  When the game is over (reopen) the lobby is there again,
    // and a pilot who was in it when the flight began is back in it.
    shut(why) { if (shut) return; const was = inside() || entering; wanted = wanted || was; shut = true; leave({ keep: true, why: was ? why : "" }); },
    reopen() {
      if (!shut) return;
      shut = false;
      $("hallState").textContent = "";
      show();
      if (wanted) void enter();
    },
    // Does a page that has just been loaded enter (once it has a game to
    // fly: enterWhenReady)?  Yes, unless the pilot
    // left the lobby (in this tab, or on an earlier visit and has not
    // entered since), or a link to a room brought the page: that pilot has
    // somebody to fly with.  A tab that was in the lobby is in it again
    // after a reload, link or not.
    auto({ link = false, off = false } = {}) {
      const tab = stored(sessionStorage, "wc:hall");
      if (off || tab === "out") return false;
      return tab === "in" || (stored(localStorage, "wc:hall") !== "out" && !link);
    },
    // For the page tests (scripts/web-hall.mjs).
    test: {
      net: () => net,
      // (room: the answer this page gives to a question for open rooms, whatever its own room;
      // mute: it gives none, as a page older than the question.)
      configure(s) { settings = { ...settings, ...s }; },
      // A message as a page that keeps no rules would send it.
      raw(obj) { const m = typeof obj === "string" ? obj : JSON.stringify(obj); for (const p of net.players) if (p.id !== net.selfId) net.send(p.id, m); },
      refill() { mine.tokens = RATE.burst; keepMine(); },
    },
  };
  return api;
}
