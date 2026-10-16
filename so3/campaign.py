"""SO3 "Culloden" -- the campaign's missions and scene texts, as data for
so3/build.py.  so3/story.md is the story these serve; scripts/wc2data.py
says what the fields are.

Positions are kilometres relative to the nav point; a nav point's position
is relative to the mission's origin.  Ship types are Wing Commander II's
numbers (the Hellcat is ours, 45: the program draws types 48 and 49, its asteroid bases, by other rules)."""

HELLCAT = 45
WATERLOO, SARTHA, DRAKHRI, FREE_TRADER, MORNINGSTAR, DORKATHI = 17, 5, 6, 12, 41, 13
HULK = 29                # ours: a Dorkathi with its flak turrets stripped (every WC2 transport has them)
EPEE, RAPIER, SABRE, GOTHRI, JALKEHI, GRIKATH, STRAKHA, KAMEKH, AYERS_ROCK = 3, 1, 4, 50, 7, 8, 9, 16, 48   # Gothri: SO2 flies its own as type 50
ASTEROIDS, MINES, POD = 51, 52, 53       # not ships: a field, a field, a thing to tractor in (class 5, 6, 61)
# What the nav lists say of the non-ships (no SHIP file to read it from).
SPECIAL_TYPES = {ASTEROIDS: (0, 5), MINES: (0, 6), POD: (0, 61)}

# Comm personalities (COMMUNIC.Tnn): who answers on the radio.
MANIAC = 0x24            # "It's party time!" (INTEL.036 flies him)
DOOMSDAY = 0x09          # (INTEL.009)
STINGRAY = 0x04          # (INTEL.004)
BERGSTROM = 0x0B         # Shadow's voice, "Affirmative, Captain", and INTEL.011: a steady wingman
DOWNTOWN = 0x10          # a generic Confederation pilot: "Affirmative, sir"; flies by the ship type's file
FLIGHT_OPS = 0x11        # a Confederation ship's bridge: "Affirmative, Captain."
KILRATHI = 0x1A          # "Die, Terran coward!"
KILRATHI_ACE = 0x19      # the Imperial Guard
KILRATHI_SHIP = 0x1B     # a Kilrathi capital ship's bridge
NOBODY = 0


# The Hellcat's own numbers (the record HCl's conversion carried was the
# Rapier's).  WC2's for comparison -- Rapier / Sabre / Morningstar:
# recharge 6 / 9 / 9, speed 450 / 400 / 400, acceleration 25 / 22 / 22,
# turn 10 / 8 / 8, shields 115 / 80 / 150, armour 75-75-65-65 /
# 200-200-180-180 / 200-200-200-200.
HELLCAT_STATS = dict(
    shield_recharge=4,            # slower than either
    max_speed=420,                # kps; the afterburner is the game's 1200
    acceleration=24,
    yaw=9, pitch=9, roll=9,       # a tenth under the Rapier, above the heavies
    shield_front=100, shield_rear=100,   # under the Rapier's: the armour is what it has
    armor_front=120, armor_rear=120, armor_left=90, armor_right=90,
)
# Guns inboard pair first, then the outboard pair; six missiles; one decoy.
HELLCAT_GUNS = ["particle", "particle", "neutron", "neutron"]
HELLCAT_MISSILES = ["spiculum_ir"] * 6
HELLCAT_DECOYS = 1


def ship(name, shiptype, side, **kw):
    """A mission ship as keyword arguments for wc2data.ship_record; the
    type's kind and class are filled in by the builder."""
    d = dict(name=name, shiptype=shiptype, side=side)
    d.update(kw)
    return d


def player(shiptype, pos, yaw=0):
    return ship(TYPE_NAMES[shiptype], shiptype, 0, pos=pos, yaw=yaw, behave=0, radius=20, b33=4, ai=6, group=0)


def wingman(name, shiptype, leader, pilot):
    return ship(name, shiptype, 0, behave=4, important=1, formation=1, b33=3, ai=5, leader=leader, group=0, objective=leader, pilot=pilot)


