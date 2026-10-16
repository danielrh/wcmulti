#!/bin/bash
# Every SO3 mission once with two instances: launch, autopilot to the first nav, memory, a screenshot.
S=${S:-$(cd "$(dirname "$0")" && pwd)/out}
cd $S
: > $S/spawntest.log
port=13520
for sm in "1 0" "1 1" "1 2" "1 3" "2 0" "2 1" "2 2" "2 3" "3 0" "3 1" "3 2" "3 3" "4 0" "4 1" "4 2" "4 3" "5 0" "5 1" "5 2" "5 3"; do
  set -- $sm; d=st$1$2; rm -rf $d; port=$((port+1))
  GAME=so3 GAMEDIR=/home/sandbox/dev/dosbox3d/build-so3/game WCPORT=$port ./w2duo.sh $S/$d 55 "$1 $2" "14:a,30:!mem,32:!shot=h32,36:a,50:!mem" "14:a,36:a" > /dev/null 2>&1
  {
    echo "== s$1 m$2"
    grep -h "spawn produced\|left slot\|screen:\|free main memory\|despawn slot .* explode=1\|unknown key\|Error" $d/host.err | sed 's/^wcnet: //' | tr '\n' ';' | sed 's/;/; /g'
    echo
    grep -h "screen:\|left slot\|Error" $d/client.err | sed 's/^/  client: /' | head -5
  } >> $S/spawntest.log
done
echo "== spawntest done" >> $S/spawntest.log
