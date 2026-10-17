"""SO3 "Culloden" -- the campaign's missions and scene texts, as data for
so3/build.py.  so3/story.md is the story these serve; scripts/wc2data.py
says what the fields are.

Positions are kilometres relative to the nav point; a nav point's position
is relative to the mission's origin.  Ship types are Wing Commander II's
numbers (the Hellcat is ours, 45: the program draws types 48 and 49, its asteroid bases, by other rules)."""

HELLCAT = 45
WATERLOO, SARTHA, DRAKHRI, FREE_TRADER, MORNINGSTAR, DORKATHI = 17, 5, 6, 12, 41, 13
HULK = 29                # ours: a Dorkathi with its flak turrets stripped (every WC2 transport has them)
EPEE, RAPIER, SABRE, BROADSWORD, GOTHRI, JALKEHI, GRIKATH, STRAKHA, KAMEKH = 3, 1, 4, 2, 50, 7, 8, 9, 16   # Gothri: SO2 flies its own as type 50
FRALTHRA, RALATHA, YARD = 21, 20, 48     # the yard wears the asteroid base's model (SO2's Ayer's Rock)
ASTEROIDS, MINES, POD, PILOT_POD = 51, 52, 53, 54   # not ships: a field, a field, a thing to tractor in (class 5, 6, 61, 44)
# What the nav lists say of the non-ships (no SHIP file to read it from).
SPECIAL_TYPES = {ASTEROIDS: (0, 5), MINES: (0, 6), POD: (0, 61), PILOT_POD: (0, 44)}

# Comm personalities (COMMUNIC.Tnn): who answers on the radio.
MANIAC = 0x24            # "It's party time!" (INTEL.036 flies him)
DOOMSDAY = 0x09          # (INTEL.009)
STINGRAY = 0x04          # (INTEL.004)
BERGSTROM = 0x0B         # Shadow's voice, "Affirmative, Captain", and INTEL.011: a steady wingman
DOWNTOWN = 0x0C          # a Confederation pilot with an INTEL file (0x10 had none: a wingman with it crashed the loader)
FLIGHT_OPS = 0x11        # a Confederation ship's bridge: "Affirmative, Captain."
KILRATHI = 0x1A          # "Die, Terran coward!"
KILRATHI_ACE = 0x19      # the Imperial Guard
KILRATHI_SHIP = 0x1B     # a Kilrathi capital ship's bridge
TORO = 0x0E              # a Confederation pilot with an INTEL file (a wingman needs one: INTEL.0nn by the decimal id)
PALADIN_SHIP = 0x08      # Paladin's voice from a ship's bridge (SO2's Gamal Gan)
MANDARIN = 0x25          # the Mandarins' pilots
PRESCOTT = 0x06          # the traitor's taunts (SO2 gave them to Jazz)
TRADER, TRADER2 = 0x12, 0x13   # freighter masters
CARRIER, BRIDGE = 0x0F, 0x10   # a carrier's bridge voices
NOBODY = 0

# ALTSHIPS.003 records (copies of SO2's): alternative records some mission ships wear.
ALT_MANDARIN_SABRE, ALT_MANDARIN_SABRE2, ALT_MANDARIN_BROADSWORD = 13, 14, 16
ALT_TENDER, ALT_FRIENDLY_TENDER = 17, 18   # a disabled transport; a transport of our side you can land on


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
    # The user's second ruling (2026-10-17): tankier than anything of its
    # size despite the weak shields -- the first numbers (120/120/90/90, the
    # Rapier's core of 135) were outmatched by the Gothri.  Armour above the
    # Sabre's 200/200/180/180 and a core above the Broadsword's 300.
    armor_front=250, armor_rear=250, armor_left=220, armor_right=220,
    core=400,
)
# Guns as (weapon, x, y, z, armed): mirrored pairs close to the nose (the
# Rapier's nose lasers sit at x = +-24, y = 36, z = 30; its wing cannons out
# at |x| 104-152).  armed 0 = part of the gun group selected at launch: the
# particle cannons, as the Rapier launches with its lasers; six missiles;
# one decoy.
HELLCAT_GUNS = [("particle", 16, 36, 30, 0), ("particle", -16, 36, 30, 0),
                ("neutron", 40, 32, 30, 1), ("neutron", -40, 32, 30, 1)]
HELLCAT_MISSILES = ["spiculum_ir"] * 6
HELLCAT_DECOYS = 1


def ship(name, shiptype, side, **kw):
    """A mission ship as keyword arguments for wc2data.ship_record; the
    type's kind and class are filled in by the builder."""
    d = dict(name=name, shiptype=shiptype, side=side)
    d.update(kw)
    return d


# Wing ships take Special Operations 2's own launch positions: a ship left
# at the nav point's origin sits inside the carrier and explodes at launch.
def player(shiptype, pos=(3000, 0, 0), yaw=35, behave=0, objective=-1):
    return ship(TYPE_NAMES[shiptype], shiptype, 0, pos=pos, yaw=yaw, behave=behave, radius=20, b33=4, ai=6, group=0, objective=objective)


def wingman(name, shiptype, pilot, leader=1, pos=(1500, 0, 0), formation=1, ai=5):
    """The player's wingman (orders 5: the second player's seat)."""
    return ship(name, shiptype, 0, pos=pos, yaw=35, behave=4, important=1, formation=formation, b33=4, ai=ai, leader=leader, group=0, objective=leader, pilot=pilot)


def third(name, shiptype, pilot, leader=1, pos=(1000, 0, 1000), formation=2):
    """An AI pilot of the player's wing, behind the wingman."""
    return ship(name, shiptype, 0, pos=pos, yaw=35, behave=4, important=1, formation=formation, b33=4, ai=4, leader=leader, group=0, objective=leader, pilot=pilot)


def fourth(name, shiptype, pilot, leader=1):
    return third(name, shiptype, pilot, leader, pos=(2000, 0, 2000), formation=3)


def capital(name, shiptype, side, pos, yaw=0, pilot=FLIGHT_OPS, important=1, ai=4, alt=0, behave=-1, leader=-1, objective=-1):
    return ship(name, shiptype, side, pos=pos, yaw=yaw, behave=behave, important=important, b33=4, ai=ai, pilot=pilot, alt=alt, leader=leader, objective=objective)


def escortee(name, shiptype, pos, pilot, leader=1, alt=0, important=0):
    """A ship of ours that follows the player (behaviour 6, as SO2's Bastille).
    Not "important": the loss of a ship the slot's campaign script does not
    know sends it down a path with no scene (SERIES packet 30), and the
    program ends; SO2's own escorted freighters are not marked either."""
    # formation 4: slots 1-3 are the wing's, and a ship given the wingman's
    # slot rams the second player's ship all the way to the first nav point
    return ship(name, shiptype, 0, pos=pos, yaw=35, behave=6, important=important, formation=4, radius=15, b33=4, ai=4, leader=leader, group=0, objective=leader, pilot=pilot, alt=alt)


def joiner(name, shiptype, pos, pilot, leader=1, important=0):
    """A ship of ours waiting at a nav point that forms up on the player (SO2's Iguana)."""
    return ship(name, shiptype, 0, pos=pos, behave=4, important=important, formation=5, radius=15, b33=4, ai=4, leader=leader, group=0, objective=leader, pilot=pilot)


def friend(name, shiptype, pos, yaw, pilot, important=0, ai=4):
    """A fighter of ours already in a fight at a nav point."""
    return ship(name, shiptype, 0, pos=pos, yaw=yaw, behave=2, important=important, radius=20, b33=4, ai=ai, group=0, pilot=pilot)