def capital(name, shiptype, side, pos, yaw=0, pilot=FLIGHT_OPS, important=1, ai=4):
    return ship(name, shiptype, side, pos=pos, yaw=yaw, behave=-1, important=important, b33=4, ai=ai, pilot=pilot)


def third(name, shiptype, leader, pilot, formation=2):
    """An AI pilot of the player's wing, behind the wingman."""
    return ship(name, shiptype, 0, behave=4, formation=formation, b33=4, ai=4, leader=leader, group=0, objective=leader, pilot=pilot)


def field(kind, pos, radius):
    """An asteroid (ASTEROIDS) or mine (MINES) field of about radius km."""
    return ship("asteroids" if kind == ASTEROIDS else "mines", kind, 2, pos=pos, behave=-1, important=1, radius=max(0, radius - 3000), b33=3, ai=2)


def pod(name, pos):
    """Something to tractor in: an objective of kind 6 names it."""
    return ship(name, POD, 0, pos=pos, behave=-1, important=1, b33=3, ai=3)


def runner(name, shiptype, pos, yaw, toward, pilot=KILRATHI_SHIP):
    """A capital ship under way toward mission ship `toward`, as WC2's Ralatha
    that gets away: behaviour 6 with that ship as its objective."""
    return ship(name, shiptype, 1, pos=pos, yaw=yaw, behave=6, radius=6, b33=3, ai=4, objective=toward, pilot=pilot)


def wave(nav, types, ships, phase=2):
    """A reinforcement wave for the nav point whose record precedes this one:
    the engine reads the following records while their visible byte is the
    next phase number, and spawns their ships at the current nav point once
    its fight is over (SO2.EXE ovr115:17F6)."""
    return dict(name="", pos=nav["pos"], visible=phase, types=types, ships=ships)


def hulk(name, shiptype, pos, yaw=0):
    """Something to shoot at that does not shoot back."""
    return ship(name, shiptype, 1, pos=pos, yaw=yaw, behave=-1, b33=4, ai=0, pilot=NOBODY)


def wing(name, shiptype, count, pos, yaw, group, objective, pilot=KILRATHI, ai=4, side=1, first_index=None):
    """A leader placed at pos and count-1 followers in formation.  The
    followers need the leader's mission ship index: pass first_index."""
    out = [ship(name, shiptype, side, pos=pos, yaw=yaw, behave=2, radius=20, b33=4, ai=ai, group=group, objective=objective, pilot=pilot)]
    for i in range(1, count):
        out.append(ship(name, shiptype, side, behave=4, formation=i, b33=4, ai=ai, leader=first_index, group=group, objective=first_index, pilot=pilot))
    return out


TYPE_NAMES = {HELLCAT: "Hellcat", WATERLOO: "Waterloo", SARTHA: "Sartha", DRAKHRI: "Drakhri", FREE_TRADER: "Free Trader", MORNINGSTAR: "Morningstar", DORKATHI: "Dorkathi", HULK: "Hulk",
              EPEE: "Epee", RAPIER: "Rapier", SABRE: "Sabre", GOTHRI: "Gothri", JALKEHI: "Jalkehi", GRIKATH: "Grikath", STRAKHA: "Strakha", KAMEKH: "Kamekh"}


