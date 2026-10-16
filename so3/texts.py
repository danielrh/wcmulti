"""SO3 "Culloden": the scene texts of SERIES.S03.

The scripts of the scenes are Special Operations 2's (SERIES.S02's blocks:
block = series * 4 + mission; 20-23 shared), so each block keeps its number
of strings and every line keeps its speaker.  `scripts/wc2scene.py
SERIES.S02 <block>` prints the skeleton a block is written against: the
rooms, who speaks each line (talker ids: 0 the player, 2 the wing commander,
4 the Captain, 8 Stingray, 10 Toro, 12 the accused / Prescott, 14 Sparks,
16 Paladin, 18 Doomsday, 22 Downtown, 24 Thrakhath, 26 the Emperor, 28
Maniac, 58 Ghraffid, 30/36/38 radio voices, 200 the narrator) and which
lines are the variants a debriefing picks by the mission's outcome.
Lines stay under 119 characters.  \\x86 is the callsign, \\x85 the surname,
\\x87 the kill count.  The story: so3/story.md.
"""

SERIES_TEXT = {}

# Block 0, mission 1-0 Shakedown.  0-22 the prologue film (a court-martial,
# then the Kilrathi), 23-30 the briefing by comm screen, 31-38 the Captain's
# day cabin and 39-41 the recording, 42-44 radio.
SERIES_TEXT[0] = [
    "Confederation Sector Headquarters, Torgo",                                                                   # 0 title
    "",                                                                                                           # 1
    "No!  I can give you the names of the Mandarin cells!  Their Kilrathi contacts!",                            # 2 Voss
    "ANYTHING!",                                                                                                  # 3 Voss
    "Justice is decided in the court, Commander, \n not with a bargain.",                                        # 4 Bergstrom
    " ",                                                                                                          # 5
    "Lights, please---",                                                                                          # 6 caption
    "Now that we have seen the evidence taken from the courier's shuttle---",                                     # 7 Banbridge
    "---this military tribunal has reached its verdict.",                                                         # 8
    "Lieutenant Commander Aaron Voss, you stand accused of espionage and 	high treason.",                     # 9
    "You are hereby pronounced guilty of all charges.",                                                           # 10
    "For these crimes, you are sentenced to death.",                                                              # 11
    "This court cannot condemn me!",                                                                              # 12 Voss
    "Only history will decide if I'm a villain or a hero.",                                                       # 13
    "And our history will be written by the Kilrathi.",                                                           # 14
    "Guards, take him away!",                                                                                     # 15 Banbridge
    "Meanwhile...",                                                                                               # 16
    "The Hellcat V is nearing the final stage of prototype 		testing.",                                        # 17
    "A squadron of test pilots is being transferred to the cruiser Culloden.",                                    # 18
    "We must have that prototype!",                                                                               # 19 Ghraffid
    "Bring me this Hellcat, even if you have to spill blood to get it.",                                          # 20
    "Preferably.",                                                                                                # 21
    " ",                                                                                                          # 22
    "TCS Culloden, Flight Operations",                                                                            # 23 title
    "",                                                                                                           # 24
    "Colonel, this is Prescott in Flight Ops.  The Hellcats are fuelled, armed, and they still smell of the factory.",  # 25
    "Captain Banbridge wants a full shakedown before we go dark: live fire on the hulks at Nav 1, then a sweep of Nav 2.",  # 26
    "The new hand-off computers do the targeting for you now.  Trust them.  That is what they are for.",         # 27
    "One more thing.  If you meet anything out there, nothing goes home to report us.  Nothing.",                 # 28
    "Understood.  A new fighter and a free-fire range.  It's almost my birthday.",                                # 29 player
    "Good hunting, \x86.  The Culloden holds station here until you are back aboard.",                           # 30
    "The Captain wants you in his day cabin.  Now.",                                                              # 31
    "Ten minutes later.",                                                                                         # 32
    "Sit down, Colonel.  You flew well.  That is not why you are here.",                                          # 33 Banbridge
    "Sir, the recon pair at Nav 2---",                                                                            # 34 player
    "---if either of them got off a signal, every picket between here and the yards knows a cruiser is loose.",  # 35 player
    "Nobody blames you for the Kilrathi, \x86.  Sparks pulled the Hellcats' computers after you landed.",       # 36 Banbridge
    "Every one of them logged an uplink burst during your fight.  To what, we do not know.  Until we do, nobody flies them.",  # 37
    "Before we sailed, Intelligence took this off a Mandarin courier.  You need to hear it.  You need to hear it 		now.",  # 38
    "The Hellcat flies in three weeks, and the key will be in the relay's hands before the carrier sails.",       # 39 Voss (recording)
    "Their escort is the Colonel from K'Tithrak Mang.  Tell our friends to make him comfortable.",                # 40
    "He will be the first to find out what his new fighter does when we ask it to.",                             # 41
    "Culloden to \x86: two recon contacts at Nav 2.  Do not let them jump.",                                    # 42 radio
    "Range control: the hulks are hot.  Weapons free, Colonel.",                                                  # 43
    "Flight Ops: your computers are reporting a fault.  Disregard, it is a telemetry glitch.",                    # 44
]

# Block 1, mission 1-1 Hand-me-downs.  0-12 Kilrathi sector command, 13 a
# title card, 14-24 the briefing room, 25-43 the repair deck with Sparks
# (37-39 by kills, 40-41 by the corvette, ship 10).
SERIES_TEXT[1] = [
    "Kabla Meth, Kilrathi Sector Command, Deneb Sector.",
    "Report, grandson.  The yards at Ghraffid's station.",
    "I am told a Terran ship was seen two systems from them.  Was this 		carelessness?",
    "It was not seen by us, my Emperor.  It was told to us.",
    "The Mandarin device the humans carry in their new fighters spoke, as the traitor promised it would.",
    "One cruiser, deep in our space, with six fighters that do not yet know they are ours to switch off.",
    "A cruiser is nothing.  What is it doing eleven jumps from its fleet?",
    "Looking for the yards.  There is no other prize on that road.",
    "I have sent a wing to the place the device marked, and the corvette Kur'ratha to hunt the cruiser.",
    "The first hull must not be seen before it is finished.  Attend to it.",
    "When the device speaks again, I will know where it sleeps.",
    "I would remind you of one thing, Prince Thrakhath---",
    "A ship that knows it is hunted fights like one.  Kill it before it learns.",
    "The Torgo drift, behind the Kilrathi line.",
    "Briefing Room, TCS Culloden, 0500 hours.",
    " ",
    "The Hellcats stay in the hangar.  Last night their computers took a key from somebody we have not found yet.",
    "Whoever holds it can switch your hand-off and your missile locks off in a fight.  So today you fly Epees.",
    "Sparks has rigged direction finders on the utility barge.  Stingray drives it; it listens for the uplink.",
    "Keep the barge alive while it listens.  A corvette is hunting this ship; if we meet it, I want it dead.",
    "Valkyrie hands out the rest of the assignments.",
    "\x86, you lead.  Doomsday is your wing, Downtown your third.  Stay on the barge.",
    "That is all.  Bring it home, and bring the bearing.",
    "Dismissed.",
    "",
    "You brought my barge back, Colonel.  I was ready to be angry at somebody.",
    "Stingray says the finders got a clean bearing.  Straight into the rocks.",
    "Which is where I'd put a relay if I wanted it to listen without being seen.",
    "Tell me about the Hellcats, Sparks.  Straight.",
    "Straight?  The hand-off computer has a maintenance channel that takes orders by radio.  Last night it took a key.",
    "Somebody out there holds a key to my fighter.",
    "A master key, Colonel, and it lives in whatever sent that burst.  Get me the machine it lives in.",
    "And if we get it?",
    "Then I can burn the channel out and sign the six of them off.  Until then a stranger's hand is on the switch.",
    "Who had access to the computers, Sparks?",
    "The programme's liaison, Colonel.  And me.  I know which one of us I'd look at.",
    "So, how was the patrol?",
    "Rough enough.  I took out \x87 of them---",
    "Rough.  I only got one of them---",
    "Bad.  I couldn't touch them---",
    "---and the corvette at Nav 2 is still out there.  We couldn't finish her.",
    "---and the corvette at Nav 2 is scrap.  Downtown put the last missile in her himself.",
    "It doesn't feel like enough, flying an Epee while six Hellcats sit on the deck.",
    "Find me that machine, Sparks.",
]

# Block 2, mission 1-2 The relay.  0-6 Maniac on the flight deck, 7 the
# tannoy, 8-15 the bridge, 16-19 the landing call (18 no core, 19 core),
# 20-29 Toro afterwards.
SERIES_TEXT[2] = [
    "Maniac.  They told me you were in a hospital bed.",
    "They told you right, \x86.  Three weeks in the Mandarins' chair.  Know how long it took anyone to come for me?",
    "Long enough that I learned to like the music they piped in.",
    "So when a cruiser nobody has heard of asked for pilots nobody would miss, I thought: that's my ship.",
    "Then they gave me a Hellcat and took it away again.  Story of my life.",
    "You'll get it back.  We have a bearing on the thing that took it.",
    "And Dalmau is out there on it alone in a Sabre, because somebody thought a lieutenant could confirm a bearing quietly.",
    "Colonel \x85 and Major Marshall, report to the bridge.",
    "Major, Lieutenant Dalmau reports the relay is a corvette, nine thousand kilometres into the rocks, with a guard.",
    "She was told to look, Captain, not to fight.  Her wingman is a Grikath squadron.",
    "An Epee cannot tractor a computer core out of a wreck.  A Sabre can.  Dalmau has had it unless somebody goes now.",
    "Sir, there's a second Sabre ready on the deck.  I volunteer to go and get her, and the core.",
    "Captain, Dalmau is one of my pilots.  I request to fly the Sabre.",
    "In the future, Major, I advise you to keep better track of your squadron.  Sparks wants that core intact, Colonel.",
    "Good luck, Colonel \x85.",
    "",
    "This is Colonel \x85, requesting permission to land.",
    "Did you get the core, \x86?",
    "Negative, Culloden.  The core is slag.  We have the relay's wreck and nothing else.",
    "Affirmative, \x86.  You are cleared to land.",
    "Glad you made it back, sir.",
    " ",
    "Thank you, Colonel \x85, for pulling me out of there.",
    "Not a problem, Lieutenant.  I'm glad I could help.",
    "At least that relay won't be talking to anybody's fighter again.",
    "I had the Vak'thra on my scope for four seconds before her Grikath found me.  Four seconds.",
    "By the way, Colonel, you can call me Ines.  My callsign is Toro.",
    "Toro, what were you doing out there on your own?",
    "Valkyrie sent me to confirm the bearing without starting a war.  I confirmed it.",
    "It sounds like the Culloden has a bad case of brave lieutenants.",
]

