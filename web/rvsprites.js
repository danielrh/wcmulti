// rvsprites.js: a ship's sprite set (SHIPnn.RVS) made from its views (SHIPnn.RV) in
// JavaScript: the same bytes as retrovega/tools/rvsprites.c, which it mirrors step
// by step (the integer arithmetic is kept on 32-bit values so the two agree).
// Used by the page when it fetches RetroVega (wc.js), and by
// scripts/rvsprites-check.mjs under node, which compares its output with the C tool's.
"use strict";

const QSIN = [0,101,201,302,402,503,603,704,804,904,1005,1105,1205,1306,1406,1506,1606,1706,1806,1906,2006,2105,2205,2305,2404,2503,2603,2702,2801,2900,2999,3098,3196,3295,3393,3492,3590,3688,3786,3883,3981,4078,4176,4273,4370,4467,4563,4660,4756,4852,4948,5044,5139,5235,5330,5425,5520,5614,5708,5803,5897,5990,6084,6177,6270,6363,6455,6547,6639,6731,6823,6914,7005,7096,7186,7276,7366,7456,7545,7635,7723,7812,7900,7988,8076,8163,8250,8337,8423,8509,8595,8680,8765,8850,8935,9019,9102,9186,9269,9352,9434,9516,9598,9679,9760,9841,9921,10001,10080,10159,10238,10316,10394,10471,10549,10625,10702,10778,10853,10928,11003,11077,11151,11224,11297,11370,11442,11514,11585,11656,11727,11797,11866,11935,12004,12072,12140,12207,12274,12340,12406,12472,12537,12601,12665,12729,12792,12854,12916,12978,13039,13100,13160,13219,13279,13337,13395,13453,13510,13567,13623,13678,13733,13788,13842,13896,13949,14001,14053,14104,14155,14206,14256,14305,14354,14402,14449,14497,14543,14589,14635,14680,14724,14768,14811,14854,14896,14937,14978,15019,15059,15098,15137,15175,15213,15250,15286,15322,15357,15392,15426,15460,15493,15525,15557,15588,15619,15649,15679,15707,15736,15763,15791,15817,15843,15868,15893,15917,15941,15964,15986,16008,16029,16049,16069,16088,16107,16125,16143,16160,16176,16192,16207,16221,16235,16248,16261,16273,16284,16295,16305,16315,16324,16332,16340,16347,16353,16359,16364,16369,16373,16376,16379,16381,16383,16384,16384];
const NSIZES = 13, NROLLS = 32;   // (32 steps of 11.25 degrees since 2026-10-22: tools/rvsprites.c's)
const SCALES = [256, 215, 181, 152, 128, 108, 91, 76, 64, 54, 45, 38, 32, 27, 22, 19, 16, 13, 11, 9, 8];   // (a set's NSIZES sizes start step0 in)

function lfSin(a) {
  a &= 1023;
  if (a < 256) return QSIN[a];
  if (a < 512) return QSIN[512 - a];
  if (a < 768) return -QSIN[a - 512];
  return -QSIN[1024 - a];
}
function lfCos(a) { return lfSin(a + 256); }
const idiv = (a, b) => Math.trunc(a / b);   // C's integer division

