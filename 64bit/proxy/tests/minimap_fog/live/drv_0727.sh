#!/bin/bash
# Ticket 0727: the live pair of the minimap fix on one claimed Goldberg instance, product layout of this branch (a copy of
# server/evidence/0552/drv_0552.sh's steps, with lay.py for the layout and mm_check.py for the cells). Run from the
# worktree root with the instance claimed (owner $OWN) and the MC's GO.
# Usage: drv_0727.sh INSTANCE PHASE [ARGS]
#   snap                saves_snap snap (the instance's saves, by hash; 'stop' restores them)
#   put                 the 0552 T0 save (C5H-0552-T0, by its sha256) into the instance's saves
#   prep                lay.py prep (B = the build_server out, the composite of SH) and helper on
#   opts N              VP64Options.ini: the installer's renderer defaults and [Extras] FixMinimapUnexplored = N (0 or 1)
#   load NAME           frontend prepare (cp, vp, eui; load NAME), launch, in game
#   reveal | unreveal   server/evidence/0552/mm_block.lua on the main thread, then mm_state.lua (vp.c552)
#   check LABEL [ARGS]  mm_check.py on the running EXE (read only) -> $W/<inst>-check-LABEL.txt; ARGS go to mm_check.py
#   shot LABEL RESULT WHAT   cli.py shot into Visual Checks (issue 0727), then the minimap crop
#   log LABEL           the proxy's 'minimap fog' and options lines (lay.py collect into $W/LABEL)
#   stop                cli.py stop (safe stop), then saves_snap restore
#   restore             lay.py restore (every step; ends with provision.py verify)
set -u -o pipefail
I=${1:?instance}; PHASE=${2:?phase}; shift 2
W=${W727:-C:/c5-64bit-work/0727/live}
B=${B727:-C:/c5-64bit-work/0727/server-build}
SH=${SH727:-C:/c5-64bit-work/0727/shell-build}
OWN=${OWN727:-0727-impl}
T0=C:/c5-64bit-work/0552/inputs/C5H-0552-T0.Civ5Save
LAY=research/02-exe-internals/evidence/4967/lay.py
EVD=server/evidence/0552
SNAPPY=server/evidence/0615/4914/saves_snap.py
SHOTS=F:/c5-64bit-work/0727/shots
export C5_POOL_OWNER=$OWN C5_JOB_OWNER=$OWN C5_QUIETLOCK_WHO=$OWN
unset VP64_DEV_READY_BUILD
mkdir -p "$W/tmp" "$SHOTS"
ts() { date '+%F %T %Z'; }
sha() { sha256sum "$1" | cut -d' ' -f1 | tr -d '*'; }
echo "== $(ts) $I $PHASE $*"
user_dir() { python -I -c "import sys; sys.path.insert(0, 'tools/harness'); from instance import Instance; print(Instance.load('$I').user_dir)"; }
gamepid() { python -I tools/harness/cli.py -i "$I" status --json | python -I -c "import json,sys; print(json.load(sys.stdin)['pid'])"; }
ingame() {
  dismissed=0
  for k in $(seq 1 45); do
    out=$(python -I tools/harness/frontend.py -i "$I" ingame 2>&1); rc=$?
    echo "$out" | tail -1
    [ $rc -eq 0 ] && return 0
    [ $rc -eq 8 ] && { echo "BOX open"; exit 8; }
    if [ $dismissed -eq 0 ] && echo "$out" | grep -q load_screen_ready; then
      python -I tools/harness/frontend.py -i "$I" dismiss >/dev/null || exit 7
      dismissed=1
    fi
    python -I -c "import time; time.sleep(8)"
  done
  echo "not in game (rc $rc)"; exit 6
}
case "$PHASE" in
snap)
  python -I $SNAPPY snap --instance "$I" --work "$W/snap-$I" || exit 4
  ;;
put)
  S=$(sha "$T0")
  [ "${S:0:8}" = 79196993 ] || { echo "HASH MISMATCH: $T0 is not 0552's C5H-0552-T0 (79196993...)"; exit 2; }
  python -I $SNAPPY put --instance "$I" --work "$W/snap-$I" --file "$T0" --as C5H-0552-T0 --sha256 "$S" || exit 4
  echo "T0 sha256 ${S:0:16}"
  ;;