# Block 3, mission 1-3 Prize.  0-16 the observation deck, 17-27 the wing
# commander's office, 28-30 the deck, 31-38 the Captain if the tender died
# (ship 3), 39-49 the Captain if she lived, 50-58 the radio at Nav 2.
SERIES_TEXT[3] = [
    "TCS Culloden, Observation Deck, 2130 hours.",
    "---so the Mandarins had this chair, right, with the straps and the screens---",
    "---did I tell you about the chair?",
    "Yes.  Yes, you did.",
    "Point is, I sat in it for three weeks and I still fly better than anyone on this ship.  Including you.",
    "Which is why the first Hellcat off the line tomorrow should be mine, not some lieutenant's.",
    "Sparks signed them off an hour ago, Todd.  She burned the channel out with the core you didn't fetch.",
    "I'd have fetched it.  I'd have fetched it with a bow on it.",
    "Prescott is in the brig.  The core's logs name him: he installed the channel and he handed the key over.",
    "And tomorrow we find out what six Hellcats do with nobody's hand on the switch but ours.",
    "Two missiles for the first one.  Trust me.",
    "You know, Todd--- people say you don't listen.",
    "I listen.  I listened to three weeks of Mandarin music.",
    "When those Hellcats come back on the line, \x86, the first one is mine.",
    "I am going to fly it the way it was meant to be flown.",
    "The Kilrathi have never seen anything like it.  Neither has Confed.",
    "Just keep watching me, pal.  History in the making.  Again.",
    "Wing Commander's Office, TCS Culloden.",
    "The Hellcats are cleared.  Tomorrow's flight is the first since the grounding, and it is a sweep, not a war.",
    "Nav 1 is the picket that has been shadowing us.  Nav 2 and Nav 3 are where I think its friends are.",
    "There is a Kilrathi tender on that line whose escort left her.  If you find her, you report her.  You do not shoot her.",
    "I had planned Toro as your wing.",
    "Colonel Bergstrom, I request to fly the wing myself.",
    "If anything is going to happen to a pilot in the Hellcat, I want it to happen to me first.",
    "If you feel so strongly about this, Todd, then I have no objection.",
    "\x86: Nav 1, Nav 2, Nav 3, and home.  The Culloden moves while you are out; the computer has her new position.",
    "Dismissed.",
    "",
    "Welcome back, sir!",
    "That looked like a hard one, sir!",
    "Glad you made it back alive, sir!",
    "The Captain's Day Cabin, TCS Culloden.",
    "You destroyed a ship that had struck her colours, Colonel \x85, and cost us a prize this cruise needed!",
    "That tender carried fuel enough to take us to the yards and home.  Her crew would have told us the road.",
    "I do not care which of you fired.  You were in command of that flight.",
    "You are relieved.  You will be confined to quarters until we are back in Confederation space---",
    "---and then you will answer for her at a court-martial.",
    "Sir!  I can explain---",
    "You can explain to the court, Colonel!",
    "The Captain's Day Cabin, TCS Culloden.",
    "A fine piece of luck, that tender.  Her fuel is in our bunkers, and her crew is in our brig.",
    "We've searched her.  Her master jettisoned his manifests into the drift and his logbook is a 		forgery.",
    "We'll never know what she was carrying.  But we made one discovery.",
    "There was a human aboard her.",
    "He must have been captured, sir.  A slave.",
    "Not so.  This man was working with them willingly.  He is a Mandarin, and he is not the only one we hold.",
    "Prescott in the brig, a courier on a Kilrathi tender.  The Mandarins did not send one man against this ship.",
    "They sent a plan.  What the courier was carrying, and where, is a question for Special Operations.",
    "Paladin comes aboard at the next rendezvous.  He has watched these yards longer than we have.",
    "Dismissed, Colonel.  Well done.",
    "Terran fighter, this is the fleet tender Kirrah'ka.  We carry fuel and nothing else.",
    "Our escort has left us.  Our engines are cold.",
    "Do not shoot!  We surrender!",
    "Culloden, I have a Kilrathi tender in my sights at Nav 2.  No weapons on her.",
    "She is asking for terms.  Awaiting orders.",
    "\x86, this is Flight Ops.  Your orders are to leave the tender intact---",
    "---the Captain wants her fuel more than he wants her dead.  A prize crew is on the way.",
    "Continue your mission, \x86.",
    "Affirmative, Culloden.  \x86 out.",
]

# Block 4, mission 2-0 First patrol.  0-11 Paladin in the temporary duty
# office, 12-24 the briefing room, 25-42 the flight deck debriefing with
# Valkyrie (per nav: 28-29 Nav 1, 30-32 Nav 2, 33-35 Nav 3, 36-40 Nav 4).
SERIES_TEXT[4] = [
    "Paladin, it's good to see you again!",                                                                       # 0 player
    "Likewise, lad.  I wish I had come aboard wi' better news.",                                                  # 1 Paladin
    "I've been reading Kilrathi traffic since Torgo.  They talk about your Hellcats by name.",                     # 2
    "Somebody told them about the programme months ago, and somebody told them where this ship would be.",        # 3
    "Did you get anything from the Mandarin we took off the Kirrah'ka?",                                          # 4 player
    "He says she was carrying fuel and crew to a place the Kilrathi call Ghraffid's Rock.",                        # 5 Paladin
    "I've never heard of anyplace called Ghraffid's Rock before.",                                                 # 6 player
    "I thought Special Operations knew the names of every hole the Kilrathi dig.",                                 # 7 player
    "Oh, I've heard of this one before, to be sure.",                                                              # 8 Paladin
    "It's the yard, laddie.  I just don't know which rock, in which system, is all.",                              # 9
    "But I intend to find out before much longer.  I have a man inside.",                                          # 10
    "If I can be of any help, you know where to find me.",                                                         # 11
    "Briefing Room, TCS Culloden.",                                                                                # 12 title
    "The Kilrathi have been tracking this ship since the range.  Their relay is dead; their memory is not.",       # 13 Valkyrie
    "It is up to us to make them pay for every look they take---",                                                 # 14
    "---and to look like a cruiser that is lost, not one that is hunting.",                                        # 15
    "Six Hellcats are cleared.  Today four of them fly, and I want them back.  All four.",                         # 16
    "No doubt the pickets will be out there, waiting for us.",                                                     # 17
    "I want you to remember why you are serving aboard the Culloden before you fly today.",                        # 18
    "You are here because nobody else would take you.  Prove them wrong.",                                         # 19
    "You listen as Valkyrie gives out the assignments.",                                                           # 20 narrator
    "\x86, you're leading Maniac, Doomsday and Downtown on the first full Hellcat patrol.",                       # 21
    "Patrol your Nav Points.  Watch out for a minefield between 		Nav 1 and 2.",                                # 22
    "That is all, ladies and gentlemen.  Dismissed.",                                                              # 23
    "",                                                                                                            # 24
    "Welcome back, sir!",                                                                                          # 25 deck
    "Flight Deck, TCS Culloden.",                                                                                  # 26 title
    "What do you have to report?",                                                                                 # 27 Valkyrie
    "Didn't make it to Nav 1.",                                                                                    # 28 player
    "Nav 1 was deserted.",                                                                                         # 29
    "At Nav 2, there were four Strakha.",                                                                          # 30
    "None of them survived.",                                                                                      # 31
    "We couldn't take all of them out.",                                                                           # 32
    "At Nav 3 there was a single Gothri who tried to take the four of us on 		by himself.",                     # 33
    "He managed to get away from us somehow.",                                                                     # 34
    "He didn't have a chance against the Hellcat, of course.",                                                     # 35
    "What did you find at Nav 4?",                                                                                 # 36 Valkyrie
    "We didn't make it out to Nav 4.",                                                                             # 37
    "There were four Jalkehi waiting for us.",                                                                     # 38
    "We couldn't take all of them out.",                                                                           # 39
    "They weren't much of a problem.  The hand-off works, Colonel.  With nobody else's hand on it.",              # 40
    "I'll take this to the Captain and to Paladin immediately.",                                                   # 41 Valkyrie
    "Dismissed, \x86.",                                                                                           # 42
]

