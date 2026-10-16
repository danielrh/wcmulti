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
* **Captain Wayne Banbridge**, commanding.  `people.v22` [Tolwyn],
  recoloured: hair and brows darkened, the uniform's braid changed.  A
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
* **Major Etienne "Doomsday" Montclair.**  `people.v05`.  Gets the bad
  missions, says so, flies them anyway.  Leads the reserve squadron's Epee
  patrols; the second player's seat in those.
* **Captain Dirk "Stingray" Wright.**  `people.v06` (or `v21`/`v38` in
  uniform).  Alive, loud, convinced the Hellcats should be his.  The
  wingman in the Morningstar missions' escorts and the long route.
* **Lt. Cmdr. Janet "Sparks" McCullough**, chief of the deck.  `people.v07`.
  Pulls the Hellcats' computers after mission 1 and finds the uplink.  The
  campaign's honest technician: everything she says about the ships is true.
* **Dr. Lyle Prescott**, the Hellcat programme's liaison.  `people.v10`,
  renamed.  Gives the first briefing and tells the player to trust the
  hand-off computers.  The Mandarins' man aboard; the relay's logs name him
  in series 1.  Not a monster: his family is on a Mandarin world, and the
  campaign lets him say so before he is taken off the story.
* **Lieutenant "Downtown".**  `people.v04` or `people.v09` (to be
  confirmed on the portrait sheet).  The Concordia's youngest, alive in this
  telling, transferred with Maniac and Doomsday.  Flies the reserve
  squadron's third ship, the one the bad missions are hardest on.
* **Lt. Ines "Toro" Dalmau**, the Culloden's own pilot.  `people.v33`,
  renamed.  Named so that a death in the reserve missions means something;
  the engine keeps the count.

**Elsewhere**

* **Dez Morrow**, master of the Free Trader *Lucky Mandy*, and **Pilar
  Oyelaran** of the *Tsiolkovsky*.  `people.v31` and `people.v30`, renamed.
  Smugglers to the Kilrathi colonies, stranded with the yard's approach
  charts in their holds.  Clemency and an escort home for the charts.
* **Kalralahr Ghraffid nar Ragitagha**, master of the yard.  `people.v17`
  (a Kilrathi face the games used little).  Proud of what he is building;
  the campaign's voice of the enemy in two comm scenes and the finale.
* **The Mandarin relay's keeper.**  Voice only, no face: the recorded
  "Have a nice day" and two taunts in the asteroid field.

**Available but not used:** Paladin (`v02`) as the handler who sold the
raid, in a prologue; Hobbes (`v03`) to read the Kilrathi charts.  Spirit
and Jazz are dead; Angel (`v14`, `v01`) and Tolwyn do not sail with us.

## The ships

| | type | who | when |
|---|---|---|---|
| Hellcat V | 45 | the player, Maniac | series 1; later if restored |
| Hellcat, manual law | 46 (same art, crippled numbers) | the same | series 1 missions 3 and 4 |
| Epee, Rapier | 3, 1 | the reserve squadron: Doomsday, Bergstrom, Downtown, Dalmau | series 3 and the long route |
| Sabre | 4 | Bergstrom, from mission 3-1 on | the one found at the depot |
| Morningstar | 41 | the player, with escorts | 2-1 and the finale |
| TCS Culloden | 17 (Waterloo) | home | every launch and landing |
| Lucky Mandy, Tsiolkovsky | 12 (Free Trader) | the traders | series 2 and 3, and an ending |
| Target hulk | 29 (a Dorkathi with its turrets stripped) | the range | mission 1-0 |
| The relay | 48 (Ayer's Rock) | the Mandarins' | mission 1-3 |
| The yard | 25 (K supply depot), with a Fralthra hull | the Kilrathi | the finale |

The "manual law" Hellcat is a second type number with the Hellcat's files
and a worse record: top speed 380, recharge 2, turn 7, no missiles.  The
engine swaps a ship's type between missions for free; the story says why.

## The campaign

### Series 1: Shakedown (Hellcats)

**1-0 Shakedown** *(built)*.  Live fire on three condemned hulks at the
range, a Sartha pair that was watching the range, a Drakhri recon pair at
Nav 2 that must not get home.  Prescott's briefing: trust the computers.
Debrief: Sparks found an uplink burst in every Hellcat's log, and the
recording ends with the player's own callsign.  *Flag:* a Drakhri escaped.

**1-1 First blood.**  A Kilrathi picket at the jump point the Culloden
needs.  Two nav points; at the second the computers drop the hand-off (the
radio says so; the game cannot change a ship mid-flight, so this is the
last mission the Hellcats fly whole).  If a Drakhri escaped in 1-0, the
picket is awake: a Kamekh corvette and two more Jalkehi.

**1-2 Dark** *(manual-law Hellcats)*.  The Culloden runs for an asteroid
field to hide and to follow the uplink's bearing.  Escort her in, fight in
the rocks with a fighter that will not lock a missile.  Objective: the
Culloden alive at the field.

**1-3 The relay** *(manual-law Hellcats)*.  The abandoned Mandarin relay
deep in the field, its guard of Strakha and Drakhri, and its data core.
Kill the guard, tractor the core before the last defender rams it.
*Branch:* **core recovered** goes to series 2, the Hellcats restored in
full; **core destroyed** goes to series 3, the reserve squadron, and the
Hellcats stay down until 3-2 is won.  Either way the logs name Prescott.

### Series 2: The Hellcat road

**2-0 Clearing the field.**  Out of the rocks, the Hellcats back, against
the search the relay's silence called down: three waves at one nav point,
the last with a Fralthra.  Maniac's seat.

**2-1 Torpedo run** *(Morningstar)*.  The Fralthra that got away is
sitting at a jump point the Culloden must use.  The player in a
Morningstar with Stingray's Hellcat as escort (the second player flies the
Hellcat).  Objective: the cruiser dead; failure means the long route later
whatever the traders do.

