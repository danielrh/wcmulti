// so3.js -- Special Operations 3 (the Culloden campaign) for the page.
//
// SO3 is Special Operations 2's program with a third set of data files
// (so3/README.md).  Those files come from the game's own, so the site holds
// them sealed: so3/so3.enc is one AES-256-GCM blob whose key is the SHA-256
// of a file every owner of Special Operations 2 has, GAMEDAT/SERIES.S02.
// Given the player's game files, this derives the key from their copy,
// opens the blob and returns the files to install beside the game's, plus
// SO3.EXE and SO3P.EXE (the campaign with a seat for a third player), each
// the player's SO2.EXE with the manifest's byte patches.
// so3/pack.mjs made the blob; the container inside is, per file, a u32 path
// length, the path (UTF-8), a u32 size and the bytes, little-endian.

const PACKAGE = "so3/";

const hex = (bytes) => Array.from(new Uint8Array(bytes), (b) => b.toString(16).padStart(2, "0")).join("");
const unhex = (s) => new Uint8Array(s.match(/../g).map((h) => parseInt(h, 16)));

// The files of SO3, from the player's game files (paths relative to the
// game directory).  Throws with a message fit for the status line.
export async function loadSo3(gameFiles, onStatus) {
  const say = (s) => { if (onStatus) onStatus(s); };
  say("Fetching the Culloden campaign…");
  const [manifest, blob] = await Promise.all([
    fetch(PACKAGE + "so3.json", { cache: "no-cache" }).then((r) => { if (!r.ok) throw new Error(`so3.json: HTTP ${r.status}`); return r.json(); }),
    fetch(PACKAGE + "so3.enc", { cache: "no-cache" }).then((r) => { if (!r.ok) throw new Error(`so3.enc: HTTP ${r.status}`); return r.arrayBuffer(); }),
  ]);
  const anchor = gameFiles.find((f) => f.path.toUpperCase() === manifest.anchor.toUpperCase());
  if (!anchor) throw new Error(`The Culloden campaign needs Special Operations 2's ${manifest.anchor}, which is not among the game files.`);
  const keyBytes = await crypto.subtle.digest("SHA-256", anchor.data);
  const check = hex(await crypto.subtle.digest("SHA-256", keyBytes));
  if (check !== manifest.keyCheck) {
    throw new Error(`Your ${manifest.anchor} (${anchor.data.length} bytes) is not the one the Culloden campaign was built against (${manifest.anchorSize} bytes): another release of Special Operations 2.`);
  }
  const key = await crypto.subtle.importKey("raw", keyBytes, "AES-GCM", false, ["decrypt"]);
  say("Opening the Culloden campaign…");
  let plain;
  try {
    plain = await crypto.subtle.decrypt({ name: "AES-GCM", iv: unhex(manifest.iv) }, key, blob);
  } catch (e) {
    throw new Error("The Culloden campaign's package did not open: it is damaged or does not match this page.");
  }
  const dv = new DataView(plain);
  const bytes = new Uint8Array(plain);
  const files = [];
  let off = 0;
  const dec = new TextDecoder("utf-8");
  while (off + 8 <= bytes.length) {
    const nameLen = dv.getUint32(off, true), size = dv.getUint32(off + 4, true);
    const path = dec.decode(bytes.subarray(off + 8, off + 8 + nameLen));
    files.push({ path, data: bytes.slice(off + 8 + nameLen, off + 8 + nameLen + size) });
    off += 8 + nameLen + size;
  }
  if (files.length !== manifest.files.length) throw new Error(`The Culloden campaign's package holds ${files.length} files, its manifest says ${manifest.files.length}.`);
  // The programs: the player's own SO2.EXE with the manifest's byte patches
  // (its data-file number and the saved game's name), made here, shipped by
  // nobody: SO3.EXE, and SO3P.EXE for the three-seat campaign.
  for (const exe of manifest.exes || [manifest.exe]) {
    const source = gameFiles.find((f) => f.path.toUpperCase() === exe.source.toUpperCase());
    if (!source) throw new Error(`The Culloden campaign needs ${exe.source}, which is not among the game files.`);
    files.push({ path: exe.target, data: patchExe(source.data, exe) });
  }
  return files;
}

// `data` with each of spec.patches' byte sequences replaced, after checking
// that each occurs exactly as often as the list says (another build of the
// program would not).
export function patchExe(data, spec) {
  let out = new Uint8Array(data);
  for (const p of spec.patches) {
    const find = unhex(p.find), repl = unhex(p.replace);
    if (find.length !== repl.length) throw new Error("a patch changes the program's size");
    const at = [];
    outer: for (let i = 0; i + find.length <= out.length; i++) {
      for (let j = 0; j < find.length; j++) if (out[i + j] !== find[j]) continue outer;
      at.push(i); i += find.length - 1;
    }
    if (at.length !== p.count) throw new Error(`Your ${spec.source} is not the build the Culloden campaign was made for (${p.what}: ${at.length} of ${p.count} places).`);
    for (const i of at) out.set(repl, i);
  }
  return out;
}