def mission_1():
    """Shakedown.  The first flight of the Hellcats: live fire on captured
    hulks at the range, a Kilrathi pair that was watching the range, and a
    recon patrol at the second nav point that must not get away."""
    ships = [
        capital("Culloden", WATERLOO, 0, pos=(0, 0, 0), yaw=45),          # 0
        player(HELLCAT, pos=(2200, 0, 0), yaw=35),                         # 1
        wingman("Maniac", HELLCAT, leader=1, pilot=MANIAC),                # 2
        # captured Kilrathi transports with their turrets stripped, towed out
        # as targets (a fighter hull with no orders still flies; a transport
        # with its guns still shoots)
        hulk("Target hulk", HULK, pos=(0, 0, -2500), yaw=20),              # 3
        hulk("Target hulk", HULK, pos=(1800, 300, -4000), yaw=-70),        # 4
        hulk("Target hulk", HULK, pos=(-1700, -200, -4200), yaw=140),      # 5
    ]
    # the reaction force the relay's ping called in, once the hulks are dead
    # (one new fighter type per nav point beside the player's: a fighter
    # type keeps all its views in memory, and three of them do not fit)
    ships += wing("Gothri", GOTHRI, 4, pos=(9000, 0, -12000), yaw=170, group=1, objective=1, pilot=KILRATHI_ACE, first_index=6)     # 6, 7, 8, 9
    ships += wing("Drakhri", DRAKHRI, 3, pos=(8000, 0, 7000), yaw=-130, group=2, objective=1, pilot=KILRATHI_ACE, first_index=10)  # 10, 11, 12
    range_ = dict(name="Range", pos=(35000, 0, 25000), types=[HULK], ships=[3, 4, 5])
    navs = [
        # (the player first, the wing next, the carrier last: slots are given in
        # this order and the multiplayer layer seats the second player in slot 1)
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, HELLCAT], ships=[1, 2, 0]),
        range_,
        wave(range_, types=[GOTHRI], ships=[6, 7, 8, 9]),
        dict(name="Nav 2", pos=(-30000, 0, 60000), types=[DRAKHRI], ships=[10, 11, 12]),
    ]
    objectives = [
        (0, 1, "Proceed to the range at Nav 1"),
        (5, 1, ".the hulks, and what comes for you after them"),
        (0, 3, "Sweep Nav 2"),
        (4, 10, "Destroy the recon patrol"),
        (5, 3, ".its wingmen too"),
        (1, 0, "Return to the Culloden"),
    ]
    return dict(series=1, mission=0, name="Shakedown", series_name="Culloden",
                header=dict(launch=0, carrier=0, player=1, wing=[2]),
                navs=navs, ships=ships, objectives=objectives)


def mission_2():
    """Hand-me-downs.  The Hellcats grounded (their computers answer to a key
    we do not hold), the player leads Doomsday and Downtown in Epees,
    escorting the two Morningstars whose direction finders will trace the
    uplink to its source.  A Drakhri ambush at the edge of the rocks goes
    for the bombers; a Kamekh hunting the Culloden waits beyond."""
    ships = [
        capital("Culloden", WATERLOO, 0, pos=(0, 0, 0), yaw=45),                       # 0
        player(EPEE, pos=(2200, 0, 0), yaw=35),                                         # 1
        wingman("Doomsday", EPEE, leader=1, pilot=DOOMSDAY),                            # 2
        third("Downtown", EPEE, leader=1, pilot=DOWNTOWN),                              # 3
        ship("Stingray", MORNINGSTAR, 0, pos=(1500, -300, -800), yaw=35, behave=2, important=1, radius=20, b33=4, ai=4, group=0, objective=10, pilot=STINGRAY),   # 4
        ship("Valkyrie", MORNINGSTAR, 0, pos=(-1500, -300, -800), yaw=35, behave=2, important=1, radius=20, b33=4, ai=4, group=0, objective=10, pilot=BERGSTROM), # 5
        field(ASTEROIDS, pos=(0, 0, 0), radius=8000),                                   # 6
    ]
    ships += wing("Drakhri", DRAKHRI, 3, pos=(6000, 0, -8000), yaw=100, group=1, objective=4, first_index=7)                    # 7, 8, 9
    ships += [ship("Kamekh", KAMEKH, 1, pos=(4000, 0, -6000), yaw=160, behave=2, radius=6, b33=3, ai=4, objective=1, pilot=KILRATHI_SHIP)]  # 10
    ships += wing("Grikath", GRIKATH, 3, pos=(7000, 500, -9000), yaw=150, group=2, objective=5, first_index=11)                   # 11, 12, 13
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, EPEE, MORNINGSTAR], ships=[1, 2, 3, 4, 5, 0]),
        dict(name="Rockline", pos=(35000, 0, 25000), types=[ASTEROIDS, DRAKHRI], ships=[6, 7, 8, 9]),
        dict(name="Nav 2", pos=(-30000, 0, 60000), types=[KAMEKH, GRIKATH], ships=[10, 11, 12, 13]),
    ]
    objectives = [
        (0, 1, "Escort the Morningstars to the rockline"),
        (5, 1, ".the ambush"),
        (0, 2, "Escort them on to Nav 2"),
        (4, 10, "Destroy the Kamekh"),
        (2, 4, ".Stingray's Morningstar survives"),
        (2, 5, ".Valkyrie's Morningstar survives"),
        (1, 0, "Return to the Culloden"),
    ]
    return dict(series=1, mission=1, name="Hand-me-downs",
                header=dict(launch=0, carrier=0, player=1, wing=[2, 3, 4, 5]),
                navs=navs, ships=ships, objectives=objectives)