# Block 5, mission 2-1 Scramble.  0-16 cards in the barracks (Maniac 28,
# Doomsday 18, Downtown 22, Toro 10), 17-27 the bridge and the launch, 28-40
# the bridge afterwards (32-35 by kills).
SERIES_TEXT[5] = [
    "Barracks, TCS Culloden.",                                                                                     # 0 title
    "This is a friendly little game of cards, \x86.  Just like the 		old days.",                                # 1 Maniac
    "I thought this'd be a good way for you to get to know the rest of 		the squadron a little better.",        # 2
    "This gentleman on my left is Major Etienne \"Doomsday\" Montclair.  He has an unusual 		hobby.",          # 3 Maniac
    "I speak Kilrathi.  Or at least I do as fluently as any 		man who learned it from prisoners can.",           # 4 Doomsday
    "I also study how they die.  It is what we are all going to do out here, so I like to know the local customs.",  # 5
    "Hardly what I'd call a relaxing pastime.",                                                                    # 6 player
    "Now I'd like you to meet Lieutenant \"Downtown\".  Youngest man on the Concordia, and now the youngest here.",  # 7 Maniac
    "This kid is an ace and he flies exactly the way I do.",                                                       # 8
    "Isn't that just a bit dangerous, Downtown?",                                                                  # 9 player
    "Hey, you know how it is, sir.  Spirit used to say: if it's time to get your ticket punched---",              # 10 Downtown
    "---you can't do anything about it.  She was right about everything else.",                                   # 11
    "You and Lieutenant Dalmau already know each other, of course.",                                               # 12 Maniac
    "He pulled me out of the rocks, Major.  I'd say we've met.",                                                   # 13 Toro
    "Ahem, yes.  Well, whose turn is it to deal again?",                                                           # 14 player
    "I believe it's yours.",                                                                                       # 15 Maniac
    " ",                                                                                                           # 16
    "The Bridge, TCS Culloden.",                                                                                   # 17 title
    "Your report, Colonel Bergstrom?",                                                                             # 18 Banbridge
    "Captain, our scanners show a wave of Kilrathi fighters coming out of the asteroid field!",                    # 19 Valkyrie
    "Scramble the alert fighters!",                                                                                # 20 Banbridge
    "Red Alert!  Attack craft approaching!",                                                                       # 21
    "Colonel \x85 and Captain Wright!  Report to the flight deck 		immediately.",                             # 22 tannoy
    "",                                                                                                            # 23
    "Stingray, do you read?",                                                                                      # 24 player
    "Of course, sir.  Sabres.  They give me a Sabre and six Gothri.  Ready to attack on your command.",           # 25 Stingray
    "These cats thought they'd catch a cruiser with her Hellcats cold in the hangar...",                          # 26 player
    "Let's go give them a little surprise of our own, Stingray.",                                                  # 27
    "Report to the Bridge immediately, sir!",                                                                      # 28
    "The Bridge, TCS Culloden.",                                                                                   # 29 title
    "Colonel \x85, Captain Wright, let's hear your report...",                                                    # 30 Banbridge
    "There were six Gothri and one Kamekh.",                                                                       # 31 player
    "I nailed \x87 of them.  Stingray blanked.",                                                                  # 32
    "I held them off while Stingray took down \x87.",                                                             # 33
    "I nailed \x87 of them.  Stingray took down the rest.",                                                       # 34
    "We couldn't nail any of them, but we forced them to break off.",                                              # 35
    "Your fast action has saved this ship, officers.",                                                             # 36 Banbridge
    "They came straight at the hangar.  They know what is in it.",                                                 # 37
    "And they know the Culloden is the only Confederation ship within eleven jumps.",                              # 38
    "We must be ready to repel other attacks at any time.  The alert fighters stay armed.",                        # 39
    "I will mention your work in my report, if we ever get to send one.  Dismissed.",                              # 40
]

# Block 6, mission 2-2 Beacon.  0-11 the observation deck (Doomsday 18,
# Downtown 22), 12-19 the launch (Flight Ops 38, Toro 10), 20-34 the wing
# commander's office (30-32 by kills), 35-37 the warning in flight, 38 blank.
SERIES_TEXT[6] = [
    "Observation Deck, TCS Culloden.",                                                                             # 0 title
    "I've got a question for you, \x86.  When are you going to retire?",                                         # 1 Doomsday
    "A Colonel with your record could have a desk and a window, and nobody would call it running.",                # 2
    "What are you trying to prove, still flying the front lines after 		ten years?",                            # 3
    "In case you haven't noticed, Major, there's a war on!",                                                       # 4 player
    "The Confederation needs every good pilot we have!",                                                           # 5
    "We didn't mean any disrespect, sir.  It's just that we've heard about 		what happened at K'Tithrak Mang.",  # 6 Downtown
    "Spirit went into that station so the rest of us could go home.  Doomsday says it was the only way it could end.",  # 7
    "How many more of us die before somebody else is the one who goes in?",                                        # 8
    "He says this ship is doomed too.  He says it about every ship.",                                              # 9
    "How does that make you feel, sir, knowing he is usually right?",                                              # 10
    "It makes me feel just fine, Lieutenant.  I'm looking forward to proving him wrong.",                          # 11 player
    "Colonel \x85, report to the flight deck immediately.",                                                       # 12 tannoy
    "",                                                                                                            # 13
    "\x86, the Culloden is passing through a dense concentration 		of mines and asteroids.",                   # 14 Flight Ops
    "The attack on the ship was the first move of something bigger.  The Captain wants to know what.",             # 15
    "We have detected low level transmissions from 		what may be a Confederation data pod at Nav 3.",          # 16
    "You are to investigate these transmissions.  The alert birds are yours; the Hellcats stay with the ship.",    # 17
    "It's good to be flying with you, \x86.",                                                                     # 18 Toro
    "Let's hope you still think so after we chase down the source of 		those transmissions.",                  # 19 player
    "Welcome back, sir!",                                                                                          # 20 deck
    "Looks like it was a tough fight, sir!",                                                                       # 21
    "Glad you made it back alive, sir!",                                                                           # 22
    "Wing Commander's Office",                                                                                     # 23 title
    "I am glad to see you made it back safely, \x86.  What do you have to 		report?",                          # 24 Valkyrie
    "I got out of Nav 3 before the bomb went off.  That 		warning came just in time.",                          # 25 player
    "A few more seconds and I would have been vaporized!",                                                         # 26
    "Aside from that I ran into three Drakhri at Nav 1.",                                                          # 27
    "I encountered three Jalkehi at Nav 2.",                                                                       # 28
    "At Nav 3, there were four Sartha hiding in the mines, waiting for whoever came for the pod.",                 # 29
    "I only managed to take down \x87 of the enemy on this mission.",                                             # 30
    "I only managed to take down one of the enemy on this mission.",                                               # 31
    "I couldn't take down any of the enemy on this mission.",                                                      # 32
    "A Confederation beacon as bait.  Prescott's friends have been busy.  You look like you can use a little rest.",  # 33 Valkyrie
    "You've earned it.  Dismissed.",                                                                               # 34
    "\x86, we've just decoded the transmissions from Nav 3.",                                                     # 35 radio
    "It's a warning beacon from a timed explosive device!",                                                        # 36
    "Get out of there immediately!",                                                                               # 37
    "	",                                                                                                          # 38
]

# Block 7, mission 2-3 Valkyrie.  0-5 the observation deck (Downtown 22),
# 6-16 the barracks with Toro (10), 17-28 the flight deck (Toro back alone),
# 29-32 the deck and a title, 33-42 the wing commander's office.
SERIES_TEXT[7] = [
    "Observation Deck, TCS Culloden.",                                                                             # 0 title
    "What's wrong, Downtown?  Everyone's been acting like they're at a funeral.",                                  # 1 player
    "The Kilrathi have been making broadcasts on the guard channel, sir.  We picked one up while you were out.",   # 2 Downtown
    "They named this ship.  They named the Hellcat.  They read out the Captain's name and yours.",                 # 3
    "They had every one of those names from Prescott, and we let him walk around the hangar for a month!",         # 4
    "I didn't let him walk.  	The programme sent him, and the programme's badge opened every door.",             # 5 player
    "Ten minutes later...",                                                                                        # 6 caption
    "Barracks, TCS Culloden.",                                                                                     # 7 title
    "What did you want to talk about, Toro?",                                                                      # 8 player
    "You looked like a man counting, \x86.  I thought you could use somebody who isn't.",                         # 9 Toro
    "Ten years of this, and I still count.  Who went out, who came back.",                                         # 10 player
    "I'd have given my career, given everything, for one more of them to have come back.",                         # 11
    "You're the best pilot on this ship, and an officer who brings people home.",                                  # 12 Toro
    "You brought me home.  Let that count too.",                                                                   # 13
    "Toro... I don't know what to say...",                                                                         # 14 player
    "Then don't say anything, \x86.",                                                                             # 15 Toro
    " 		",                                                                                                      # 16
    "Flight Deck, TCS Culloden.",                                                                                  # 17 title
    "Hey, Colonel!  You missed one hell of a killer patrol.",                                                      # 18 Toro
    "Toro, what are you doing back from your patrol already?  And where 		is your lead?",                      # 19 player
    "Still at Nav 1.  We ran into a massive enemy force.",                                                         # 20 Toro
    "I was ordered to return immediately to the Culloden.",                                                        # 21
    "This isn't a simulator exercise, Lieutenant!",                                                                # 22 player
    "When you leave your lead behind, you're leaving her to die!",                                                 # 23
    "But Valkyrie ordered me back so the Kilrathi 		wouldn't get both Sabres, and so somebody would warn the ship!",  # 24 Toro
    "You mean... Valkyrie was your lead?  Valkyrie is still out there?",                                           # 25 player
    "Yes, she said she'd hold them off and then head back here---",                                                # 26 Toro
    "Excuse me, I have work to do.",                                                                               # 27 player
    "",                                                                                                            # 28
    "Welcome back, sir!",                                                                                          # 29 deck
    "Looks like it was a tough fight, sir!",                                                                       # 30
    "Glad you made it back alive, sir!",                                                                           # 31
    "Wing Commander's Office.",                                                                                    # 32 title
    "Valkyrie, are you all right?",                                                                                # 33 player
    "Yes.  Thank you for coming out, Colonel.  I had run out of ideas and nearly out of Sabre.",                   # 34 Valkyrie
    "I still can't believe Toro just left you out there.",                                                         # 35 player
    "Toro went back for help at my order.",                                                                        # 36 Valkyrie
    "A corvette and five Gothri do not sit on a patrol line by accident.  Somebody had to tell the ship.",         # 37
    "Valkyrie, you're more use to this ship than any warning.",                                                    # 38 player
    "Doomsday would say that is exactly how the doomed ones talk.",                                                # 39
    "I lost a squadron once by giving the other order, \x86.  I will not give it again.",                         # 40 Valkyrie
    "When I thought I would not be here to give any order at all...  Well.  You came.  Thank you.",                # 41
    " ",                                                                                                           # 42
]

