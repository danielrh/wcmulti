# Special Operations 3: *Culloden*

A fan campaign for Wing Commander II's engine, flown in the Hellcat V.  This
is the story the missions in `campaign.py` serve, with the reasons for its
shape.  Mission numbers are the campaign's (the engine counts series of
four).

## The brief we started from

A Waterloo-class cruiser on a deep infiltration to sabotage a key capital
ship construction facility.  The Hellcat introduced as the next generation
fighter and flown on the first mission; a saboteur hacks the new flight
control systems and the Hellcats are grounded for several missions; a secret
asteroid base holds the clues to the hack and brings them back online.  Near
the Kilrathi base, two stranded free traders who smuggled to Kilrathi
colonies trade maps for clemency and safe passage; defend them and the
campaign jumps ahead to a bombing run, a Morningstar escorted by two
Hellcats; lose them and the long, dangerous map is flown instead.

## What was changed, and why

**The sabotage happens in the player's hands, not in a briefing.**  Being
told "you are grounded" after one flight takes the new fighter away as a
plot device.  Here the Hellcats' computers misbehave while the player is
flying them, and the debriefing plays back what the fighter's own computer
logged.  The grounding is something the player saw.

**Nobody is grounded.  The Hellcats fly without their computers.**  Three
missions in a worse ship right after the showcase is a tease.  Instead the
same airframe flies with the fly-by-wire pulled: a second ship type with the
same art and crippled numbers (slower afterburner, lazier turn, no missile
lock).  The engine does this for free, and the player feels exactly what the
computer was doing for them.  When the computers come back, so does the
fighter they remember.

**The clue is a thing to fetch, not a thing to be told.**  The hack is an
uplink: the Hellcats' computers answer a transmitter hidden in an asteroid
field, an abandoned Mandarin relay, since the Mandarins are the traitors
this engine already knows (Ayer's Rock, their asteroid base, is a ship type
in Special Operations 2).  The mission is to find the relay, kill its guard
and tractor its core out before the guard finishes it, which Wing Commander
II scores as a pick-up objective.  Core captured: the computers are cleaned
properly and the Hellcats come back whole.  Core lost in the fight: they come
back on a partial fix, with the targeting link still gone.  That is a branch
the player earns in a dogfight, not in a cutscene.

**The saboteur is someone the player has met.**  The Hellcat programme's
liaison officer, who gives the briefing on the first mission and tells the
player to trust the computers.  The campaign never names them as a suspect
before the core's logs do.

**The traders are a choice with two failure modes, not one.**  Both survive
and send the full charts: the short route, straight to the yard.  One lost:
partial charts, the long route through a defended jump chain.  Both lost: the
long route, and the Culloden takes the hits meant for them, so the last
missions launch from a damaged ship.  The engine's campaign script branches
on exactly these: which mission ships were alive at the end.

**The finale uses two seats.**  The strike is one Morningstar with the
torpedoes and the escort Hellcats.  In two-player games the host flies the
bomber and the wingman the escort, which is the division of labour the
multiplayer layer already has (the Morningstar has no turret, so there is
no gunner seat to argue about).

**The target has a name worth a cruiser.**  The yard builds the first hull
of a new Kilrathi carrier class, which is why one cruiser is risked eleven
jumps behind the lines, and why Admiral Tolwyn is aboard a ship that small.
Players who know the novels will recognise what is being built; the campaign
does not need them to.

**The ship is small on purpose.**  A Waterloo carries a handful of fighters.
Six Hellcats and two Morningstars, no reserves, no second chances: a pilot
lost stays lost (the engine already keeps that count), and the Culloden's
own survival is an objective more than once.

## The campaign

*2668, after the Mandarin affair.*  TCS Culloden, Waterloo-class, Captain
Isolde Marr commanding, carries the Hellcat V evaluation flight (six
pre-production fighters, their programme liaison Lt. Cmdr. Deel Halloran),
two Morningstars, and Admiral Tolwyn, who wants this done quietly.  The
player leads the flight.  Maniac, transferred after the Concordia business,
is the second ship, and the second player.

### Act 1: Shakedown (Hellcats)

**1. Shakedown.**  Live fire on three captured hulks at the range, a pair of
Sartha that was watching the range, and a Drakhri recon pair at Nav 2 that
must not get home.  The briefing introduces the Hellcat and tells the player
to trust the hand-off computer.  Debriefing: every Hellcat logged an uplink
burst during the fight, and the recording ends with the player's own
callsign.  *Consequence:* a Drakhri that got away means Act 2 opens with
the Culloden found.

**2. First blood.**  A Kilrathi picket in the way of the raid.  Mid-fight the
computers drop the target hand-off and the missiles will not lock (the
mission's second nav point is flown in the crippled type; the radio says
why).  The flight limps home.

### Act 2: Blind (Hellcats without computers)

**3. Dark.**  The Culloden runs for an asteroid field to hide and to follow
the uplink's bearing.  Escort her in; fight in the rocks.  If mission 1 lost
a Drakhri, this is a defence of the cruiser instead, and she is hit.

**4. The relay.**  The abandoned Mandarin relay (Ayer's Rock) in the field,
its guard, and its core.  Tractor the core before the guard destroys it.
*Branch:* core recovered, the Hellcats return whole; core destroyed, they
return without the targeting link.  Either way the core's logs name
Halloran, who is taken off flight status and the story.

**5. Clearing the field.**  Out of the rocks, the Hellcats back (in whichever
shape), against the search that the relay's silence has called down.

### Act 3: The traders

**6. The Lucky Mandy and the Tsiolkovsky.**  Two Free Traders adrift near a
Kilrathi colony, engines dead, holds full of what they were not supposed to
carry.  They have the yard's approach charts.  Clemency and an escort home
for the charts, says the player; they take it.  Defend both through two
waves while they relight their drives.  *Branch:* both alive, the short
route; one, the long route; none, the long route and a damaged Culloden.

**7a. The short route** (both traders): straight to the yard's system; one
transit mission against a surprised picket.

**7b. The long route** (one or none): two missions through a defended jump
chain, flown in Hellcats, the second a defence of the Culloden at a jump
point with no charts to tell her where the mines are.

### Act 4: The yard

**8. Picket.**  Both players in Hellcats: kill the yard's early-warning post
and its fighter screen before the strike launches.  What is left of the
screen is what the strike meets.

**9. The yard.**  The host in a Morningstar with the torpedoes, the wingman
and one AI Hellcat as escort.  The yard, its construction frame and the
half-built hull in it.  Endings: the yard destroyed and the Culloden alive;
the yard destroyed and the Culloden lost, the survivors landing on the Lucky
Mandy; the yard still standing.

## Mission 1 as built

What `campaign.py` puts in MODULE.003 for series 1, mission 0:

* Nav 0, the Culloden: the cruiser, the player's Hellcat and Maniac's.
* Nav 1, the range: three Sartha hulks that do not shoot back, and a Sartha
  pair that does, placed beyond the hulks so that the first passes are a
  shooting gallery and the fight comes to the player.
* Nav 2: a Drakhri pair with the Imperial Guard's voice, the recon patrol.
* Objectives: reach the range, clear it, sweep Nav 2, destroy the recon
  leader, clear Nav 2, land on the Culloden.
* Scene text: the briefing and debriefing lines above, in the slots of
  Special Operations 2's first block.  The pictures behind them are still
  the old game's, and the opening cutscene's lines are blank, until the scene
  scripts are ours (see `docs/so3.md`).
