// Voice-only regression tests: real Chrome WebRTC, synthetic microphones, no
// game files or lobby server. Uses Chrome's normal autoplay policy.
// PLAYWRIGHT_DIR=/path/to/playwright-install node scripts/web-voice-test.mjs
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { readFile, mkdtemp, writeFile, rm } from "node:fs/promises";
import { createServer } from "node:http";
import { tmpdir } from "node:os";
import path from "node:path";

const require = createRequire(path.resolve(process.env.PLAYWRIGHT_DIR || process.cwd(), "package.json"));
const { chromium } = require("playwright");
const source = await readFile(new URL("../web/voice.js", import.meta.url), "utf8");
const fixture = `<!doctype html><label id="voiceLabel">Voice <select id="voice">
  <option value="off">Off</option><option value="listen">Listen only</option>
  <option value="ptt">Push to talk</option><option value="on">Always on</option>
  </select></label><div id="voiceState"></div>
  <button id="voiceResume" hidden>Enable voice playback</button>
  <script type="module">
    import { initVoice } from '/voice.js';
    const id = Number(location.search.slice(1));
    window.game = { selfId: id, code: 'VOICE-TEST', iceServers: [],
      players: [{id: 0, occupied: true}, {id: 1, occupied: true}] };
    window.events = [];
    window.voice = initVoice({ game: () => window.game, names: () => new Map([[0, 'HOST'], [1, 'WINGMAN']]),
      chatLine: (text) => events.push(text), log: (text) => events.push(text),
      announce: () => announce(voice.choice()), signal: (to, data) => signal(to, data) });
    window.ready = true;
  </script>`;