# Block 8, mission 3-0 The traders.  0-14 Paladin in the duty office, 15-29
# the wing commander's office, 30-33 the deck and a title, 34-52 the bridge
# (34-40 the Tsiolkovsky home, 41-46 lost; 38-40 / 44-46 by kills).
SERIES_TEXT[8] = [
    "Temporary Duty Office, TCS Culloden.",                                                                        # 0 title
    "Have you had any luck tracking down Ghraffid's Rock yet, Paladin?",                                           # 1 player
    "Not so far.  Did ye hear about the two free traders adrift off the Kilrathi colony ahead, laddie?",           # 2 Paladin
    "That's all I have been hearing about lately!",                                                                # 3 player
    "Why must a cruiser on a raid stop for every smuggler who ran out of luck?",                                   # 4
    "The Lucky Mandy and the Tsiolkovsky are more than smugglers.",                                                # 5 Paladin
    "They have run fuel and parts to the Kilrathi colonies for years, and they have the approach charts to prove it.",  # 6
    "And now they're stranded, and frightened, and for sale.",                                                     # 7
    "They've caused the Confederation a lot of anger and frustration. ",                                           # 8
    "Just because some of it was deserved doesn't 		mean their charts are any less real.",                      # 9
    "I had a Kilrathi tender in my sights, James, and I let her live.  That is the only reason you are aboard.",   # 10 player
    "If that was a mistake, all I want is the chance to put it right.",                                            # 11
    "I'd wager you'll cross paths with Ghraffid's people before this is over, \x86.",                             # 12 Paladin
    "What makes you say that?  Do you know something about those 		traders that I don't?",                     # 13 player
    "Nothing that you probably won't know before long, laddie.",                                                   # 14 Paladin
    "Wing Commander's Office.",                                                                                    # 15 title
    "The traders have offered us a deal, \x86.",                                                                  # 16 Valkyrie
    "They want fuel cells, water and a Confederation pardon for 		the yard's approach charts.",                 # 17
    "They will meet one fighter and the barge at the rendezvous or the 		deal is off.",                          # 18
    "The Captain wants you to fly that fighter, \x86.",                                                           # 19
    "The Captain can't be serious about paying smugglers in the middle of Kilrathi space!",                        # 20 player
    "Has he really approved a pardon for two men who sold fuel to the enemy for years?",                           # 21
    "\x86, we have no charts of the jump chain ahead.  They do.  They are of no further use to anybody dead.",   # 22 Valkyrie
    "Is there any particular reason you want me to fly this one, Valkyrie?",                                       # 23 player
    "Paladin thinks the traders are being watched, and that whoever watches them 		will want our barge.",       # 24 Valkyrie
    "So I'm the bait to attract him.",                                                                             # 25 player
    "The Captain believes you are the only pilot on this ship---",                                                 # 26 Valkyrie
    "---who can bring a barge and a smuggler home through an ambush.  Maniac follows at a distance.",             # 27
    "They're fuelling a Sabre for you on the Flight Deck, Colonel.  Dismissed.",                                   # 28
    "",                                                                                                            # 29
    "Welcome back, sir!",                                                                                          # 30 deck
    "Looks like it was a tough fight, sir!",                                                                       # 31
    "Glad you made it back alive, sir!",                                                                           # 32
    "The Bridge, TCS Culloden.",                                                                                   # 33 title
    "Congratulations on a successful exchange, Colonel.",                                                          # 34 Banbridge
    "Thank you, sir.  I wasn't surprised that somebody would be waiting for the 	traders.",                       # 35 player
    "On the way back to the Culloden, a Ralatha and an entire wing of 		Drakhri---",                             # 36
    "---tried to destroy the unarmed ship carrying the charts.",                                                   # 37
    "I nailed \x87 of them.  And I brought the Tsiolkovsky home.",                                                # 38
    "I only nailed one of them.  But I brought the Tsiolkovsky home.",                                             # 39
    "I couldn't nail any of them.  But I brought the Tsiolkovsky home.",                                           # 40
    "I heard that the trader was lost, Colonel.",                                                                  # 41 Banbridge
    "I wasn't surprised that somebody would be waiting for her.",                                                  # 42 player
    "But I wasn't expecting an ambush of a Ralatha and a wing of Drakhri.",                                        # 43
    "At least I nailed \x87 of those Kilrathi bastards.",                                                         # 44
    "At least I nailed one of those Kilrathi bastards.",                                                           # 45
    "And I couldn't nail any of those Kilrathi bastards.  I'm sorry, sir.",                                        # 46
    "The Kilrathi are watching everything that moves in this sector, Colonel.",                                    # 47 Banbridge
    "One of the traders' crew knew something they did not want us 		to have, hence the ambush.",                 # 48
    "The Tsiolkovsky's master has been to the yard.  He has seen the first hull in its frame.",                    # 49
    "Paladin is with him now.  We are hoping for the rest of the road.",                                           # 50
    "Anything else to report, Colonel?",                                                                           # 51
    "Just one more thing.  Whoever set that ambush knew the rendezvous, Captain.  Before we did.",                 # 52 player
]

# Block 9, mission 3-1 Theft.  0-15 Maniac briefs Doomsday and Downtown,
# then the repair deck (Sparks 14, Doomsday 18, Downtown 22); 16-30 the
# bridge; 31-38 Maniac in flight; 39-50 Flight Ops afterwards (46-49 by kills).
SERIES_TEXT[9] = [
    "Doomsday, Downtown, I want you to fly a standard diamond patrol.",                                            # 0 Maniac
    "Report anything unusual.  And make me proud of you.",                                                         # 1
    "Yes, sir.",                                                                                                   # 2 Doomsday
    "Today the Kilrathi will learn to fear the name of the Culloden's reserve squadron, 		sir.",                # 3 Downtown
    "Five minutes later...",                                                                                       # 4 caption
    "Repair Deck, TCS Culloden.",                                                                                  # 5 title
    "Major Montclair?  Lieutenant?  Why are you suited up for a flight?",                                          # 6 Sparks
    "Isn't Commander Prescott supposed to be in the brig?",                                                        # 7 Sparks
    "What makes you think he isn't, Chief McCullough?",                                                            # 8 Doomsday
    "Because a Hellcat is on the ship elevator with a man in a programme flight suit in it---",                    # 9 Sparks
    "---and he's going to launch in another couple of minutes.",                                                   # 10
    "But nobody's scheduled to fly a Hellcat until tomorrow...",                                                   # 11 Doomsday
    "That fighter is fully armed and loaded!",                                                                     # 12 Downtown
    "Signal an alert!  We have to stop him!",                                                                      # 13
    "",                                                                                                            # 14
    "",                                                                                                            # 15
    "The Bridge, TCS Culloden.",                                                                                   # 16 title
    "Ten minutes ago, a Hellcat was taken from the Flight 		Deck.",                                              # 17 Banbridge
    "We have confirmation from Chief McCullough that the pilot was Commander Prescott.",                           # 18
    "He was let out under guard to help her strip the channel.  He set a charge to cover his escape...",           # 19
    "Two of the guard are dead, and Sparks and Doomsday are in Sickbay.",                                          # 20
    "It'll be at least another fifteen minutes until 	the Flight Deck is operational again.",                     # 21
    "As soon as we can launch fighters, you'll go in pursuit of Commander Prescott.",                              # 22
    "The Hellcat doesn't have the range to reach the nearest Kilrathi colony---",                                  # 23
    "---and the Hellcats stay down until Sparks has looked at the five he touched---",                             # 24
    "---so Prescott must be headed for a rendezvous with a nearby 		Kilrathi ship, and you fly Morningstars.",  # 25
    "Your mission is to recapture or destroy Prescott and the Hellcat.",                                           # 26
    "Whatever happens, it must not fall into the hands of the enemy.",                                             # 27
    "Good hunting, gentlemen.",                                                                                    # 28
    "",                                                                                                            # 29
    "",                                                                                                            # 30
    "Hey, \x86, remember when I told you the Morningstar had capabilities 		I couldn't talk about?",           # 31 Maniac
    "Well, this ship is equipped with a new type of 		missile called the Mace.  It's a tactical nuke!",         # 32
    "There's a nuclear missile in my ship?!",                                                                      # 33 player
    "Yeah!  Great, isn't it?  Listen, the best way to use it---",                                                  # 34 Maniac
    "---is to fire it at the enemy squadron when you first see 'em.",                                              # 35
    "I also use my particle cannons to set it off, once it's out 	of range.",                                     # 36
    "Just don't aim it at anything near you!  Like my Hellcat.  If you see my Hellcat, aim somewhere else.",       # 37
    "I wish I thought you were kidding about this, Todd!",                                                         # 38 player
    "\x86, this is Culloden.",                                                                                    # 39 Flight Ops
    "The Captain is asking if you had any success in finding Prescott?",                                           # 40
    "No such luck, Culloden.  He's vanished.",                                                                     # 41 player
    "He must have met his ride before we got to the tenders at Nav 5.",                                            # 42
    "Unfortunately for us, that gamble seems to have paid off.",                                                   # 43
    "His escape was timed with a coordinated enemy presence 		intended to discourage pursuit.",                 # 44
    "We encountered a hell of a lot of Kilrathi out there.",                                                       # 45
    "I took out \x87, and Maniac blanked.",                                                                       # 46
    "I blanked, but Maniac got his share.",                                                                        # 47
    "I took out \x87, and Maniac got his share.",                                                                 # 48
    "We both blanked, unfortunately.",                                                                             # 49
    "Land immediately, \x86.  Flight Ops out.",                                                                   # 50
]

# Block 10, mission 3-2 Rendezvous.  0-8 Paladin's news, 9-18 Maniac on the
# observation deck, 19-26 the wing commander's office, 27-35 Paladin on the
# radio from the Kirrah'ka.
SERIES_TEXT[10] = [
    "Temporary Duty Office, TCS Culloden.",                                                                        # 0 title
    "I have more news for you, laddie.",                                                                           # 1 Paladin
    "Scorpion, one of my Special Operations agents, went in among the yard's Mandarin technicians sixteen months ago.",  # 2
    "I hadn't heard anything from him for a year, and thought he might be dead...",                                # 3
    "...but he sent us a burst from Ghraffid's Rock.",                                                             # 4
    "From Scorpion's burst and the trader's charts, we've been able to fix that yard.",                            # 5
    "I'm takin' you wi' me to look at it, laddie.  Close.",                                                        # 6
    "Welcome back to the Special Operations Division.",                                                            # 7
    "Now we'll need two other officers for this mission...",                                                       # 8
    "Twenty minutes later...",                                                                                     # 9 caption
    " ",                                                                                                           # 10
    "\x86, Prescott took MY Hellcat.  Mine.  The one with my name stencilled under the canopy.",                  # 11 Maniac
    "Sparks has the other five in pieces looking for whatever he left in them.  Doomsday can't see straight.",     # 12
    "And now the Kilrathi have a Hellcat.  My career is over.",                                                    # 13
    "Believe me, Todd, I know how you feel right now.",                                                            # 14 player
    "But the battle isn't over yet.",                                                                              # 15
    "You've been transferred to Paladin's Special Operations 		Division.  Both of us have.",                   # 16
    "They've fixed the yard.  The Kilrathi call it Ghraffid's Rock.",                                              # 17
    "It is where Prescott took the Hellcat.  And that's where we're headed next.",                                 # 18
    "Wing Commander's Office",                                                                                     # 19 title
    "Maniac, \x86, your orders have arrived from the Captain and from Paladin.",                                  # 20 Valkyrie
    "You will rendezvous with Paladin at the coordinates 		programmed into your Nav computers.",                # 21
    "You will receive further orders from Paladin.",                                                               # 22
    "Good luck, \x86.  And to you, Todd.  Bring my Morningstars back.",                                           # 23
    "Thanks, Valkyrie.  Come on, \x86, let's get moving!",                                                        # 24 Maniac
    "",                                                                                                            # 25
    "",                                                                                                            # 26
    "\x86, this is Paladin.  Good to see ye again, laddie!",                                                      # 27 Paladin
    "Welcome to the newly-rechristened Kirrah'ka.  Formerly the		Kilrathi fleet tender you were kind enough not to shoot.",  # 28
    "I'm sure you'll recognize her, \x86.",                                                                       # 29
    "We'll take this ship to Ghraffid's Rock, since a Waterloo cruiser would 		attract a little too much attention.",  # 30
    "I don't think they'll suspect a visit from their own tender.",                                                 # 31
    "She's got fuel, munitions, repair capabilities, a complete 		Confed interior refitting---",                 # 32
    "---and electronic screening for masking two Morningstar fighters.",                                           # 33
    "Sounds good, James.  We're coming in to dock now.",                                                           # 34 player
    "",                                                                                                            # 35
]