**2-2 The Lucky Mandy.**  Two Free Traders adrift near a Kilrathi colony,
drives dead, holds full.  Morrow and Oyelaran have the yard's charts.
Defend both through two waves while they relight.  *Branch:* both alive,
the short route (series 5 directly); one, the long route (series 4); none,
the long route and the Culloden launches the next mission damaged.

**2-3 Transit.**  The short route's one surprise: a picket that was not on
the charts.  Hellcats.

### Series 3: Hand-me-downs (the reserve squadron)

**3-0 Hand-me-downs.**  Doomsday leads the player, in Epees, on the patrol
nobody wanted: a mining colony's distress call that is a Kilrathi trap.
Downtown or Dalmau flies the third ship and may not come back.

**3-1 Salvage.**  An abandoned Confederation supply depot in a dead system,
with one flyable Sabre on its deck and a Kilrathi salvage team arriving at
the same time.  Rapiers.  Hold the depot until the Sabre is flown off:
Bergstrom takes it, and flies it from here on; the player stays in the
Rapier.  (Two-player: the second player has the Sabre.)

**3-2 The hard one.**  The Kilrathi listening post that holds a copy of the
relay's core, behind a minefield and a Ralatha.  Rapiers, Bergstrom's
Sabre, no second chances.  *Branch:* **won**, the Hellcats come back
(series 5 in Hellcats, with the partial fix: the missile link is gone,
four missiles not six); **lost**, the campaign stays in Rapiers and
Epees, with a Morningstar for the strike.

**3-3 The Lucky Mandy** *(reserve ships)*.  The traders' rescue flown in
Epees and the Sabre, harder for it: the same two waves and a third.
*Branch* as 2-2.

### Series 4: The long route

**4-0 Jump chain.**  Without charts the Culloden takes the defended
approach.  A picket at each end of a jump, flown back to back.

**4-1 Minefield.**  Defend the Culloden at a jump point whose mines are
not on any chart she has; the enemy knows where they are.  Objective: the
Culloden alive.  If both traders were lost, she arrives already damaged.

**4-2 Jump chain, second leg.**  Stingray's seat.  A Kamekh and its
escort; a Dorkathi convoy to pass up or take.

**4-3 Picket, the long way.**  The yard's outer early-warning post, taken
from the wrong side.

### Series 5: The yard

**5-0 Picket.**  The yard's inner early-warning post and its fighter
screen, before the strike launches.  Hellcats if restored, else Rapiers.
What is left of the screen is what the strike meets.

**5-1 The yard.**  The player in a Morningstar with the torpedoes, Maniac
(the second player) and one AI Hellcat as escort, or Rapiers if the
Hellcats never came back.  The yard, its frame, the half-built hull.
Ghraffid on the comm.

**5-2 Home.**  The Culloden jumping out under attack; the flight covers
her until the jump engines are up.  *Endings:* yard destroyed and Culloden
alive; yard destroyed and Culloden lost, the survivors landing on the Lucky
Mandy; the yard still standing.

**5-3 Reserved** for the ending that needs its own flight (the Lucky Mandy
landing), or a fourth leg of the long route.

### Where the two-player seats fall

| missions | the second player flies |
|---|---|
| 1-0, 1-1, 2-0, 2-2, 2-3, 5-0 | Maniac's Hellcat |
| 1-2, 1-3 | Maniac's manual-law Hellcat |
| 2-1, 5-1 | the escort Hellcat (host in the Morningstar) |
| 3-0, 3-3 | Doomsday's Epee |
| 3-1, 3-2 | Bergstrom's Rapier, then her Sabre |
| 4-x | Stingray's ship of the day |
| 5-2 | whatever the flight has left |

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
6. **The manual-law Hellcat**, type 46: one more record.
