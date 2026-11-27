#!/bin/bash
# 0805: reloading mods after a game in one EXE session (beta 4 player report: the game hangs at "Reload GameCore DLL",
# no new Logs/VP64/run folder). Live driver for a Goldberg test instance; run from the worktree root with the claim
# (owner impl-0805) and the dispatcher's go for game instances. Template: server/evidence/0716/drv_0716_patch.sh and
# drv_0716.sh.
# Usage: drv_0805.sh INSTANCE PHASE [ARGS]
#   install            the beta 4 hotfix installer (0.1b4h1, by hash) on the instance, the player's VP 5.4.8 DLL first
#   remove             its removal, tree diffs, set-dll --previous, verify --quick
#   launch [SAVE]      frontend prepare (cp, vp, eui; load SAVE, default C5H-4773-TINY_0018), cli launch, in game, dismiss
#   menu               frontend prepare with an empty configuration (the harness menu acts as stock: ActivateDLC on
#                      show; every mod choice by clicks), cli launch
#   turns [N]          N turns (turns.py end + wait), default 3
#   shot NAME TEXT     a diagnostic shot to $W/shots/NAME.png (Visual Checks feed, issue 0805)
#   click X Y          cli.py click (relative coordinates)
#   key KEY            cli.py key
#   runs [LABEL]       the run folders, the tail of stopwatch.log and of the newest run's vpproxy.log -> $W/<LABEL>.txt
#   dump [LABEL]       cli.py dump of the live EXE (it goes on running) into $W/dumps
#   stop               cli.py stop (safe stop)
#   lay B SH [LABEL]   the product layout of a build_server output B with the composite of shell build SH, laid by
#                      research/02-exe-internals/evidence/4967/lay.py (prep, backup by SHA-256) and helper on (D33)
#   unlay [LABEL]      lay.py collect, then restore (by hash; ends with provision.py verify)
set -u -o pipefail
I=${1:?instance}; PHASE=${2:?phase}; shift 2
unset VP64_DEV_READY_BUILD
W=F:/c5-64bit-work/0805
EV=$W/inst-$I; mkdir -p "$EV" "$W/shots" "$W/dumps"
EXE=$W/inputs/VP64-Patch-0.1b4h1-std-only-for-VP5.4.8.exe
EXE_SHA=99a406bdca3a6b3a1136cf003ead4c125879357959d72fb0e970c86db8500a31
PLAYER=$W/inputs/player-vp548.dll
PLAYER_SHA=262aee88c5312c4bd4e899da1d8b413c000bc996fa57abfe8d04b87fd37df16e
export C5_POOL_OWNER=impl-0805 C5_JOB_OWNER=impl-0805 C5_QUIETLOCK_WHO=impl-0805
TH=research/06-tooling-and-workflows/evidence/2600/treehash.py
RI=research/06-tooling-and-workflows/evidence/2600/run_installer.py
ts() { date '+%F %T %Z'; }
sha() { sha256sum "$1" | cut -d' ' -f1 | tr -d '*'; }
need() { [ "$(sha "$1")" = "$2" ] || { echo "HASH MISMATCH $1"; exit 2; }; echo "ok $(basename "$1") ${2:0:16}"; }
root() {
  python -I tools/harness/pool.py list | awk -v i="$I" '$1 == i {gsub(/\\/, "/", $2); print $2}'
}
echo "== $(ts) $I $PHASE $*" | tee -a "$W/session.log"
case "$PHASE" in
install|remove)
  ROOT=$(root); [ -n "$ROOT" ] || { echo "no root for $I"; exit 2; }
  MG="$ROOT/$I/Documents/My Games/Sid Meier's Civilization 5"
  GAME="$ROOT/$I/install"
  DLC="$GAME/Assets/DLC"
  need "$EXE" $EXE_SHA; need "$PLAYER" $PLAYER_SHA
  if [ "$PHASE" = install ]; then
    python -I tools/harness/pool.py holder "$ROOT/$I" | tail -1
    python -I tools/instances/provision.py set-dll "$I" --dll "$PLAYER" | tail -3 || exit 4
    python -I $TH list "$MG" $EV/mg-0-before.tsv || exit 5
    python -I $TH list "$DLC" $EV/dlc-0-before.tsv || exit 5
    python -I $RI "$EXE" --dir "$MG" --gamedir "$GAME" --action install --procroot "$ROOT/$I" --log "$EV/1-install.log"
    rc=$?; echo "installer exit $rc"
    grep -E "Game EXE:|Warning:|variant|options" "$EV/1-install.log" | head -12
    [ $rc -eq 0 ] || exit 6
    python -I $TH list "$MG" $EV/mg-1-installed.tsv
    cp "$MG/VP64Options.ini" "$EV/1-VP64Options-installed.ini"
    echo "PHASE install DONE $(ts)"
  else
    python -I $RI "$EXE" --dir "$MG" --gamedir "$GAME" --action remove --procroot "$ROOT/$I" --log "$EV/3-remove.log"
    echo "removal exit $?"
    python -I $TH list "$MG" $EV/mg-2-removed.tsv
    python -I $TH list "$DLC" $EV/dlc-2-removed.tsv
    python -I $TH diff $EV/dlc-0-before.tsv $EV/dlc-2-removed.tsv --out "$EV/3-dlc-before-vs-removed.txt"
    echo "dlc diff exit $?"
    python -I $TH diff $EV/mg-0-before.tsv $EV/mg-2-removed.tsv --apart Logs cache Saves ModdedSaves ModUserData \
      --out "$EV/3-mg-before-vs-removed.txt"; echo "mg diff exit $?"
    python -I tools/instances/provision.py set-dll "$I" --previous | tail -2
    echo "instance VP DLL now: $(sha "$MG/MODS/(1) Community Patch/CvGameCore_Expansion2.dll" | cut -c1-16)"
    python -I tools/instances/provision.py verify "$I" --quick | grep -E '"ok"|errors' | head -2
    echo "PHASE remove DONE $(ts)"
  fi
  ;;