# Block 11, mission 3-3 Jump point.  0-8 aboard the Kirrah'ka (Paladin 16,
# Doomsday 18), 9-19 launch and radio, 20-25 the jump point.
SERIES_TEXT[11] = [
    "",                                                                                                            # 0
    "Now that you're here, we can start for Ghraffid's Rock.",                                                     # 1 Paladin
    "Doomsday will be manning the commo room during your missions.  He has the Kilrathi for it.",                  # 2
    "I just want to contribute somehow to getting 		that Hellcat back.",                                        # 3 Doomsday
    "And get even for what Prescott did on the deck.",                                                             # 4
    "Two good men.  I guess it was just their time to get their tickets punched.  It will be ours soon enough.",   # 5
    "There will be time to remember absent friends later, lad.",                                                   # 6 Paladin
    "Right now, we've work to do.",                                                                                # 7
    "\x86, as soon as we've refuelled your Morningstar, you'll launch again.",                                    # 8
    "	",                                                                                                          # 9
    "Paladin, this is \x86.  All systems green.",                                                                 # 10 player
    "I've programmed the Nav course into the computers.",                                                          # 11 Paladin
    "\x86 and Maniac, you'll fly escort for the Kirrah'ka to the Jump Point.",                                    # 12
    "Paladin, I don't think it'd be a good idea to jump out separately.",                                          # 13 Maniac
    "A tender full of Confed engineers and two Morningstars that can't be seen are no use to anybody apart.",      # 14 Paladin
    "You'll redock with the Kirrah'ka at the Jump Point, Todd.",                                                   # 15
    "That way, we can make sure we'll all get to Ghraffid's Rock.",                                                # 16
    "Let's go.  I want to see this yard.",                                                                         # 17 player
    "I'm just hoping Prescott is out there, somewhere...",                                                         # 18
    "We'll AutoNav on your mark, \x86!",                                                                          # 19 Paladin
    "Paladin, this is \x86.  The Jump Point is secure.",                                                          # 20 player
    "We only had to get past eight fighters to 		get here.",                                                     # 21
    "Two Strakha, four Jalkehi, and two Gothri aces.  I nailed \x87 of 		them, and Maniac got...",              # 22
    "You can update the scoreboard later, lad.  Land an' we'll jump out-system.",                                  # 23 Paladin
    "",                                                                                                            # 24
    "",                                                                                                            # 25
]

# Block 12, mission 4-0 Countersign.  0-19 the yard's security hails the
# tender (38 the yard, 16 Paladin, 18 Doomsday), 20-34 the bridge and
# Scorpion's warning (36 Scorpion, 28 Maniac), 35-49 Paladin's debriefing
# (36-38 the Gothri at the tender, 39-41 the Sartha at Nav 1, 42-45 by kills).
SERIES_TEXT[12] = [
    "Ghraffid's Rock, Hyperion System.",                                                                           # 0 title
    "This is Rock Approach, calling fleet tender Kirrah'ka.",                                                      # 1 the yard
    "H'as aiy'hra n'hakh ri'kahri krikajj, nai korekh sha'yi...",                                                  # 2
    "Please respond with the countersign, Kirrah'ka.",                                                             # 3
    "Doomsday, do ye know what in the hell they're talkin' about, lad?",                                           # 4 Paladin
    "That is the first half of a line from an old Kilrathi war story.  It 		means: \"Beyond the eyes of my enemy---",  # 5 Doomsday
    "---I shall prepare for the day of his destruction.\"",                                                        # 6
    "She's waiting for us to give the countersign.",                                                               # 7 Paladin
    "Kirrah'ka to Rock Approach... responding with countersign...",                                                # 8 Doomsday
    "Kir'kha n'ikh rakh k'har, Sharhi nar Hhallas...",                                                             # 9
    "God, I hope I'm remembering that story correctly!",                                                           # 10
    "I think the next line is:  I, Sharhi of Hhallas, swear to avenge my honour.",                                 # 11
    "I guess we'll find out if I'm wrong real soon now...  I usually am.",                                         # 12
    "Countersign received and confirmed.  You're late, Kirrah'ka!  Where 	is Captain Hrakthrai?",               # 13 the yard
    "We had engine problems.  The Captain is supervising 		the efforts to repair them.",                         # 14 Doomsday
    "Our approach will be delayed for at least an hour.",                                                          # 15
    "Understood.  Keep us informed of your situation, Kirrah'ka.  Rock 		Approach out.",                         # 16 the yard
    "Well, we survived that!  What now, Paladin?",                                                                 # 17 Doomsday
    "Now we do what we came here to do, lad.  Look at the yard...",                                                # 18 Paladin
    "...and then get out of here alive afterwards.",                                                               # 19
    "                       ",                                                                                     # 20
    "It's going well, James.  No sign that the yard knows 	who we are.",                                           # 21 player
    "Doomsday is going to be monitoring the VDU for more burst transmissions 		from Scorpion.",                  # 22 Paladin
    "If we're lucky, we'll get the information we need quickly.",                                                  # 23
    "Paladin, this is Doomsday.  You'd better come to the Bridge 	immediately, sir.",                              # 24 Doomsday
    "Five minutes later...",                                                                                       # 25 caption
    "We're picking up something right now, Major Taggart.",                                                        # 26 Doomsday
    "Paladin, this is Scorpion.  The yard is scrambling two wings of fighters.",                                   # 27 Scorpion
    "They've detected some sort of electronic screen on your ship, and 		they want a closer look at you.",      # 28
    "The electronic masking of your two Morningstars won't stand up at 		close range.",                         # 29 Paladin
    "If they're already onto us, it doesn't make any difference anyway.",                                          # 30 player
    "We'll go out and deal with the first wing---",                                                                # 31
    "---then chase down the second wing before they can report in.",                                               # 32
    "Then we'd probably get out of here before they scramble any more fighters.",                                  # 33
    "That's right.  But first, we'll give these guys a personal demonstration 		of what a Morningstar can do!",  # 34 Maniac
    "We've won another round, lads!  Good work!",                                                                  # 35 Paladin
    "We encountered five Gothri right outside the front door.",                                                    # 36 player
    "They're all history.",                                                                                        # 37
    "We couldn't get them all, unfortunately.",                                                                    # 38
    "At Nav 1 there were five more Sartha.",                                                                       # 39
    "None of those guys put up much of a fight.",                                                                  # 40
    "They put up a good fight.  I don't think we nailed them all.",                                                # 41
    "I took down \x87 of them.  Maniac blanked.",                                                                 # 42
    "I didn't take any of them down.  Damn them all!  Maniac nailed his share.",                                   # 43
    "I took down \x87 of them.  Maniac nailed his share.",                                                        # 44
    "We couldn't nail any of them, but it looked like they were	 retreating.",                                    # 45
    "You'd better dock immediately, lads. ",                                                                       # 46 Paladin
    "We're leaving this neighbourhood now, before reinforcements	 can arrive.",                                   # 47
    "No arguments from us about that, James!",                                                                     # 48 player
    "",                                                                                                            # 49
]

# Block 13, mission 4-1 Ceremony.  0-17 the bridge: Scorpion's last burst
# (36), the Culloden's answer (4), Doomsday 18, Paladin 16; 18-24 the launch;
# 25-37 the debriefing (26-29 Nav 1, 30-33 Nav 2, 34 kills).
SERIES_TEXT[13] = [
    "We're picking up another transmission from Scorpion, sir...",                                                 # 0 Doomsday
    "The entire yard is under security alert.  They're searching for the 		source of these transmissions.",    # 1 Scorpion
    "I don't have much time, Paladin.  Listen closely---",                                                         # 2
    "Ghraffid's Rock is not a yard.  It is the yard: the first hull of the new carrier class is in the frame here.",  # 3
    "Every slip, every dock, every Mandarin technician they bought is 		here, under the rock.",                 # 4
    "Because of the security risks, they've never brought the fleet's lords 	here together---",                   # 5
    "---until now.  The keel ceremony is tomorrow.  Ghraffid and the admirals are all here.  This is your chance to---",  # 6
    "They're jamming Scorpion's transmissions.  He's been cut off.",                                               # 7 Doomsday
    "Alert the Culloden of the situation.  Scramble the transmission.",                                            # 8 Paladin
    "Special Operations Unit calling TCS Culloden.  We have located 		the yard, and its guests.",               # 9 Doomsday
    "I'm afraid you're on your own, Paladin.  We have our own situation here---",                                  # 10 Banbridge
    "The Culloden reports that she is about to come under attack.",                                                # 11 Doomsday
    "They just sealed off the flight deck for repairs.",                                                           # 12
    "They won't be able to scramble any fighters!",                                                                # 13
    "They're beyond our help.  By the time we can get back, 		it'll be over one way or another.",               # 14 Paladin
    "We must concentrate on our own problems here.",                                                               # 15
    "If the yard traced the destination of Scorpion's tight-beam 	communications, they will be here soon.",       # 16
    "\x86, you and Maniac had better launch immediately.",                                                        # 17
    "\x86, this is Paladin, do you read?",                                                                        # 18 Paladin
    "I'm setting course for open space---",                                                                        # 19
    "---you and Maniac had better make sure we're not intercepted.",                                               # 20
    "Do they have a fix on our location?",                                                                         # 21 Maniac
    "I'm sure of it.  Those bastards are jamming our communications now.",                                         # 22 Paladin
    "And I'm detecting ships approaching from two directions.",                                                    # 23
    "Ye'd better move fast, lads, or this is going to be a 	very short mission.  Paladin out.",                   # 24
    "\x86, this is Paladin.  Your report, lad?",                                                                  # 25 Paladin
    "There were four Sartha at Nav 1.",                                                                            # 26 player
    "None of them survived.",                                                                                      # 27
    "We couldn't stop all of them.",                                                                               # 28
    "Didn't touch base at Nav 1.",                                                                                 # 29
    "We knocked out two Fralthra cruisers at Nav 2, James.  The guests' escort.",                                  # 30
    "We knocked out one Fralthra cruiser at Nav 2, James.  The other one	is still out there somewhere.",         # 31
    "We couldn't knock out either of those two Fralthra cruisers at Nav 2, James.",                                # 32
    "We couldn't make it to Nav 2, James.",                                                                        # 33
    "I got \x87, Maniac took the rest.",                                                                          # 34
    "There'll be more of them.  Damn, but I still can't get 	through to the Culloden.",                           # 35 Paladin
    "We're on our own now, lads.",                                                                                 # 36
    "You are cleared to land.",                                                                                    # 37
]

