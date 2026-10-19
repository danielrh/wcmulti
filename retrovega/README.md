# RetroVega (working title)

A new DOS space-combat game: 640×400, EMS 4.0, a 128-slot entity table. It
is built from the ground up so that the multiplayer DOSBox in this
repository can puppeteer it like WC.EXE: same named memory, same calls, same
hook contracts. It is set in Vega Strike's universe: the Terran
Confederation against the Aera, with the Rlaan meddling.

- [docs/spec.md](docs/spec.md): the engine specification. It covers the platform, the fixed memory layout, the code ABI the hook layer drives, the simulation rules, video, the EMS budget, art direction and milestones.
- [data/ships.csv](data/ships.csv): the roster. Ship type numbers, roles, retro stats, and the source VDU image and mesh in Vega Strike's assets.
- [data/weapons.csv](data/weapons.csv): guns, turrets and missiles, numbered as they appear in the gun table.
- [abi/rvabi.def](abi/rvabi.def): the single source of every offset the hook layer uses; `tools/rvabi.py` lays it out.
- [docs/roster.png](docs/roster.png): contact sheet of the roster's VDU images (`tools/vdu_sheet.py docs/roster.png`).
- [docs/art.md](docs/art.md): the sprite renderer. It turns a Vega Strike mesh into WC1's 37 views at twice WC1's resolution, in a livery. [docs/franklin-liveries.png](docs/franklin-liveries.png) shows the Franklin in four liveries.

The art comes from Vega Strike's Assets-Production, copied to
`~/dev/vegastrike-assets` (GPL-2.0, © the Vega Strike contributors; see
its `vega-license.txt`). Anything derived from it here is GPL as well.

## Title ideas

| title | why |
|---|---|
| **Coreward** (my pick) | Vega Strike's history says the Aera are "determined to punch through a corridor to core-ward expansion". That is the war in one word. |
| The New Jungle | The Aera homeworld was a hellish jungle, and "space became the new jungle, a realm to be watched and controlled". |
| Shmrn Pocket | The campaign's goal: the Andolian task force cut off defending the Shmrn. |
| Vega Frontier | Plain and retro. |
| Corridor | Short, and fits a box. |

The folder stays `retrovega/` until a title is chosen. Renaming is a
`git mv` plus the executable name and signature in the layer's `kGames`
row.

## Story backdrop

From Vega Strike's own history ("A brief history in time and space",
section "More or Less Now"):

- The Rlaan and the Aera drift toward an unofficial cease-fire on their border. That frees the Aera to turn on the human border.
- The Confederation has already lost several new border worlds.
- An Andolian task force sent to defend the Shmrn was surrounded when both neighbouring systems were overrun, and its fate is unknown.
- The Rlaan will not help in earnest. They fear an Aera victory, but they are happy to see the Confederation bled white, and to expand while it is busy.

The player flies from a Clydesdale-class carrier with the relief group
pushing toward the Shmrn pocket.

The Rlaan are the third side throughout (`commAnimInfo` side 2, the "others"
the AI already knows):

- Gaozong probes shadow every battle.
- Xuanzong jammers break missile locks and fill the comms with static.
- Shizong disablers cripple whoever is winning.
- Shenzong haulers strip the wrecks.
- Now and then they help, when the Aera go after civilians.

The finale is the Aera dreadnought Leonidas.

Possible series:

1. Border picket: Aera probing raids after the cease-fire, and the first Rlaan probe.
2. Convoys (Mule, Ox) to the front, with Redeemer militia wingmen.
3. Breaking the Agasicles screen with Osprey bombers.
4. Into the pocket: reaching the Andolian Kahans, and escorting a Shaper Mongoose envoy to the Rlaan.
5. The Rlaan double game.
6. Hunting the carrier Agesipolis.
7. Leonidas.

## Roster

| | Terran Confederation | Aera | Rlaan |
|---|---|---|---|
| fighters | Schroedinger (light, the starter), Gawain (interceptor), Redeemer (militia assault), Pacifier/Osprey (fighter-bomber with a rear gunner seat), Lancelot/Destiny (heavy, if the art budget allows); Progeny/Mongoose rare, mostly escorted | Nicander, Ariston, Areus, plus three new riffs on them: Agis (light swarm fighter), Cleomenes (heavy assault), Archidamus (ace) | Shizu (scout), Zhuangzong (line), Taizong (assault bomber), Shizong (disabler), Xuanzong (jammer), Gaozong (probe) |
| small craft | Franklin, the personnel transport: usually escorted, flown by the player on one or two missions where nothing else can get through | | |
| capital ships | Clydesdale (home carrier), Kahan (Andolian destroyer), Hawking (cruiser), Mule and Ox (transports) | Agasicles (destroyer), Anaxidamus (cruiser), Agesipolis (carrier), Charillus (transport), Leonidas (dreadnought) | Ruizong (destroyer), Shundi (cruiser and launch ship), Shenzong (transport) |

The factions differ in defence:

- Terran ships are balanced.
- Aera ships have strong shields.
- Rlaan ships have thin shields and very thick organic armour, as their Vega Strike stats have it.

Vega Strike has only three Aera fighters, so the other three are mesh edits
of those, rendered through the same pipeline so that they match.

## Open questions

1. The title.
2. In-game names: current Vega Strike names (Pacifier, Lancelot, Progeny, Leonidas) or the older ones the VDU files still carry (Osprey, Destiny, Mongoose, Yavok)?
3. Is the Lancelot/Destiny in?
4. The Aera riffs: are these names and roles good? Another idea is an older-generation Aera fighter in the flat-teal style of their capital ships, as a contrast to the glowing modern hulls.