def lone(name, shiptype, pos, yaw, objective=1, pilot=KILRATHI_ACE, ai=4, alt=0, side=1):
    """A single enemy placed where it stands."""
    return ship(name, shiptype, side, pos=pos, yaw=yaw, behave=2, radius=20, b33=4, ai=ai, group=-1, objective=objective, pilot=pilot, alt=alt)


def field(kind, pos, radius):
    """An asteroid (ASTEROIDS) or mine (MINES) field of about radius km."""
    return ship("asteroids" if kind == ASTEROIDS else "mines", kind, 2, pos=pos, behave=-1, important=1, radius=max(0, radius - 3000), b33=3, ai=2)


def pod(name, pos, kind=POD, side=2):
    """Something to tractor in: an objective of kind 6 names it (SO2's pods are side 2)."""
    return ship(name, kind, side, pos=pos, behave=-1, important=1, b33=3, ai=3)


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


def wing(name, shiptype, count, pos, yaw, group, objective, pilot=KILRATHI, ai=4, side=1, first_index=None, alt=0):
    """A leader placed at pos and count-1 followers in formation.  The
    followers need the leader's mission ship index: pass first_index."""
    out = [ship(name, shiptype, side, pos=pos, yaw=yaw, behave=2, radius=20, b33=4, ai=ai, group=group, objective=objective, pilot=pilot, alt=alt)]
    for i in range(1, count):
        out.append(ship(name, shiptype, side, behave=4, formation=i, b33=4, ai=ai, leader=first_index, group=group, objective=first_index, pilot=pilot, alt=alt))
    return out


TYPE_NAMES = {HELLCAT: "Hellcat", WATERLOO: "Waterloo", SARTHA: "Sartha", DRAKHRI: "Drakhri", FREE_TRADER: "Free Trader", MORNINGSTAR: "Morningstar", DORKATHI: "Dorkathi", HULK: "Hulk",
              EPEE: "Epee", RAPIER: "Rapier", SABRE: "Sabre", BROADSWORD: "Broadsword", GOTHRI: "Gothri", JALKEHI: "Jalkehi", GRIKATH: "Grikath", STRAKHA: "Strakha", KAMEKH: "Kamekh",
              FRALTHRA: "Fralthra", RALATHA: "Ralatha", YARD: "Yard"}


def mission(series, m, name, ships, navs, objectives, wing_, launch=0, carrier=0, series_name=None):
    return dict(series=series, mission=m, name=name, series_name=series_name,
                header=dict(launch=launch, carrier=carrier, player=1, wing=wing_), navs=navs, ships=ships, objectives=objectives)


CULLODEN = lambda **kw: capital("Culloden", WATERLOO, 0, pos=(0, 0, 0), yaw=45, **kw)   # noqa: E731



# ---------------------------------------------------------------------------
# The missions.  Each takes the shape of Special Operations 2's mission in the
# same slot -- its objective list, the mission ship indices its scenes test,
# its nav point positions, its landing ship -- because the campaign script and
# the scene scripts are SO2's until the CSCP bytecode is ours (see
# docs/newcampaign.md).  Memory: one new fighter type per nav point beside the
# player's; at the launch nav the carrier, the player's type and at most one
# light type more.

def m_shakedown():
    """1-0 Shakedown (Hellcats; Maniac).  Live fire on the hulks at the range;
    the reaction force the relay's ping called in arrives there once the hulks
    are dead; the Drakhri recon pair at Nav 2 must not get home."""
    ships = [
        CULLODEN(),                                                               # 0
        player(HELLCAT),                                                          # 1
        wingman("Maniac", HELLCAT, MANIAC),                                       # 2
        hulk("Target hulk", HULK, pos=(0, 0, -2500), yaw=20),                     # 3
        hulk("Target hulk", HULK, pos=(1800, 300, -4000), yaw=-70),               # 4
        hulk("Target hulk", HULK, pos=(-1700, -200, -4200), yaw=140),             # 5
    ]
    ships += wing("Gothri", GOTHRI, 4, pos=(9000, 0, -12000), yaw=170, group=1, objective=1, pilot=KILRATHI_ACE, first_index=6)    # 6-9
    ships += wing("Drakhri", DRAKHRI, 3, pos=(8000, 0, 7000), yaw=-130, group=2, objective=1, pilot=KILRATHI_ACE, first_index=10)  # 10-12
    range_ = dict(name="Range", pos=(30000, 0, 40000), types=[HULK, GOTHRI], ships=[3, 4, 5])
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, HELLCAT], ships=[1, 2, 0]),
        range_,
        wave(range_, types=[GOTHRI], ships=[6, 7, 8, 9]),     # (its type is listed on the range too: loaded before the fight fragments the heap)
        dict(name="Nav 2", pos=(70000, 0, 85000), types=[DRAKHRI], ships=[10, 11, 12]),
    ]
    objectives = [(0, 1, "Live fire at the range, Nav 1"), (0, 3, "Sweep Nav 2"), (5, 3, ".the recon patrol: nothing goes home"), (1, 0, "Return to the Culloden"), (0, 0, "")]
    return mission(1, 0, "Shakedown", ships, navs, objectives, [2], series_name="Culloden")


def m_handmedowns():
    """1-1 Hand-me-downs (Epees; Doomsday, Downtown).  The Hellcats grounded,
    the reserve squadron escorts Sparks' direction-finder barge, Stingray
    driving, to take the bearing on the uplink.  A corvette hunting the
    Culloden waits at Nav 2 (ship 10: the debriefing asks after it)."""
    ships = [
        CULLODEN(),                                                               # 0
        player(EPEE),                                                             # 1
        wingman("Doomsday", EPEE, DOOMSDAY),                                      # 2
        third("Downtown", EPEE, DOWNTOWN),                                        # 3
        escortee("Barge", FREE_TRADER, pos=(1500, -300, -1500), pilot=STINGRAY),  # 4  the finder barge
        lone("Sartha", SARTHA, pos=(4000, 0, -6000), yaw=150, objective=4),       # 5  a scout at Nav 1 (not the Drakhri of 1-0's last nav: a wingman's program died on a type repeated across the mission boundary)
    ]
    ships += wing("Grikath", GRIKATH, 4, pos=(5000, 400, -8000), yaw=160, group=1, objective=4, first_index=6)   # 6-9  go for the barge
    ships += [capital("Kur'ratha", KAMEKH, 1, pos=(2500, 0, -9000), yaw=170, pilot=KILRATHI_SHIP, important=0, behave=2, objective=4, ai=3)]  # 10
    ships += [field(ASTEROIDS, pos=(0, 0, 0), radius=8000)]                                                         # 11
    ships += wing("Drakhri", DRAKHRI, 4, pos=(3000, 0, -7000), yaw=120, group=2, objective=1, first_index=12)     # 12-15
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, EPEE, FREE_TRADER], ships=[1, 2, 3, 4, 0]),
        dict(name="Nav 2", pos=(35000, 0, 105000), types=[GRIKATH, KAMEKH], ships=[6, 7, 8, 9, 10]),
        dict(name="Nav 3", pos=(-70000, 0, 110000), types=[], ships=[]),
        dict(name="Rockline", pos=(-35000, 0, 60000), types=[ASTEROIDS, DRAKHRI], ships=[11, 12, 13, 14, 15]),
        dict(name="Nav 1", pos=(45000, 0, 45000), types=[SARTHA], ships=[5]),
    ]
    objectives = [(0, 4, "Escort the barge to Nav 1"), (5, 4, ".the scout"), (0, 1, "On to Nav 2 for the bearing"), (5, 1, ".the corvette"),
                  (0, 2, "Nav 3"), (0, 3, "The rockline"), (5, 3, ".the ambush"), (1, 0, "Return to the Culloden")]
    return mission(1, 1, "Hand-me-downs", ships, navs, objectives, [2, 3, 4])


