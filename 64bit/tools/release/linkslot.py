"""One x64 release link of team-3500 at a time (MC 2026-11-13 22:27, D107: release links held 12.7 GB each).

    python -I tools/release/linkslot.py take --who <issue> [--what <text>]   exit 0 taken, 4 held by someone else
    python -I tools/release/linkslot.py release --who <issue>                exit 0 released, 4 not yours, 0 if free
    python -I tools/release/linkslot.py show                                 prints the holder or "free"

Not a quiet-machine lock and not for timing: it only keeps two of the team's own full-LTO links (server/build_server.py
or tools/x64-dll/build.py --config release) from running at once. Heavy starts still go through their gates
(COMMON 16). The slot is a directory created atomically; a holder older than 60 minutes is reported as stale (ask the
lead) but never broken by this tool. Replaced by 0159's workstation-wide rule once that lands.
The slot directory's default is C:/c5-64bit-work/3500/release-link.slot (ticket 0193, D133: no lock or slot file on D:;
never on F:, COMMON 15); C5_3500_LINKSLOT overrides it. Was D:/c5-64bit-work/3500/release-link.slot until 2026-11-15.
"""
import argparse
import json
import os
import sys
import time
from pathlib import Path

SLOT = Path(os.environ.get('C5_3500_LINKSLOT', 'C:/c5-64bit-work/3500/release-link.slot'))


def holder():
    try:
        return json.loads((SLOT / 'holder.json').read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return None


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest='verb', required=True)
    t = sub.add_parser('take')
    t.add_argument('--who', required=True)
    t.add_argument('--what', default='')
    r = sub.add_parser('release')
    r.add_argument('--who', required=True)
    sub.add_parser('show')
    a = ap.parse_args(argv)

    if a.verb == 'show':
        h = holder()
        if not SLOT.exists():
            print('free')
        else:
            age = (time.time() - h['since']) / 60 if h else -1
            print('held by %s since %.0f min: %s%s' % (h.get('who') if h else '?', age, h.get('what', '') if h else '',
                                                      ' (STALE: ask the lead)' if age > 60 else ''))
        return 0
    if a.verb == 'take':
        SLOT.parent.mkdir(parents=True, exist_ok=True)
        try:
            SLOT.mkdir()
        except FileExistsError:
            h = holder()
            print('linkslot: held by %s (%s); wait and take again' % (h.get('who') if h else '?',
                                                                      h.get('what', '') if h else ''), file=sys.stderr)
            return 4
        (SLOT / 'holder.json').write_text(json.dumps({'who': a.who, 'what': a.what, 'since': time.time()}),
                                          encoding='utf-8')
        print('linkslot: taken by %s' % a.who)
        return 0
    # release
    if not SLOT.exists():
        print('linkslot: already free')
        return 0
    h = holder()
    if h and h.get('who') != a.who:
        print('linkslot: held by %s, not %s; not released' % (h.get('who'), a.who), file=sys.stderr)
        return 4
    try:
        (SLOT / 'holder.json').unlink()
    except OSError:
        pass
    SLOT.rmdir()
    print('linkslot: released by %s' % a.who)
    return 0


if __name__ == '__main__':
    sys.exit(main())