def mission_3():
    """The relay.  Rapiers into the rocks: Strakha at the edge, the Mandarin
    relay (Ayer's Rock) and its Drakhri guard at the heart, and the relay's
    data core to tractor in.  The relay's keeper is already running for the
    jump point in a Kamekh, with the other half of the key."""
    ships = [
        capital("Culloden", WATERLOO, 0, pos=(0, 0, 0), yaw=45),                       # 0
        player(RAPIER, pos=(2200, 0, 0), yaw=35),                                       # 1
        wingman("Valkyrie", RAPIER, leader=1, pilot=BERGSTROM),                         # 2
        third("Downtown", RAPIER, leader=1, pilot=DOWNTOWN),                            # 3
        field(ASTEROIDS, pos=(0, 0, 0), radius=9000),                                   # 4
    ]
    ships += wing("Strakha", STRAKHA, 2, pos=(5000, 800, -7000), yaw=120, group=1, objective=1, first_index=5)                  # 5, 6
    ships += [capital("Relay", AYERS_ROCK, 1, pos=(0, 0, -6000), yaw=0, pilot=KILRATHI_SHIP, important=0)]                      # 7
    ships += wing("Drakhri", DRAKHRI, 3, pos=(3000, 0, -9000), yaw=160, group=2, objective=1, first_index=8)                     # 8, 9, 10
    ships += [pod("Data core", pos=(900, 200, -4800))]                                                                             # 11
    ships += [runner("Vak'thra", KAMEKH, pos=(0, 0, -3000), yaw=0, toward=13)]                                                    # 12
    ships += [capital("Tanker", DORKATHI, 1, pos=(0, 0, -30000), yaw=0, pilot=KILRATHI_SHIP, important=0)]                      # 13
    ships += wing("Drakhri", DRAKHRI, 2, pos=(1500, 0, -4000), yaw=0, group=3, objective=1, first_index=14)                      # 14, 15
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, RAPIER], ships=[1, 2, 3, 0]),
        dict(name="Rockline", pos=(30000, 0, 20000), types=[ASTEROIDS, STRAKHA], ships=[4, 5, 6]),
        dict(name="Relay", pos=(45000, 0, 50000), types=[AYERS_ROCK, DRAKHRI, POD], ships=[7, 8, 9, 10, 11]),
        dict(name="Jump point", pos=(-10000, 0, 75000), types=[KAMEKH, DORKATHI, DRAKHRI], ships=[12, 13, 14, 15]),
    ]
    objectives = [
        (0, 1, "Proceed into the rocks"),
        (5, 1, ".the Strakha"),
        (0, 2, "Find the relay"),
        (5, 2, ".its guard"),
        (6, 11, "Recover the relay's data core"),
        (4, 12, ".the Vak'thra, if you can catch her"),
        (1, 0, "Return to the Culloden"),
    ]
    return dict(series=1, mission=2, name="The relay",
                header=dict(launch=0, carrier=0, player=1, wing=[2, 3]),
                navs=navs, ships=ships, objectives=objectives)


