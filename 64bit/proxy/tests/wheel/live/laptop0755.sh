#!/bin/bash
# Ticket 0755 ([Extras] FixSmoothWheel): the laptop series, one headed job per key value (wheel_0755.py), each submitted
# only after the one before it ended (the laptop's runner otherwise starts a queued headed job in its second slot:
# two games at once, runbook 4.15), then fetched. Own code, GPL-3.0-or-later; after server/evidence/0738/laptop0738.sh.
#   SHIP=<bin/, proxy/CvGameCore_Expansion2.dll, composite/shell_composite.dll, purity.tsv, probe/wheel_probe.dll,
#        saves/W5042-TINY_0008.Civ5Save> OUT=<fetch root> [KEYS="1 0"] [LANES=real,posted] [REPEATS=3] [HOST=laptop]
#   bash proxy/tests/wheel/live/laptop0755.sh        (from the worktree root, inside MC-B's laptop grant)
# Needs tools/harness/native/bin built in this worktree. Exit 0 all ended and fetched; 2 a file missing; 5 a submit failed.
set -u
SHIP=${SHIP:-C:/c5-64bit-work/0755/ship}; OUT=${OUT:-F:/c5-64bit-work/0755/laptop}; LOG=${LOG:-$OUT/series.log}
for f in "$SHIP/bin/server.exe" "$SHIP/proxy/CvGameCore_Expansion2.dll" "$SHIP/composite/shell_composite.dll" \
         "$SHIP/purity.tsv" "$SHIP/probe/wheel_probe.dll" "$SHIP/saves/W5042-TINY_0008.Civ5Save" tools/harness/native/bin; do
  [ -e "$f" ] || { echo "laptop0755: $f missing" >&2; exit 2; }
done
mkdir -p "$OUT"
ASSETS=${ASSETS:-$HOME/.claude/skills/vp-dll-dev/assets}
[ -d "$ASSETS" ] || { echo "laptop0755: ASSETS=$ASSETS missing (set ASSETS)" >&2; exit 2; }
TB="python -I -X utf8 tools/testbed/tb.py --host ${HOST:-laptop}"
jid() { sed -n 's/^submitted \([0-9a-f-]*\) .*/\1/p'; }     # job ids end in hex
say() { echo "$(date '+%F %T') $*" | tee -a "$LOG"; }
for key in ${KEYS:-1 0}; do
  job=""
  for try in 1 2 3; do
    job=$($TB submit --headed --expect-min 15 --name "0755 wheel key=$key" --timeout 3600 \
      --file "$SHIP/bin"=pp/bin --file "$SHIP/proxy/CvGameCore_Expansion2.dll"=pp/proxy/CvGameCore_Expansion2.dll \
      --file "$SHIP/composite/shell_composite.dll"=pp/composite/shell_composite.dll --file "$SHIP/purity.tsv"=w0755/purity.tsv \
      --file "$SHIP/probe/wheel_probe.dll"=w5042/wheel_probe.dll \
      --file "$SHIP/saves/W5042-TINY_0008.Civ5Save"=saves/W5042-TINY_0008.Civ5Save \
      --file tools/harness/native/bin=native --file "$ASSETS"=frontend-assets -- \
      "%TESTBED_PYTHON%" proxy/tests/wheel/live/wheel_0755.py --key "$key" --lanes "${LANES:-real,posted}" \
      --repeats "${REPEATS:-3}" 2>&1 | jid)
    [ -n "$job" ] && break
    sleep 15
  done
  [ -n "$job" ] || { say "SUBMIT FAILED: key=$key"; exit 5; }
  say "submitted $job key=$key"; echo "$job key=$key" >> "$OUT/jobs.txt"
  st=""
  for w in $(seq 1 90); do st=$($TB wait "$job" --timeout 170 2>&1 | tail -1); echo "$st" | grep -q -E 'done|failed|cancelled' && break; done
  say "ended $job key=$key: $st"
  $TB fetch "$job" --out "$OUT/$job" --max-mb 1 --include out/w0755.json --include out/vpproxy.log --include out/server.log \
    >> "$LOG" 2>&1
  say "fetched $job"
done
say "ALL ENDED"; exit 0
