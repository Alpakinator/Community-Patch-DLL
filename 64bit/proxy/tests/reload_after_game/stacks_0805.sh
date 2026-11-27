#!/bin/bash
# 0805: every thread's stack and the module list of a full dump of the EXE (cli.py dump), read with the Windows SDK's
# x86 cdb, no symbol server (exports and the local PDBs named by PDBDIR only). Output beside the dump.
# Usage: stacks_0805.sh DUMP.dmp [PDBDIR]
set -u -o pipefail
D=${1:?dump}; P=${2:-}
CDB="/c/Program Files (x86)/Windows Kits/10/Debuggers/x86/cdb.exe"
OUT="${D%.dmp}-stacks.txt"
SYM="${P:-.}"
"$CDB" -z "$(cygpath -w "$D")" -y "$SYM" -c ".lines -d; lm; ~*kn 40; !locks; q" > "$OUT" 2>&1
echo "cdb exit $? -> $OUT"
grep -n "CvGameCore\|vp64\|shell_composite\|server" "$OUT" | head -40
