// RetroVega's sprite sets made off the page's thread (web/wc.js rvMakeSet):
// a type's views (SHIPnn.RV) and the recipe in, the set (SHIPnn.RVH or .RVS,
// the same bytes as retrovega/tools/rvsprites.c makes) out, its buffer
// handed over rather than copied.  The emulator keeps running and the game's
// music plays on while a set is made (a second or two a fighter).
import { makeSet } from "./rvsprites.js";

onmessage = (e) => {
  const { id, rv, args } = e.data;
  try {
    const set = makeSet(rv, ...args);
    postMessage({ id, set }, [set.buffer]);
  } catch (err) {
    postMessage({ id, error: err && err.message ? err.message : String(err) });
  }
};
