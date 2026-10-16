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
* **Dr. Lyle Prescott**, the Hellcat programme's liaison.  `people.v38`
  (a dress-uniform face from SO2's court), renamed; not `v10`, which is
  Bear, who matters later in the novels and is left alone.  Gives the first briefing and tells the player to trust the
  hand-off computers.  The Mandarins' man aboard; the relay's logs name him
  in series 1.  Not a monster: his family is on a Mandarin world, and the
  campaign lets him say so before he is taken off the story.
* **Lieutenant "Downtown".**  `people.v05`.  The Concordia's youngest, alive in this
  telling, transferred with Maniac and Doomsday.  Flies the reserve
  squadron's third ship, the one the bad missions are hardest on.
* **Lt. Ines "Toro" Dalmau**, the Culloden's own pilot.  `people.v33`,
  renamed.  Named so that a death in the reserve missions means something;
  the engine keeps the count.

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

Everything in series 1 follows from one mechanism, and every later branch
from one of two facts.

1. **The Hellcat's hand-off computer takes a remote key.**  The programme
   shipped it with a maintenance channel; Prescott, the liaison, is the
   Mandarins' man and knows it.  Months before the Culloden sailed, the
   Mandarins hid a relay in an asteroid field on her planned route.
2. **Mission 1: the relay pings the Hellcats.**  Sparks finds the burst in
   every log; the recording ends "KEY ACCEPTED".  Nothing has failed, but
   from now on whoever holds the key can switch the hand-off and the missile
   lock off, in a fight, by radio.  Banbridge grounds the Hellcats: not
   because they are broken, because they can be broken by someone else.
   The same ping told the Kilrathi where the Culloden was: that is the heavy
   wing that arrives at the range once the hulks are dead, and why the recon
   pair at Nav 2 must not get home.
3. **Mission 2: find the source.**  The Morningstars carry direction
   finders; the Epees keep them alive while they take the bearing.  The
   bearing points into the rocks.
4. **Mission 3: the relay and its core.**  The core holds the relay's half
   of the key pair.  With it Sparks can purge the maintenance channel.  The
   core's logs also name Prescott, who is arrested.  But the relay's keeper
   left before we arrived, in a Kamekh, carrying the other half: a working
   key, and the Culloden's position.
5. **Mission 4: the Vak'thra.**  If she dies, the loose key dies with her:
   Sparks purges the channel, certifies the fighters, and nobody knows the
   Culloden is here.  **The Hellcats fly again because the player killed the
   only other key.**  If she jumps, two things are true at once: the
   Kilrathi know where the Culloden is, so every jump ahead is awake (the
   long road), and the key is loose, so the Hellcats stay in the hangar.
6. **The traders.**  Their charts are the one way to skip the defended
   jump chain.  Both alive: short road.  Otherwise: long road.
7. **The long road is flown in the reserve ships whoever takes it.**  On
   the bad path the Hellcats are not certified.  On the good path Banbridge
   orders them held: six airframes, no spares, one strike; he will not spend
   them on a week of pickets.  The briefing says which reason applies.
8. **The strike** is flown with Hellcats if they were certified, in Rapiers
   if not.  Same yard, same torpedoes, different escort.

## The campaign

### Series 1: the Hellcats, and losing them

**1-0 Shakedown** *(Hellcats; Maniac)*.  The hulks at the range, then the
reaction force the relay's ping called in: three Gothri and two Jalkehi,
heavy fighters the particle-and-neutron pairing is for.  The Drakhri recon
pair at Nav 2 must not get home.  Debrief: the uplink in the logs; the
Hellcats are grounded.

**1-1 Hand-me-downs** *(Epees; Doomsday, Downtown)*.  Escort the two
Morningstars, Stingray and Valkyrie flying them, to the rockline for the
bearing and on to Nav 2, where a Kamekh is hunting the Culloden.  A Drakhri
ambush goes for the bombers.  Objectives: the Kamekh dead, both
Morningstars home.

**1-2 The relay** *(Rapiers; Valkyrie, Downtown)*.  Strakha in the rocks,
the relay and its Drakhri guard, the data core to tractor in.  At the
far jump point the Vak'thra is already under way for Kilrathi space with
a tanker waiting; Rapiers can chase her but will not catch her.

**1-3 Torpedo run** *(Morningstar; Stingray, Valkyrie in Rapiers)*.  Catch
the Vak'thra before the jump point, through her Gothri and Jalkehi escort.
*The fork:* dead, series 2; gone, series 3.

### Series 2: the Hellcat road (Hellcats certified)

**2-0 Kamekh hunt.**  Two corvettes and their Gothri, the fighters the
Hellcat was built to meet.  **2-1 The Lucky Mandy.**  The traders adrift
near a Kilrathi colony; two waves while they relight.  *Branch:* both alive,
2-2; otherwise series 3 at its second mission.  **2-2 Transit.**  The short
road's one picket, Grikath bombers heading for the Culloden.  **2-3 The
inner picket.**  The yard's early-warning post and its Gothri aces.

### Series 3: the long road (reserve ships, whoever comes)

**3-0 Known.**  The Kilrathi know a cruiser is loose: defend the Culloden
against a Grikath strike at the first jump.  Rapiers.  **3-1 Salvage.**  A
dead Confederation depot on the way, one flyable Sabre on its deck, a
Kilrathi salvage team arriving: hold it until the Sabre is flown off.
Valkyrie takes it and keeps it.  **3-2 Minefield.**  A jump point whose
mines are on no chart we have; the enemy knows where they are.  **3-3 The
outer picket.**  The yard's early-warning post from the wrong side, in a
Morningstar with the Sabre as escort.

### Series 4: the strike, Hellcats

**4-0 The yard.**  The Morningstar with the torpedoes, Maniac's Hellcat and
one more as escort.  The yard, its frame, the half-built hull.  **4-1
Home.**  Cover the Culloden until her jump engines are up.  **4-2 / 4-3**
the endings that need a flight of their own.

### Series 5: the strike, Rapiers

The same two missions and endings with the escort in Rapiers and the
Sabre.  Reached by anyone whose Hellcats never came back.

### Where the two-player seats fall

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
