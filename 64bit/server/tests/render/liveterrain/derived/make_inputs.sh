#!/usr/bin/env bash
# Ticket 4915 (epic 0615), 2026-11-20: rebuild test_derived's recorded inputs (ticket 4904's cases frame, units, tiny_fh,
# huge_chain) OFFLINE from 4902's recordings, the way ticket 4903 made them (render/evidence/0465/4903/prep.py, chain_fh.sh
# and 4907's hookrun.sh huge chain), into the kept folder that test_derived.cpp reads by default.
#
#   bash server/tests/render/liveterrain/derived/make_inputs.sh            (from the repository root)
#
# Environment (all optional):
#   REC   4902's recordings, read-only          default F:/c5-64bit-work/0465/4902
#   OUT   the kept folder                        default C:/c5-64bit-work/keep/liveterrain-derived-4903
#   RB    a render/build.py build folder         default C:/c5-64bit-work/0615/w4/rbuild (lt_producer_run is built there)
#   LIB   TerrainPiecesLibrary.fxsb (read-only)  default $C5R_GAME_DIR/Resource/Common/TerrainPiecesLibrary.fxsb
# The producer runs with 4903's rule (--protect-water 0 --protect-marsh 0 --protect-rivers 0, live-terrain.md 8.9), so the fields equal
# 4903's: the huge chain's field SHA-256 are checked against 4907's record (server/evidence/0465/4907/hookrun_huge.jsonl).
# Exit 0 made, non-zero a step failed.
set -u -o pipefail
REC=${REC:-F:/c5-64bit-work/0465/4902}
OUT=${OUT:-C:/c5-64bit-work/keep/liveterrain-derived-4903}
RB=${RB:-C:/c5-64bit-work/0615/w4/rbuild}
LIB=${LIB:-${C5R_GAME_DIR:?set C5R_GAME_DIR or LIB}/Resource/Common/TerrainPiecesLibrary.fxsb}
WHO=${C5_JOB_OWNER:-w4-4915}
export C5_JOB_OWNER=$WHO
P=render/evidence/0465/4903/prep.py
[ -f "$P" ] || { echo "make_inputs: run from the repository root"; exit 2; }
RUN=$(find "$RB" -name lt_producer_run.exe 2>/dev/null | head -1)
[ -n "$RUN" ] || { echo "make_inputs: lt_producer_run.exe not under $RB (python -I render/build.py --build-dir $RB --target lt_producer_run)"; exit 2; }
mkdir -p "$OUT"
step() { echo "+ $*"; "$@" || { echo "make_inputs: FAILED: $*"; exit 1; }; }
T="--globals $REC/tiny_globals.json --plots $REC/tiny_plots.json --field $REC/tiny_t0008_field.npz"
H="--globals $REC/huge/huge_globals.json --plots $REC/huge/huge_plots.json --field $REC/huge/huge_field.npz"
# the loads, and the edited plot tables (plot rows of 4902's tables: Tiny (50,16) is flat grassland; huge (89,56) hills,
# (90,56) flat)
step python -I $P $T --out "$OUT/tiny"
step python -I $P $T --out "$OUT/tiny_sm" --edit 50,16:mountain=1
step python -I $P $T --out "$OUT/tiny_sh" --edit 50,16:hills=1
step python -I $P $H --out "$OUT/huge"
step python -I $P $H --out "$OUT/huge_a" --edit 89,56:hills=0
step python -I $P $H --out "$OUT/huge_ab" --edit 89,56:hills=0 90,56:mountain=1
mkdir -p "$OUT/fh" "$OUT/hugechain"
# Tiny FH chain: flat->mountain, ->flat, ->hills, ->flat; the clean rule ignored (DIAGNOSTIC, live-terrain.md 8.5)
step python -I perf/quietlock/quietlock.py run --who $WHO --what "4915 restore 4904 inputs: producer FH chain Tiny" -- \
  "$RUN" --lib "$LIB" --block "$OUT/tiny/block.bin" --plots "$OUT/tiny/plots.txt" --h16 "$OUT/tiny/h16.raw" --m8 "$OUT/tiny/m8.raw" \
  --change "$OUT/tiny_sm/after.txt,$OUT/tiny/plots.txt,$OUT/tiny_sh/after.txt,$OUT/tiny/plots.txt" \
  --result "$OUT/fh/r1,$OUT/fh/r2,$OUT/fh/r3,$OUT/fh/r4" --field-after "$OUT/fh/field" \
  --diagnostic-ignore-clean 1 --protect-water 0 --protect-marsh 0 --protect-rivers 0
# huge chain: hills->flat at (89,56), then flat->mountain at (90,56), both live
step python -I perf/quietlock/quietlock.py run --who $WHO --what "4915 restore 4904 inputs: producer chain huge" -- \
  "$RUN" --lib "$LIB" --block "$OUT/huge/block.bin" --plots "$OUT/huge/plots.txt" --h16 "$OUT/huge/h16.raw" --m8 "$OUT/huge/m8.raw" \
  --change "$OUT/huge_a/after.txt,$OUT/huge_ab/after.txt" \
  --result "$OUT/hugechain/r1,$OUT/hugechain/r2" --field-after "$OUT/hugechain/field" \
  --protect-water 0 --protect-marsh 0 --protect-rivers 0
# the edited tables' folders keep only after.txt (their other files repeat the load's)
for d in tiny_sm tiny_sh huge_a huge_ab; do rm -f "${OUT:?}/${d:?}/block.bin" "${OUT:?}/${d:?}/plots.txt" "${OUT:?}/${d:?}/h16.raw" "${OUT:?}/${d:?}/m8.raw"; done
# the SHA-256 list of every file (README.txt excluded)
( cd "$OUT" && find . -type f ! -name SHA256.txt ! -name README.txt | sort | xargs sha256sum > SHA256.txt ) || exit 1
echo "make_inputs: ok $OUT"
