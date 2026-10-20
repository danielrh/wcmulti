#!/usr/bin/env python3
"""One self-contained page that plays the built rooms the way the game will:

    roompreview.py [--out build/places/rooms.html]

It reads art/places/KEY/place.json (places.py's output) and the cast's
talking faces (art/people/faces/), embeds every picture, and draws at
640x400:

    bar, lounge   pick who sits in each seat; the mouse over a pilot turns
                  them to the player; a click opens their close-up, the
                  portrait saying their line (data/people.csv) in lip sync.
                  Tally polishes on his own and looks up when pointed at.
    briefing      the XO speaks her lines (mouth from the text, gesture by
                  sentence), a click on her zooms in, then the pilots rise.
    quarters,     the hotspots, named under the picture
    locker
    deck          Kit's loops in front of the chosen fighter on its legs
                  (with --deck)

The hotspot and board outlines can be shown with a tick box.
"""
import argparse
import base64
import csv
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import lipsync  # noqa: E402

ROOMS = ['bar', 'lounge', 'briefing', 'quarters', 'locker']


def uri(path):
    with open(path, 'rb') as f:
        return 'data:image/png;base64,' + base64.b64encode(f.read()).decode()


def pngs(obj):
    if isinstance(obj, dict):
        for v in obj.values():
            yield from pngs(v)
    elif isinstance(obj, list):
        for v in obj:
            yield from pngs(v)
    elif isinstance(obj, str) and obj.endswith('.png'):
        yield obj


def people_in(place):
    keys = set()
    for seat in place.get('seats', {}).values():
        keys |= set(seat)
    for a in place.get('actors', {}).values():
        if a.get('person'):
            keys.add(a['person'])
    if place.get('speaker', {}).get('person'):
        keys.add(place['speaker']['person'])
    return keys


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', default=os.path.join(ROOT, 'build', 'places', 'rooms.html'))
    ap.add_argument('--deck', action='store_true', help='the deck too (its fighters add 5 MB)')
    args = ap.parse_args()
    with open(os.path.join(ROOT, 'data', 'people.csv')) as f:
        people = {r['key']: r for r in csv.DictReader(f)}
    rooms, faces = {}, {}
    for key in ROOMS + (['deck'] if args.deck else []):
        d = os.path.join(ROOT, 'art', 'places', key)
        if not os.path.exists(os.path.join(d, 'place.json')):
            continue
        with open(os.path.join(d, 'place.json')) as f:
            place = json.load(f)
        rooms[key] = {'place': place, 'img': {p: uri(os.path.join(d, p)) for p in set(pngs(place))}}
        for k in people_in(place):
            fd = os.path.join(ROOT, 'art', 'people', 'faces', k)
            if k in faces or not os.path.isdir(fd):
                continue
            with open(os.path.join(fd, 'face.json')) as f:
                fj = json.load(f)
            p = people[k]
            names = ['base'] + ['mouth_%d' % i for i in range(9)] + ['eyes_%d' % i for i in range(3)]
            faces[k] = {'name': p['name'], 'callsign': p['callsign'], 'role': p['role'], 'line': p['line'],
                        'steps': lipsync.frames(p['line'], frames_per_step=1), 'step_ms': lipsync.STEP_MS,
                        'eye_box': fj['eye_box'], 'mouth_box': fj['mouth_box'],
                        'img': {n: uri(os.path.join(fd, n + '.png')) for n in names}}
    page = PAGE.replace('/*DATA*/', 'const ROOMS = %s;\nconst FACES = %s;' % (json.dumps(rooms), json.dumps(faces)))
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, 'w') as f:
        f.write(page)
    print('%s: %d rooms, %d faces, %.1f MB' % (args.out, len(rooms), len(faces), len(page) / 1e6))