# Block 14, mission 4-2 The yard.  0-14 the bridge: Scorpion (36), then
# Ghraffid (58); 15-29 the decision and the launch; 30-43 the debriefing
# (31-33 the yard stands, 35-42 the yard destroyed).
SERIES_TEXT[14] = [
    "Damn it, we're still two hours from open space.",                                                             # 0 Paladin
    "I don't think we're going to make it, lads.",                                                                 # 1
    "Paladin, I'm receiving a tight-beam comm from the Rock...",                                                   # 2 Doomsday
    "Scorpion to Kirrah'ka.  Do you read me?",                                                                     # 3 Scorpion
    "Loud and clear, Scorpion.",                                                                                   # 4 Doomsday
    "I don't think they can find me now.  I should be safe to transmit 		for a while.",                         # 5 Scorpion
    "They've moved the ceremony up.  The lords are going aboard the hull tonight.",                                # 6
    "Everyone's certain that the Culloden is on her way here.",                                                    # 7
    "I wish that was true, Scorpion.  But---",                                                                     # 8 Doomsday
    "Not over an unsecured channel, Doomsday!  You could...",                                                      # 9 Paladin
    "Too late for caution, Taggart.  You should hire better underlings.",                                          # 10 Ghraffid
    "Kalralahr Ghraffid!",                                                                                         # 11 Paladin
    "Commander Prescott, you may execute that Terran spy now.",                                                    # 12 Ghraffid
    "You can still surrender, Taggart.  All of you are more valuable 	to us alive than dead.",                    # 13
    "Go to hell, Ghraffid!  Cut the transmission, Doomsday.",                                                      # 14 Paladin
    "                       ",                                                                                     # 15
    "We have two choices before us, lads...",                                                                      # 16 Paladin
    "We can run, or we can stand and fight.",                                                                      # 17
    "We have heard nothing from the Culloden since her last transmission.",                                        # 18
    "For all we know she has been destroyed.",                                                                     # 19
    "The way I see it, we have but one choice.",                                                                   # 20 player
    "To do what this ship came eleven jumps to do!",                                                               # 21
    "Destroying the yard won't be easy, \x86.  Two Morningstars, two Maces, and every gun they own.",             # 22 Paladin
    "I can't order you to fly that mission.",                                                                      # 23
    "I'll volunteer.",                                                                                             # 24 Maniac
    "If you want a reason why I want to go, I'll give you one.",                                                   # 25 player
    "Scorpion.  And a man called Prescott.",                                                                       # 26
    "Five minutes later...",                                                                                       # 27 caption
    "I'm setting course for the Rock, Paladin.",                                                                   # 28 player
    "Good luck, \x86!",                                                                                           # 29 Paladin
    "Paladin, this is \x86.",                                                                                     # 30 player
    "We couldn't get close enough to destroy the yard.",                                                           # 31
    "You'd better land immediately, \x86.",                                                                       # 32 Paladin
    "I just hope we can make it to open space in time.",                                                           # 33
    "Paladin, this is \x86.",                                                                                     # 34 player
    "We succeeded in destroying the yard.  The hull went with it.",                                                # 35
    "We could see the fireworks from here, \x86!",                                                                # 36 Paladin
    "They had five Sabres and two Kamekhs defending the Rock.",                                                    # 37 player
    "And the Rock had some good defences of its own.",                                                             # 38
    "I took out \x87, and Maniac accounted for the rest.",                                                        # 39
    "Any sign of Prescott, or the Hellcat?",                                                                       # 40 Paladin
    "No sign of them at all.",                                                                                     # 41 player
    "I'm on final docking approach, Paladin.",                                                                     # 42
    "		",                                                                                                        # 43
]

# Block 15, mission 4-3 Duel.  0-5 the Culloden's bridge (Valkyrie 2,
# Banbridge 4); 6-24 the Kirrah'ka: Prescott's two transmissions (46), 7-14
# the taunt, 15-22 the pod; 25-33 Paladin's course; 34-43 the Captain if
# Prescott escaped, 44-50 if he died; 51-64 the Kilrathi epilogue; 65 blank;
# 66-68 Paladin and Maniac at the launch.
SERIES_TEXT[15] = [
    "The Bridge, TCS Culloden.",                                                                                   # 0 title
    "The Kamekh is making its attack approach now.",                                                               # 1 Valkyrie
    "Prepare to fire the main gun.",                                                                               # 2 Banbridge
    "But Captain, we're only at 60 percent power.  The plant could blow.",                                         # 3 Valkyrie
    "You heard the order, Colonel.  Fire the main gun!",                                                           # 4 Banbridge
    "",                                                                                                            # 5
    "Meanwhile...",                                                                                                # 6 caption
    "I think we still have a chance to jump out of here alive, but---",                                            # 7 Paladin
    "Paladin, I'm receiving a transmission from the Rock!",                                                        # 8 Doomsday
    "\x86, this is Prescott.",                                                                                    # 9 Prescott
    "I knew you would be here!  Your mission wasn't a total failure---",                                           # 10
    "---the Kalralahr will have to move his yard before your Culloden comes back with a fleet.",                   # 11
    "But you probably won't live that long.  I have your Hellcat, \x86, and I know what it does.",                # 12
    "And this time, nobody's hand is on the switch but mine.",                                                     # 13
    "Better start running, \x86.  You don't have much time left.",                                                # 14
    "Meanwhile...",                                                                                                # 15 caption
    "Paladin, I'm receiving a transmission from the Rock!",                                                        # 16 Doomsday
    "\x86, this is Prescott.",                                                                                    # 17 Prescott
    "Prescott, can you hear me?  Your signal is weak.",                                                            # 18 player
    "I'm in an escape pod.  Ghraffid's people took the Hellcat and left me to the radiation 	---all systems failing---",  # 19 Prescott
    "They have the Hellcat, \x86.  One of his aces flies it now.",                                                # 20
    "They left me here to die.  I gave them everything and they left me.  Kill him for me, \x86!  Kill him---",   # 21
    "We've lost transmission.",                                                                                    # 22 Doomsday
    "So... the Hellcat is still out there.  Ready to fly another mission, 	Maniac?",                              # 23 player
    "Against my own Hellcat?  Any time, \x86.",                                                                   # 24 Maniac
    "Paladin, this is \x86.  Have you tracked the Hellcat yet?",                                                  # 25 player
    "No sign of it yet, \x86.  But I know how much Ghraffid wants you dead---",                                   # 26 Paladin
    "---I'd wager it won't leave until it's made sure of you.",                                                     # 27
    "So it's probably somewhere between us and the Jump Point.",                                                   # 28
    "I'm transmitting a Nav course to you now.",                                                                   # 29
    "If you don't meet it, jump back to the Culloden.",                                                            # 30
    "They've signalled us that they've taken care of their trouble and have the flight deck open again.",         # 31
    "I'll follow you there.",                                                                                      # 32
    "Providing the Morningstars' jump drives work.",                                                               # 33 Maniac
    "",                                                                                                            # 34
    "",                                                                                                            # 35
    "The Bridge, TCS Culloden.",                                                                                   # 36 title
    "I heard from Paladin that the Hellcat got away from you, \x86.",                                             # 37 Banbridge
    "It's probably on a Kilrathi deck even as we speak, with every one of its secrets.",                           # 38
    "We won't have a chance to catch it again.",                                                                   # 39
    "I know you did your best, \x86... but this time, that wasn't enough.",                                       # 40
    "Dismissed.",                                                                                                  # 41
    "",                                                                                                            # 42
    "",                                                                                                            # 43
    "The Bridge, TCS Culloden.",                                                                                   # 44 title
    "Good work, Colonel!  I knew that Hellcat wouldn't get away from you twice.",                                  # 45 Banbridge
    "And the yard is destroyed, Captain.  The hull with it.",                                                      # 46 player
    "Thank you, sir.  I'm sorry to report that we couldn't destroy	 the yard.",                                   # 47 player
    "But Ghraffid cannot keep it where it is now that we have seen it, sir.",                                      # 48
    "I'm sending a commendation to Confed High Command for your work 	on this cruise, \x86.",                     # 49 Banbridge
    "Once again, you've proved yourself to be a true hero!",                                                       # 50
    "Two days later...",                                                                                           # 51 caption
    "Kabla Meth, Kilrathi Sector Command, Deneb Sector.",                                                          # 52 title
    "My... Heir.",                                                                                                 # 53 Emperor
    "Is it true that you failed to capture the Terrans' new fighter 		intact---",                               # 54
    "---and you personally lost the first hull of my carriers.",                                                   # 55
    "---and you also allowed the Culloden to see my yard and 		live?",                                           # 56
    "The Mandarins were about to outlive their usefulness to us.",                                                 # 57 Thrakhath
    "And while the hull is lost, we did keep the 		attentions of one cruiser well occupied.",                    # 58
    "By doing so, we were able to hold the Deneb frontier, and 		the Terrans learned nothing they can use.",    # 59
    "Even as we speak, the Culloden is running for home with her 		tail between her legs.",                     # 60
    "Grandson---",                                                                                                 # 61 Emperor
    "---I am not pleased.  But I am patient.",                                                                     # 62
    "Lay down another keel.  And find me another Prescott.",                                                       # 63
    "",                                                                                                            # 64
    "",                                                                                                            # 65
    "Maniac, get a move on lad!",                                                                                  # 66 Paladin
    "I'm going, I'm going!",                                                                                       # 67 Maniac
    "Damn!",                                                                                                       # 68
]