def mission_4():
    """Torpedo run.  The Vak'thra is making for the jump point with the
    keeper and the other half of the key.  The player in a Morningstar,
    Stingray and Valkyrie in Rapiers.  Dead, the key dies with her and the
    Hellcats can be cleared; gone, the Kilrathi know where we are and the
    Hellcats stay down."""
    ships = [
        capital("Culloden", WATERLOO, 0, pos=(0, 0, 0), yaw=45),                       # 0
        player(MORNINGSTAR, pos=(2200, 0, 0), yaw=35),                                  # 1
        wingman("Stingray", RAPIER, leader=1, pilot=STINGRAY),                          # 2
        third("Valkyrie", RAPIER, leader=1, pilot=BERGSTROM),                           # 3
        runner("Vak'thra", KAMEKH, pos=(3000, 0, -9000), yaw=20, toward=8),             # 4
    ]
    ships += wing("Gothri", GOTHRI, 3, pos=(2500, 0, -8000), yaw=30, group=1, objective=1, pilot=KILRATHI_ACE, first_index=5)   # 5, 6, 7
    ships += [capital("Tender", DORKATHI, 1, pos=(0, 0, -2000), yaw=0, pilot=KILRATHI_SHIP, important=0)]                       # 8
    ships += wing("Drakhri", DRAKHRI, 2, pos=(2000, 0, -3000), yaw=0, group=3, objective=1, first_index=9)                      # 9, 10
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, MORNINGSTAR, RAPIER], ships=[1, 2, 3, 0]),
        dict(name="Vak'thra", pos=(30000, 0, 20000), types=[KAMEKH, GOTHRI], ships=[4, 5, 6, 7]),
        # scene 211 is Special Operations 2's "disabled freighter hails you" at
        # its mission 4's Nav 2, played on arrival (mode 4); our block 3 gives it
        # the tender's lines
        dict(name="Jump point", pos=(-5000, 0, 70000), types=[DORKATHI, DRAKHRI], ships=[8, 9, 10], steps=[(1, 211, 4)]),
    ]
    objectives = [
        (0, 1, "Intercept the Vak'thra"),
        (4, 4, "Destroy the Vak'thra before she jumps"),
        (5, 1, ".her escort"),
        (0, 2, "The jump point"),
        (5, 2, ".whoever waits there"),
        (1, 0, "Return to the Culloden"),
    ]
    return dict(series=1, mission=3, name="Torpedo run",
                header=dict(launch=0, carrier=0, player=1, wing=[2, 3]),
                navs=navs, ships=ships, objectives=objectives)


MISSIONS = [mission_1(), mission_2(), mission_3(), mission_4()]