prep)
  [ -f "$SH/bin/shell_composite.dll" ] || { echo "no composite in $SH/bin"; exit 2; }
  python -I -X utf8 $LAY prep "$I" --build "$B" --work "$W/pp-$I" --composite-dll "$SH/bin/shell_composite.dll" \
    --composite-ini "$SH/bin/shell_composite.ini" || exit 4
  python -I -X utf8 $LAY helper "$I" --work "$W/pp-$I" on || exit 4
  ;;
opts)
  N=${1:?0 or 1}
  U=$(user_dir)
  [ -n "$U" ] || exit 2
  # the beta 3 installer's renderer defaults (observer-switch-minimap.md 6.4) and the key under test; lay.py backed the
  # file up at prep and puts it back by hash at restore
  printf '[Renderer]\r\nServerRenderer = 1\r\nServerUnits = 1\r\nServerCities = 1\r\nServerLandmarks = 1\r\nServerFarmFields = 1\r\nServerCombat = 1\r\n[Extras]\r\nFixMinimapUnexplored = %s\r\n' "$N" > "$U/VP64Options.ini"
  echo "VP64Options.ini:"; cat "$U/VP64Options.ini"
  ;;
load)
  python -I tools/harness/frontend.py -i "$I" prepare --mods cp,vp,eui --load "${1:?save name}" || exit 4
  python -I tools/harness/cli.py -i "$I" launch || exit 5
  ingame
  ;;
reveal|unreveal)
  sed -e "s/@MODE@/$PHASE/" $EVD/mm_block.lua > "$W/tmp/mm_block-$PHASE-$I.lua"
  python -I tools/harness/lua.py --instance "$I" --state InGame -f "$W/tmp/mm_block-$PHASE-$I.lua" || exit 4
  python -I -c "import time; time.sleep(4)"
  out=$(python -I tools/harness/lua.py --instance "$I" --state InGame -f $EVD/mm_state.lua) || exit 4
  echo "$out" | tee "$W/$I-$PHASE.txt"
  echo "$out" | grep -q "rec $PHASE ran=[0-9.]* ok=true" || { echo "FAIL: $PHASE did not run ok"; exit 9; }
  ;;
check)
  L=${1:?label}; shift
  PID=$(gamepid)
  BLK=$(grep -o 'block x0=[0-9]* y0=[0-9]* [0-9]*x[0-9]*' "$W/$I-reveal.txt" 2>/dev/null | head -1 | sed -E 's/block x0=([0-9]+) y0=([0-9]+) ([0-9]+)x([0-9]+)/\1 \2 \3 \4/')
  python -I proxy/tests/minimap_fog/mm_check.py --pid "$PID" ${BLK:+--block $BLK} "$@" | tee "$W/$I-check-$L.txt"
  exit ${PIPESTATUS[0]}
  ;;
shot)
  L=${1:?label}; R=${2:?result}; WHAT=${3:?what}
  python -I tools/harness/cli.py -i "$I" shot "$SHOTS/$I-$L.png" --what "$WHAT" --result "$R" --issue 0727 || exit 4
  python -I server/evidence/0683/crop_minimap.py "$SHOTS/$I-$L.png" "$SHOTS/$I-$L-minimap.png"
  ;;
log)
  L=${1:?label}
  python -I -X utf8 $LAY collect "$I" --work "$W/pp-$I" --label "$L" >/dev/null || exit 4
  grep -rh "minimap fog\|FixMinimapUnexplored" "$W/pp-$I/$L" 2>/dev/null | cut -c1-400 | tee "$W/$I-log-$L.txt"
  ;;
stop)
  python -I tools/harness/cli.py -i "$I" stop --human-turn-hook tools.harness.turns:stop_readiness; rc=$?
  echo "stop exit $rc"
  [ $rc -eq 0 ] || exit 4
  ;;
restore)
  python -I -X utf8 $LAY restore "$I" --work "$W/pp-$I"; rc=$?
  echo "lay restore exit $rc"
  python -I $SNAPPY restore --instance "$I" --work "$W/snap-$I"; rc2=$?
  echo "saves restore exit $rc2"
  [ $rc -eq 0 ] && [ $rc2 -eq 0 ] || exit 4
  ;;
*) echo "unknown phase $PHASE"; exit 2 ;;
esac
echo "== $(ts) $I $PHASE done"
