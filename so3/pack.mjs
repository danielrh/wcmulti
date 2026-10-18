#!/usr/bin/env node
// so3/pack.mjs -- the SO3 files for the page, readable only with the game.
//
//   node so3/pack.mjs pack   <plain dir> <anchor file> <out dir>
//   node so3/pack.mjs unpack <out dir>   <anchor file> <dest dir>
//
// Every file under <plain dir> goes into one AES-256-GCM blob, so3.enc; the
// key is the SHA-256 of <anchor file>, a file of Special Operations 2 that
// every owner has and no patch has touched (GAMEDAT/SERIES.S02).  so3.json
// beside it names the anchor, the key's own SHA-256 (to tell a wrong key
// from a damaged blob), the IV and the files' names and sizes.  The page
// (web/so3.js) does the same with the player's copy of the anchor.
import { createHash, randomBytes, createCipheriv, createDecipheriv, webcrypto } from "node:crypto";
import { readFileSync, writeFileSync, readdirSync, statSync, mkdirSync } from "node:fs";
import { join, relative, dirname } from "node:path";

const walk = (dir, base = dir) => readdirSync(dir).flatMap((f) => {
  const p = join(dir, f);
  return statSync(p).isDirectory() ? walk(p, base) : [relative(base, p).split("\\").join("/")];
}).sort();

// The plain container: for each file a u32 path length, the path (UTF-8),
// a u32 size and the bytes; little-endian, as the page's DataView reads it.
const container = (dir, files) => {
  const parts = [];
  for (const f of files) {
    const data = readFileSync(join(dir, f));
    const name = Buffer.from(f, "utf8");
    const head = Buffer.alloc(8);
    head.writeUInt32LE(name.length, 0);
    head.writeUInt32LE(data.length, 4);
    parts.push(head, name, data);
  }
  return Buffer.concat(parts);
};

const keyOf = (anchorPath) => createHash("sha256").update(readFileSync(anchorPath)).digest();

const [, , cmd, a, b, c] = process.argv;
if (cmd === "pack") {
  const [plain, anchor, out] = [a, b, c];
  const files = walk(plain);
  const body = container(plain, files);
  const key = keyOf(anchor);
  const iv = randomBytes(12);
  const cipher = createCipheriv("aes-256-gcm", key, iv);
  const enc = Buffer.concat([cipher.update(body), cipher.final(), cipher.getAuthTag()]);
  mkdirSync(out, { recursive: true });
  writeFileSync(join(out, "so3.enc"), enc);
  // plain.json beside the plain directory carries the executable patch list
  const plainInfo = JSON.parse(readFileSync(join(dirname(plain), "plain.json"), "utf8"));
  const manifest = {
    version: 1,
    anchor: "GAMEDAT/SERIES.S02",
    exe: plainInfo.exe,
    exes: plainInfo.exes || [plainInfo.exe],   // SO3.EXE and SO3P.EXE (the third seat), each from the player's SO2.EXE
    anchorSize: statSync(anchor).size,
    keyCheck: createHash("sha256").update(key).digest("hex"),
    iv: iv.toString("hex"),
    files: files.map((f) => ({ path: f, size: statSync(join(plain, f)).size })),
    bytes: enc.length,
  };
  writeFileSync(join(out, "so3.json"), JSON.stringify(manifest, null, 1) + "\n");
  console.log(`packed ${files.length} files, ${body.length} bytes -> ${enc.length} bytes in ${out}`);
} else if (cmd === "unpack") {
  const [out, anchor, dest] = [a, b, c];
  const manifest = JSON.parse(readFileSync(join(out, "so3.json"), "utf8"));
  const key = keyOf(anchor);
  const check = createHash("sha256").update(key).digest("hex");
  if (check !== manifest.keyCheck) throw new Error("the anchor file does not give the key this package was made with");
  const enc = readFileSync(join(out, "so3.enc"));
  const decipher = createDecipheriv("aes-256-gcm", key, Buffer.from(manifest.iv, "hex"));
  decipher.setAuthTag(enc.subarray(enc.length - 16));
  const body = Buffer.concat([decipher.update(enc.subarray(0, enc.length - 16)), decipher.final()]);
  let off = 0, n = 0;
  while (off < body.length) {
    const nameLen = body.readUInt32LE(off), size = body.readUInt32LE(off + 4);
    const name = body.subarray(off + 8, off + 8 + nameLen).toString("utf8");
    const data = body.subarray(off + 8 + nameLen, off + 8 + nameLen + size);
    mkdirSync(dirname(join(dest, name)), { recursive: true });
    writeFileSync(join(dest, name), data);
    off += 8 + nameLen + size;
    n++;
  }
  console.log(`unpacked ${n} files into ${dest}`);
} else {
  console.error("usage: pack.mjs pack <plain dir> <anchor> <out dir> | unpack <out dir> <anchor> <dest dir>");
  process.exit(2);
}