// lf_rotate_setup: 16.16 steps, the turned image's box, the origin's place in it
function rotateSetup(w, h, cx, cy, roll, scale) {
  const c = lfCos(roll), s = lfSin(roll);
  const xs = [-cx, w - cx, -cx, w - cx], ys = [-cy, -cy, h - cy, h - cy];
  let minx = 0, maxx = 0, miny = 0, maxy = 0;
  for (let i = 0; i < 4; i++) {
    const ox = idiv((xs[i] * c - ys[i] * s) >> 14, 1) * scale;   // (x c - y s) >> 14 then * scale / 256
    const oy = idiv((xs[i] * s + ys[i] * c) >> 14, 1) * scale;
    const px = idiv(ox, 256), py = idiv(oy, 256);
    if (px < minx) minx = px; if (px > maxx) maxx = px;
    if (py < miny) miny = py; if (py > maxy) maxy = py;
  }
  const r = {};
  r.ocx = -minx + 1; r.ocy = -miny + 1;
  r.ow = maxx - minx + 2; r.oh = maxy - miny + 2;
  r.du = idiv((c * 4) * 256, scale); r.dv = idiv((-s * 4) * 256, scale);
  r.dru = idiv((s * 4) * 256, scale); r.drv = idiv((c * 4) * 256, scale);
  r.u00 = (cx * 65536) + 32768 + (-r.ocx) * r.du + (-r.ocy) * r.dru;
  r.v00 = (cy * 65536) + 32768 + (-r.ocx) * r.dv + (-r.ocy) * r.drv;
  return r;
}
// lf_rotate_row: output row y, columns xa..xb-1, from the dense source
function rotateRow(dense, w, h, r, y, xa, xb, out, outOff) {
  let u = r.u00 + y * r.dru + xa * r.du, v = r.v00 + y * r.drv + xa * r.dv;
  for (let x = xa; x < xb; x++, u += r.du, v += r.dv) {
    const su = Math.floor(u / 65536), sv = Math.floor(v / 65536);   // C: >> 16 (arithmetic)
    if (su < 0 || sv < 0 || su >= w || sv >= h) continue;
    const p = dense[sv * w + su];
    if (p) out[outOff + x] = p;
  }
}

// a WC sprite (scripts/wc2data.py) to a dense picture, 0 = clear
function decode(d, off, len) {
  const dv = new DataView(d.buffer, d.byteOffset + off, len);
  const x2 = dv.getInt16(0, true), x1 = dv.getInt16(2, true), y1 = dv.getInt16(4, true), y2 = dv.getInt16(6, true);
  const w = x1 + x2 + 2, h = y1 + y2 + 2, ox = x1 + 1, oy = y1 + 1;
  const px = new Uint8Array(w * h);
  let o = 8;
  while (o + 6 <= len) {
    const key = dv.getInt16(o, true), vx = dv.getInt16(o + 2, true), vy = dv.getInt16(o + 4, true);
    o += 6;
    if (key === 0) break;
    const x = vx + x1 + 1, line = (vy + y1 + 1) * w, cnt = idiv(key, 2);
    if (key % 2 === 0) {
      for (let i = 0; i < cnt; i++) px[line + x + i] = d[off + o + i];
      o += cnt;
    } else {
      let n = 0;
      while (n < cnt && o < len) {
        const k = d[off + o++];
        if (k % 2 === 0) {
          for (let i = 0; i < k / 2; i++) px[line + x + n + i] = d[off + o + i];
          o += k / 2;
        } else {
          const c = d[off + o++];
          for (let i = 0; i < (k - 1) / 2; i++) px[line + x + n + i] = c;
        }
        n += idiv(k, 2);
      }
    }
  }
  return { w, h, ox, oy, px };
}

function mirror(a) {
  const px = new Uint8Array(a.w * a.h);
  for (let y = 0; y < a.h; y++)
    for (let x = 0; x < a.w; x++) px[y * a.w + x] = a.px[y * a.w + (a.w - 1 - x)];
  return { w: a.w, h: a.h, ox: a.w - 1 - a.ox, oy: a.oy, px };
}

// the view at a size and turn, cropped to its painted box, as run-length rows with a row table
// the picture turned by roll and scaled, by the flight's own kernel (every output pixel samples one source pixel)
function turned(src, scale, roll) {
  const r = rotateSetup(src.w, src.h, src.ox, src.oy, roll, scale);
  const img = new Uint8Array(r.ow * r.oh);
  for (let y = 0; y < r.oh; y++)
    rotateRow(src.px, src.w, src.h, r, y, 0, r.ow, img, y * r.ow);   // (the span is only a speed-up in C)
  return { img, r };
}

