"""The engine-utility vtable (catalogue symbol pac_util_vftable): which of its slots the product may read.

Epic 0590, 2026-11-20 (research/02-exe-internals/exe-build-table.md section 16). The vtable has 264 entries in Steam
builds 4390913 and 26577625, but the bodies of the functions at slots 195 and 256 differ between the two builds, and
two more slots (54, 82) are look-alikes that the address matcher swaps. The product was reviewed for exactly this
use: it copies the whole table of the running EXE and replaces slot 102 (getSecsPerTurn), whose function is its own
catalogue symbol (pac_secs_per_turn) and is compared at run time. Reading or replacing any other slot assumes that
slot means the same in every known build, which nobody has checked.

So this test fails when
  - a product source file other than the reviewed ones names the vtable's symbol, or
  - the reviewed files define another slot constant, index the copied table with anything but the reviewed slot, or
    change the reviewed slot or the entry count.
To add a slot: check its function in every known build (same slot, same behaviour), give the function its own
catalogue symbol with a run-time comparison, write the reasoning into research section 16, then extend REVIEWED here.
"""
import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]

# file (repository path) -> what it may do with the symbol
REVIEWED_FILES = {
    'proxy/src/proxy_pacing.cpp',
    'proxy/src/proxy_pacing.h',
}
# the one slot the product reads and replaces, and the table's length it copies
REVIEWED = {'kSlotSecsPerTurn': 102}
ENTRIES = 264

# Subscripts of the EXE's table (t[...]) and of the proxy's copy (table[...]) that are reviewed: the reviewed slot; the
# whole-table loop (i, 1 + i); the entry before slot 0 (0) and after the last (1 + kEntries), which are only compared
# with the image's sections; the array sizes (1 + kEntries, kEntries + 2); and [1], used only as the address of slot 0
# (the vptr the proxy writes).
ALLOWED_SUBSCRIPT = re.compile(
    r'^\s*(?:1\s*\+\s*kSlotSecsPerTurn|0|1|i|1\s*\+\s*i|1\s*\+\s*kEntries|kEntries\s*\+\s*2)\s*$')
# kSlots (the player slots the frame scans) is not a vtable slot
SLOT_CONSTANT = re.compile(r'\bconst\s+int\s+(kSlot(?!s\b)[A-Za-z0-9_]+)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;')

PRODUCT_DIRS = ('proxy/src', 'shell', 'server/src', 'render/src', 'options', 'installer')
SOURCE_SUFFIXES = {'.cpp', '.h', '.hpp', '.c', '.inc', '.inl'}
SYMBOL = re.compile(r'\b(?:VPX_)?pac_util_vftable\b')


def _strip_comments(text):
    text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
    return re.sub(r'//[^\n]*', ' ', text)


def _product_sources():
    for d in PRODUCT_DIRS:
        root = REPO / d
        if not root.is_dir():
            continue
        for p in root.rglob('*'):
            if p.is_file() and p.suffix.lower() in SOURCE_SUFFIXES:
                rel = p.relative_to(REPO).as_posix()
                if '/tests/' in rel or '/evidence/' in rel or '/build' in rel:
                    continue
                yield rel, p


def test_only_the_reviewed_files_name_the_vtable():
    users = sorted(rel for rel, p in _product_sources()
                   if SYMBOL.search(_strip_comments(p.read_text(encoding='utf-8', errors='replace'))))
    assert users, 'the scan found no user at all: the test no longer looks where the product is'
    extra = [u for u in users if u not in REVIEWED_FILES]
    assert not extra, (
        'these files name pac_util_vftable without a slot review (exe-build-table.md section 16): %s' % extra)


def test_slot_constants_are_the_reviewed_ones():
    text = _strip_comments((REPO / 'proxy/src/proxy_pacing.h').read_text(encoding='utf-8', errors='replace'))
    slots = dict((m.group(1), int(m.group(2), 0))
                 for m in SLOT_CONSTANT.finditer(text))
    assert slots == REVIEWED, 'slot constants of the engine-utility vtable changed without a review: %s' % slots
    m = re.search(r'\bconst\s+int\s+kEntries\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;', text)
    assert m and int(m.group(1), 0) == ENTRIES, 'kEntries is no longer %d' % ENTRIES


def test_the_copied_table_is_indexed_only_with_the_reviewed_slot():
    text = _strip_comments((REPO / 'proxy/src/proxy_pacing.cpp').read_text(encoding='utf-8', errors='replace'))
    # every subscript of the EXE's table (t[...]) and of the copy (->table[...]) in the pacing code
    subs = re.findall(r'(?:\bt|->\s*table|\.\s*table)\s*\[([^\]]*)\]', text)
    assert subs, 'no table subscript found: the pacing code changed shape, review this test'
    allowed = ALLOWED_SUBSCRIPT
    bad = sorted(set(s.strip() for s in subs if not allowed.match(s)))
    assert not bad, (
        'the engine-utility table is indexed with %s: only slot kSlotSecsPerTurn (102) is reviewed for every known '
        'build (exe-build-table.md section 16)' % bad)


def test_negative_a_second_slot_is_caught(tmp_path):
    # the checks above must be able to fail: the same expressions on a text that reads slot 195
    text = 'const int kSlotSecsPerTurn = 102;\nconst int kSlotOther = 195;\n'
    slots = dict((m.group(1), int(m.group(2), 0))
                 for m in SLOT_CONSTANT.finditer(text))
    assert slots != REVIEWED
    subs = re.findall(r'(?:\bt|->\s*table|\.\s*table)\s*\[([^\]]*)\]', 'x = t[1 + 195]; g_page->table[1 + kSlotOther] = y;')
    allowed = ALLOWED_SUBSCRIPT
    assert [s for s in subs if not allowed.match(s)] == ['1 + 195', '1 + kSlotOther']
