#!/bin/bash
# Every slot once in campaign mode, one host+wingman pair at a time (runs sharing a game directory corrupt each
# other's save files): barracks, briefing, flight with the host killing, forced landing, debriefing, the next
# mission's start.  sweep.sh "s m" ...
S=${S:-$(cd "$(dirname "$0")" && pwd)/out}
port=15200
for sm in "$@"; do
  d="$S/sw${sm// /}"; rm -rf "$d"; port=$((port+10))
  WCPORT=$port $(dirname "$0")/campaignrun.sh "$d" 330 "$sm" > /dev/null 2>&1
  {
    echo "== $sm: $(grep -h 'starting: mission' $d/host.err | cut -c17-40 | tr '\n' ';') | kills: $(grep -h 'kill foes: [1-9]' $d/host.err | wc -l) | landed: $(grep -c 'mission status 0 -> 1' $d/host.err) | host end: $(grep -h 'screen: Error\|Unsuccessful\|screen: A' $d/host.err | head -1 | cut -c1-60) | wingman starts: $(grep -c 'wingman start' $d/client.err) | wingman errors: $(grep -h 'screen: Error\|Unsuccessful' $d/client.err | head -1 | cut -c1-50)"
  } >> $S/sweep.log
done
echo "== sweep done" >> $S/sweep.log