launch)
  SAVE=${1:-C5H-4773-TINY_0018}
  python -I tools/harness/frontend.py -i "$I" prepare --mods cp,vp,eui --load "$SAVE" || exit 4
  python -I tools/harness/cli.py -i "$I" launch || exit 5
  dismissed=0
  rc=1
  for k in $(seq 1 60); do
    out=$(python -I tools/harness/frontend.py -i "$I" ingame 2>&1); rc=$?
    echo "$out" | tail -1
    [ $rc -eq 0 ] && break
    [ $rc -eq 8 ] && { echo "BOX open"; exit 8; }
    if [ $dismissed -eq 0 ] && echo "$out" | grep -q load_screen_ready; then
      python -I tools/harness/frontend.py -i "$I" dismiss >/dev/null || exit 7
      dismissed=1
    fi
    python -I -c "import time; time.sleep(8)"
  done
  [ $rc -eq 0 ] || { echo "not in game (rc $rc)"; exit 6; }
  echo "IN GAME $(ts)"
  ;;
menu)
  python -I tools/harness/frontend.py -i "$I" prepare --mods "" || exit 4
  python -I tools/harness/cli.py -i "$I" launch || exit 5
  echo "LAUNCHED (menu only) $(ts)"
  ;;
turns)
  N=${1:-3}
  for k in $(seq 1 "$N"); do
    T=$(python -I tools/harness/lua.py --instance "$I" "return Game.GetGameTurn()" 2>/dev/null | tail -1 | tr -dc '0-9')
    [ -n "$T" ] || { echo "no turn (channel silent)"; exit 5; }
    python -I tools/harness/turns.py end --instance "$I"; e=$?
    [ $e -eq 0 ] || { echo "turns end exit $e at turn $T"; exit 5; }
    python -I tools/harness/turns.py wait --instance "$I" "$T" --timeout 600; w=$?
    [ $w -eq 0 ] || { echo "turns wait exit $w at turn $T"; exit 5; }
    echo "turn $T done $(ts)"
  done
  ;;
shot)
  NAME=${1:?name}; TEXT=${2:?text}
  python -I tools/harness/cli.py -i "$I" shot "$W/shots/$NAME.png" --scale 0.5 --what "DIAGNOSTIC: $TEXT" --result info \
    --issue 0805
  ;;
click)
  python -I tools/harness/cli.py -i "$I" click "$@"
  ;;
key)
  python -I tools/harness/cli.py -i "$I" key "$@"
  ;;
runs)
  L=${1:-runs}
  ROOT=$(root); MG="$ROOT/$I/Documents/My Games/Sid Meier's Civilization 5"
  {
    echo "== $(ts) run folders (newest last)"
    ls -1tr "$MG/Logs/VP64/run" 2>/dev/null | tail -6
    echo "== stopwatch.log tail"
    tail -25 "$MG/Logs/stopwatch.log" 2>/dev/null
    NEW=$(ls -1t "$MG/Logs/VP64/run" 2>/dev/null | head -1)
    if [ -n "$NEW" ]; then
      echo "== $NEW/vpproxy.log tail"
      tail -40 "$MG/Logs/VP64/run/$NEW/vpproxy.log" 2>/dev/null
    fi
    echo "== processes of the instance"
    python -I tools/harness/cli.py -i "$I" leftovers 2>&1 | tail -8
  } | tee "$W/$I-$L.txt"
  ;;
dump)
  L=${1:-dump}
  python -I tools/harness/cli.py -i "$I" dump --out "$W/dumps/$L"
  ;;
stop)
  python -I tools/harness/cli.py -i "$I" stop --human-turn-hook tools.harness.turns:stop_readiness; rc=$?
  echo "stop exit $rc"
  exit $rc
  ;;
lay)
  B=${1:?build_server out}; SH=${2:?shell build}; L=${3:-pp}
  LAY=research/02-exe-internals/evidence/4967/lay.py
  python -I -X utf8 $LAY prep "$I" --build "$B" --work "$W/$L" --composite-dll "$SH/bin/shell_composite.dll" \
    --composite-ini "$SH/bin/shell_composite.ini" || exit 4
  python -I -X utf8 $LAY helper "$I" --work "$W/$L" on || exit 4
  echo "LAID $(ts)"
  ;;
unlay)
  L=${1:-pp}
  LAY=research/02-exe-internals/evidence/4967/lay.py
  python -I -X utf8 $LAY collect "$I" --work "$W/$L" --label logs; c=$?
  python -I -X utf8 $LAY restore "$I" --work "$W/$L"; r=$?
  echo "collect exit $c, restore exit $r $(ts)"
  exit $r
  ;;
*)
  echo "unknown phase $PHASE"; exit 2
  ;;
esac