PAGE = r'''<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Carrier Rooms</title>
<style>
:root{--bg:#14181b;--panel:#1f2529;--ink:#e6e2d8;--dim:#a9aca6;--accent:#d29a3a;--line:#3a434a}
@media (prefers-color-scheme: light){:root:not([data-theme="dark"]){--bg:#efece4;--panel:#e2ddd2;--ink:#1d2024;--dim:#555a5e;--accent:#9a6512;--line:#b9b2a3}}
:root[data-theme="light"]{--bg:#efece4;--panel:#e2ddd2;--ink:#1d2024;--dim:#555a5e;--accent:#9a6512;--line:#b9b2a3}
:root[data-theme="dark"]{--bg:#14181b;--panel:#1f2529;--ink:#e6e2d8;--dim:#a9aca6;--accent:#d29a3a;--line:#3a434a}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font:15px/1.4 system-ui,sans-serif}
main{max-width:1300px;margin:0 auto;padding:16px}
h1{font-size:20px;margin:0 0 4px}
p.sub{margin:0 0 12px;color:var(--dim)}
nav{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:10px}
nav button,.ctl button{background:var(--panel);color:var(--ink);border:1px solid var(--line);border-radius:6px;padding:7px 12px;cursor:pointer;font:inherit}
nav button.on{border-color:var(--accent);color:var(--accent)}
canvas{width:100%;max-width:1280px;aspect-ratio:16/10;display:block;image-rendering:pixelated;background:#000;border-radius:6px}
.ctl{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin:10px 0;min-height:36px}
.ctl label{display:flex;gap:6px;align-items:center;color:var(--dim)}
select{background:var(--panel);color:var(--ink);border:1px solid var(--line);border-radius:6px;padding:5px}
#status{color:var(--accent);min-height:22px}
#about{color:var(--dim);margin:6px 0 0}
</style></head><body><main>
<h1>Carrier rooms</h1>
<p class="sub">The built 640&times;400 layers, composed as the game will. Point at people and things; click them.</p>
<nav id="tabs"></nav>
<canvas id="cv" width="640" height="400"></canvas>
<div id="status"></div>
<div class="ctl" id="ctl"></div>
<label class="ctl"><input type="checkbox" id="dbg"> show hotspots and boards</label>
<p id="about"></p>
</main>
<script>
/*DATA*/
const cv = document.getElementById('cv'), ctx = cv.getContext('2d');
ctx.imageSmoothingEnabled = false;
const IMG = {}, ALPHA = new Map();
function load(src) { return new Promise(r => { const im = new Image(); im.onload = () => r(im); im.src = src; }); }
async function loadAll() {
  for (const [k, r] of Object.entries(ROOMS)) { IMG[k] = {}; for (const [n, u] of Object.entries(r.img)) IMG[k][n] = await load(u); }
  for (const [k, f] of Object.entries(FACES)) { f.im = {}; for (const [n, u] of Object.entries(f.img)) f.im[n] = await load(u); }
}
function alphaOf(im) {
  if (!ALPHA.has(im)) {
    const c = document.createElement('canvas'); c.width = im.width; c.height = im.height;
    const x = c.getContext('2d'); x.drawImage(im, 0, 0);
    ALPHA.set(im, x.getImageData(0, 0, im.width, im.height).data);
  }
  return ALPHA.get(im);
}
function hitSprite(im, x0, y0, x, y) {
  const lx = Math.floor(x - x0), ly = Math.floor(y - y0);
  if (lx < 0 || ly < 0 || lx >= im.width || ly >= im.height) return false;
  return alphaOf(im)[(ly * im.width + lx) * 4 + 3] > 0;
}
const inRect = (r, x, y) => x >= r[0] && y >= r[1] && x < r[0] + r[2] && y < r[1] + r[3];

let room = null, P = null, I = null, picks = {}, hover = null, mode = 'room', talk = null, t0 = performance.now();
let brief = null, stage = 0, xoZoom = false, deckShip = null;
const status = document.getElementById('status'), ctl = document.getElementById('ctl');

const DEFAULT_PICKS = {bar: {left: 'TECH', middle: 'ECHO', right: 'ANCHOR'},
                       lounge: {left: 'COMET', middle: 'TORCH', right: 'FROST'}};
function setRoom(k) {
  room = k; P = ROOMS[k].place; I = IMG[k]; mode = 'room'; talk = null; hover = null; brief = null; stage = 0; xoZoom = false;
  document.querySelectorAll('#tabs button').forEach(b => b.classList.toggle('on', b.dataset.k === k));
  document.getElementById('about').textContent = P.about || '';
  ctl.innerHTML = '';
  if (P.seats) {
    picks = Object.assign({}, DEFAULT_PICKS[k] || {});
    for (const [seat, pilots] of Object.entries(P.seats)) {
      const lab = document.createElement('label'); lab.textContent = seat + ' ';
      const sel = document.createElement('select');
      sel.innerHTML = '<option value="">(empty)</option>' + Object.keys(pilots).map(p =>
        `<option value="${p}">${FACES[p] ? FACES[p].callsign || p : p}</option>`).join('');
      sel.value = picks[seat] || ''; sel.onchange = () => { picks[seat] = sel.value; };
      lab.appendChild(sel); ctl.appendChild(lab);
    }
  }
  if (P.speaker) {
    const b = document.createElement('button'); b.textContent = 'Play the briefing';
    b.onclick = () => { brief = {start: performance.now(), line: P.speaker.lines[0]}; stage = 0; };
    const r = document.createElement('button'); r.textContent = 'Reset';
    r.onclick = () => { brief = null; stage = 0; xoZoom = false; };
    ctl.append(b, r);
  }
  if (P.fighter) {
    const lab = document.createElement('label'); lab.textContent = 'fighter ';
    const sel = document.createElement('select');
    sel.innerHTML = Object.keys(P.fighter.ships).map(s => `<option>${s}</option>`).join('');
    deckShip = Object.keys(P.fighter.ships).includes('REDEEMR') ? 'REDEEMR' : Object.keys(P.fighter.ships)[0];
    sel.value = deckShip; sel.onchange = () => { deckShip = sel.value; };
    lab.appendChild(sel); ctl.appendChild(lab);
  }
}

// ---------- the rooms' parts
function seatSprite(seat, pilot) {
  const poses = P.seats[seat][pilot];
  const turned = (hover && hover.kind === 'pilot' && hover.key === pilot) || (talk && talk.key === pilot);
  const s = (turned && poses.face) || poses.talk || poses.face;
  return [I[s.file], s.x, s.y];
}
function actorFrame(a, now) {
  if ((hover && hover.kind === 'actor' && hover.key === a.person) || (talk && talk.key === a.person)) return a.hover;
  const idle = a.clips.idle, glance = a.clips.glance;
  const loopMs = idle.reduce((s, c) => s + c[1], 0), period = 12000;
  const t = (now - t0) % period;
  if (glance && t > period - glance[0][1]) return glance[0][0];
  let u = t % loopMs;
  for (const [f, ms] of idle) { if (u < ms) return f; u -= ms; }
  return idle[0][0];
}
function drawActor(a, now) {
  const k = a.frames.indexOf(actorFrame(a, now)), [x, y, w, h] = a.rect, c = a.columns;
  ctx.drawImage(I[a.atlas], (k % c) * w, Math.floor(k / c) * h, w, h, x, y, w, h);
}
function speakerCell(now) {
  if (!brief) return [0, 0];
  let t = now - brief.start;
  for (const [g, m, ms] of brief.line.steps) { if (t < ms) return [g, m]; t -= ms; }
  return [0, 0];
}
function briefingTime(now) { return brief ? now - brief.start - brief.line.steps.reduce((s, c) => s + c[2], 0) : -1; }
function drawChalk(r) {
  ctx.save(); ctx.fillStyle = 'rgba(235,235,225,0.85)'; ctx.font = '9px monospace';
  const rows = [['SPLASH COUNT', ''], ['Prime', '14'], ['Echo', '12'], ['Torch', '11'], ['Comet', '9']];
  rows.forEach(([a, b], i) => { ctx.fillText(a, r[0] + 6, r[1] + 12 + i * 10); if (b) ctx.fillText(b, r[0] + r[2] - 6 - ctx.measureText(b).width, r[1] + 12 + i * 10); });
  ctx.restore();
}
function drawMedals(r) {
  ctx.save();
  const cols = ['#b23', '#26a', '#3a5'];
  for (let i = 0; i < 3; i++) {
    const cx = r[0] + r[2] * (0.2 + 0.3 * i), cy = r[1] + r[3] * 0.72;
    ctx.fillStyle = cols[i]; ctx.fillRect(cx - 3, cy - 9, 6, 6);
    ctx.fillStyle = '#d9b44a'; ctx.beginPath(); ctx.arc(cx, cy, 3.2, 0, 7); ctx.fill();
  }
  ctx.restore();
}
const layer = document.createElement('canvas'); layer.width = 640; layer.height = 400;
const lc = layer.getContext('2d');
function drawDeck(now) {
  const F = P.fighter, s = F.ships[deckShip], L = F.legs[s.legs];
  ctx.drawImage(I[P.back], 0, 0);
  ctx.drawImage(I[s.under], s.ux, s.uy);
  const foot = I[L.foot], strut = I[L.strut], saddle = I[L.saddle];
  for (const [sx, top] of s.sleds) {
    const H = Math.max(F.deck - top + L.seat, foot.height + saddle.height), x = sx - L.axis, y = F.deck - H;
    for (let yy = saddle.height; yy < H - foot.height + 2; yy++) ctx.drawImage(strut, x, y + yy);
    ctx.drawImage(foot, x, y + H - foot.height); ctx.drawImage(saddle, x, y);
  }
  ctx.drawImage(I[s.sprite], s.x, s.y);
  const A = P.actor, order = ['idle', 'console-work', 'ready-nod'];
  const lens = order.map(c => A.clips[c].ms.reduce((a, b) => a + b, 0)), total = lens.reduce((a, b) => a + b, 0);
  let t = (now - t0) % total, ci = 0;
  while (t >= lens[ci]) { t -= lens[ci]; ci++; }
  const clip = A.clips[order[ci]]; let k = 0;
  while (k < clip.frames - 1 && t >= clip.ms[k]) { t -= clip.ms[k]; k++; }
  const [px, py, pw, ph] = A.patch;
  lc.clearRect(0, 0, 640, 400); lc.drawImage(I[P.front], 0, 0); lc.clearRect(px, py, pw, ph);
  lc.drawImage(I[clip.atlas], (k % clip.columns) * pw, Math.floor(k / clip.columns) * ph, pw, ph, px, py, pw, ph);
  ctx.drawImage(layer, 0, 0);
}

// ---------- close-ups
function drawTalk(now) {
  ctx.save(); ctx.filter = 'blur(5px) brightness(0.55)'; ctx.drawImage(I[P.back], -20, -12, 680, 425); ctx.restore();
  const f = FACES[talk.key], t = now - talk.start, step = Math.floor(t / f.step_ms);
  const v = step < f.steps.length ? f.steps[step] : 0;
  const k = 1.45, size = 256 * k, x0 = 24, y0 = 400 - size + 14;
  ctx.imageSmoothingEnabled = true;
  ctx.drawImage(f.im.base, x0, y0, size, size);
  const blink = (t % 3700) < 160 ? ((t % 3700) < 60 || (t % 3700) > 110 ? 1 : 2) : 0;
  const e = f.eye_box, m = f.mouth_box;
  ctx.drawImage(f.im['eyes_' + blink], x0 + e[0] * k, y0 + e[1] * k, (e[2] - e[0]) * k, (e[3] - e[1]) * k);
  ctx.drawImage(f.im['mouth_' + v], x0 + m[0] * k, y0 + m[1] * k, (m[2] - m[0]) * k, (m[3] - m[1]) * k);
  ctx.imageSmoothingEnabled = false;
  ctx.fillStyle = '#d29a3a'; ctx.font = 'bold 16px system-ui';
  ctx.fillText(f.callsign ? `"${f.callsign}"` : f.role.toUpperCase(), 420, 150);
  ctx.fillStyle = '#e6e2d8'; ctx.font = '13px system-ui'; ctx.fillText(f.name, 420, 170);
  wrap(f.line.slice(0, Math.max(0, Math.round(t / 55))), 420, 205, 200, 17);
  if (t > f.steps.length * f.step_ms + 1800) { talk = null; mode = 'room'; }
}
function wrap(text, x, y, w, lh) {
  ctx.fillStyle = '#f0ece0'; ctx.font = '14px system-ui';
  let line = '';
  for (const word of text.split(' ')) {
    const t = line ? line + ' ' + word : word;
    if (ctx.measureText(t).width > w && line) { ctx.fillText(line, x, y); y += lh; line = word; } else line = t;
  }
  ctx.fillText(line, x, y);
}
function drawXoZoom(now) {
  const S = P.speaker, C = S.closeup, [g, m] = speakerCell(now), [w, h] = C.size, k = g * C.columns + m;
  ctx.drawImage(I[C.back], 0, 0);
  ctx.drawImage(I[C.atlas], (k % C.columns) * w, Math.floor(k / C.columns) * h, w, h, C.x, C.y, w, h);
}

// ---------- the frame
function frame(now) {
  if (room === 'deck') drawDeck(now);
  else if (mode === 'talk') drawTalk(now);
  else if (xoZoom) drawXoZoom(now);
  else {
    ctx.drawImage(I[P.back], 0, 0);
    if (P.boards && P.boards.chalkboard) drawChalk(P.boards.chalkboard);
    if (P.boards && P.boards.medal_case) drawMedals(P.boards.medal_case);
    for (const [seat, who] of Object.entries(picks)) if (who && P.seats && P.seats[seat][who]) {
      const [im, x, y] = seatSprite(seat, who); ctx.drawImage(im, x, y);
    }
    for (const a of Object.values(P.actors || {})) drawActor(a, now);
    if (P.speaker) {
      const S = P.speaker, [g, m] = speakerCell(now), k = g * S.columns + m, [w, h] = S.size;
      ctx.drawImage(I[S.atlas], (k % S.columns) * w, Math.floor(k / S.columns) * h, w, h, S.x, S.y, w, h);
      const after = briefingTime(now);
      if (after > 600) stage = Math.min(P.stages.order.length - 1, 1 + Math.floor((after - 600) / P.stages.ms));
      const st = P.stages.poses[P.stages.order[stage]];
      if (st) ctx.drawImage(I[st.file], st.x, st.y);
    }
  }
  if (brief && briefingTime(now) < 0 && room === 'briefing') {
    ctx.fillStyle = 'rgba(0,0,0,0.55)'; ctx.fillRect(0, 368, 640, 32);
    let t = now - brief.start, said = 0, total = 0;
    for (const s of brief.line.steps) total += s[2];
    said = Math.round(brief.line.text.length * Math.min(1, t / total));
    ctx.fillStyle = '#f0ece0'; ctx.font = '12px system-ui';
    const txt = brief.line.text.slice(0, said), parts = txt.split('. ');
    ctx.fillText(parts[parts.length - 1].slice(-95), 10, 388);
  }
  if (document.getElementById('dbg').checked && mode === 'room' && !xoZoom) {
    ctx.save(); ctx.lineWidth = 1;
    ctx.strokeStyle = '#ff0'; for (const h of Object.values(P.hotspots || {})) ctx.strokeRect(h.rect[0] + .5, h.rect[1] + .5, h.rect[2], h.rect[3]);
    ctx.strokeStyle = '#0ff'; for (const r of Object.values(P.boards || {})) ctx.strokeRect(r[0] + .5, r[1] + .5, r[2], r[3]);
    ctx.restore();
  }
  requestAnimationFrame(frame);
}

// ---------- the mouse
function at(ev) { const r = cv.getBoundingClientRect(); return [(ev.clientX - r.left) * 640 / r.width, (ev.clientY - r.top) * 400 / r.height]; }
function find(x, y) {
  if (room === 'deck' || mode !== 'room') return null;
  if (xoZoom) return {kind: 'zoom', label: 'back to the room'};
  for (const a of Object.values(P.actors || {})) if (inRect(a.rect, x, y)) {
    const k = a.frames.indexOf(actorFrame(a, performance.now())), [ax, ay, w, h] = a.rect;
    return {kind: 'actor', key: a.person, label: FACES[a.person] ? `${FACES[a.person].name} ("${FACES[a.person].callsign}")` : a.person};
  }
  if (P.seats) for (const [seat, who] of Object.entries(picks)) if (who) {
    for (const pose of ['talk', 'face']) {
      const s = P.seats[seat][who][pose];
      if (s && hitSprite(I[s.file], s.x, s.y, x, y)) {
        const f = FACES[who];
        return {kind: 'pilot', key: who, label: f ? `${f.name} ("${f.callsign}")` : who};
      }
    }
  }
  for (const [n, h] of Object.entries(P.hotspots || {})) if (inRect(h.rect, x, y)) return {kind: 'spot', key: n, does: h.does, label: `${n} → ${h.does}`};
  return null;
}
cv.onmousemove = ev => {
  const [x, y] = at(ev); hover = find(x, y);
  cv.style.cursor = hover ? 'pointer' : 'default';
  status.textContent = hover ? hover.label : '';
};
cv.onmouseleave = () => { hover = null; status.textContent = ''; };
cv.onclick = ev => {
  if (mode === 'talk') { mode = 'room'; talk = null; return; }
  if (xoZoom) { xoZoom = false; return; }
  const [x, y] = at(ev), h = find(x, y);
  if (!h) return;
  if ((h.kind === 'pilot' || h.kind === 'actor') && FACES[h.key]) { talk = {key: h.key, start: performance.now()}; mode = 'talk'; }
  else if (h.kind === 'spot' && h.does === 'closeup') xoZoom = true;
  else if (h.kind === 'spot') status.textContent = `${h.key}: the game would now ${h.does}`;
};

loadAll().then(() => {
  const tabs = document.getElementById('tabs');
  for (const k of Object.keys(ROOMS)) {
    const b = document.createElement('button'); b.textContent = k; b.dataset.k = k; b.onclick = () => setRoom(k); tabs.appendChild(b);
  }
  setRoom(Object.keys(ROOMS)[0]);
  requestAnimationFrame(frame);
});
</script></body></html>
'''


if __name__ == '__main__':
    main()