def m_relay():
    """1-2 The relay (Sabres; Stingray).  Toro, pinned at the rockline by the
    Grikath that chased her off the relay; the relay itself, the corvette
    Vak'thra with her Drakhri in the heart of the rocks.  When she dies her
    computer core (ship 13) tumbles free: the Sabre's tractor beam brings it
    home.  (SO2's slot: Sabre, Stingray, a pickup at 13.)"""
    ships = [
        CULLODEN(),                                                               # 0
        player(SABRE, behave=2, objective=3),                                     # 1
        wingman("Stingray", SABRE, STINGRAY),                                     # 2
    ]
    ships += wing("Grikath", GRIKATH, 4, pos=(4000, 500, -6000), yaw=140, group=1, objective=14, first_index=3)   # 3-6  on Toro
    ships += [field(ASTEROIDS, pos=(0, 0, 0), radius=9000)]                                                        # 7
    ships += [capital("Vak'thra", KAMEKH, 1, pos=(0, 0, -5000), yaw=10, pilot=KILRATHI_SHIP, important=0, behave=2, objective=1, ai=3)]  # 8
    ships += wing("Drakhri", DRAKHRI, 4, pos=(2500, 0, -7000), yaw=160, group=2, objective=1, first_index=9)      # 9-12
    ships += [pod("Data core", pos=(400, 200, -4800))]                                                             # 13
    ships += [friend("Toro", SABRE, pos=(2000, 0, -4000), yaw=-20, pilot=TORO)]                                    # 14
    relay = dict(name="Relay", pos=(-45000, 0, 100000), types=[KAMEKH, DRAKHRI, POD], ships=[8, 9, 10, 11, 12], reveals=[(0, 2)])
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, SABRE], ships=[1, 2, 0]),
        dict(name="Rockline", pos=(-20000, 0, 60000), types=[GRIKATH, ASTEROIDS, SABRE], ships=[14, 3, 4, 5, 6, 7]),
        relay,
        wave(relay, types=[POD], ships=[13]),     # the core, once the fight at the relay is over
    ]
    objectives = [(0, 1, "The rockline: Toro is in trouble"), (5, 1, ".the Grikath"), (0, 2, "Find the relay"), (6, 13, "Recover the Vak'thra's core"), (1, 0, "Return to the Culloden")]
    return mission(1, 2, "The relay", ships, navs, objectives, [2, 14])


def m_prize():
    """1-3 Prize (Hellcats; Maniac).  The Hellcats cleared: their first flight,
    a sweep.  At Nav 2 the Kilrathi fleet tender Kirrah'ka, abandoned by her
    escort, strikes her colours (SO2's hail scene 211); her escort comes back
    for her.  Shoot her and the Captain has words (SO2's slot tests ship 3).
    The Culloden moves during the flight: the landing is ship 11."""
    ships = [
        CULLODEN(),                                                               # 0
        player(HELLCAT),                                                          # 1  (not SO2's escort behaviour 1: with no escortee the autopilot never engages again)
        wingman("Maniac", HELLCAT, MANIAC),                                       # 2
        # side 0: she has struck her colours.  A Kilrathi-side ship that must stay alive keeps the autopilot
        # locked until the player has flown far from her; a Confederation-side one does not, and shooting
        # her still counts (ship 3 gone: the court-martial debriefing)
        capital("Kirrah'ka", DORKATHI, 0, pos=(2500, 0, -2500), yaw=0, pilot=KILRATHI_SHIP, important=0, ai=0, alt=ALT_TENDER),  # 3  surrenders
    ]
    ships += wing("Sartha", SARTHA, 4, pos=(3000, 0, -8000), yaw=170, group=0, objective=1, ai=3, first_index=4)       # 4-7  her escort, back for her
    ships += wing("Jalkehi", JALKEHI, 3, pos=(2000, 0, -7000), yaw=160, group=0, objective=1, ai=3, first_index=8)     # 8-10
    ships += [CULLODEN(ai=2)]                                                                                            # 11  the landing
    ships += [ship("Jalkehi", JALKEHI, 1, behave=4, formation=3, b33=4, ai=3, leader=8, group=0, objective=8, pilot=KILRATHI),
              ship("Jalkehi", JALKEHI, 1, behave=4, formation=4, b33=4, ai=3, leader=8, group=0, objective=8, pilot=KILRATHI)]   # 12, 13
    ships += wing("Sartha", SARTHA, 2, pos=(2500, 0, -5000), yaw=150, group=1, objective=1, first_index=14)            # 14, 15  the picket at Nav 1 (Sartha: 1-2 ends among Drakhri)
    nav2 = dict(name="Nav 2", pos=(15000, 0, 100000), types=[DORKATHI, SARTHA], ships=[3], steps=[(1, 211, 4)])
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, HELLCAT], ships=[1, 2, 0]),
        dict(name="Nav 1", pos=(60000, 0, 40000), types=[SARTHA], ships=[14, 15], reveals=[(0, 0), (1, 5)]),
        nav2,
        wave(nav2, types=[SARTHA], ships=[4, 5, 6, 7]),
        dict(name="Nav 3", pos=(-20000, 0, 70000), types=[JALKEHI], ships=[8, 9, 10, 12, 13]),
        dict(name="Culloden", pos=(0, 0, 0), visible=0, types=[WATERLOO], ships=[11]),
    ]
    objectives = [(0, 1, "Sweep Nav 1"), (5, 1, ".the picket"), (0, 2, "Nav 2"), (0, 4, "Home by way of Nav 3"), (5, 4, ".the Jalkehi"),
                  (1, 11, "Return to the Culloden"), (0, 0, "Return to the Culloden")]
    return mission(1, 3, "Prize", ships, navs, objectives, [2], carrier=11)


def m_first_patrol():
    """2-0 First patrol (four Hellcats: Maniac, Doomsday, Downtown).  Nav 1
    empty, four Strakha at Nav 2, a lone Gothri ace at Nav 3 who may get away,
    four Jalkehi at Nav 4 (the debriefing reports each)."""
    ships = [
        CULLODEN(),                                                               # 0
        player(HELLCAT),                                                          # 1
        wingman("Maniac", HELLCAT, MANIAC),                                       # 2
        third("Doomsday", HELLCAT, DOOMSDAY),                                     # 3
        fourth("Downtown", HELLCAT, DOWNTOWN),                                    # 4
    ]
    ships += wing("Strakha", STRAKHA, 4, pos=(4000, 0, -7000), yaw=150, group=1, objective=1, ai=3, first_index=5)   # 5-8
    ships += [lone("Gothri", GOTHRI, pos=(3000, 500, -8000), yaw=170, objective=1, pilot=KILRATHI_ACE)]                # 9
    ships += wing("Jalkehi", JALKEHI, 4, pos=(5000, 0, -8000), yaw=160, group=1, objective=1, ai=3, first_index=10)  # 10-13
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, HELLCAT], ships=[1, 2, 3, 4, 0]),
        dict(name="Nav 1", pos=(55000, 0, 40000), types=[], ships=[]),
        dict(name="Nav 2", pos=(100000, 0, 70000), types=[STRAKHA], ships=[5, 6, 7, 8]),
        dict(name="Nav 3", pos=(-5000, 0, 90000), types=[GOTHRI], ships=[9]),
        dict(name="Nav 4", pos=(-40000, 0, 40000), types=[JALKEHI], ships=[10, 11, 12, 13]),
    ]
    objectives = [(0, 1, "Nav 1"), (0, 2, "Nav 2"), (5, 2, "."), (0, 3, "Nav 3"), (5, 3, "."), (0, 4, "Nav 4"), (5, 4, "."), (1, 0, "Return to the Culloden")]
    return mission(2, 0, "First patrol", ships, navs, objectives, [2, 3, 4], series_name="The road")