# The scene texts of SERIES.S03, block 0: the script of Special Operations
# 2's first block names its lines by number, so the count and the roles of
# the slots are kept: 0-22 the new-game introduction (blank for now: the
# scene's pictures are the old game's), 23-30 the briefing (23 a title
# card, 25-28 the briefer, 29 the player, 30 the briefer's send-off), 31-38
# the debriefing (32 a title card, 33 and 36-38 the Admiral, 34-35 the
# player), 39-41 a recording played in it, 42-44 radio lines in flight;
# 45 strings in all.
# \x86 is the player's callsign, \x85 the player's surname.
SERIES_TEXT = {
    # Block 0, mission 1-0.  0-22 the new-game introduction (blank: its
    # pictures are the old game's), 23-30 the briefing (Prescott on the comm
    # screen), 31-38 the debriefing (the Captain's cabin), 39-41 the
    # recording, 42-44 radio lines in flight.
    0: [
        "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
        "TCS Culloden, Flight Operations",
        "",
        "Colonel, this is Prescott in Flight Ops.  The Hellcats are fuelled, armed, and they still smell of the factory.",
        "Captain Banbridge wants a full shakedown before we go dark: live fire on the hulks at Nav 1, then a sweep of Nav 2.",
        "The new hand-off computers do the targeting for you now.  Trust them.  That is what they are for.",
        "One more thing.  If you meet anything out there, nothing goes home to report us.  Nothing.",
        "Understood.  A new fighter and a free-fire range.  It's almost my birthday.",
        "Good hunting, \x86.  The Culloden holds station here until you are back aboard.",
        "The Captain wants you in his day cabin.  Now.",
        "Ten minutes later.",
        "Sit down, Colonel.  You flew well.  That is not why you are here.",
        "Sir, the recon pair at Nav 2---",
        "---if either of them jumped, every picket between here and the yards knows a cruiser is loose.",
        "They are not my problem tonight.  Your fighters are.  Listen to this.",
        "Sparks pulled the Hellcats' computers after you landed.  Every one logged an uplink burst during your fight.",
        "An uplink to what?  We are eleven jumps from any Confed transmitter.  Until Sparks knows, nobody flies them.",
        "FLIGHT CONTROL CORE 7 --- UPLINK ACKNOWLEDGED --- KEY ACCEPTED",
        "TARGET HAND-OFF SUSPENDED PENDING REMOTE AUTHORISATION",
        "Have a nice day, \x86.",
        "Culloden to \x86: two recon contacts at Nav 2.  Do not let them jump.",
        "Range control: the hulks are hot.  Weapons free, Colonel.",
        "Flight Ops: your computers are reporting a fault.  Disregard, it is a telemetry glitch.",
    ],
    # Block 1, mission 1-1.  0-12 Kilrathi sector command (the Emperor and
    # Prince Thrakhath), 13 a title card, 14-24 the briefing (the wing
    # commander's face is Angel's until the faces are ours), 25-43 the
    # debriefing on the repair deck with Sparks: 37-39 by kills, 40-41 by
    # whether the Kamekh died.
    1: [
        "Kabla Meth, Kilrathi Sector Command, Deneb Sector.",
        "Report, grandson.  The yards at Ghraffid's station.",
        "I am told a Terran ship was seen two systems from them.  Was this carelessness?",
        "It was not seen by us, my Emperor.  It was told to us.",
        "The Mandarin device the humans carry in their new fighters spoke, as the traitor promised it would.",
        "One cruiser, deep in our space, with six fighters that do not yet know they are ours to switch off.",
        "A cruiser is nothing.  What is it doing eleven jumps from its fleet?",
        "Looking for the yards.  There is no other prize on that road.",
        "I have sent a reaction wing to the place the device marked, and a corvette to hunt it.",
        "The first hull must not be seen before it is finished.  Attend to it.",
        "When the device speaks again, I will know where it sleeps.",
        "I would remind you of one thing, Prince Thrakhath---",
        "A ship that knows it is hunted fights like one.  Kill it before it learns.",
        "The Torgo drift, behind the Kilrathi line.",
        "Briefing Room, TCS Culloden, 0500 hours.",
        " ",
        "The Hellcats stay in the hangar.  Last night their computers took a key from somebody we have not found yet.",
        "Whoever holds it can switch your hand-off and your missile locks off in a fight.  So today you fly Epees.",
        "Stingray and I take the two Morningstars out with direction finders to get a bearing on that uplink.",
        "Keep the bombers alive while they listen.  A corvette is hunting this ship; if we find it, they have the torpedoes.",
        "Valkyrie hands out the rest of the assignments.",
        "\x86, you lead.  Doomsday is your wing, Downtown your third.  Stay on the bombers.",
        "That is all.  Bring them home, and bring the bearing.",
        "Dismissed.",
        "",
        "You brought my bombers back, Colonel.  I was ready to be angry at somebody.",
        "Valkyrie says the finders got a clean bearing.  Straight into the rocks.",
        "Which is where I'd put a relay if I wanted it to listen without being seen.",
        "Tell me about the Hellcats, Sparks.  Straight.",
        "Straight?  The hand-off computer has a maintenance channel that takes orders by radio.  Last night it took a key.",
        "Somebody out there holds a key to my fighter.",
        "Two somebodies, if it is a pair.  One half sits in that relay.  The other half is with whoever installed it.",
        "And if we take the relay?",
        "Then I burn the channel out.  Until then every one of those six has a stranger's hand on its switch.",
        "Who had access to the computers, Sparks?",
        "The programme's liaison, Colonel.  And me.  I know which one of us I'd look at.",
        "So, how was the patrol?",
        "Rough enough.  I took out \x87 of them---",
        "Rough.  I only got one of them---",
        "Bad.  I couldn't touch them---",
        "---and the corvette at Nav 2 is still out there.  We couldn't finish her.",
        "---and the corvette at Nav 2 is scrap.  The Morningstars earned their keep.",
        "It doesn't feel like enough, flying an Epee while six Hellcats sit on the deck.",
        "Find me that relay, Sparks.",
    ],
    # Block 2, mission 1-2.  0-6 Maniac on the flight deck, 7 the tannoy,
    # 8-15 the bridge (Banbridge, Maniac, the player), 16-19 the landing
    # call (18 if the core was lost, 19 if not), 20-29 Valkyrie afterwards
    # (Minx's face until the faces are ours).
    2: [
        "Maniac.  They told me you were in a hospital bed.",
        "They told you right, \x86.  Three weeks in the Mandarins' chair.  Know how long it took anyone to come for me?",
        "Long enough that I learned to like the music they piped in.",
        "So when a cruiser nobody has heard of asked for pilots nobody would miss, I thought: that's my ship.",
        "Then they gave me a Hellcat and took it away again.  Story of my life.",
        "You'll get it back.  We're going into the rocks for the thing that took it.",
        "In Rapiers.  Terrific.  Wake me when the war needs a real pilot.",
        "Colonel \x85 and Major Marshall, report to the bridge.",
        "Major, the bearing puts the relay inside the rockline, nine thousand kilometres deep.  There will be a guard on it.",
        "Then send the Hellcats, Captain.  That is what they are for.",
        "The Hellcats are what the relay is for, Major.  I am not flying them into the thing that owns them.",
        "Sir, give me the Rapiers.  Valkyrie and Downtown.  We go in, kill the guard, and tractor the relay's core out.",
        "Captain, the Colonel is half my squadron.  I should be on this.",
        "You should be on the deck learning patience, Major.  Sparks wants that core intact, Colonel.  The core is the key.",
        "Good hunting, Colonel \x85.",
        "",
        "This is Colonel \x85, requesting permission to land.",
        "Did you get the core, \x86?",
        "Negative, Culloden.  The core is slag.  We have the relay's logs and nothing else.",
        "Affirmative, \x86.  You are cleared to land.",
        "Glad you made it back, sir.",
        " ",
        "Thank you, Colonel \x85.  That was a cleaner run than I have flown in a year.",
        "You kept the Strakha off me, Valkyrie.  I'd call it even.",
        "Sparks has the core open already.  The logs name the man who installed that relay.",
        "Prescott.  Our liaison.  He is in the brig, and he has started talking.",
        "By the way, Colonel: Rhea.  Valkyrie when the helmet is on.",
        "Rhea.  What did Prescott say?",
        "That the relay's keeper left before we came, in a Kamekh, with the other key and our position in his pocket.",
        "Then we are not done.  If that ship makes the jump, the Hellcats stay on the deck for good.",
    ],
    # Block 3, mission 1-3.  0-16 Maniac and the player on the observation
    # deck, 17-27 the wing commander's office (Maniac asks and is refused),
    # 28-30 the deck, 31-38 the Captain if the Vak'thra got away, 39-49 the
    # Captain if she died, 50-58 the radio at the jump point.
    3: [
        "TCS Culloden, Observation Deck, 2130 hours.",
        "---so the Mandarins had this chair, right, with the straps and the screens---",
        "---did I tell you about the chair?",
        "Yes.  Yes, you did.",
        "Point is, I sat in it for three weeks and I still fly better than anyone on this ship.  Including you.",
        "Which is why I should be in the Morningstar tomorrow, not Stingray in a Rapier babysitting you.",
        "You're not flying tomorrow because Banbridge grounded you for the deck brawl, Todd.",
        "Stingray started it.  He said the Hellcats were his if I 'kept losing them'.",
        "I didn't lose them.  A traitor with a radio lost them.",
        "And tomorrow I get them back.  One corvette, one key, one torpedo.",
        "Two torpedoes.  Bring two.  Trust me.",
        "You know, Todd--- people say you don't listen.",
        "I listen.  I listened to three weeks of Mandarin music.",
        "When those Hellcats come back on the line, \x86, the first one is mine.",
        "I am going to fly it the way it was meant to be flown.",
        "The Kilrathi have never seen anything like it.  Neither has Confed.",
        "Just keep watching me, pal.  History in the making.  Again.",
        "Wing Commander's Office, TCS Culloden.",
        "The Vak'thra left the rocks nine hours ago for the Kilrathi side, at corvette speed, with the second key aboard.",
        "If she makes the jump, the Kilrathi learn where we are, and the Hellcats stay down for the rest of the cruise.",
        "So she does not.  You take a Morningstar, \x86.  Two torpedoes, and her Gothri will try to keep you from using them.",
        "Stingray flies your escort in a Rapier.  I fly the second.",
        "Colonel Bergstrom, I request to fly the escort myself.",
        "If anything on this ship gets the Hellcats back, I want to be there when it does.",
        "You are grounded until the Captain says otherwise, Major.  Stingray flies.",
        "\x86: intercept her at Nav 1, kill her before Nav 2.  If she jumps, come home anyway.  We need every ship we have.",
        "Dismissed.",
        "",
        "Welcome back, sir!",
        "That looked like a hard one, sir!",
        "Glad you made it back alive, sir!",
        "The Captain's Day Cabin, TCS Culloden.",
        "She jumped, Colonel \x85.  With the keeper, the key, and our position.",
        "By tomorrow every picket from here to the yards is awake, and the short road is closed to us, charts or no charts.",
        "The Hellcats stay in the hangar: the other key is loose, and the next Kilrathi relay could switch them off on you.",
        "We take the long road, in the ships we have.  That is not a punishment.  It is arithmetic.",
        "Get some sleep.  Tomorrow we are a cruiser with a reserve squadron and a very long way to go.",
        "Sir--- I can still---",
        "You can still fly, Colonel.  That is what I am counting on.",
        "The Captain's Day Cabin, TCS Culloden.",
        "A fine piece of flying.  The Vak'thra is debris, and nobody who was aboard her can tell anyone where we are.",
        "Sparks has burned the channel out of all six computers.  With the core ours and no other key alive, she signs them off.",
        "Which means the Hellcats fly again tomorrow, and the Kilrathi still think they are hunting a rumour.",
        "Prescott stands trial at home.  He asked me to tell you his family is on a Mandarin world.  I said it changed nothing.",
        "It doesn't, sir.  What is next?",
        "Two free traders, the Mandy and the Tsiolkovsky, adrift off the Kilrathi colony ahead with the yards' charts aboard.",
        "Their charts are the one road past the jump chain.  We will need them alive to use it.",
        "First, the corvettes that were escorting the Vak'thra's tender have turned back toward us.  Two Kamekhs.",
        "The Hellcats were built for exactly that.  Go and prove it.",
        "Dismissed, Colonel.  Well done.",
        "Terran fighter, this is the fleet tender Kirrah'ka.  We carry fuel and nothing else.",
        "Our escort has left us.  Our engines are cold.",
        "Do not shoot!  We surrender!",
        "Culloden, I have a Kilrathi tender in my sights at the jump point.  No weapons on her.",
        "She is asking for terms.  Awaiting orders.",
        "\x86, this is Flight Ops.  Your orders are to leave the tender intact---",
        "---the Captain wants her fuel more than he wants her dead.  A prize crew is on the way.",
        "Continue your mission, \x86.",
        "Affirmative, Culloden.  \x86 out.",
    ],
}
