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
  const resume = $("voiceResume");
  const details = $("voiceDetails"), diagnostics = $("voiceDiagnostics"), inputState = $("voiceInputState");
  const events = [];
  let micError = "";
  function log(message) {
    events.push({ time: new Date().toISOString(), message });
    if (events.length > 80) events.shift();
    host.log(message);
  }
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
    micError = "";
    micWanted = Promise.resolve().then(() => navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true, autoGainControl: true } }))
      .then((stream) => {
        if (generation !== micGeneration || !talks(choice)) {
          for (const t of stream.getTracks()) t.stop();
          return null;
        }
        mic = stream; applyMute(); render(); return stream;
      })
      .catch((e) => {
        if (generation !== micGeneration) return null;
        micError = `No microphone (${e.name}). Check Chrome and macOS microphone permissions and your input device.`;
        host.chatLine(micError + " Voice is listen only for you.", "sys");
        choice = "listen"; sel.value = choice; save(); host.announce();
        return null;
      })
      .finally(() => { if (generation === micGeneration) { micWanted = null; render(); } });
    return micWanted;
  }
  function releaseMic() {
    ++micGeneration; micWanted = null;
    if (mic) for (const t of mic.getTracks()) t.stop();
    mic = null;
    if (inputState) { inputState.hidden = true; inputState.textContent = ""; }
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

  function play(id, link) {
    if (!link.audio || link.playing) return;
    link.playing = true;
    // Invoke play inside the button's event handler to preserve user activation.
    link.audio.play().then(() => {
      if (links.get(id) !== link) return;
      link.playbackError = ""; render();
    }).catch((err) => {
      if (links.get(id) !== link) return;
      link.playbackError = `${err.name}: ${err.message}`;
      log(`voice: playing player ${id}: ${link.playbackError}`);
      render();
    }).finally(() => { link.playing = false; });
  }
  resume?.addEventListener("click", () => { for (const [id, link] of links) play(id, link); });

  function open(id, offerer) {
    const g = host.game();
    const pc = new RTCPeerConnection({ iceServers: g.iceServers });
    const link = { pc, audio: null, offerer, pending: [], playbackError: "" };
    links.set(id, link);
    if (mic && talks(choice)) for (const t of mic.getAudioTracks()) pc.addTrack(t, mic);
    else pc.addTransceiver("audio", { direction: "recvonly" });
    pc.onicecandidate = (e) => host.signal(id, { voice: "ice", candidate: e.candidate ? e.candidate.toJSON() : null });
    pc.ontrack = (e) => {
      const a = link.audio || (link.audio = new Audio());
      a.srcObject = e.streams[0] || new MediaStream([e.track]);
      a.autoplay = true;
      a.onplaying = () => { if (links.get(id) === link) { link.playbackError = ""; render(); } };
      play(id, link);
      render();
    };
    pc.onconnectionstatechange = () => {
      log(`voice: player ${id} ${pc.connectionState}`);
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
    } catch (e) { log(`voice: offer to player ${id} failed: ${e.name}`); }
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
    } catch (e) { log(`voice: signal (${data.voice}) from player ${from} failed: ${e.name}`); }
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
      text = up.length === them.length ? `voice connected with ${up.map(name).join(", ")}` : "voice connecting…";
      text += choice === "ptt" ? " — hold ` to talk" : choice === "on" && mic ? " — your mic is open" : choice === "listen" ? " — listening" : "";
    }
    const blocked = Array.from(links.values()).some((l) => l.playbackError);
    if (blocked) text += " — audio playback blocked; click Enable voice playback";
    if (resume) resume.hidden = !blocked;
    if (micError) text += " — " + micError;
    if (micWanted) text += " — waiting for microphone permission";
    if (mic?.getAudioTracks().some((t) => t.muted)) text += " — microphone input is muted or unavailable";
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
    pttDown = false; micError = "";
    save(); applyMute();
    // PTT and Always share the same sending track; applyMute handles the switch.
    // Only changes to sending/receiving capability need a new negotiation.
    if (talks(previous) !== talks(choice) || optedIn(previous) !== optedIn(choice)) closeAll();
    if (talks(choice)) await acquireMic(); else releaseMic();
    host.announce();
    void update();
  });
  // A report contains counters and states, never microphone recordings, SDP,
  // candidate IP addresses or TURN credentials. Sample twice to see progress.
  async function report() {
    const peers = [];
    for (const [id, link] of links) {
      const row = { id, choice: remote.get(id), connection: link.pc.connectionState,
        ice: link.pc.iceConnectionState, signaling: link.pc.signalingState,
        playback: !link.audio ? "no track" : link.playbackError || (link.audio.paused ? "paused" : "playing"),
        output: link.audio ? { muted: link.audio.muted, volume: link.audio.volume, readyState: link.audio.readyState } : null,
        directions: link.pc.getTransceivers().map((t) => ({ requested: t.direction, negotiated: t.currentDirection,
          sending: t.sender.track ? { enabled: t.sender.track.enabled, readyState: t.sender.track.readyState } : null })),
        receivedTrack: link.audio?.srcObject?.getAudioTracks().map((t) => ({readyState: t.readyState, muted: t.muted})),
        sentBytes: 0, receivedBytes: 0, receivedPackets: 0, receivedEnergy: 0 };
      try {
        const stats = await link.pc.getStats();
        stats.forEach((s) => {
          if (s.type === "outbound-rtp" && s.kind === "audio") {
            row.sentBytes += s.bytesSent || 0;
            const source = stats.get(s.mediaSourceId);
            if (source) { row.inputLevel = source.audioLevel; row.inputEnergy = source.totalAudioEnergy; }
          }
          if (s.type === "inbound-rtp" && s.kind === "audio") {
            row.receivedBytes += s.bytesReceived || 0; row.receivedPackets += s.packetsReceived || 0;
            row.receivedEnergy += s.totalAudioEnergy || 0; row.receivedLevel = s.audioLevel;
            row.packetsLost = s.packetsLost; row.jitter = s.jitter;
          }
          if (s.type === "transport" && s.selectedCandidatePairId) {
            const pair = stats.get(s.selectedCandidatePairId);
            row.route = {local: stats.get(pair?.localCandidateId)?.candidateType,
              remote: stats.get(pair?.remoteCandidateId)?.candidateType, roundTripSeconds: pair?.currentRoundTripTime};
          }
        });
      } catch (e) { row.statsError = e.name; }
      peers.push(row);
    }
    return { version: 1, time: new Date().toISOString(), choice,
      microphone: { pending: !!micWanted, error: micError,
        tracks: mic ? mic.getAudioTracks().map((t) => ({ label: t.label, enabled: t.enabled, muted: t.muted, readyState: t.readyState })) : [] },
      participants: others().map((id) => ({id, choice: remote.get(id) || "unknown"})),
      peers, events: events.slice() };
  }
  let inspecting = false, lastInputEnergy = null, lastMicGeneration = 0, quietSince = 0;
  async function showReport() {
    if (inspecting) return;
    inspecting = true;
    const generation = micGeneration;
    try {
      const snapshot = await report();
      if (generation !== micGeneration) return;
      if (details?.open && diagnostics) diagnostics.textContent = JSON.stringify(snapshot, null, 2);
      if (!inputState) return;
      const track = snapshot.microphone.tracks[0];
      inputState.hidden = !track;
      if (!track) return;
      if (lastMicGeneration !== generation) { lastInputEnergy = null; quietSince = 0; lastMicGeneration = generation; }
      const sources = snapshot.peers.filter((p) => typeof p.inputEnergy === "number");
      const energy = sources.length ? Math.max(...sources.map((p) => p.inputEnergy)) : null;
      let text;
      if (!track.enabled) { text = "muted — hold ` to talk"; quietSince = 0; }
      else if (track.muted) text = "input unavailable — check your microphone";
      else if (energy === null) text = "waiting for a voice connection to measure input";
      else if (lastInputEnergy !== null && energy > lastInputEnergy) { text = "sound detected"; quietSince = 0; }
      else {
        if (!quietSince) quietSince = Date.now();
        text = Date.now() - quietSince >= 5000
          ? "no sound detected. If you are speaking, check your input device, input volume, and laptop lid."
          : "speak to check input";
      }
      lastInputEnergy = energy;
      inputState.textContent = `${track.label || "Microphone"}: ${text}`;
    }
    finally { inspecting = false; }
  }
  details?.addEventListener("toggle", () => { if (details.open) void showReport(); });
  setInterval(() => { if (details?.open || mic) void showReport(); }, 1000);
  $("voiceCopy")?.addEventListener("click", async () => {
    const text = JSON.stringify(await report(), null, 2);
    try { await navigator.clipboard.writeText(text); diagnostics.textContent = "Copied voice report.\n" + text; }
    catch (e) { diagnostics.textContent = "Select and copy this report:\n" + text; }
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
      ++roomGeneration; closeAll(); releaseMic(); remote.clear(); pttDown = false; micError = ""; render();
    },
    report,
    // The legacy hearing flag only confirms a track exists. report() separates
    // network counters from actual incoming energy and browser playback.
    links: () => Object.fromEntries(Array.from(links, ([id, l]) => [id, { state: l.pc.connectionState, hearing: !!(l.audio && l.audio.srcObject) }])),
  };
}
