// rvsets.js: RetroVega's files a flight asks for, served to the game's file system, for both pages that run it
// (web/wc.js: RV.EXE in the emulator; retrovega/host/web: the native build), and the tarball reader they load
// the game's files with.
//
// The ships' views (SHIPnn.RV, 1-9 MB each at 6 degrees) are not in the
// game's tarball.  When a set a flight needs is not among the game's files,
// the engine writes the flight's list into RVWANT.TXT ("want GEN SHIP02.RVH
// ...", retrovega/src/ships.c ships_want: at the briefing, so the fetching
// starts while it is read, and again at the launch, where it waits) and the
// page, polling that file, fetches each missing type's views from the site
// (rv/SHIPnn.RV.gz, listed in rv/ships.json), makes the set in a worker
// (web/rvsprites.js: the same bytes as the C tool's) and writes it among the
// game's files; sets the flight does not need are removed.  The browser's
// Cache API keeps the views by their hash and the made sets by hash, recipe
// and build, so a second flight with the same types downloads and makes
// nothing; without it (an http page, a private window) everything still
// works, fetched each time.
import { makeSet } from "./rvsprites.js";

const RV_ART_CACHE = "rv-art", RV_SET_CACHE = "rv-sets-";
const RV_RECIPE = { RVH: [0, 0, 0, 1, 2], RVS: [0, 4, 9] };   // build.sh's: the block mode's ladder; the sharp sets (?res=1)
const mb = (n) => (n / 1048576).toFixed(1) + " MB";

async function rvCache(name) { try { return window.caches ? await caches.open(name) : null; } catch (e) { return null; } }