const floor16 = (a) => Math.floor(a / 65536), floor8 = (a) => Math.floor(a / 256);

// The smaller sizes: a picture shrunk by coverage, not by sampling, so a wing a pixel
// wide survives as a line instead of dropping out.  Output pixel (x, y) owns the box
// of source pixels whose centres fall in its footprint; it is drawn when enough of them
// are ship (half from half size up, a quarter below half, an eighth below a quarter, any
// below an eighth), with the colour of the ship pixel nearest the box's centre.  Byte
// for byte the C tool's shrunk().
function shrunk(img, tw, th, tcx, tcy, scale) {
  const r = rotateSetup(tw, th, tcx, tcy, 0, scale);
  const out = new Uint8Array(r.ow * r.oh);
  for (let y = 0; y < r.oh; y++) {
    const vc = r.v00 + y * r.drv, v0 = vc - idiv(r.drv, 2), v1 = v0 + r.drv;
    const r0 = -floor16(32768 - v0), r1 = -floor16(32768 - v1);   // rows with centres in [v0, v1)
    for (let x = 0; x < r.ow; x++) {
      const uc = r.u00 + x * r.du, u0 = uc - idiv(r.du, 2), u1 = u0 + r.du;
      const c0 = -floor16(32768 - u0), c1 = -floor16(32768 - u1);
      const total = (c1 - c0) * (r1 - r0);
      if (total <= 0) continue;
      const need = scale >= 128 ? idiv(total + 1, 2) : scale >= 64 ? idiv(total + 3, 4) : scale >= 32 ? idiv(total + 7, 8) : 1;
      let count = 0, best = -1, pick = 0;
      for (let rr = r0 < 0 ? 0 : r0; rr < r1 && rr < th; rr++)
        for (let c = c0 < 0 ? 0 : c0; c < c1 && c < tw; c++) {
          const p = img[rr * tw + c];
          if (!p) continue;
          count++;
          const dx = floor8(c * 65536 + 32768 - uc), dy = floor8(rr * 65536 + 32768 - vc);
          const d = dx * dx + dy * dy;
          if (best < 0 || d < best) { best = d; pick = p; }
        }
      if (count >= need) out[y * r.ow + x] = pick;
    }
  }
  return { img: out, r };
}

function render(src, scale, roll) {
  let img, r;
  if (scale < 256) {
    const big = turned(src, 256, roll);
    ({ img, r } = shrunk(big.img, big.r.ow, big.r.oh, big.r.ocx, big.r.ocy, scale));
  } else
    ({ img, r } = turned(src, scale, roll));
  let bx0 = r.ow, bx1 = 0, by0 = r.oh, by1 = 0;
  for (let y = 0; y < r.oh; y++)
    for (let x = 0; x < r.ow; x++)
      if (img[y * r.ow + x]) {
        if (x < bx0) bx0 = x; if (x + 1 > bx1) bx1 = x + 1;
        if (y < by0) by0 = y; if (y + 1 > by1) by1 = y + 1;
      }
  if (bx1 <= bx0) { bx0 = by0 = 0; bx1 = by1 = 1; }
  const w = bx1 - bx0, h = by1 - by0, ox = r.ocx - bx0, oy = r.ocy - by0;
  const out = new Uint8Array(w * h * 2 + 4 * (h + 1) + 64);
  const tab = new DataView(out.buffer);
  let n = 4 * (h + 1);
  for (let y = 0; y < h; y++) {
    const line = img.subarray((by0 + y) * r.ow + bx0, (by0 + y) * r.ow + bx0 + w);
    tab.setUint32(4 * y, n - 4 * (h + 1), true);
    let x = 0;
    while (x < w) {
      let skip = 0, run = 0;
      while (x < w && !line[x]) { x++; skip++; }
      if (x >= w) break;
      while (x + run < w && line[x + run] && run < 255) run++;
      while (skip > 255) { out[n++] = 255; out[n++] = 0; skip -= 255; }
      out[n++] = skip; out[n++] = run;
      out.set(line.subarray(x, x + run), n);
      n += run; x += run;
    }
    out[n++] = 0; out[n++] = 0;
  }
  tab.setUint32(4 * h, n - 4 * (h + 1), true);
  if (n & 1) out[n++] = 0;
  return { w, h, ox, oy, data: out.subarray(0, n) };
}