const server = createServer((req, res) => {
  res.setHeader("Content-Type", req.url === "/voice.js" ? "text/javascript" : "text/html");
  res.end(req.url === "/voice.js" ? source : fixture);
});
await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
const url = `http://127.0.0.1:${server.address().port}/`;
const temp = await mkdtemp(path.join(tmpdir(), "wc-voice-"));
// A continuous tone makes totalAudioEnergy a deterministic assertion, instead
// of mistaking silent RTP packets or the ontrack event for working audio.
const samples = 48000 * 2, wav = Buffer.alloc(44 + samples * 2);
wav.write("RIFF"); wav.writeUInt32LE(wav.length - 8, 4); wav.write("WAVEfmt ", 8);
wav.writeUInt32LE(16, 16); wav.writeUInt16LE(1, 20); wav.writeUInt16LE(1, 22);
wav.writeUInt32LE(48000, 24); wav.writeUInt32LE(96000, 28);
wav.writeUInt16LE(2, 32); wav.writeUInt16LE(16, 34); wav.write("data", 36);
wav.writeUInt32LE(samples * 2, 40);
for (let i = 0; i < samples; i++) wav.writeInt16LE(Math.round(8000 * Math.sin(2 * Math.PI * 440 * i / 48000)), 44 + i * 2);
await writeFile(path.join(temp, "tone.wav"), wav);
let browser;
const errors = [];
try {
  browser = await chromium.launch({ headless: true,
    ...(process.env.CHROME ? {executablePath: process.env.CHROME} : {}),
    args: ["--use-fake-device-for-media-stream", "--use-fake-ui-for-media-stream",
      `--use-file-for-fake-audio-capture=${path.join(temp, "tone.wav")}`] });
  async function pair({ delayMic = false, blockPlayback = false, savedChoice = "off", silentMic = false } = {}) {
    const contexts = await Promise.all([browser.newContext(), browser.newContext()]);
    const pages = await Promise.all(contexts.map((c) => c.newPage()));
    for (const [id, page] of pages.entries()) {
      page.on("pageerror", (e) => errors.push(e.message));
      await page.addInitScript(({ delayMic, blockPlayback, savedChoice, silentMic }) => {
        if (localStorage.getItem("wc:voice") === null) localStorage.setItem("wc:voice", savedChoice);
        window.pcs = [];
        const RTC = window.RTCPeerConnection;
        window.RTCPeerConnection = class extends RTC {
          constructor(config) { super(config); window.pcs.push(this); }
        };
        window.captured = [];
        const gum = navigator.mediaDevices.getUserMedia.bind(navigator.mediaDevices);
        navigator.mediaDevices.getUserMedia = async (...args) => {
          if (window.delayMic) await new Promise((resolve) => { window.resolveMic = resolve; });
          // Both synthetic mics play the same tone. Disable processing in the
          // fixture so echo cancellation/noise suppression cannot remove that
          // tone as stationary noise or a copy of the other synthetic mic.
          let stream = await gum({ ...args[0], audio: { ...args[0].audio, echoCancellation: false,
            noiseSuppression: false, autoGainControl: false } });
          if (silentMic) {
            stream.getTracks().forEach((t) => t.stop());
            window.silentContext = new AudioContext();
            stream = silentContext.createMediaStreamDestination().stream;
          }
          window.captured.push(stream); return stream;
        };
        window.delayMic = delayMic;
        const play = HTMLMediaElement.prototype.play;
        window.blockPlayback = blockPlayback;
        const autoplay = Object.getOwnPropertyDescriptor(HTMLMediaElement.prototype, "autoplay");
        Object.defineProperty(HTMLMediaElement.prototype, "autoplay", { ...autoplay,
          set(value) { autoplay.set.call(this, window.blockPlayback ? false : value); } });
        HTMLMediaElement.prototype.play = function () {
          if (window.blockPlayback) return Promise.reject(new DOMException("Playback needs a gesture", "NotAllowedError"));
          return play.call(this);
        };
      }, { delayMic, blockPlayback, savedChoice, silentMic });
      await page.exposeFunction("announce", (choice) => pages[1-id].evaluate(({id, choice}) => window.voice?.remote(id, choice), {id, choice}));
      await page.exposeFunction("signal", (to, data) => pages[to].evaluate(({id, data}) => { void window.voice?.signal(id, data); }, {id, data}));
      await page.goto(url + "?" + id);
      await page.waitForFunction(() => window.ready);
    }
    if (savedChoice !== "off") {
      await Promise.all(pages.map((p, id) => p.evaluate(({id, savedChoice}) => voice.remote(1-id, savedChoice), {id, savedChoice})));
    }
    return { pages, close: () => Promise.all(contexts.map((c) => c.close())) };
  }
  const choose = (page, choice) => page.selectOption("#voice", choice);
  async function connected(pages) {
    await Promise.all(pages.map((p) => p.waitForFunction(() => Object.values(voice.links()).some((l) => l.state === "connected"), null, {timeout: 12000})));
  }
  const energy = (page) => page.evaluate(async () => {
    let total = 0;
    for (const pc of window.pcs.filter((pc) => pc.connectionState === "connected")) {
      (await pc.getStats()).forEach((s) => { if (s.type === "inbound-rtp" && s.kind === "audio") total += s.totalAudioEnergy || 0; });
    }
    return total;
  });
  async function hearsTone(page) {
    // Poll resolved stats in Node; do not let a Promise itself satisfy a page
    // predicate before any decoded audio has actually arrived.
    const deadline = Date.now() + 12000;
    while (Date.now() < deadline) {
      if (await energy(page) > 0.01) return;
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    assert.fail("no non-silent incoming audio within 12 seconds");
  }
  async function test(name, fn, options) {
    const session = await pair(options);
    try { await fn(session.pages); console.log("ok  " + name); }
    catch (e) {
      console.error("FAIL " + name + ": " + e.message);
      for (const [i, p] of session.pages.entries()) console.error(i, await p.evaluate(async () => ({state: document.getElementById('voiceState').textContent, links: voice.links(), events})));
      process.exitCode = 1;
    } finally { await session.close(); }
  }
  await test("always on sends non-silent audio both ways", async (pages) => {
    await choose(pages[0], "on"); await choose(pages[1], "on");
    await connected(pages); await Promise.all(pages.map(hearsTone));
    const received = await Promise.all(pages.map(energy));
    assert(received.every((e) => e > 0.01));
  });
  await test("push to talk -> always unmutes the existing microphone", async (pages) => {
    await choose(pages[0], "on"); await choose(pages[1], "ptt"); await connected(pages);
    await choose(pages[1], "on");
    assert.equal(await pages[1].evaluate(() => captured.at(-1).getAudioTracks()[0].enabled), true);
    await connected(pages); await hearsTone(pages[0]);
  });
  await test("listen only -> always renegotiates from either player", async (pages) => {
    await choose(pages[0], "on"); await choose(pages[1], "listen"); await connected(pages);
    await choose(pages[1], "on"); await connected(pages); await hearsTone(pages[0]);
    await choose(pages[0], "listen"); await connected(pages); await hearsTone(pages[0]);
    await choose(pages[0], "on"); await connected(pages); await hearsTone(pages[1]);
  });
  await test("opting out while microphone permission is pending stops late capture", async (pages) => {
    await choose(pages[0], "on");
    await pages[0].waitForFunction(() => !!window.resolveMic);
    await choose(pages[0], "off");
    await pages[0].evaluate(() => { window.delayMic = false; window.resolveMic(); });
    await pages[0].waitForFunction(() => captured.length > 0);
    assert.equal(await pages[0].evaluate(() => captured[0].getAudioTracks()[0].readyState), "ended");
  }, {delayMic: true});
  await test("leaving clears peers and releases the microphone", async (pages) => {
    await choose(pages[0], "on"); await choose(pages[1], "on"); await connected(pages);
    await pages[0].evaluate(() => { voice.stopAll(); window.game = null; });
    assert.deepEqual(await pages[0].evaluate(() => voice.links()), {});
    assert.equal(await pages[0].evaluate(() => captured[0].getAudioTracks()[0].readyState), "ended");
  });
  await test("blocked playback is visible and recoverable", async (pages) => {
    await choose(pages[0], "on"); await choose(pages[1], "on"); await connected(pages);
    await pages[0].waitForSelector("#voiceResume", {state: "visible", timeout: 3000});
    await pages[0].evaluate(() => { window.blockPlayback = false; });
    await pages[0].click("#voiceResume");
    await pages[0].waitForSelector("#voiceResume", {state: "hidden", timeout: 3000});
    await hearsTone(pages[0]);
  }, {blockPlayback: true});
  assert.deepEqual(errors, [], "no uncaught page errors");
} finally {
  await browser?.close();
  await new Promise((resolve) => server.close(resolve));
  await rm(temp, {recursive: true, force: true});
}