def m_scramble():
    """2-1 Scramble (Sabres, the alert fighters; Stingray).  Six Gothri and
    a corvette come for the Culloden out of the rocks."""
    ships = [
        CULLODEN(important=0, pilot=BRIDGE),                                      # 0
        player(SABRE, behave=2, objective=3),                                     # 1
        wingman("Stingray", SABRE, STINGRAY),                                     # 2
    ]
    ships += wing("Gothri", GOTHRI, 6, pos=(-9000, 0, 9000), yaw=-135, group=1, objective=1, ai=2, first_index=3)   # 3-8
    ships += [capital("Kamekh", KAMEKH, 1, pos=(-12000, 0, 12000), yaw=-135, pilot=KILRATHI_ACE, important=0, behave=2, objective=0, ai=3)]   # 9
    ships += [capital("Kamekh", KAMEKH, 1, pos=(-14000, 0, 14000), yaw=-135, pilot=KILRATHI_ACE, important=0, behave=7, ai=3)]               # 10 (SO2 keeps a second in reserve)
    ships += [field(ASTEROIDS, pos=(0, 0, 0), radius=9000), field(ASTEROIDS, pos=(6000, 0, 2000), radius=8000), field(ASTEROIDS, pos=(-5000, 0, -3000), radius=8000)]  # 11-13
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, GOTHRI, KAMEKH], ships=[1, 2, 3, 4, 5, 6, 7, 8, 9, 0]),
        dict(name="Asteroids", pos=(-10000, 0, 40000), types=[ASTEROIDS], ships=[11, 12, 13]),
    ]
    objectives = [(5, 0, "Defend the Culloden"), (1, 0, ".land on the Culloden"), (0, 1, ".")]
    return mission(2, 1, "Scramble", ships, navs, objectives, [2])


def m_beacon():
    """2-2 Beacon (Sabres; Toro).  A Confederation distress beacon in the
    mines at Nav 3, with Sartha waiting beside it; it is a bomb, and the
    Culloden's warning (scene 212) arrives when you are on top of it."""
    ships = [
        CULLODEN(pilot=BRIDGE),                                                   # 0
        player(SABRE),                                                            # 1
        wingman("Toro", SABRE, TORO),                                             # 2
        field(MINES, pos=(0, 0, 0), radius=9000),                                 # 3
        pod("Beacon", pos=(1500, 0, -3000)),                                      # 4
    ]
    ships += wing("Sartha", SARTHA, 2, pos=(3000, 0, -5000), yaw=150, group=2, objective=2, ai=3, first_index=5)      # 5, 6
    ships += wing("Sartha", SARTHA, 2, pos=(-3000, 0, -5000), yaw=-150, group=1, objective=5, ai=3, first_index=7)    # 7, 8
    ships += wing("Jalkehi", JALKEHI, 3, pos=(4000, 0, -7000), yaw=160, group=2, objective=1, ai=3, first_index=9)   # 9-11
    ships += [field(ASTEROIDS, pos=(0, 0, 0), radius=9000)]                                                           # 12
    ships += wing("Drakhri", DRAKHRI, 3, pos=(3000, 0, -6000), yaw=150, group=3, objective=1, first_index=13)        # 13-15
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, SABRE], ships=[1, 2, 0]),
        dict(name="Nav 3", pos=(-60000, 0, 50000), types=[SARTHA, MINES, POD], ships=[3, 4, 5, 6, 7, 8], steps=[(1, 212, 4), (1, 213, 1)]),
        dict(name="Nav 2", pos=(-35000, 0, 110000), types=[JALKEHI], ships=[9, 10, 11]),
        dict(name="Nav 1", pos=(30000, 0, 45000), types=[DRAKHRI, ASTEROIDS], ships=[12, 13, 14, 15]),
        dict(name="message", pos=(0, 0, 0), visible=0, types=[], ships=[], steps=[(1, 212, 0)]),
    ]
    objectives = [(0, 3, "Nav 1"), (0, 2, "Nav 2"), (5, 2, "."), (0, 1, "Nav 3: the beacon"), (5, 1, "."), (1, 0, "Return to the Culloden")]
    return mission(2, 2, "Beacon", ships, navs, objectives, [2])


def m_valkyrie():
    """2-3 Valkyrie (Sabres; Maniac).  Toro came home alone on Valkyrie's
    order; Valkyrie is still out there at Nav 1 with five Gothri and a
    corvette (ship 10, the objective).  Bring her home."""
    ships = [
        CULLODEN(),                                                               # 0
        player(SABRE, behave=2, objective=3),                                     # 1
        wingman("Maniac", SABRE, MANIAC),                                         # 2
    ]
    ships += wing("Gothri", GOTHRI, 5, pos=(4000, 0, -7000), yaw=160, group=1, objective=11, first_index=3)   # 3-7
    ships += [field(ASTEROIDS, pos=(0, 0, 0), radius=9000), field(ASTEROIDS, pos=(5000, 0, 4000), radius=8000)]   # 8, 9
    ships += [capital("Kamekh", KAMEKH, 1, pos=(6000, 0, -9000), yaw=170, pilot=KILRATHI_SHIP, important=0, behave=2, objective=11, ai=4)]  # 10
    ships += [friend("Valkyrie", SABRE, pos=(1000, 0, -3000), yaw=20, pilot=BERGSTROM, important=1)]                                          # 11 (marked, as SO2's Angel)
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, SABRE], ships=[1, 2, 0]),
        dict(name="Nav 1", pos=(30000, 0, 100000), types=[SABRE, GOTHRI, KAMEKH], ships=[11, 3, 4, 5, 6, 7, 10]),
        dict(name="Asteroids", pos=(-25000, 0, 55000), types=[ASTEROIDS], ships=[8, 9]),
    ]
    objectives = [(0, 1, "Nav 1: Valkyrie"), (4, 10, ".the corvette"), (1, 0, "Return to the Culloden"), (0, 2, ".not really")]
    return mission(2, 3, "Valkyrie", ships, navs, objectives, [2])


def m_traders():
    """3-0 The traders (Sabres; Maniac).  The Culloden's barge carries the
    traders' price to the rendezvous where the Tsiolkovsky (ship 3) waits
    with the yard's approach charts; home by way of Nav 2, where a Ralatha
    and five Drakhri lie for the unarmed ships."""
    ships = [
        CULLODEN(),                                                               # 0
        player(SABRE, behave=8, objective=3),                                     # 1
        escortee("Barge", FREE_TRADER, pos=(1000, 0, 1000), pilot=TRADER),        # 2
        joiner("Tsiolkovsky", FREE_TRADER, pos=(2000, 0, -3000), pilot=TRADER2),  # 3
    ]
    ships += wing("Drakhri", DRAKHRI, 5, pos=(4000, 0, -7000), yaw=160, group=1, objective=3, ai=1, first_index=4)    # 4-8
    ships += [capital("Ralatha", RALATHA, 1, pos=(3000, 0, -12000), yaw=170, pilot=KILRATHI_SHIP, important=0, behave=2, objective=1, ai=0)]  # 9
    ships += [field(ASTEROIDS, pos=(0, 0, 0), radius=9000), field(ASTEROIDS, pos=(6000, 0, 3000), radius=8000), field(MINES, pos=(-5000, 0, -4000), radius=8000)]  # 10-12
    ships += [wingman("Maniac", SABRE, MANIAC)]                                                                        # 13
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, SABRE, FREE_TRADER], ships=[1, 13, 2, 0]),
        dict(name="Rendezvous", pos=(30000, 0, 100000), types=[FREE_TRADER], ships=[3]),
        dict(name="Nav 2", pos=(50000, 0, 50000), types=[DRAKHRI, RALATHA, FREE_TRADER], ships=[4, 5, 6, 7, 8, 9]),
        dict(name="Deathpoint", pos=(-30000, 0, 75000), types=[ASTEROIDS, MINES], ships=[10, 11, 12]),
    ]
    objectives = [(0, 1, "The rendezvous"), (3, 3, "Meet the Tsiolkovsky"), (0, 2, "Home by way of Nav 2"), (5, 2, "."), (1, 0, "Return to the Culloden"), (0, 3, ".")]
    return mission(3, 0, "The traders", ships, navs, objectives, [13, 2, 3], series_name="The long road")


