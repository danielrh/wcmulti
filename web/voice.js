// Voice between the players, for those who opt in.
//
// Each player chooses in the room form: off (the default), listen only,
// push to talk, or always on; the choice is kept in local storage and told
// to the others with the hello.  Voice runs only when everybody in the room
// has opted in: one player who has not wants no voice, and gets none, and
// nobody else hears or is heard either -- one voice on an open mic could
// easily be rude.  A player who has opted in while another has not sees
// that on the form (the control lights up), and Fly says so, so nobody is
// surprised by the silence or by a voice.
//
// The sound goes over WebRTC audio, a second peer connection beside the
// game's data channel, negotiated over that data channel (the page's own
// lobby messages, as the hello and the chat are) with the lobby's ICE
// servers.  The browser's own audio processing does the rest: echo
// cancellation (it knows what the browser itself plays, the game's sound
// included), noise suppression, automatic gain, and Opus.  Push to talk is
// the backquote key, or a controller button (web/gamepad.js).
const VOICE_KEY = "wc:voice";
const CHOICES = ["off", "listen", "ptt", "on"];
const PTT_CODE = "Backquote";

export function initVoice(host) {
  const $ = (id) => document.getElementById(id);
  const sel = $("voice"), state = $("voiceState");
  let choice = "off";
  try { const saved = localStorage.getItem(VOICE_KEY); if (CHOICES.includes(saved)) choice = saved; } catch (e) { /* off */ }
  sel.value = choice;
  const remote = new Map();   // player id -> that player's choice
  const links = new Map();    // player id -> { pc, audio, offerer, pending }
  let mic = null;             // the microphone, while the choice is a talking one
  let micWanted = null;       // the acquisition in progress
  let pttDown = false;
  let micGeneration = 0, roomGeneration = 0;

  const optedIn = (c) => c === "listen" || c === "ptt" || c === "on";
  const talks = (c) => c === "ptt" || c === "on";
  const name = (id) => host.names().get(id) || "Player " + (id + 1);
  function others() {
    const g = host.game();
    return g ? g.players.filter((p) => p.occupied && p.id !== g.selfId).map((p) => p.id) : [];
  }
  const everyoneIn = () => optedIn(choice) && others().length > 0 && others().every((id) => optedIn(remote.get(id)));

  function save() { try { localStorage.setItem(VOICE_KEY, choice); } catch (e) { /* not kept */ } }

  async function acquireMic() {
    if (mic) { applyMute(); return mic; }
    if (micWanted) return micWanted;
    const generation = ++micGeneration;
    micWanted = Promise.resolve().then(() => navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true } }))
      .then((stream) => {
        if (generation !== micGeneration || !talks(choice)) {
          for (const t of stream.getTracks()) t.stop();
          return null;
        }
        mic = stream; applyMute(); return stream;
      })
      .catch((e) => {
        if (generation !== micGeneration) return null;
        host.chatLine(`No microphone (${e.name}): voice is listen only for you`, "sys");
        choice = "listen"; sel.value = choice; save(); host.announce();
        return null;
      })
      .finally(() => { if (generation === micGeneration) micWanted = null; });
    return micWanted;
  }
  function releaseMic() {
    ++micGeneration; micWanted = null;
    if (mic) for (const t of mic.getTracks()) t.stop();
    mic = null;
  }
  function applyMute() {
    if (mic) for (const t of mic.getAudioTracks()) t.enabled = choice === "on" || (choice === "ptt" && pttDown);
  }

  function close(id) {
    const link = links.get(id);
    if (!link) return;
    links.delete(id);
    try { link.pc.close(); } catch (e) { /* gone */ }
    if (link.audio) { link.audio.pause(); link.audio.srcObject = null; }
  }
  function closeAll() { for (const id of Array.from(links.keys())) close(id); }

  function open(id, offerer) {
    const g = host.game();
    const pc = new RTCPeerConnection({ iceServers: g.iceServers });
    const link = { pc, audio: null, offerer, pending: [] };
    links.set(id, link);
    if (mic && talks(choice)) for (const t of mic.getAudioTracks()) pc.addTrack(t, mic);
    else pc.addTransceiver("audio", { direction: "recvonly" });
    pc.onicecandidate = (e) => host.signal(id, { voice: "ice", candidate: e.candidate ? e.candidate.toJSON() : null });
    pc.ontrack = (e) => {
      const a = link.audio || (link.audio = new Audio());
      a.srcObject = e.streams[0] || new MediaStream([e.track]);
      a.autoplay = true;
      a.play().catch((err) => host.log(`voice: playing player ${id}: ${err.message}`));
      render();
    };
    pc.onconnectionstatechange = () => {
      host.log(`voice: player ${id} ${pc.connectionState}`);
      if (pc.connectionState === "failed") { close(id); setTimeout(update, 3000); }
      render();
    };
    return link;
  }
  async function offer(id) {
    const link = open(id, true);
    try {
      const sdp = await link.pc.createOffer();
      if (links.get(id) !== link) return;
      await link.pc.setLocalDescription(sdp);
      host.signal(id, { voice: "offer", sdp: link.pc.localDescription.sdp });
    } catch (e) { host.log(`voice: offer to player ${id} failed: ${e.message}`); }
  }
  async function flush(link) {
    const queued = link.pending.splice(0);
    for (const c of queued) { try { await link.pc.addIceCandidate(c || undefined); } catch (e) { /* stale */ } }
  }

  // Voice signaling from another player (a lobby message over the data channel).
  async function signal(from, data) {
    if (!data || typeof data !== "object" || typeof data.voice !== "string") return;
    const generation = roomGeneration;
    try {
      if (data.voice === "offer") {
        if (!everyoneIn()) return;  // (not for us: our choice, or somebody's, is off)
        close(from);
        if (micWanted) await micWanted;
        if (generation !== roomGeneration || !everyoneIn()) return;
        const link = open(from, false);
        await link.pc.setRemoteDescription({ type: "offer", sdp: data.sdp });
        await flush(link);
        const answer = await link.pc.createAnswer();
        if (links.get(from) !== link) return;
        await link.pc.setLocalDescription(answer);
        host.signal(from, { voice: "answer", sdp: link.pc.localDescription.sdp });
      } else if (data.voice === "answer") {
        const link = links.get(from);
        if (!link || !link.offerer || link.pc.signalingState !== "have-local-offer") return;
        await link.pc.setRemoteDescription({ type: "answer", sdp: data.sdp });
        await flush(link);
      } else if (data.voice === "ice") {
        const link = links.get(from);
        if (!link) return;
        if (link.pc.remoteDescription) { try { await link.pc.addIceCandidate(data.candidate || undefined); } catch (e) { /* stale */ } }
        else link.pending.push(data.candidate);
      }
    } catch (e) { host.log(`voice: signal (${data.voice}) from player ${from} failed: ${e.message}`); }
  }

  // Voice is on with everybody, or with nobody: the lower id offers.
  async function update() {
    const g = host.game(), generation = roomGeneration;
    if (!g || !everyoneIn()) { closeAll(); render(); return; }
    if (talks(choice) && !mic) await acquireMic();
    if (generation !== roomGeneration || g !== host.game()) return;
    if (!everyoneIn()) { closeAll(); render(); return; }  // the choice may have changed meanwhile
    for (const id of others()) {
      if (!links.has(id) && g.selfId < id) void offer(id);
    }
    for (const id of Array.from(links.keys())) if (!others().includes(id)) close(id);
    render();
  }

  function render() {
    const them = others();
    const waiting = them.filter((id) => !optedIn(remote.get(id)));
    let text = "";
    if (!them.length) text = optedIn(choice) ? "voice with whoever joins and opts in" : "";
    else if (!optedIn(choice)) text = waiting.length < them.length ? `${them.filter((id) => optedIn(remote.get(id))).map(name).join(", ")} opted in: choose a voice option to hear and be heard` : "";
    else if (waiting.length) text = `no voice until ${waiting.map(name).join(", ")} opts in`;
    else {
      const up = them.filter((id) => links.get(id) && links.get(id).pc.connectionState === "connected");
      text = up.length === them.length ? `voice on with ${up.map(name).join(", ")}` : "voice connecting…";
      text += choice === "ptt" ? " — hold ` to talk" : choice === "on" ? " — your mic is open" : " — listening";
    }
    state.textContent = text;
    // The control lights up for a player who has not opted in while another has.
    sel.parentElement.classList.toggle("attention", !optedIn(choice) && them.some((id) => optedIn(remote.get(id))));
  }

  // What Fly should say, or null: a voice on one side only.
  function warning() {
    const them = others();
    if (!them.length) return null;
    const in_ = them.filter((id) => optedIn(remote.get(id))), out = them.filter((id) => !optedIn(remote.get(id)));
    if (optedIn(choice) && out.length) return `No voice this session: ${out.map(name).join(", ")} did not opt in.`;
    if (!optedIn(choice) && in_.length) return `${in_.map(name).join(", ")} opted into voice and you are off: no voice this session.`;
    return null;
  }

  sel.addEventListener("change", async () => {
    const previous = choice;
    choice = CHOICES.includes(sel.value) ? sel.value : "off";
    pttDown = false;
    save(); applyMute();
    // PTT and Always share the same sending track; applyMute handles the switch.
    // Only changes to sending/receiving capability need a new negotiation.
    if (talks(previous) !== talks(choice) || optedIn(previous) !== optedIn(choice)) closeAll();
    if (talks(choice)) await acquireMic(); else releaseMic();
    host.announce();
    void update();
  });
  // Push to talk: the backquote key, which neither game uses, held down.  The
  // key stops here and does not reach the game.
  const ptt = (down) => { if (choice !== "ptt" || pttDown === down) return; pttDown = down; applyMute(); render(); };
  window.addEventListener("keydown", (e) => { if (e.code === PTT_CODE && choice === "ptt") { ptt(true); e.preventDefault(); e.stopPropagation(); } }, true);
  window.addEventListener("keyup", (e) => { if (e.code === PTT_CODE && choice === "ptt") { ptt(false); e.preventDefault(); e.stopPropagation(); } }, true);
  window.addEventListener("blur", () => ptt(false));

  render();
  return {
    choice: () => choice,
    remote: (id, c) => {
      c = CHOICES.includes(c) ? c : "off";
      const previous = remote.get(id);
      remote.set(id, c);
      // The lower id must offer again when the answerer changes Listen/Always.
      if (talks(previous) !== talks(c) || optedIn(previous) !== optedIn(c)) close(id);
      void update();
    },
    forget: (id) => { remote.delete(id); close(id); void update(); },
    signal,
    warning,
    ptt,
    update: () => void update(),
    stopAll: () => {
      ++roomGeneration; closeAll(); releaseMic(); remote.clear(); pttDown = false; render();
    },
    // For tests: the connection state of each voice link.
    links: () => Object.fromEntries(Array.from(links, ([id, l]) => [id, { state: l.pc.connectionState, hearing: !!(l.audio && l.audio.srcObject) }])),
  };
}
