"""SO3 "Culloden" -- the campaign's missions and scene texts, as data for
so3/build.py.  so3/story.md is the story these serve; scripts/wc2data.py
says what the fields are.

Positions are kilometres relative to the nav point; a nav point's position
is relative to the mission's origin.  Ship types are Wing Commander II's
numbers (the Hellcat is ours, 45: the program draws types 48 and 49, its asteroid bases, by other rules)."""

HELLCAT = 45
WATERLOO, SARTHA, DRAKHRI, FREE_TRADER, MORNINGSTAR = 17, 5, 6, 12, 41

# Comm personalities (COMMUNIC.Tnn): who answers on the radio.
MANIAC = 0x24            # "It's party time!"
FLIGHT_OPS = 0x11        # a Confederation ship's bridge: "Affirmative, Captain."
KILRATHI = 0x1A          # "Die, Terran coward!"
KILRATHI_ACE = 0x19      # the Imperial Guard
NOBODY = 0


# The Hellcat's own numbers (the record HCl's conversion carried was the
# Rapier's).  WC2's for comparison -- Rapier / Sabre / Morningstar:
# recharge 6 / 9 / 9, speed 450 / 400 / 400, acceleration 25 / 22 / 22,
# turn 10 / 8 / 8, shields 115 / 80 / 150, armour 75-75-65-65 /
# 200-200-180-180 / 200-200-200-200.
HELLCAT_STATS = dict(
    shield_recharge=5,            # a little slower than either
    max_speed=420,                # kps; the afterburner is the game's 1200
    acceleration=24,
    yaw=9, pitch=9, roll=9,       # a tenth under the Rapier, above the heavies
    shield_front=125, shield_rear=125,
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


TYPE_NAMES = {HELLCAT: "Hellcat", WATERLOO: "Waterloo", SARTHA: "Sartha", DRAKHRI: "Drakhri", FREE_TRADER: "Free Trader", MORNINGSTAR: "Morningstar"}


def mission_1():
    """Shakedown.  The first flight of the Hellcats: live fire on captured
    hulks at the range, a Kilrathi pair that was watching the range, and a
    recon patrol at the second nav point that must not get away."""
    ships = [
        capital("Culloden", WATERLOO, 0, pos=(0, 0, 0), yaw=45),          # 0
        player(HELLCAT, pos=(2200, 0, 0), yaw=35),                         # 1
        wingman("Maniac", HELLCAT, leader=1, pilot=MANIAC),                # 2
        hulk("Target hulk", SARTHA, pos=(0, 0, -2500), yaw=20),            # 3
        hulk("Target hulk", SARTHA, pos=(1800, 300, -4000), yaw=-70),      # 4
        hulk("Target hulk", SARTHA, pos=(-1700, -200, -4200), yaw=140),    # 5
    ]
    ships += wing("Sartha", SARTHA, 2, pos=(7000, 0, -9000), yaw=95, group=1, objective=1, first_index=6)      # 6, 7
    ships += wing("Drakhri", DRAKHRI, 2, pos=(8000, 0, 7000), yaw=-130, group=2, objective=1, pilot=KILRATHI_ACE, first_index=8)  # 8, 9
    navs = [
        # (the player first, the wing next, the carrier last: slots are given in
        # this order and the multiplayer layer seats the second player in slot 1)
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, HELLCAT], ships=[1, 2, 0]),
        dict(name="Range", pos=(35000, 0, 25000), types=[SARTHA], ships=[3, 4, 5, 6, 7]),
        dict(name="Nav 2", pos=(-30000, 0, 60000), types=[DRAKHRI], ships=[8, 9]),
    ]
    objectives = [
        (0, 1, "Proceed to the range at Nav 1"),
        (5, 1, ".the hulks and whoever is watching them"),
        (0, 2, "Sweep Nav 2"),
        (4, 8, "Destroy the recon patrol"),
        (5, 2, ".its wingman too"),
        (1, 0, "Return to the Culloden"),
    ]
    return dict(series=1, mission=0, name="Shakedown", series_name="Culloden",
                header=dict(launch=0, carrier=0, player=1, wing=[2]),
                navs=navs, ships=ships, objectives=objectives)


MISSIONS = [mission_1()]

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
    0: [
        "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
        "TCS Culloden, Flight Operations",
        "",
        "Colonel, Flight Ops.  The Hellcats are fuelled, armed, and they still smell of the factory.",
        "Admiral Tolwyn wants a full shakedown before this ship goes dark: live fire on the hulks we towed to Nav 1, then a sweep through Nav 2.",
        "The new flight computers do the target hand-off for you now.  Trust them.  That is what they are for.",
        "One more thing.  If you meet anything out there, nothing goes home to report us.  Nothing.",
        "Understood.  A new fighter and a free-fire range.  It's almost my birthday.",
        "Good hunting, \x86.  The Culloden holds station here until you are back aboard.",
        "The Admiral wants you in Flight Ops.  Now.",
        "Ten minutes later.",
        "Sit down, Colonel.  You flew well.  That is not why you are here.",
        "Sir, the recon pair at Nav 2---",
        "---if either of them jumped, every picket between here and the yard knows a cruiser is loose.",
        "They are not my problem tonight.  Your fighter is.  Listen to this.",
        "Flight Ops pulled the Hellcats' computers after you landed.  Every one of them logged an uplink burst during your fight.",
        "An uplink to what, Colonel?  We are eleven jumps from the nearest Confederation transmitter.",
        "FLIGHT CONTROL CORE 7 --- UPLINK ACKNOWLEDGED --- KEY ACCEPTED",
        "TARGET HAND-OFF SUSPENDED PENDING REMOTE AUTHORISATION",
        "Have a nice day, \x86.",
        "Culloden to \x86: two recon contacts at Nav 2.  Do not let them jump.",
        "Range control: the hulks are hot.  Weapons free, Colonel.",
        "Flight Ops: your computers are reporting a fault.  Disregard, it is a telemetry glitch.",
    ],
}