def m_theft():
    """3-1 Theft (Morningstars; Maniac).  Prescott has taken a Hellcat off
    the deck and run for a Kilrathi rendezvous: mines at Nav 1, Sartha, two
    Jalkehi, four Gothri, and at Nav 5 the tenders he was running to."""
    ships = [
        CULLODEN(pilot=FLIGHT_OPS),                                               # 0
        player(MORNINGSTAR),                                                      # 1
        wingman("Maniac", MORNINGSTAR, MANIAC),                                   # 2
        field(MINES, pos=(0, 0, 0), radius=9000),                                 # 3
    ]
    ships += wing("Sartha", SARTHA, 3, pos=(3000, 0, -6000), yaw=150, group=2, objective=1, ai=3, first_index=4)     # 4-6
    ships += wing("Jalkehi", JALKEHI, 2, pos=(3000, 0, -7000), yaw=160, group=0, objective=1, pilot=KILRATHI_ACE, first_index=7)   # 7, 8
    ships += wing("Gothri", GOTHRI, 4, pos=(4000, 0, -8000), yaw=160, group=1, objective=1, ai=3, first_index=9)     # 9-12
    ships += [capital("Dorkathi", DORKATHI, 1, pos=(0, 0, -6000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=2, ai=2, objective=1),
              capital("Dorkathi", DORKATHI, 1, pos=(2500, 0, -7500), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=4, ai=3, leader=13, objective=13),
              capital("Kamekh", KAMEKH, 1, pos=(-2500, 0, -8000), yaw=10, pilot=KILRATHI_SHIP, important=0, behave=2, ai=4, objective=1)]   # 13-15
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[MORNINGSTAR, WATERLOO], ships=[1, 2, 0]),
        dict(name="Nav 1", pos=(60000, 0, 20000), types=[MINES], ships=[3]),
        dict(name="Nav 2", pos=(80000, 0, 70000), types=[SARTHA], ships=[4, 5, 6]),
        dict(name="Nav 3", pos=(30000, 0, 100000), types=[JALKEHI], ships=[7, 8]),
        dict(name="Nav 4", pos=(10000, 0, 60000), types=[GOTHRI], ships=[9, 10, 11, 12]),
        dict(name="Nav 5", pos=(-40000, 0, 110000), types=[DORKATHI, KAMEKH], ships=[13, 14, 15]),
    ]
    objectives = [(0, 1, "Nav 1"), (0, 2, "Nav 2"), (0, 3, "Nav 3"), (0, 4, "Nav 4"), (5, 4, "."), (0, 5, "Nav 5: his rendezvous"), (5, 5, "."), (1, 0, "Return to the Culloden")]
    return mission(3, 1, "Theft", ships, navs, objectives, [2])