function stored(v, r, asym) {
  if (asym) return r % (NROLLS / 8) === 0;
  return r <= NROLLS / 4;
}

// the 30-degree sub-lattice's view j (of 62) as a view of the .RV's lattice (nb, nh):
// the index, or {mirrorOf} when it must be made as the mirror image of that view
function subView(j, nb, nh, asym) {
  const step = idiv(nb + 1, 6);
  if (j === 0) return 0;
  if (j === 36) return 1 + nb * nh;
  if (j <= 35) {
    const b = idiv(j - 1, 7), h = (j - 1) % 7;
    return 1 + (b * step + step - 1) * nh + h * step;
  }
  const b = idiv(j - 37, 5), k = (j - 37) % 5 + 1;
  if (asym) return 2 + nb * nh + (b * step + step - 1) * (nh - 2) + (k * step - 1);
  return { mirrorOf: 1 + (b * step + step - 1) * nh + (6 - k) * step };
}

// the set for a .RV's bytes: a Uint8Array of the .RVS
// lf_pair_of: the mirrored heading's index of view v, or its own (the poles, nose on, tail on)
function lfPairOf(v, nb, nh) {
  const base = 2 + nb * nh;
  if (v === 0 || v === 1 + nb * nh) return v;
  if (v < base) {
    const band = Math.floor((v - 1) / nh), j = (v - 1) % nh;
    if (j === 0 || j === nh - 1) return v;
    return base + band * (nh - 2) + (nh - 2 - j);
  }
  const band = Math.floor((v - base) / (nh - 2)), k = (v - base) % (nh - 2);
  return 1 + band * nh + (nh - 2 - k);
}