# Block 16, mission 5-0 The assault (a lost exit).  0-11 Kilrathi command,
# 12-19 the briefing room, 20-38 the debriefing (deck lines, then Valkyrie:
# 25-28 Nav 1, 29-31 Nav 2, 32-34 Nav 3, 35-37 kills), 39-48 the epilogue.
SERIES_TEXT[16] = [
    "Kabla Meth, Kilrathi Sector Command, Deneb Sector.",                                                          # 0 title
    "Rise, grandson---",                                                                                           # 1 Emperor
    "What is the current status of the Terran cruiser?",                                                           # 2
    "Since the relay found her, the Culloden has not come within two jumps 		of the yard.",                     # 3 Thrakhath
    "Surely, there must be some ship brave enough to dare us?",                                                    # 4 Emperor
    "There is only one.  The Culloden.  And she knows she is hunted.",                                             # 5 Thrakhath
    "Culloden-?  That is the ship of the pilot \x85 who gave us so 		much difficulty?",                         # 6 Emperor
    "Yes, always an unstabilizing influence.  We should eliminate him 		and the Culloden.",                     # 7 Thrakhath
    "But what of the new fighter?",                                                                                # 8 Emperor
    "We can always acquire the Hellcat through other 		means.  The Mandarins have more Prescotts.",             # 9 Thrakhath
    "Very well.  If the presence of the Culloden disturbs you, 		grandson---",                                  # 10 Emperor
    "---you may destroy it.",                                                                                      # 11
    "Briefing Room, TCS Culloden.",                                                                                # 12 title
    "	",                                                                                                          # 13
    "We have detected a large scale Kilrathi assault force making an 		approach towards the Culloden.",         # 14 Valkyrie
    "There is no doubt that this is the spearhead of an operation intended to finish us before we reach the yard.",  # 15
    "The Captain has given the order.  We turn for home at the next jump.",                                        # 16
    "Until then, and until the danger has passed, we remain on 		full alert.",                                  # 17
    "\x86, you and Stingray will fly the first patrol.  Good luck.",                                              # 18
    "",                                                                                                            # 19
    "Outstanding, Colonel!",                                                                                       # 20 deck
    "Looks like it was a little rough out there, sir.",                                                            # 21
    "I'll do my best to put her back together, sir.",                                                              # 22
    "	",                                                                                                          # 23
    "How was your patrol, \x86?",                                                                                 # 24 Valkyrie
    "At Nav 1, there were three Grikath moving closer to the Culloden.",                                           # 25 player
    "None of them survived.",                                                                                      # 26
    "Couldn't nail all of them, Valkyrie.",                                                                        # 27
    "Couldn't make it to Nav 1 on this patrol.",                                                                   # 28
    "What about Nav 2?",                                                                                           # 29 Valkyrie
    "There were four Kamekhs escorted by three Gothri aces.",                                                      # 30 player
    "Too much interference... I couldn't make it to Nav 2.",                                                       # 31
    "Do you have anything else to report?",                                                                        # 32 Valkyrie
    "Two Sartha at Nav 3.",                                                                                        # 33 player
    "Didn't encounter anything else on this patrol.",                                                              # 34
    "All in all, I got \x87 enemy ships.",                                                                        # 35
    "At least I managed to take down one of those enemy ships.",                                                   # 36
    "And I blanked completely, it wasn't my night.",                                                               # 37
    "Thank you for your report, \x86.  Get some sleep.  Tomorrow we jump for home.",                              # 38 Valkyrie
    "Two days later...",                                                                                           # 39 caption
    "Kabla Meth, Kilrathi Sector Command, Deneb Sector.",                                                          # 40 title
    "You summoned me, my Emperor?",                                                                                # 41 Thrakhath
    "Give me news of the cruiser, grandson.",                                                                      # 42 Emperor
    "She has turned for home, my lord!  The yard is safe and the keel is laid.",                                   # 43 Thrakhath
    "Even as we speak, the Culloden is crawling back along the jump chain 		she came by.",                      # 44
    "Grandson---",                                                                                                 # 45 Emperor
    "---I am pleased!",                                                                                            # 46
    "Now finish my carrier.  The Terrans will see it soon enough.",                                                # 47
    "",                                                                                                            # 48
]

# Block 17, mission 5-1 Picket line (a lost exit).  0-6 a Mandarin reports
# to Ghraffid (58 / 200), 7-17 the briefing room, 18-38 the wing commander's
# office (23-24 Nav 1, 27-31 Nav 2, 33 Nav 3), 39-48 the epilogue.
SERIES_TEXT[17] = [
    "It has been some time since your last communication.  Report your 		progress.",                            # 0 Ghraffid
    "Everything has gone according to schedule, so far.",                                                          # 1 the Mandarin
    "So far?  You sound as if you anticipate a problem.",                                                          # 2 Ghraffid
    "Only one.  \x86.  He seems highly intuitive, and he 		possesses extraordinary combat skill.",              # 3 the Mandarin
    "He may jeopardize my escape plans.",                                                                          # 4
    "Colonel \"\x86\" \x85, the Hero of K'Tithrak Mang.  Does he fly regular patrols now?",                     # 5 Ghraffid
    "Yes.  I am transmitting the time and coordinates of his next patrol 		to you now...",                      # 6 the Mandarin
    "Briefing Room, TCS Culloden.",                                                                                # 7 title
    "I have new patrol assignments for everyone...",                                                               # 8 Valkyrie
    "These will be standard patrols, but today they will have added 		significance.",                           # 9
    "The Captain has decided.  Without the charts and without the Hellcats' secret, this raid is over.",           # 10
    "We hold this system until the jump drive is charged, and then we go home the way we came.",                   # 11
    "Until the drive is up---",                                                                                    # 12
    "We are all that stands between the Kilrathi and a cruiser that cannot run.",                                  # 13
    "Keeping a clear track of our enemy is vital.",                                                                 # 14
    "\x86, you and Downtown will patrol and clear your 		Nav Points.",                                          # 15
    "Good luck.  Dismissed.",                                                                                      # 16
    "",                                                                                                            # 17
    "Welcome back, sir!",                                                                                          # 18 deck
    "Looks like it was a tough fight, sir!",                                                                       # 19
    "Glad you made it back alive, sir!",                                                                           # 20
    "Wing Commander's Office, TCS Culloden.",                                                                      # 21 title
    "How was your patrol, \x86?",                                                                                 # 22 Valkyrie
    "Hard work.  At Nav 1 there were two Jalkehi waiting in the 		asteroid field.",                               # 23 player
    "I detoured around Nav 1... something didn't feel right about it...",                                          # 24
    "\x86, now I must send out another patrol!  Why must you disregard 	orders this way?",                       # 25 Valkyrie
    "What did you find at Nav 2?",                                                                                 # 26
    "Five Sartha and one Fralthra.",                                                                               # 27 player
    "The Fralthra is history.",                                                                                    # 28
    "The Fralthra escaped.",                                                                                       # 29
    "I will alert the Captain... we cannot risk an attack on the Culloden with the drive down.",                   # 30 Valkyrie
    "I couldn't make it to Nav 2 on this patrol.",                                                                 # 31 player
    "Was there any enemy presence at Nav 3?",                                                                      # 32 Valkyrie
    "A solo Gothri ace.  He was not a problem.",                                                                   # 33 player
    "This was very strange, \x86...",                                                                             # 34 Valkyrie
    "For days, there has been a minimal enemy presence in this area---",                                           # 35
    "But suddenly, they knew where you would be.  To the minute.",                                                 # 36
    "It's suspicious, all right.  Prescott was not the only one.",                                                 # 37 player
    "I am disappointed in your performance on this patrol, \x86.  	Dismissed.",                                  # 38 Valkyrie
    "Two days later...",                                                                                           # 39 caption
    "Kabla Meth, Kilrathi Sector Command, Deneb Sector.",                                                          # 40 title
    "You summoned me, my Emperor?",                                                                                # 41 Thrakhath
    "Give me news of the cruiser, grandson.",                                                                      # 42 Emperor
    "She has turned for home, my lord!  The yard is safe and the keel is laid.",                                   # 43 Thrakhath
    "Even as we speak, the Culloden is crawling back along the jump chain 		she came by.",                      # 44
    "Grandson---",                                                                                                 # 45 Emperor
    "---I am pleased!",                                                                                            # 46
    "Now finish my carrier.  The Terrans will see it soon enough.",                                                # 47
    "",                                                                                                            # 48
]

