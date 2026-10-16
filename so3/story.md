# Special Operations 3: *Culloden*

A fan campaign for Wing Commander II's engine, flown in the Hellcat V.  This
is the story, the cast and the mission plan that `campaign.py` builds from;
the dialogue comes next, mission by mission.  Series and mission numbers
are the engine's (series of four); the twenty slots are Special Operations
2's, which is what the campaign script we inherit can play.  Branches are
marked; until the campaign script is ours (see "What the engine needs"),
the missions play in slot order.

## The premise

*2668, a few months after the Mandarin affair.*  Confederation intelligence
has found a hidden Kilrathi construction yard eleven jumps behind the lines,
laying down the first hull of a new carrier class.  A fleet action is out of
the question; one ship might slip through.  TCS Culloden, Waterloo-class,
takes aboard the Hellcat V evaluation flight (six pre-production fighters
and the programme's liaison), two Morningstars, a handful of pilots nobody
else wanted, and goes dark.  Objective: find the yard, kill it, come home.

What the brief did not say: the Hellcats' flight computers answer to
someone else.

## The cast

Faces are WC2's portrait files (`people.vNN`); a name in brackets is the
character the face belonged to.  "Recoloured" means a palette change in a
copy of the file, under a new name in our scene files.

**TCS Culloden**

* **The player.** Colonel, callsign from the save.  The Hellcat flight lead.
* **Captain Wayne Banbridge**, commanding.  `people.v22`: the grey-haired
  officer in dress braid seen once in SO2's court scene, nobody's regular,
  so he needs no recolouring; Tolwyn (`v02`) recoloured is the fallback.  A
  destroyer captain given a cruiser and a secret.  Dry, careful, protective
  of his ship, and privately sure the raid is a career officer's gamble with
  his crew.  His arc: from counting the risks to taking the one that matters.
* **Lt. Colonel Rhea "Valkyrie" Bergstrom**, the Culloden's wing commander.
  `people.v15` [Minx], recoloured: dark hair, so she is not Minx.  Not a
  traitor, not anyone's love interest.  She flew Sabres off the Gettysburg,
  lost her squadron to a bad order and has opinions about admirals.  She
  assigns the seats, flies the reserve ships herself when the Hellcats are
  down, and is the one who takes the found Sabre.
* **Major Todd "Maniac" Marshall.**  `people.v08`, `v39` in dress uniform.
  Transferred off the Concordia after Special Operations 2 and still sore
  about it: he spent "a long ride" strapped in the Mandarins' chair at
  Ayer's Rock and, by his account, nobody hurried.  The second player's seat
  in the Hellcat missions.  Comm voice 0x24, the game's own.
* **Major Etienne "Doomsday" Montclair.**  `people.v09`.  Gets the bad
  missions, says so, flies them anyway.  Leads the reserve squadron's Epee
  patrols; the second player's seat in those.
* **Captain Dirk "Stingray" Wright.**  `people.v04`.  Alive, loud,
  convinced the Hellcats should be his.  The
  wingman in the Morningstar missions' escorts and the long route.
* **Lt. Cmdr. Janet "Sparks" McCullough**, chief of the deck.  `people.v07`.
  Pulls the Hellcats' computers after mission 1 and finds the uplink.  The
  campaign's honest technician: everything she says about the ships is true.
* **Cmdr Lyle Prescott**, the Hellcat programme's test-pilot liaison (a pilot: he steals a Hellcat in 3-1 and flies it against the player in 4-3).  `people.v38`
  (a dress-uniform face from SO2's court), renamed; not `v10`, which is
  Bear, who matters later in the novels and is left alone.  Gives the first briefing and tells the player to trust the
  hand-off computers.  The Mandarins' man aboard; the relay's logs name him
  in series 1.  Not a monster: his family is on a Mandarin world, and the
  campaign lets him say so before he is taken off the story.
* **Lieutenant "Downtown".**  `people.v05`.  The Concordia's youngest, alive in this
  telling, transferred with Maniac and Doomsday.  Flies the reserve
  squadron's third ship, the one the bad missions are hardest on.
* **Lt. Ines "Toro" Dalmau**, the Culloden's own pilot.  Minx's face
  (talker id 10, `people.v30`), under her own name and nobody's traitor:
  the pilot pinned at the rockline in 1-2, the wingman of 2-2, the one who
  came home on Valkyrie's order in 2-3.

**Elsewhere**

* **Dez Morrow**, master of the Free Trader *Lucky Mandy*, and **Pilar
  Oyelaran** of the *Tsiolkovsky*.  Faces still to pick from the ones not
  yet named (`v01`, `v06`, `v20`, `v21`, `v30`, `v33`, `v39`).
  Smugglers to the Kilrathi colonies, stranded with the yard's approach
  charts in their holds.  Clemency and an escort home for the charts.
* **Kalralahr Ghraffid nar Ragitagha**, master of the yard.  `people.v17`
  (a Kilrathi face the games used little).  Proud of what he is building;
  the campaign's voice of the enemy in two comm scenes and the finale.
* **The Mandarin relay's keeper.**  Voice only, no face: the recorded
  "Have a nice day" and two taunts in the asteroid field.

**Paladin** is aboard from 2-0 as the Special Operations planner: the
yard, the agent Scorpion, the Kirrah'ka's refit are his.  Faces are SO2's
actors until the recolouring step (docs/newcampaign.md says how the face
files are named).

**Available but not used:** Hobbes (`v03`) to read the Kilrathi charts.
Spirit and Jazz are dead; Angel (`v14`) and Tolwyn (`v02`) do not sail
with us; Bear (`v10`) is spoken for by the novels.

## The ships

| | type | who | when |
|---|---|---|---|
| Hellcat V | 45 | the player, Maniac | mission 1-0; series 2 and 4 if certified |
| Epee, Rapier | 3, 1 | the reserve squadron: Doomsday, Bergstrom, Downtown, Dalmau | 1-1, 1-2, series 3 and 5 |
| Sabre | 4 | Bergstrom, from mission 3-1 on | the one found at the depot |
| Kamekh | 16 | the Vak'thra and the corvettes of 2-0 | 1-1 to 1-3, 2-0 |
| Morningstar | 41 | the player, with escorts; Stingray and Valkyrie in 1-1 | 1-1, 1-3, 3-3, the strike |
| TCS Culloden | 17 (Waterloo) | home | every launch and landing |
| Lucky Mandy, Tsiolkovsky | 12 (Free Trader) | the traders | series 2 and 3, and an ending |
| Target hulk | 29 (a Dorkathi with its turrets stripped) | the range | mission 1-0 |
| The relay | 48 (Ayer's Rock) | the Mandarins' | mission 1-2 |
| The yard | 25 (K supply depot), with a Fralthra hull | the Kilrathi | the finale |


## Why things happen: the chain, stated once

1. **The Hellcat's hand-off computer takes a remote key.**  The programme
   shipped it with a maintenance channel; Cmdr Lyle Prescott, the
   programme's test-pilot liaison, is the Mandarins' man and knows it.  A
   Mandarin relay, the corvette Vak'thra with a Kilrathi crew, waits in an
   asteroid field on the Culloden's route.
2. **1-0 Shakedown: the relay pings the Hellcats.**  Sparks finds the burst
   in every log.  Nothing has failed, but whoever holds the key can switch
   the hand-off and the missile locks off, in a fight, by radio.  Banbridge
   grounds the Hellcats.  The same ping told the Kilrathi where the Culloden
   was: that is the Gothri that arrive at the range once the hulks are dead,
   and why the Drakhri pair at Nav 2 must not get home.
3. **1-1 Hand-me-downs: find the source.**  Sparks' direction finders ride
   the utility barge, Stingray driving; the reserve squadron in Epees keeps
   it alive while it takes the bearing.  The bearing points into the rocks.
   The corvette Kur'ratha, hunting the ship, waits at Nav 2.
4. **1-2 The relay: the Vak'thra and her core.**  Toro, sent to confirm the
   bearing quietly, is pinned at the rockline by Grikath.  The player and
   Stingray, in Sabres (the one fighter aboard with a tractor beam), kill
   the relay corvette; her computer core tumbles free and is tractored
   home.  With the core Sparks has the master key, burns the channel out
   and signs the Hellcats off; the core's logs name Prescott, who goes to
   the brig.  **The Hellcats fly again because the player brought the core
   home** (without it Sparks pulls the boards instead; the text says so).
5. **1-3 Prize: the first cleared flight,** a sweep in Hellcats with Maniac.
   At Nav 2 the Kilrathi fleet tender Kirrah'ka, abandoned by her escort,
   strikes her colours.  Spared, she is the Culloden's fuel and, refitted,
   the Trojan horse of series 3 and 4; her crew includes a Mandarin courier
   whose cargo manifest is Paladin's first thread to the yard.  Shot, she is
   the court-martial the Captain promises (SO2's slot ends there).
6. **Series 2, the road:** the Kilrathi know a cruiser is loose.  The first
   full Hellcat patrol (2-0), the strike on the Culloden met by the alert
   Sabres (2-1), a Confederation beacon that is a bomb (2-2), Valkyrie left
   holding off a corvette so Toro could warn the ship (2-3).
7. **Series 3, the long road:** the free traders' charts, bought at a
   rendezvous the Kilrathi knew about before we did (3-0); Prescott, let out
   to help Sparks, steals a Hellcat and runs for a Kilrathi rendezvous (3-1:
   the Morningstars' first flight, Maniac's Mace speech); Paladin's agent
   Scorpion fixes the yard, and the Kirrah'ka, refitted, carries two
   Morningstars toward it with Paladin aboard (3-2, 3-3).
8. **Series 4, the yard:** Doomsday's Kilrathi buys the countersign (4-0);
   Scorpion reports the keel ceremony and the fleet's lords, and is executed
   on an open channel by Kalralahr Ghraffid (4-1, 4-2); the Morningstars kill
   the yard and the first hull (4-2); Prescott, in the stolen Hellcat, waits
   at the jump point for the player (4-3).  The Culloden, attacked meanwhile,
   fires her main gun at sixty percent.
9. **5-3 The last patrol:** the long way home, every picket awake.  The
   epilogue is the Kilrathi court's: a new keel, somewhere no Terran has
   looked.  Slots 5-0, 5-1 and 5-2 are the endings the inherited campaign
   script turns to when a mission is lost: the raid called off.

## The campaign, slot by slot

Each mission reskins Special Operations 2's mission in the same slot (its
objective list, the ship indices its scenes test, its landing ship), because
the campaign script and the scene scripts are SO2's.  `campaign.py` has the
data; `texts.py` every line of the 24 scene blocks.

| slot | mission | ships | the second player |
|---|---|---|---|
| 1-0 | Shakedown | Hellcats; hulks, Gothri wave, Drakhri | Maniac |
| 1-1 | Hand-me-downs | Epees and the barge; Grikath, Kur'ratha, Drakhri | Doomsday |
| 1-2 | The relay | Sabres; Grikath, the Vak'thra and Drakhri, the core | Stingray |
| 1-3 | Prize | Hellcats; Drakhri, the Kirrah'ka and her escort, Jalkehi | Maniac |
| 2-0 | First patrol | four Hellcats; Strakha, a Gothri ace, Jalkehi | Maniac |
| 2-1 | Scramble | Sabres; six Gothri and a Kamekh at the ship | Stingray |
| 2-2 | Beacon | Sabres; Drakhri, Jalkehi, Sartha in the mines, the bomb | Toro |
| 2-3 | Valkyrie | Sabres; five Gothri and a Kamekh on Valkyrie | Maniac |
| 3-0 | The traders | Sabres, the barge, the Tsiolkovsky; Drakhri and a Ralatha | Maniac |
| 3-1 | Theft | Morningstars; mines, Sartha, Jalkehi, Gothri, tenders and a Kamekh | Maniac |
| 3-2 | Rendezvous | Morningstars; transports, Gothri, Drakhri; land on the Kirrah'ka | Maniac |
| 3-3 | Jump point | Morningstars from the Kirrah'ka; Strakha, Jalkehi, Gothri aces | Maniac |
| 4-0 | Countersign | Morningstars; five Gothri on the tender, Sartha | Maniac |
| 4-1 | Ceremony | Morningstars; Sartha, Mandarin Sabres, two Fralthra | Maniac |
| 4-2 | The yard | Morningstars; the yard, Mandarin Sabres, two Kamekhs | Maniac |
| 4-3 | Duel | Morningstars; Mandarin Sabres, Prescott's Hellcat; land on the Culloden | Maniac |
| 5-0 | The assault (lost) | Sabres; Grikath, four Kamekhs and Gothri, Sartha | Stingray |
| 5-1 | Picket line (lost) | Rapiers; Jalkehi, a Fralthra and Sartha, a Gothri ace | Downtown |
| 5-2 | Blown (lost) | Morningstars from the Kirrah'ka; Mandarin Sabres and Broadswords | Maniac |
| 5-3 | The last patrol | Morningstars; Jalkehi, Drakhri, Mandarin Broadswords | Maniac |

## Where the two-player seats fall

| missions | the second player flies |
|---|---|
| 1-0, 2-0, 2-1, 2-2, 2-3, 4-0, 4-1 | Maniac's Hellcat |
| 1-1 | Doomsday's Epee |
| 1-2 | Valkyrie's Rapier |
| 1-3, 3-3, 5-x | Stingray's Rapier beside the host's Morningstar |
| 3-0, 3-1, 3-2 | Valkyrie's Rapier, then her Sabre |

## What the engine needs for this, in order

1. **Missions in MODULE.003** -- the tools exist; each mission is a dozen
   lines in `campaign.py`.
2. **The campaign script** (CAMPAIGN.S03, CSCP bytecode): the branches on
   the core, the traders and 3-2, the restored-Hellcat types, the endings.
   Until it is decoded and written, the missions play in slot order with
   Special Operations 2's own branch rules.  This is the big one.
3. **Scene text** per mission (SERIES.S03 string tables; the tools exist),
   and **scene scripts** for scenes whose pictures we choose: the engine's
   rooms exist (bridge, briefing room, barracks, flight deck, repair deck,
   observation deck) and need only their names changed.
4. **Faces:** copies of `people.v22` and `people.v15` recoloured
   (hair and uniform palette entries) under new file names.
5. **The Hellcat in the launch art:** the boarding side view (LAUNCH.V00
   block 4, 314x129) from the Hellcat's side view scaled, and the fly-out
   (LAUNGAME.V06, 37 frames) from its rear views; both referenced by name in
   the scene files, so copies can be ours.