// The recipe, as tools/rvsprites.c takes it: firstSize rollFrom rollSizes, then lattice (3: the sets on the
// 30-degree sub-lattice, 62 views; 1: on every view of the ship's lattice) and factor (1: full size; 2 or 4: the
// ladder from half or quarter size, the engine drawing the sprites that many times bigger as blocks - RV.EXE
// /res 2's SHIPnn.RVH is `0 0 0 1 2`: the ladder only, every ship turned exactly from it)
function makeSet(rv, firstSize, rollFrom, rollSizes, lattice, factor) {
  firstSize = firstSize || 0; rollFrom = rollFrom || 0; rollSizes = rollSizes === undefined ? 4 : rollSizes;
  lattice = lattice || 3; factor = factor || 1;
  const step0 = factor === 4 ? 8 : factor === 2 ? 4 : 0;
  const dv = new DataView(rv.buffer, rv.byteOffset, rv.byteLength);
  const b0 = dv.getUint32(4, true) & 0xffffff, b1 = dv.getUint32(8, true) & 0xffffff;
  if (String.fromCharCode(rv[b1], rv[b1 + 1], rv[b1 + 2], rv[b1 + 3]) !== "RVSH") throw new Error("not a ship file");
  const type = rv[b1 + 4], asym = rv[b1 + 7] & 1, nb = rv[b1 + 11] || 5, nh = rv[b1 + 12] || 7;
  let nviews = rv[b1 + 5] | (rv[b1 + 6] << 8);
  const total = dv.getUint32(b0, true), nv = ((dv.getUint32(b0 + 4, true) & 0xffffff) - 4) / 4;
  if (nviews > nv) nviews = nv;
  const views = [];
  for (let v = 0; v < nviews; v++) {
    const off = b0 + (dv.getUint32(b0 + 4 + 4 * v, true) & 0xffffff);
    const end = v + 1 < nv ? b0 + (dv.getUint32(b0 + 4 + 4 * (v + 1), true) & 0xffffff) : b0 + total;
    views[v] = decode(rv, off, end - off);
  }
  // block 0 keeps the lattice's views; the sets use the 30-degree sub-lattice (mirrored headings made), or every
  // view: the stored ones alone for a ladder, else with the mirrored headings made (the engine's indices for them)
  const nview0 = nviews;
  const sub = [];
  let nsub;
  if (lattice === 3) {
    nsub = 62;
    for (let v = 0; v < 62; v++) {
      const src = subView(v, nb, nh, asym);
      sub[v] = typeof src === "number" ? views[src] : mirror(views[src.mirrorOf]);
    }
  } else if (rollSizes === 0) {
    nsub = nviews;
    for (let v = 0; v < nsub; v++) sub[v] = views[v];
  } else {
    nsub = asym ? nviews : 2 + nb * nh + nb * (nh - 2);
    for (let v = 0; v < nsub; v++) sub[v] = (v < nviews && views[v].w) ? views[v] : mirror(views[lfPairOf(v, nb, nh)]);
  }
  const blocks = [];
  for (let k = 0; k < NSIZES; k++)
    for (let r = 0; r < NROLLS; r++) {
      if (k < firstSize || (r && (k < rollFrom || k >= rollFrom + rollSizes))) continue;
      const srcViews = (k === 0 && r === 0) ? views : sub, nv = (k === 0 && r === 0) ? nview0 : nsub;
      const parts = [], meta = [];
      let n = 0;
      for (let v = 0; v < nv; v++) {
        if (!stored(v, r, asym)) { meta.push({ w: 0, h: 0, ox: 0, oy: 0, at: 0 }); continue; }
        const one = render(srcViews[v], SCALES[k + step0], r * (1024 / NROLLS));
        meta.push({ w: one.w, h: one.h, ox: one.ox, oy: one.oy, at: n });
        parts.push(one.data); n += one.data.length;
      }
      if (n === 0) continue;
      blocks.push({ size: k, roll: r, nv, parts, meta, len: n });
    }
  let headLen = 52;
  for (const b of blocks) headLen += 12 + 12 * b.nv;
  let at = headLen, total2 = headLen;
  for (const b of blocks) total2 += b.len;
  const out = new Uint8Array(total2), o = new DataView(out.buffer);
  out.set([82, 86, 83, 80], 0);                // "RVSP"
  out[4] = type; out[5] = nview0 & 255; out[6] = NSIZES; out[7] = NROLLS; out[8] = rollSizes;
  out[9] = asym | (factor === 2 ? 2 : factor === 4 ? 4 : 0);
  out[10] = nb; out[11] = nh;
  for (let k = 0; k < 16; k++) o.setUint16(12 + 2 * k, k < NSIZES ? SCALES[k + step0] : 0, true);
  if (lattice === 3) { out[44] = 5; out[45] = 7; } else { out[44] = nb; out[45] = nh; }
  o.setUint16(46, nview0, true);
  o.setUint32(48, blocks.length, true);
  let p = 52;
  for (const b of blocks) {
    out[p] = b.size; out[p + 1] = b.roll; o.setUint16(p + 2, b.nv, true);
    o.setUint32(p + 4, at, true); o.setUint32(p + 8, b.len, true); p += 12;
    for (const m of b.meta) {
      o.setUint16(p, m.w, true); o.setUint16(p + 2, m.h, true);
      o.setInt16(p + 4, m.ox, true); o.setInt16(p + 6, m.oy, true); o.setUint32(p + 8, m.at, true); p += 12;
    }
    b.at = at; at += b.len;
  }
  for (const b of blocks) {
    let q = b.at;
    for (const part of b.parts) { out.set(part, q); q += part.length; }
  }
  return out;
}

export { makeSet };