# Block 18, mission 5-2 Blown (a lost exit).  0-13 Ghraffid's Rock: Prescott
# (12) and the Mandarin courier who flew him out (10); 14-23 the Kirrah'ka
# and the launch; 24-38 Paladin's debriefing (27-30 Nav 1, 31-34 Nav 2);
# 39-48 the epilogue; 49-55 Paladin if the way is clear.
SERIES_TEXT[18] = [
    "Ghraffid's Rock, Hyperion System.",                                                                           # 0 title
    "You've done well, Captain.",                                                                                  # 1 Prescott
    "And now that my plan has succeeded---",                                                                       # 2
    "---and I've brought the Hellcat and its key to the Kilrathi---",                                              # 3
    "---I'm guaranteed a high-ranking position in the post-war 		Kilrathi regime.",                             # 4
    "But I was the one who flew you off that tender and brought you here!",                                        # 5 the courier
    "You had absolutely nothing to do with it, Commander!  Why should you 		get all the reward?!",              # 6
    "Please--- Call me Lyle.",                                                                                     # 7 Prescott
    "I'm afraid you're only hired help, Captain.  Not a sworn Mandarin.",                                          # 8
    "I've served the cause for over ten years.  The Kalralahr 	himself gave me the Hellcat to fly.",              # 9
    "If you became more involved in our cause, there could be a 		place for you as well, Captain.",             # 10
    "\"Captain\" and \"Commander\" sound so formal, Lyle... you can 	call me Nadia.",                            # 11 the courier
    "Anytime, sweetheart...",                                                                                      # 12 Prescott
    "I think this is going to be the start of a beautiful 		friendship, Lyle.",                                  # 13 the courier
    "I have the Rock on my sensors.  It has just activated all 		defence systems.",                              # 14 Doomsday
    "---And I'm picking up chatter from at least six other craft on an 		intercept course with us.",            # 15
    "If they spot us, our whole mission is blown.",                                                                # 16 Paladin
    "I can jump the Kirrah'ka further out-system to throw them off our trail.",                                    # 17
    "But we can't trust the jump drives of those Morningstars...",                                                 # 18
    "Maniac and I will run their gauntlet and rejoin you after you jump.",                                         # 19 player
    "You'd better hurry!",                                                                                         # 20 Doomsday
    "Five minutes later...",                                                                                       # 21 caption
    "James, this is \x86, do you read?",                                                                          # 22 player
    "We'll meet at Nav 3, laddie!",                                                                                # 23 Paladin
    "Paladin, this is \x86, do you read?",                                                                        # 24 player
    "I don't think they like visitors in the Hyperion System, lad!",                                               # 25 Paladin
    "I picked up communications from a Mandarin squadron in this area.",                                           # 26
    "That was probably the wing of five Sabres at Nav 1.",                                                         # 27 player
    "We took all of them out.  None of them survived to report on our	presence here.",                           # 28
    "We couldn't take all of them out, James.  The Rock will	know that we're here.",                               # 29
    "We didn't encounter a large squadron.  Must've missed them.",                                                 # 30
    "And we ran into two Broadswords at Nav 2.",                                                                   # 31
    "They're both history.",                                                                                       # 32
    "We couldn't ice both of them.",                                                                               # 33
    "We couldn't check out Nav 2, Paladin.",                                                                       # 34
    "Damnation, laddie!  Those ships will report back in---",                                                      # 35 Paladin
    "---we'll have to jump out-system before the reinforcements arrive.  This raid is over.",                      # 36
    "Ye'd better dock immediately, \x86.",                                                                        # 37
    "",                                                                                                            # 38
    "Two days later...",                                                                                           # 39 caption
    "Kabla Meth, Kilrathi Sector Command, Deneb Sector.",                                                          # 40 title
    "You summoned me, my Emperor?",                                                                                # 41 Thrakhath
    "Give me news of the cruiser, grandson.",                                                                      # 42 Emperor
    "She has turned for home, my lord!  The yard is safe and the keel is laid.",                                   # 43 Thrakhath
    "Even as we speak, the Culloden is crawling back along the jump chain 		she came by.",                      # 44
    "Grandson---",                                                                                                 # 45 Emperor
    "---I am pleased!",                                                                                            # 46
    "Now finish my carrier.  The Terrans will see it soon enough.",                                                # 47
    "",                                                                                                            # 48
    "You took care of all of those ships, so it's probably safe	to continue our mission.",                         # 49 Paladin
    "With luck, they'll send out reinforcements to scout our 	old location---",                                    # 50
    "---which'll give us enough time to go in close for a flyby.",                                                 # 51
    "And I'll scan them for all the data I can.",                                                                  # 52
    "After we get enough information, we'll jump out again.",                                                      # 53
    "Affirmative, Paladin.",                                                                                       # 54 player
    "",                                                                                                            # 55
]

# Block 19, mission 5-3 The last patrol.  0-10 the Captain's day cabin,
# 11-30 the wing commander's office (16 the yard destroyed, 17 not), 31-45
# the bridge (36-39 all navs cleared, 40-44 not), 46-55 the epilogue.
SERIES_TEXT[19] = [
    "The Captain's Day Cabin, TCS Culloden.",                                                                      # 0 title
    "It's good to have you back aboard, 		Colonel \x85.  The tender did well by you.",                          # 1 Banbridge
    "Thank you, Captain.  Has there been any news from 	home?",                                                   # 2 player
    "Unfortunately, yes.  Since Enigma, we have been losing ground 		everywhere else.",                          # 3 Banbridge
    "The Kilrathi have made steady advances against our forces.",                                                  # 4
    "The Sixth Fleet is engaged near the sector border, and nobody is coming eleven jumps to fetch us.",            # 5
    "I requested that Confed High Command send a relief---",                                                       # 6
    "---but we haven't received a reply transmission from them yet.",                                              # 7
    "So we go home the long way, as we came.  A cruiser with six Hellcats and no spares.",                          # 8
    "If we lose this ship out here, nobody will ever know what you did at that yard...",                           # 9
    "This war is far from over, sir.  I won't give up hope yet.",                                                  # 10 player
    "Wing Commander's Office, TCS Culloden.",                                                                      # 11 title
    "I am glad you are safe, \x86.  Both of you.",                                                                # 12 Valkyrie
    "I feel I let you down, Valkyrie.",                                                                            # 13 player
    "By now, the Hellcat is on a Kilrathi deck, and every one of its secrets with it.",                            # 14
    "Apart from that, \x86, your mission was a success.",                                                         # 15 Valkyrie
    "Now that the Kilrathi have lost their yard and their first hull---",                                          # 16
    "Now that we have seen the yard and know where it lies---",                                                    # 17
    "---their new carrier is years further off than it was.",                                                      # 18
    "And we've received our orders from the Captain.",                                                             # 19
    "The Culloden goes home.  The long way, through every picket that watched us come.",                           # 20
    "What!  We can't make it back the way we came, Valkyrie!",                                                     # 21 player
    "I'm afraid we must, \x86.",                                                                                  # 22 Valkyrie
    "I'm sending out patrols to clear our way to the Jump Point---",                                               # 23
    "---and then we will run for the Confederation with all possible speed.",                                      # 24
    "You've flown very difficult missions these last few days, \x86.",                                            # 25
    "I'll assign another pilot to fly the patrol.",                                                                # 26
    "No.  I want to fly the last patrol.",                                                                         # 27 player
    "I want to take one last look at the place where we lost a Hellcat and 	nearly lost everything else.",        # 28
    "",                                                                                                            # 29
    "",                                                                                                            # 30
    " ",                                                                                                           # 31
    " ",                                                                                                           # 32
    " ",                                                                                                           # 33
    "The Bridge, TCS Culloden.",                                                                                   # 34 title
    "Your report, Colonel?",                                                                                       # 35 Banbridge
    "All Nav Points cleared, Captain.",                                                                            # 36 player
    "Good work, \x86.  Colonel Bergstrom, signal Jump Alert throughout 	the ship.",                               # 37 Banbridge
    "We leave this sector now, \x86, but I know we'll 	be back.",                                                 # 38
    "And next time, we will not come alone.",                                                                      # 39
    "I wasn't able to clear all the Nav Points, Captain.",                                                         # 40 player
    "We'll have to jump out, no matter the risk.",                                                                 # 41 Banbridge
    "Colonel Bergstrom, signal Jump Alert throughout the ship.",                                                   # 42
    "We leave this sector now, \x86, but I know we'll	be back.",                                                  # 43
    "And next time, we will not come alone.",                                                                      # 44
    "		",                                                                                                        # 45
    "Two days later...",                                                                                           # 46 caption
    "Kabla Meth, Kilrathi Sector Command, Deneb Sector.",                                                          # 47 title
    "You summoned me, my Emperor?",                                                                                # 48 Thrakhath
    "Give me news of the cruiser, grandson.",                                                                      # 49 Emperor
    "She has jumped for home, my lord.  One cruiser.  She will be a long time getting there.",                     # 50 Thrakhath
    "Even as we speak, every picket on the chain is 		waiting for her.",                                         # 51
    "Grandson---",                                                                                                 # 52 Emperor
    "---I am not pleased.  But I am patient.",                                                                     # 53
    "Lay down another keel.  And this time, build it where no Terran has ever looked.",                            # 54
    "",                                                                                                            # 55
]

# Blocks 20-23: shared scenes.  20 is the designers' stub; 21 the eject
# scenes (38 Flight Ops, 8 the rescue pilot, 30 the ship); 22 the lost
# ending in the Captain's day cabin (4 Banbridge, 2 Valkyrie); 23 the loss
# of the carrier.
SERIES_TEXT[20] = ["Special Ops 3, Series 1, Mission A, Talk1:  Conversation", "Special Ops 3,  Series 1, Mission A, Talk1:  Briefing", "Special Ops 3, Series 1, Mission A, Talk3: Debrief"]
SERIES_TEXT[21] = [
    "\x85, we are reading a distress beacon.  What is your situation?",
    "My wingman ejected.",
    "Search and Rescue is on the way, \x85... continue your mission alone.",
    "Affirmative.  \x85 out.",
    " ",
    "Culloden, this is Rescue Patrol, do you read?",
    "I've retrieved an ejected pilot.  Requesting ACLS linkage.",
    "Affirmative, Rescue Patrol, you are cleared to land.",
    " ", " ", "",
]
SERIES_TEXT[22] = [
    "The Captain's Day Cabin, TCS Culloden.",
    "At ease, Colonel Bergstrom.  I've made my decision.",
    "Because of the losses of people and ships in the last days---",
    "---we turn for Confederation space immediately.",
    "But sir, the yard is still out there!  We know where it is!",
    "And what about the Hellcats?  The programme, everything they were sent here to prove?",
    "Colonel, a cruiser that cannot fight her way to the yard cannot fight her way home either.  Home needs us more.",
    "The Kilrathi are massing for an attack so large their forces may 			reach Earth itself.",
    "But what about the Sixth Fleet?  They need every hull that can still 			fight.",
    "Rhea, there's not enough left of us to constitute a decent raid, 			let alone a fighting force.",
    "We've lost here.",
    "As the Culloden turns for the long road home---",
    "---the yard behind her lays down its keel, and the Kilrathi learn nothing of what she carried.",
    "    ", "",
]
SERIES_TEXT[23] = [
    "Culloden pilot, we've lost shields... \n decompressing fast---",
    "I'm ejecting a data capsule... \n get it to Paladin at all costs!",
    "",
    "With the destruction of your carrier, you drift helplessly through the void...",
    "With the destruction of your carrier, you drift helplessly through the void...",
    "With the destruction of your carrier, you drift helplessly through the void...",
    "With the destruction of your carrier, you drift helplessly through the void...",
    "With the destruction of your carrier, you drift helplessly through the void...",
    "With the destruction of your carrier, you drift helplessly through the void...",
    "",
]