// base: where rv/ships.json and the files it lists are ("rv/"); root: the game's directory in the file system;
// status(text), log(line): the page's
export function createSetProvider({ base, root, status, log }) {
  let manifest = null, wantGen = null, busy = null, worker = null, workerNext = 1;
  const jobs = new Map();
  const label = (key) => { const s = manifest && manifest.ships[key]; return s && s.name ? `the ${s.name}` : key; };
  const url = (file) => base + file.replace(/^rv\//, "");

  async function loadManifest() {
    if (manifest) return manifest;
    const res = await fetch(base + "ships.json", { cache: "no-cache" });
    if (!res.ok) throw new Error(`${base}ships.json: ${res.status} ${res.statusText}`);
    manifest = await res.json();
    // the made sets of other builds go (their keys carry the build); the views, by hash, stay
    try { for (const k of await caches.keys()) if (k.startsWith(RV_SET_CACHE) && k !== RV_SET_CACHE + manifest.build) await caches.delete(k); } catch (e) { /* no cache */ }
    return manifest;
  }

  // a file fetched and ungzipped, checked against the list's size: the cache's, by its hash, else the site's
  async function fetchArt(info, what) {
    const cache = await rvCache(RV_ART_CACHE), req = `/rv-art/${info.sha1}`;
    if (cache) { const hit = await cache.match(req); if (hit) return new Uint8Array(await hit.arrayBuffer()); }
    status(`Fetching ${what} (${mb(info.bytes)})…`);
    const t0 = performance.now();
    const res = await fetch(url(info.file));
    if (!res.ok) throw new Error(`${info.file}: ${res.status} ${res.statusText}`);
    const data = new Uint8Array(await new Response(res.body.pipeThrough(new DecompressionStream("gzip"))).arrayBuffer());
    if (data.length !== info.bytes) throw new Error(`${info.file}: ${data.length} bytes, the site's list says ${info.bytes}`);
    log(`fetched ${what} (${mb(data.length)}) in ${Math.round(performance.now() - t0)} ms`);
    if (cache) try { await cache.put(req, new Response(data)); } catch (e) { log(`could not keep ${what}: ${e.message}`); }
    return data;
  }

  // a set made in the worker (the page keeps drawing and the game's music plays on); the caller falls back to here
  function makeInWorker(rv, args) {
    return new Promise((resolve, reject) => {
      if (!worker) {
        worker = new Worker(new URL("./rvsprites-worker.js", import.meta.url), { type: "module" });
        worker.onmessage = (e) => {
          const job = jobs.get(e.data.id);
          if (!job) return;
          jobs.delete(e.data.id);
          if (e.data.error) job.reject(new Error(e.data.error)); else job.resolve(e.data.set);
        };
        worker.onerror = (e) => {
          for (const job of jobs.values()) job.reject(new Error(e.message || "the sprite worker failed"));
          jobs.clear();
          worker = null;
        };
      }
      const id = workerNext++;
      jobs.set(id, { resolve, reject });
      worker.postMessage({ id, rv, args });   // (a copy of the views: the cache keeps the original)
    });
  }

  // one set, written among the game's files: the cache's, else made from the views; or a file of the game's as it
  // is (an asteroid field's ROCKS.DAT: the list's files)
  async function provideOne(FS, name) {
    if (manifest.files && manifest.files[name]) {
      FS.writeFile(`${root}/${name}`, await fetchArt(manifest.files[name], name));
      return;
    }
    const m = /^(SHIP[0-9A-F]{2})\.(RVH|RVS)$/.exec(name);
    if (!m) throw new Error(`${name}: not a set file`);
    const key = m[1], ext = m[2], info = manifest.ships[key];
    if (!info) throw new Error(`${key}: no such ship on this site`);
    const sets = await rvCache(RV_SET_CACHE + manifest.build), req = `/rv-set/${info.sha1}/${ext}`;
    let set = null;
    if (sets) { const hit = await sets.match(req); if (hit) set = new Uint8Array(await hit.arrayBuffer()); }
    if (!set) {
      const rv = await fetchArt(info, `${label(key)}'s views`);
      status(`Making ${label(key)}'s sprite set…`);
      const t0 = performance.now();
      try { set = await makeInWorker(rv, RV_RECIPE[ext]); }
      catch (e) { log(`the sprite worker: ${e.message}; making ${name} on the page`); set = makeSet(rv, ...RV_RECIPE[ext]); }
      log(`made ${name} (${mb(set.length)}) in ${Math.round(performance.now() - t0)} ms`);
      if (sets) try { await sets.put(req, new Response(set)); } catch (e) { log(`could not keep ${name}: ${e.message}`); }
    }
    FS.writeFile(`${root}/${name}`, set);
  }

  // the flight's list served: the sets it needs made and written, the rest removed
  async function provide(FS, names) {
    const t0 = performance.now();
    let made = 0;
    try {
      await loadManifest();
      for (const f of FS.readdir(root))
        if (/^SHIP[0-9A-F]{2}\.(RVH|RVS)$/.test(f) && !names.includes(f)) { try { FS.unlink(`${root}/${f}`); } catch (e) { /* in use */ } }
      for (const name of names) {
        let have = false;
        try { FS.stat(`${root}/${name}`); have = true; } catch (e) { /* not yet */ }
        if (have) continue;
        await provideOne(FS, name);
        made++;
      }
      status(made ? `${made} ship set${made > 1 ? "s" : ""} ready in ${((performance.now() - t0) / 1000).toFixed(1)} s` : "The ships' sets are in");
      log(`ship sets: ${names.join(" ")} (${made} made) in ${Math.round(performance.now() - t0)} ms`);
    } catch (e) {
      log(`ship sets: ${e.message}`);
      status(`Could not get the ships' sets: ${e.message}`);
    }
  }

  // every few hundred ms while the game runs: a new list in RVWANT.TXT is served (a newer one after the current)
  function poll(FS) {
    if (!FS || busy) return;
    let text;
    try { text = new TextDecoder().decode(FS.readFile(`${root}/RVWANT.TXT`)); } catch (e) { return; }
    const m = /^want (\d+)((?: \S+)*)/.exec(text);
    if (!m || m[1] === wantGen) return;
    wantGen = m[1];
    busy = provide(FS, m[2].trim().split(/\s+/).filter(Boolean)).finally(() => { busy = null; });
  }

  return { loadManifest, poll };
}

// a .tar.gz's files ({ path, data }), as the build's tar writes them (ustar)
export async function readTarGz(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`cannot fetch ${url}: ${res.status} ${res.statusText}`);
  const stream = res.body.pipeThrough(new DecompressionStream("gzip"));
  const bytes = new Uint8Array(await new Response(stream).arrayBuffer());
  const files = [];
  const text = (off, len) => { let end = off; while (end < off + len && bytes[end] !== 0) end++; return new TextDecoder("latin1").decode(bytes.subarray(off, end)); };
  let off = 0;
  while (off + 512 <= bytes.length) {
    if (bytes[off] === 0) break;
    const name = text(off, 100);
    const size = parseInt(text(off + 124, 12).trim() || "0", 8);
    const typeflag = bytes[off + 156];
    const prefix = text(off + 345, 155);
    off += 512;
    if (typeflag === 0 || typeflag === 0x30) files.push({ path: (prefix ? prefix + "/" : "") + name, data: bytes.slice(off, off + size) });
    off += Math.ceil(size / 512) * 512;
  }
  return files;
}
