#!/bin/bash
# SO3 scenes with our faces: s1m1's briefing (Valkyrie = Bergstrom), s1m0's debriefing (the Captain = Banbridge)
S=${S:-$(cd "$(dirname "$0")" && pwd)/out}
cd $S
L="6:!mouse=0.48:0.55,8:!button=0:1,8.3:!button=0:0,12:!mouse=0.48:0.55,13:!button=0:1,13.3:!button=0:0"
rm -rf fc11 fc10
GAME=so3 GAMEDIR=/home/sandbox/dev/dosbox3d/build-so3/game WCNET=0 WCPORT=13621 WCNET_KEYSCRIPT_FROM=boot DIRECT=" " $(dirname "$0")/w2solo.sh $S/fc11 34 "1 1" "$L,16:!shot=b16,19:!shot=b19,22:!shot=b22,25:!shot=b25,28:!shot=b28,31:!shot=b31" > /dev/null 2>&1
GAME=so3 GAMEDIR=/home/sandbox/dev/dosbox3d/build-so3/game WCNET=0 WCPORT=13622 WCNET_KEYSCRIPT_FROM=boot DIRECT=" " $(dirname "$0")/w2solo.sh $S/fc10 80 "1 0" "$L,42:!status=1,50:!shot=d50,54:!shot=d54,58:!shot=d58,62:!shot=d62,66:!shot=d66,70:!shot=d70,74:!shot=d74" > /dev/null 2>&1
for f in fc11/*.ppm fc10/*.ppm; do convert "$f" "${f%.ppm}.png"; done 2>/dev/null
echo "== facecheck done"