def m_rendezvous():
    """3-2 Rendezvous (Morningstars; Maniac).  To the Kirrah'ka, the prize
    tender Paladin has made a Trojan horse of (ship 9, where you land):
    four transports and two Gothri at Nav 1, Drakhri in the rocks at Nav 2."""
    ships = [
        CULLODEN(pilot=FLIGHT_OPS),                                               # 0
        player(MORNINGSTAR, behave=8, objective=9),                               # 1
        wingman("Maniac", MORNINGSTAR, MANIAC),                                   # 2
        capital("Dorkathi", DORKATHI, 1, pos=(0, 0, -6000), yaw=0, pilot=KILRATHI_SHIP, important=0, ai=2),                               # 3
        capital("Dorkathi", DORKATHI, 1, pos=(3000, 0, -7000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=4, ai=2, leader=3, objective=3),   # 4
        capital("Dorkathi", DORKATHI, 1, pos=(-3000, 0, -7000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=4, ai=2, leader=3, objective=3),  # 5
        capital("Dorkathi", DORKATHI, 1, pos=(0, 0, -9000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=4, ai=2, leader=3, objective=3),      # 6
    ]
    ships += wing("Gothri", GOTHRI, 2, pos=(2000, 0, -5000), yaw=170, group=0, objective=1, ai=3, first_index=7)      # 7, 8
    ships += [capital("Kirrah'ka", DORKATHI, 0, pos=(0, 0, -2000), yaw=0, pilot=PALADIN_SHIP, important=1, alt=ALT_FRIENDLY_TENDER)]   # 9
    ships += [field(ASTEROIDS, pos=(0, 0, 0), radius=9000)]                                                            # 10
    ships += wing("Drakhri", DRAKHRI, 5, pos=(3000, 0, -6000), yaw=150, group=2, objective=1, ai=2, first_index=11)   # 11-15
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[MORNINGSTAR, WATERLOO], ships=[1, 2, 0]),
        dict(name="Nav 1", pos=(-25000, 0, 60000), types=[DORKATHI, GOTHRI], ships=[3, 4, 5, 6, 7, 8]),
        dict(name="Nav 2", pos=(5000, 0, 85000), types=[DRAKHRI, ASTEROIDS, MORNINGSTAR], ships=[10, 11, 12, 13, 14, 15]),
        dict(name="Kirrah'ka", pos=(45000, 0, 110000), types=[MORNINGSTAR, DORKATHI], ships=[9], steps=[(1, 117, 0)]),
    ]
    objectives = [(0, 1, "Nav 1"), (5, 1, "."), (0, 2, "Nav 2"), (5, 2, "."), (0, 3, "Nav 3: the Kirrah'ka"), (3, 9, "Rendezvous with Paladin"), (0, 0, "")]
    return mission(3, 2, "Rendezvous", ships, navs, objectives, [2], carrier=9)


def m_jump_point():
    """3-3 Jump point (Morningstars; Maniac; from the Kirrah'ka, which follows
    you).  Strakha in the mines, four Jalkehi, two Gothri aces, and the jump."""
    ships = [
        capital("Kirrah'ka", DORKATHI, 0, pos=(0, 0, 0), yaw=0, pilot=PALADIN_SHIP, important=1, behave=4, leader=1, objective=1, alt=ALT_FRIENDLY_TENDER),   # 0
        player(MORNINGSTAR, behave=2, objective=3),                               # 1
        wingman("Maniac", MORNINGSTAR, MANIAC),                                   # 2
    ]
    ships += wing("Strakha", STRAKHA, 2, pos=(3000, 0, -6000), yaw=160, group=0, objective=1, ai=3, first_index=3)   # 3, 4
    ships += [field(MINES, pos=(0, 0, 0), radius=8000)]                                                                # 5
    ships += wing("Jalkehi", JALKEHI, 4, pos=(4000, 0, -7000), yaw=160, group=1, objective=1, ai=3, first_index=6)   # 6-9
    ships += wing("Gothri", GOTHRI, 2, pos=(3000, 0, -7000), yaw=170, group=0, objective=1, ai=5, pilot=KILRATHI_ACE, first_index=10)   # 10, 11
    ships += [field(ASTEROIDS, pos=(0, 0, 0), radius=9000), field(ASTEROIDS, pos=(0, 0, 0), radius=9000), field(ASTEROIDS, pos=(0, 0, 0), radius=9000)]   # 12-14
    ships += [capital("Kirrah'ka", DORKATHI, 0, pos=(0, 0, 0), yaw=0, pilot=PALADIN_SHIP, important=1, alt=ALT_FRIENDLY_TENDER)]    # 15
    navs = [
        dict(name="Kirrah'ka", pos=(0, 0, 0), types=[DORKATHI, MORNINGSTAR], ships=[1, 2, 0]),
        dict(name="Nav 1", pos=(15000, 0, 35000), types=[STRAKHA, MINES, DORKATHI], ships=[3, 4, 5]),
        dict(name="Nav 2", pos=(35000, 0, 75000), types=[JALKEHI, DORKATHI], ships=[6, 7, 8, 9]),
        dict(name="Nav 3", pos=(50000, 0, 110000), types=[GOTHRI, DORKATHI], ships=[10, 11], steps=[(1, 117, 3)]),
        dict(name="Nav 4", pos=(5000, 0, 110000), types=[ASTEROIDS], ships=[12]),
        dict(name="Nav 5", pos=(-20000, 0, 45000), types=[ASTEROIDS], ships=[14]),
        dict(name="Jump Point", pos=(0, 0, 0), types=[DORKATHI, MORNINGSTAR], ships=[]),
        dict(name="asteroids", pos=(-50000, 0, 20000), visible=0, types=[ASTEROIDS], ships=[13]),
    ]
    objectives = [(0, 1, "Nav 1"), (5, 1, "."), (0, 2, "Nav 2"), (5, 2, "."), (0, 3, "Nav 3"), (5, 3, ".")]
    return mission(3, 3, "Jump point", ships, navs, objectives, [2, 0])


def m_countersign():
    """4-0 Countersign (Morningstars; Maniac; from the Kirrah'ka at the yard's
    doorstep).  The yard's security buys the countersign, then scrambles two
    wings anyway: five Gothri on the tender, five Sartha at Nav 1."""
    ships = [
        capital("Kirrah'ka", DORKATHI, 0, pos=(0, 0, 0), yaw=0, pilot=PALADIN_SHIP, important=1, alt=ALT_TENDER),   # 0
        player(MORNINGSTAR, behave=2, objective=3),                               # 1
        wingman("Maniac", MORNINGSTAR, MANIAC),                                   # 2
    ]
    ships += wing("Gothri", GOTHRI, 5, pos=(-8000, 0, 8000), yaw=-135, group=1, objective=1, ai=3, first_index=3)    # 3-7
    ships += wing("Sartha", SARTHA, 5, pos=(3000, 0, -6000), yaw=150, group=0, objective=1, ai=3, first_index=8)     # 8-12
    ships += [field(MINES, pos=(0, 0, 0), radius=8000), field(ASTEROIDS, pos=(0, 0, 0), radius=9000), field(ASTEROIDS, pos=(4000, 0, 4000), radius=8000)]   # 13-15
    navs = [
        dict(name="Kirrah'ka", pos=(0, 0, 0), types=[DORKATHI, MORNINGSTAR, GOTHRI], ships=[1, 2, 3, 4, 5, 6, 7, 0]),
        dict(name="Nav 1", pos=(25000, 0, -50000), types=[SARTHA], ships=[8, 9, 10, 11, 12]),
        dict(name="mines", pos=(-10000, 0, -40000), types=[MINES], ships=[13]),
        dict(name="asteroids", pos=(-20000, 0, 45000), types=[ASTEROIDS], ships=[14, 15]),
    ]
    objectives = [(5, 0, "Defend the Kirrah'ka"), (0, 1, "Nav 1"), (5, 1, "."), (1, 0, "Return to the Kirrah'ka")]
    return mission(4, 0, "Countersign", ships, navs, objectives, [2], series_name="The yard")


def m_ceremony():
    """4-1 Ceremony (Morningstars; Maniac).  Scorpion is cut off; the yard's
    guests have arrived: Sartha at Nav 1, Mandarin-flown Sabres and two
    Fralthra at Nav 2 between the tender and open space."""
    ships = [
        capital("Kirrah'ka", DORKATHI, 0, pos=(0, 0, 0), yaw=0, pilot=PALADIN_SHIP, important=1, alt=ALT_TENDER),   # 0
        player(MORNINGSTAR, behave=2, objective=7),                               # 1
        wingman("Maniac", MORNINGSTAR, MANIAC),                                   # 2
    ]
    ships += wing("Strakha", STRAKHA, 4, pos=(3000, 0, -6000), yaw=150, group=1, objective=1, ai=2, pilot=MANDARIN, first_index=3)   # 3-6 (Strakha: 4-0 ends among Sartha)
    ships += [capital("Fralthra", FRALTHRA, 1, pos=(0, 0, -9000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=2, ai=2, objective=1),
              capital("Fralthra", FRALTHRA, 1, pos=(4000, 0, -11000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=4, ai=2, leader=7, objective=7)]   # 7, 8
    ships += wing("Sabre", SABRE, 3, pos=(2000, 0, -6000), yaw=160, group=3, objective=1, ai=2, pilot=MANDARIN, alt=ALT_MANDARIN_SABRE, first_index=9)   # 9-11
    ships += [field(ASTEROIDS, pos=(0, 0, 0), radius=9000, ), field(ASTEROIDS, pos=(0, 0, 0), radius=9000), field(ASTEROIDS, pos=(5000, 0, 3000), radius=8000), field(MINES, pos=(0, 0, 0), radius=8000)]   # 12-15
    navs = [
        dict(name="Kirrah'ka", pos=(0, 0, 0), types=[DORKATHI, MORNINGSTAR], ships=[1, 2, 0]),
        dict(name="Nav 1", pos=(-35000, 0, -60000), types=[STRAKHA], ships=[3, 4, 5, 6]),
        dict(name="Nav 2", pos=(10000, 0, -90000), types=[SABRE, FRALTHRA], ships=[9, 10, 11, 7, 8]),
        dict(name="mines", pos=(50000, 0, -60000), visible=0, types=[MINES], ships=[15]),
        dict(name="asteroids", pos=(-60000, 0, -10000), visible=0, types=[ASTEROIDS], ships=[12, 13, 14]),
    ]
    objectives = [(0, 1, "Nav 1"), (5, 1, "."), (0, 2, "Nav 2"), (5, 2, "."), (1, 0, "Dock with the Kirrah'ka")]
    return mission(4, 1, "Ceremony", ships, navs, objectives, [2])


def m_yard():
    """4-2 The yard (Morningstars; Maniac).  Kill the yard (ship 10, the
    asteroid base model): four Sabres over it and one that goes for Maniac;
    two Kamekhs in the mines on the way."""
    ships = [
        capital("Kirrah'ka", DORKATHI, 0, pos=(0, 0, 0), yaw=0, pilot=PALADIN_SHIP, important=1, alt=ALT_FRIENDLY_TENDER),   # 0
        player(MORNINGSTAR, behave=2, objective=3, ),                             # 1
        wingman("Maniac", MORNINGSTAR, MANIAC),                                   # 2
    ]
    ships += wing("Sabre", SABRE, 4, pos=(3000, 0, -7000), yaw=160, group=1, objective=1, ai=2, pilot=MANDARIN, alt=ALT_MANDARIN_SABRE2, first_index=3)   # 3-6
    ships += [lone("Sabre", SABRE, pos=(-3000, 0, -7000), yaw=-160, objective=2, pilot=MANDARIN, ai=2, alt=ALT_MANDARIN_SABRE2)]                              # 7
    ships += [capital("Kamekh", KAMEKH, 1, pos=(0, 0, -6000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=2, ai=3, objective=1),
              capital("Kamekh", KAMEKH, 1, pos=(3000, 0, -8000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=4, ai=2, leader=8, objective=8)]   # 8, 9
    ships += [capital("Ghraffid's Yard", YARD, 1, pos=(0, 0, -12000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=2, ai=4, objective=1)]      # 10
    ships += [field(ASTEROIDS, pos=(0, 0, 0), radius=9000), field(MINES, pos=(0, 0, 0), radius=8000), field(MINES, pos=(0, 0, 0), radius=8000)]      # 11-13
    navs = [
        dict(name="Kirrah'ka", pos=(0, 0, 0), types=[DORKATHI, MORNINGSTAR], ships=[1, 2, 0]),
        dict(name="The yard", pos=(30000, 0, -90000), types=[YARD, SABRE], ships=[10, 3, 4, 5, 6, 7]),
        dict(name="Minefield", pos=(15000, 0, -35000), types=[KAMEKH, MORNINGSTAR], ships=[8, 9]),
        dict(name="mines", pos=(-25000, 0, -60000), visible=0, types=[MINES], ships=[13]),
        dict(name="asteroids", pos=(-40000, 0, 20000), visible=0, types=[ASTEROIDS], ships=[11]),
    ]
    objectives = [(0, 2, "."), (5, 2, "."), (0, 1, "Destroy the yard"), (4, 10, "."), (1, 0, "Return to the Kirrah'ka")]
    return mission(4, 2, "The yard", ships, navs, objectives, [2])


def m_duel():
    """4-3 Duel (Morningstars; Maniac).  Home to the Culloden (ship 10, the
    landing) past three Mandarin Sabres and, at the jump point, Prescott in
    the Hellcat he stole (ship 9: SO2's slot wants it destroyed)."""
    ships = [
        capital("Kirrah'ka", DORKATHI, 0, pos=(0, 0, 0), yaw=0, pilot=PALADIN_SHIP, important=1, alt=ALT_FRIENDLY_TENDER),   # 0
        player(MORNINGSTAR, behave=2, objective=9),                               # 1
        wingman("Maniac", MORNINGSTAR, MANIAC, ai=5),                             # 2
    ]
    ships += wing("Sabre", SABRE, 3, pos=(3000, 0, -7000), yaw=160, group=2, objective=1, ai=2, pilot=MANDARIN, alt=ALT_MANDARIN_SABRE2, first_index=3)   # 3-5
    ships += [field(MINES, pos=(0, 0, 0), radius=8000), field(MINES, pos=(0, 0, 0), radius=8000), field(ASTEROIDS, pos=(0, 0, 0), radius=9000)]   # 6-8
    ships += [ship("Prescott", HELLCAT, 1, pos=(2000, 0, -6000), yaw=170, behave=2, important=1, radius=20, b33=4, ai=5, group=-1, objective=1, pilot=PRESCOTT)]   # 9
    ships += [CULLODEN(pilot=FLIGHT_OPS)]                                                                                 # 10
    ships += [capital("Kirrah'ka", DORKATHI, 0, pos=(4000, 0, -3000), yaw=45, pilot=PALADIN_SHIP, important=1, alt=ALT_FRIENDLY_TENDER)]   # 11
    navs = [
        dict(name="Kirrah'ka", pos=(0, 0, 0), types=[DORKATHI], ships=[1, 2, 0]),
        dict(name="Nav 1", pos=(-10000, 0, -90000), types=[SABRE, MORNINGSTAR], ships=[3, 4, 5], steps=[(1, 215, 4)]),
        dict(name="Jump Point", pos=(0, 0, 0), types=[HELLCAT], ships=[9], steps=[(1, 214, 1)]),
        dict(name="Culloden", pos=(60000, 0, -30000), types=[WATERLOO, DORKATHI], ships=[10, 11]),
        dict(name="mines", pos=(45000, 0, -40000), visible=0, types=[MINES], ships=[6, 7]),
        dict(name="asteroids", pos=(-30000, 0, 5000), visible=0, types=[ASTEROIDS], ships=[8]),
    ]
    objectives = [(0, 1, "Nav 1"), (5, 1, "."), (0, 2, "The jump point"), (4, 9, ".Prescott"), (1, 10, "Land on the Culloden"), (0, 0, "Land on the Culloden")]
    return mission(4, 3, "Duel", ships, navs, objectives, [2], carrier=10)


def m_lost_assault():
    """5-0 (a lost exit) The assault (Sabres; Stingray).  The raid is known:
    Grikath at Nav 1, four corvettes (ship 6 the lead) with Gothri aces at
    Nav 2, Sartha at Nav 3; then the Culloden runs for home."""
    ships = [
        CULLODEN(),                                                               # 0
        player(SABRE),                                                            # 1
        wingman("Stingray", SABRE, STINGRAY),                                     # 2
    ]
    ships += wing("Grikath", GRIKATH, 3, pos=(3000, 0, -6000), yaw=160, group=0, objective=1, ai=5, pilot=KILRATHI_ACE, first_index=3)   # 3-5
    ships += [capital("Kamekh", KAMEKH, 1, pos=(0, 0, -7000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=2, ai=3, objective=1)]
    ships += [capital("Kamekh", KAMEKH, 1, pos=(3000, 0, -8500), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=4, ai=3, leader=6, objective=6),
              capital("Kamekh", KAMEKH, 1, pos=(-3000, 0, -8500), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=4, ai=4, leader=6, objective=6),
              capital("Kamekh", KAMEKH, 1, pos=(0, 0, -10000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=4, ai=3, leader=6, objective=6)]   # 6-9
    ships += wing("Gothri", GOTHRI, 3, pos=(2000, 0, -5000), yaw=170, group=0, objective=1, pilot=KILRATHI_ACE, first_index=10)   # 10-12
    ships += wing("Sartha", SARTHA, 3, pos=(3000, 0, -6000), yaw=150, group=0, objective=1, ai=3, first_index=13)                 # 13-15
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[SABRE, WATERLOO], ships=[1, 2, 0]),
        dict(name="Nav 1", pos=(-55000, 0, 50000), types=[GRIKATH], ships=[3, 4, 5]),
        dict(name="Nav 2", pos=(-5000, 0, 80000), types=[KAMEKH, GOTHRI], ships=[6, 7, 8, 9, 10, 11, 12]),
        dict(name="Nav 3", pos=(60000, 0, 40000), types=[SARTHA], ships=[13, 14, 15]),
    ]
    objectives = [(0, 1, "Nav 1"), (5, 1, "."), (0, 2, "Nav 2"), (4, 6, "."), (0, 3, "Nav 3"), (5, 3, "."), (1, 0, "Return to the Culloden")]
    return mission(5, 0, "The assault", ships, navs, objectives, [2], series_name="Home")


def m_lost_patrol():
    """5-1 (a lost exit) Picket line (Rapiers; Downtown).  Jalkehi in the
    rocks, a Fralthra and Sartha at Nav 2, a Gothri ace at Nav 3."""
    ships = [
        CULLODEN(),                                                               # 0
        player(RAPIER),                                                           # 1
        wingman("Downtown", RAPIER, DOWNTOWN),                                    # 2
        field(ASTEROIDS, pos=(0, 0, 0), radius=9000),                             # 3
    ]
    ships += wing("Jalkehi", JALKEHI, 2, pos=(3000, 0, -6000), yaw=160, group=0, objective=1, ai=5, pilot=KILRATHI_ACE, first_index=4)   # 4, 5
    ships += [capital("Fralthra", FRALTHRA, 1, pos=(0, 0, -9000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=7, ai=3),
              capital("Fralthra", FRALTHRA, 1, pos=(2000, 0, -11000), yaw=0, pilot=KILRATHI_SHIP, important=0, behave=2, ai=2, objective=1)]   # 6, 7
    ships += wing("Sartha", SARTHA, 5, pos=(3000, 0, -6000), yaw=150, group=2, objective=1, ai=3, first_index=8)    # 8-12
    ships += [lone("Gothri", GOTHRI, pos=(2000, 0, -5000), yaw=170, objective=1, pilot=KILRATHI_ACE, ai=5)]            # 13
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, RAPIER], ships=[1, 2, 0]),
        dict(name="Nav 1", pos=(50000, 0, 40000), types=[ASTEROIDS, JALKEHI], ships=[3, 4, 5]),
        dict(name="Nav 2", pos=(35000, 0, 100000), types=[FRALTHRA, SARTHA], ships=[6, 8, 9, 10, 11, 12, 7]),
        dict(name="Nav 3", pos=(-5000, 0, 60000), types=[GOTHRI], ships=[13]),
    ]
    objectives = [(0, 1, "Nav 1"), (5, 1, "."), (0, 2, "Nav 2"), (5, 2, "."), (0, 3, "Home by way of Nav 3"), (5, 3, "."), (1, 0, "Return to the Culloden")]
    return mission(5, 1, "Picket line", ships, navs, objectives, [2])


def m_lost_yard():
    """5-2 (a lost exit) Blown (Morningstars; Maniac; from the Kirrah'ka).
    The yard knows us: cover the tender's jump, then five Sabres and two
    Mandarin Broadswords, and dock with her (ship 14) where she comes out."""
    ships = [
        capital("Kirrah'ka", DORKATHI, 0, pos=(0, 0, 0), yaw=0, pilot=PALADIN_SHIP, important=1, behave=6, objective=0, alt=ALT_FRIENDLY_TENDER),   # 0
        player(MORNINGSTAR, behave=2, objective=3),                               # 1
        wingman("Maniac", MORNINGSTAR, MANIAC),                                   # 2
    ]
    ships += wing("Sabre", SABRE, 5, pos=(3000, 0, -7000), yaw=160, group=0, objective=1, ai=3, pilot=MANDARIN, alt=ALT_MANDARIN_SABRE2, first_index=3)   # 3-7
    ships += wing("Broadsword", BROADSWORD, 2, pos=(2000, 0, -6000), yaw=170, group=0, objective=1, ai=3, pilot=MANDARIN, alt=ALT_MANDARIN_BROADSWORD, first_index=8)   # 8, 9
    ships += [field(MINES, pos=(0, 0, 0), radius=8000, ), field(MINES, pos=(0, 0, 0), radius=8000), field(MINES, pos=(0, 0, 0), radius=8000), field(MINES, pos=(5000, 0, 0), radius=8000)]   # 10-13
    ships += [capital("Kirrah'ka", DORKATHI, 0, pos=(0, 0, 0), yaw=0, pilot=PALADIN_SHIP, important=1, alt=ALT_FRIENDLY_TENDER)]   # 14
    ships += [lone("Sabre", SABRE, pos=(-3000, 0, -6000), yaw=-160, objective=1, pilot=MANDARIN, ai=2, alt=ALT_MANDARIN_SABRE2)]   # 15
    navs = [
        dict(name="Kirrah'ka", pos=(0, 0, 0), types=[MORNINGSTAR, DORKATHI], ships=[1, 2, 0]),
        dict(name="Nav 1", pos=(-30000, 0, -40000), types=[SABRE], ships=[3, 4, 5, 6, 7, 15], reveals=[(1, 3), (0, 0)]),
        dict(name="Nav 2", pos=(0, 0, -80000), types=[BROADSWORD], ships=[8, 9]),
        dict(name="Nav 3", pos=(60000, 0, -90000), visible=0, types=[DORKATHI, MORNINGSTAR], ships=[14]),
        dict(name=".", pos=(0, 0, 0), visible=0, types=[], ships=[]),
        dict(name="mines2", pos=(25000, 0, -40000), types=[MINES], ships=[12, 13]),
    ]
    objectives = [(0, 0, "Wait for the Kirrah'ka to jump"), (3, 0, "."), (0, 1, "Nav 1"), (5, 1, "."), (0, 2, "Nav 2"), (5, 2, "."), (0, 3, "Rendezvous with the Kirrah'ka"), (1, 14, "Dock with the Kirrah'ka")]
    return mission(5, 2, "Blown", ships, navs, objectives, [2], carrier=14)


def m_last_patrol():
    """5-3 The last patrol (Morningstars; Maniac).  The way to the jump point:
    two Jalkehi, four Drakhri, three Mandarin Broadswords in the mines."""
    ships = [
        CULLODEN(pilot=FLIGHT_OPS),                                               # 0
        player(MORNINGSTAR),                                                      # 1
    ]
    ships += wing("Jalkehi", JALKEHI, 2, pos=(3000, 0, -6000), yaw=160, group=0, objective=1, ai=2, first_index=2)    # 2, 3
    ships += wing("Drakhri", DRAKHRI, 4, pos=(3000, 0, -6000), yaw=150, group=1, objective=1, ai=2, first_index=4)    # 4-7
    ships += wing("Broadsword", BROADSWORD, 3, pos=(2000, 0, -6000), yaw=170, group=0, objective=1, ai=3, pilot=MANDARIN, alt=ALT_MANDARIN_BROADSWORD, first_index=8)   # 8-10
    ships += [field(MINES, pos=(0, 0, 0), radius=8000), field(MINES, pos=(5000, 0, 0), radius=8000), field(ASTEROIDS, pos=(0, 0, 0), radius=9000)]   # 11-13
    ships += [wingman("Maniac", MORNINGSTAR, MANIAC)]                                                                   # 14
    navs = [
        dict(name="Culloden", pos=(0, 0, 0), types=[WATERLOO, MORNINGSTAR], ships=[1, 14, 0]),
        dict(name="Nav 1", pos=(5000, 0, -40000), types=[JALKEHI], ships=[2, 3]),
        dict(name="Nav 2", pos=(-30000, 0, -80000), types=[DRAKHRI], ships=[4, 5, 6, 7]),
        dict(name="Nav 3", pos=(85000, 0, -100000), types=[BROADSWORD], ships=[8, 9, 10]),
        dict(name="mines", pos=(55000, 0, -60000), types=[MINES], ships=[11, 12]),
    ]
    objectives = [(0, 1, "Nav 1"), (5, 1, "."), (0, 2, "Nav 2"), (5, 2, "."), (0, 3, "Nav 3"), (5, 3, "."), (1, 0, "Return to the Culloden")]
    return mission(5, 3, "The last patrol", ships, navs, objectives, [14])


MISSIONS = [m_shakedown(), m_handmedowns(), m_relay(), m_prize(),
            m_first_patrol(), m_scramble(), m_beacon(), m_valkyrie(),
            m_traders(), m_theft(), m_rendezvous(), m_jump_point(),
            m_countersign(), m_ceremony(), m_yard(), m_duel(),
            m_lost_assault(), m_lost_patrol(), m_lost_yard(), m_last_patrol()]

# The scene texts of SERIES.S03 (24 blocks, one per mission slot and four
# shared): so3/texts.py.
from texts import SERIES_TEXT  # noqa: E402,F401
