"""Write a DLL pack (epic 3500, issue 3504; contract tools/release/README.md section 5).

    python -I tools/release/pack.py --layout <build> --out <dir>
                                    (--fork <clone> --tag <tag> | --x86-dll <the tag's 32-bit DLL> [--x86-origin <text>])
                                    [--published-x86-dll <file> | --published-x86-sha256 <hex>] [--published-x86-origin <t>]
                                    [--test-x86-dll <file> ...] [--test-x86-origin <text>]
                                    [--purity <purity.tsv>] [--release <name>] [--fork-commit <c>] [--tools-commit <c>]
                                    [--pdb] [--major-civs N] [--shell-config thin|i2]

It writes the pack into <dir>/vp64pack/ (the driver's `pack` stage: tools/release/README.md section 2a). The pack
is what goes into the x64 folder of the D28 layout (`VP64\\` beside the proxy, vpproxy.ini `x64_dir`): the
five x64 files the server needs, copied from <build>/bin (or <build> itself when it has no bin/), the x64 DLL renamed
to CvGameCore_Expansion2_x64.dll (issue 2606, MC #3403: the EXE's DLC scan in the modpack form), a purity file and
`manifest.json`:

    {"format": 1, "release": "<release>",
     "x86_dlls": [{"sha256": "<the tag's committed 32-bit DLL>", "origin": "tag <tag> (1) Community Patch/..."},
                  {"sha256": "...", "origin": "published installer/modpack ..."},      (optional)
                  {"sha256": "...", "origin": "test only: ..."}],                       (optional, never shipped)
     "x64_dll": {"file": "CvGameCore_Expansion2_x64.dll", "sha256": "..."},     (issue 2606; before it the build's
                                                                                 name CvGameCore_Expansion2.dll)
     "purity":  {"file": "purity.tsv", "sha256": "..."},
     "built":   {"fork_commit": "...", "tools_commit": "..."},
     "files":   {"server.exe": "<sha256>", ...}}         (the other x64 files; the proxy does not read this)

x86_dlls lists every 32-bit DLL the pack fits; the proxy boots the 64-bit path when fallback_dll (the player's DLL kept
beside the proxy) has one of these hashes, else the 32-bit DLL with one loud line naming its hash. MC rulings 2026-11-13
22:20 and 22:22 (threads #203, #206): a shipped pack lists only the release tag's own committed `(1) Community
Patch/CvGameCore_Expansion2.dll` (`git show <tag>:...` with --fork/--tag, or that file given as --x86-dll by the
driver; research/06-tooling-and-workflows/release-driver.md section 1, side finding); a DLL of the release's published
installer or modpack may be added with --published-x86-* when one is known from an official source (no installer is
extracted here); --test-x86-dll adds test-only entries (for example the toolkit's local build 4e91c58a, or a test
instance's fallback such as i0ship-26234371-ia) and is never used for a shipped pack. When the tag's DLL itself cannot
be read (no such path in the tag, a missing --x86-dll file), pack.py stops: exit 10 and the line
`take_release: STOP pack (CL step 6): <what>; to unblock: <what to do>`.

Without --purity the pack gets an empty purity file of the README section 4 header (every call crosses) until issue
3503 lands. With --purity, a `dll_sha256` line in it must name the pack's x64 DLL.

manifest.json is written last (to a temporary name, then renamed): a pack folder without it is incomplete, and the
proxy treats it as no pack. <dir>/vp64pack must not exist or be empty. Exit 0 and a last line `pack: OK <pack> ...`;
exit 10 and the STOP line when the tag's 32-bit DLL is missing; exit 1 and `pack: FAILED: <why>` otherwise; exit 2 for
a usage error (argparse). Python standard library only.

--major-civs N (issue 4252, epic 0334; default 22): the pack of a DLL built with MOD_GLOBAL_MAX_MAJOR_CIVS=N. For 43 the
32-bit DLL the pack fits is the tag's `(3b) 43 Civs Community Patch/CvGameCore_Expansion2.dll`: upstream's installer
writes that DLL over `(1) Community Patch/CvGameCore_Expansion2.dll` for its three 43-civ components (VPSetupData.iss
:49-51; research/03-dll-engine-boundary/player-limits.md 1.3), so it is the DLL a 43-civ player has in (1). It is
read the same way (git show of the tag tree, never run). The manifest gets `"major_civs": N` (absent for 22, whose
manifest is unchanged). The (3b) modinfo and AdvancedSetup.lua are not carried: the player's 43-civ install already
has them in MODS/(3b) 43 Civs Community Patch/, and the pack only replaces the DLL in (1) (player-limits.md 8.1).

--shell-config thin|i2 (issue 2602, MC #3197; the release descriptor's optional shell_config, passed by the pack stage
only when the descriptor has it): written into the manifest as "shell_config", an informational key the proxy
does not read (the installer reads it). Without the option the manifest has no such key and is byte for byte the
manifest of before (absent = thin). The build's configuration is "i2" when its layout carries the compositor
(shell_composite.dll in <build>/proxy/bin/ or <build>/composite/bin/, an x86 image), else "thin"; a --shell-config
that disagrees with it is refused (exit 1), nothing written. A shell_composite.sha256 in the build beside the
compositor must name that compositor and the build's proxy (<build>/proxy/bin/CvGameCore_Expansion2.dll).

--shell-config i3 [--options FILE] (ticket 0574, D195, release 0.1b): the build of i2 (the compositor, checked as for
i2) and an option set, the VP64Options.ini values the installer writes for the player: FILE is JSON {"<Section>":
{"<Key>": <int>, ...}, ...}, checked as the descriptor's "options" field (descriptor.parse_options; [Renderer]
ServerRenderer is refused, vp64helper on sets it) and written into the manifest as "options" (always present for i3,
{} without --options). --options without --shell-config i3 is refused (exit 2). thin and i2 packs are unchanged.
"""
import argparse
import hashlib
import json
import os
import shutil
import stat
import struct
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent.parent
# The five x64 files by their names in a build's bin/ (server/build_server.py's output).
X64_FILES = ['server.exe', 'CvGameCore_Expansion2.dll', 'lua51_x64.dll', 'CvGameDatabase_x64.dll',
             'CvLocalization_x64.dll']
X64_DLL = 'CvGameCore_Expansion2.dll'   # the x64 DLL's name in the build
# Issue 2606 (MC #3403, 2026-11-17): in the pack the x64 DLL is CvGameCore_Expansion2_x64.dll. Under the build's name
# the EXE's DLC scan took it, in the modpack form (VP inside <game>\Assets\DLC), for the DLC's game DLL and loaded it
# into the 32-bit process (error 193, then 0xC0000005; installer-notes section 15). The proxy names the x64 DLL by the
# manifest's x64_dll.file (pack=1), so a pack of either name boots; packs written before 2606 keep the old name.
X64_DLL_PACK = 'CvGameCore_Expansion2_x64.dll'
PACK_NAMES = {X64_DLL: X64_DLL_PACK}     # build name -> pack name (the other files keep theirs)


def pack_name(build_name):
    """The name a build's x64 file has in the pack."""
    return PACK_NAMES.get(build_name, build_name)


def copy_writable(src, dst):
    """shutil.copy2 (contents, times) whose copy is never read-only (ticket 0629): copy2 also copies the read-only
    attribute, and a layout built from the shared read-only pins folder before 0629 holds read-only files. The source
    is not touched."""
    shutil.copy2(src, dst)
    mode = os.stat(dst).st_mode
    if not mode & stat.S_IWRITE:
        os.chmod(dst, stat.S_IMODE(mode) | stat.S_IWRITE)


PACK_FILES = [pack_name(f) for f in X64_FILES]   # the five x64 files by their names in the pack
PURITY = 'purity.tsv'
MANIFEST = 'manifest.json'
PACK_DIR = 'vp64pack'
TAG_DLL = '(1) Community Patch/CvGameCore_Expansion2.dll'
# issue 4252: the 32-bit DLL a player of another major count has in (1), by count (only upstream's 43 exists)
TAG_DLL_BY_MAJOR = {22: TAG_DLL, 43: '(3b) 43 Civs Community Patch/CvGameCore_Expansion2.dll'}
MAJOR_CIVS_DEFAULT = 22


def tag_dll(major=MAJOR_CIVS_DEFAULT):
    """The path in the tag's tree of the 32-bit DLL a player of `major` major civilizations runs; PackError for a count
    upstream ships no DLL of."""
    try:
        return TAG_DLL_BY_MAJOR[int(major)]
    except (KeyError, ValueError, TypeError):
        raise PackError('--major-civs %s: upstream ships no 32-bit DLL of that count (known: %s)'
                        % (major, ', '.join(map(str, sorted(TAG_DLL_BY_MAJOR)))))
PURITY_HEADER = '# vp64-purity 1'
SHELL_CONFIGS = ('thin', 'i2', 'i3')   # issue 2602; i3 ticket 0574
COMPOSITE_CONFIGS = ('i2', 'i3')         # the configurations whose build carries the compositor (i3: ticket 0574)
COMPOSITE = 'shell_composite.dll'
COMPOSITE_SHA = 'shell_composite.sha256'
PROXY_REL = ('proxy', 'bin', 'CvGameCore_Expansion2.dll')
COMPOSITE_DIRS = (('proxy', 'bin'), ('composite', 'bin'))   # where a build keeps the compositor


class PackError(Exception):
    pass


class PackStop(Exception):
    """The tag's 32-bit DLL cannot be read: a stop of the driver's pack stage (exit 10), not a failure."""

    def __init__(self, what, unblock):
        Exception.__init__(self, what)
        self.what, self.unblock = what, unblock

    def line(self):
        return 'take_release: STOP pack (CL step 6): %s; to unblock: %s' % (self.what, self.unblock)


def sha256_bytes(b):
    return hashlib.sha256(b).hexdigest()


def sha256_file(p):
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def pe_machine(b):
    """The PE machine field of an image's bytes (0x14C x86, 0x8664 x64), or None."""
    if len(b) < 0x40 or b[:2] != b'MZ':
        return None
    pe = struct.unpack_from('<I', b, 0x3C)[0]
    if pe + 6 > len(b) or b[pe:pe + 4] != b'PE\0\0':
        return None
    return struct.unpack_from('<H', b, pe + 4)[0]


def head(p, n=4096):
    with open(p, 'rb') as f:
        return f.read(n)


def x86_from_tag(fork, tag, major=MAJOR_CIVS_DEFAULT):
    """(sha256, source text) of the release tag's committed 32-bit DLL (of the major count, issue 4252)."""
    tdll = tag_dll(major)
    if not (Path(fork) / '.git').exists() and not (Path(fork) / 'HEAD').exists():
        raise PackError('--fork %s is not a git clone' % fork)
    if subprocess.run(['git', '-C', str(fork), 'rev-parse', '--verify', '--quiet', tag + '^{commit}'],
                      capture_output=True).returncode != 0:
        raise PackError('the tag %s is not in the fork clone %s' % (tag, fork))
    cp = subprocess.run(['git', '-C', str(fork), 'show', '%s:%s' % (tag, tdll)], capture_output=True)
    if cp.returncode != 0:
        raise PackStop('the release tag %s has no committed "%s" (git show: %s)'
                       % (tag, tdll, cp.stderr.decode('utf-8', 'replace').strip()),
                       'find the 32-bit DLL the release ships in its official source, commit-check it, and pass it as '
                       '--x86-dll with --x86-origin naming where it came from')
    b = cp.stdout
    if pe_machine(b[:4096]) != 0x14C:
        raise PackError('%s:%s is not a 32-bit PE image' % (tag, tdll))
    return sha256_bytes(b), 'tag %s %s (git show, %d bytes)' % (tag, tdll, len(b))


def x86_from_file(p, what='--x86-dll'):
    p = Path(p)
    if not p.is_file():
        if what == '--x86-dll':
            raise PackStop('the tag\'s 32-bit DLL %s does not exist' % p,
                           'check out the release tag\'s "%s" (git show <tag>:"%s") and pass that file' % (TAG_DLL, TAG_DLL))
        raise PackError('%s %s does not exist' % (what, p))
    if pe_machine(head(p)) != 0x14C:
        raise PackError('%s %s is not a 32-bit PE image' % (what, p))
    return sha256_file(p), 'file %s' % p


def git_head():
    cp = subprocess.run(['git', '-C', str(REPO), 'rev-parse', 'HEAD'], capture_output=True, text=True)
    if cp.returncode != 0:
        return 'unknown'
    dirty = subprocess.run(['git', '-C', str(REPO), 'status', '--porcelain', '--untracked-files=no'],
                           capture_output=True, text=True).stdout.strip()
    return cp.stdout.strip() + (' (dirty)' if dirty else '')


def purity_dll_sha(text):
    for ln in text.splitlines():
        parts = ln.split('\t')
        if parts[0] == 'dll_sha256' and len(parts) >= 2:
            return parts[1].strip().lower()
    return None


def build_composite(layout):
    """The compositor of a build layout (issue 2602): the path of shell_composite.dll in <layout>/proxy/bin or
    <layout>/composite/bin, or None. PackError when both have one, or it is not a 32-bit image."""
    layout = Path(layout)
    hits = [layout.joinpath(*d, COMPOSITE) for d in COMPOSITE_DIRS if layout.joinpath(*d, COMPOSITE).is_file()]
    if len(hits) > 1:
        raise PackError('the build has two compositors: %s' % ', '.join(str(h) for h in hits))
    if hits and pe_machine(head(hits[0])) != 0x14C:
        raise PackError('%s is not a 32-bit PE image' % hits[0])
    return hits[0] if hits else None


def build_shell_config(layout):
    """The configuration a build layout supports (issue 2602): 'i2' when it carries the compositor, else 'thin'."""
    return 'i2' if build_composite(layout) is not None else 'thin'


def check_shell_config(layout, shell_config):
    """Refuse (PackError) a shell_config that disagrees with the build's configuration (MC #3197). For i2 the
    build's proxy must exist, and a shell_composite.sha256 beside the compositor must name it and the proxy."""
    if shell_config not in SHELL_CONFIGS:
        raise PackError('--shell-config %r: one of %s' % (shell_config, ', '.join(SHELL_CONFIGS)))
    layout = Path(layout)
    comp = build_composite(layout)
    if (comp is not None) != (shell_config in COMPOSITE_CONFIGS):
        raise PackError('--shell-config %s disagrees with the build %s: %s' % (
            shell_config, layout, 'it carries the compositor %s (an i2 build)' % comp if comp else
            'it has no compositor (shell_composite.dll in proxy/bin or composite/bin): a thin build'))
    if comp is None:
        return None
    proxy = layout.joinpath(*PROXY_REL)
    if not proxy.is_file():
        raise PackError('--shell-config %s: the build has no proxy %s' % (shell_config, proxy))
    ms = comp.parent / COMPOSITE_SHA
    if ms.is_file():
        rows = dict(ln.split(None, 1) for ln in ms.read_text(encoding='utf-8').splitlines()
                    if ln.strip() and not ln.startswith('#') and len(ln.split(None, 1)) == 2)
        want = {'proxy': sha256_file(proxy), 'composite': sha256_file(comp)}
        for k, v in want.items():
            if rows.get(k, '').strip().lower() != v:
                raise PackError("%s does not name the build's %s (%s...)" % (ms, k, v[:16]))
    return comp


def check_options(shell_config, options):
    """The option set an i3 pack records (ticket 0574): options checked as the descriptor's field ({} when None);
    None for thin and i2. PackError for options given to another configuration or a bad set."""
    if shell_config != 'i3':
        if options is not None:
            raise PackError('--options only with --shell-config i3 (this pack: %s)' % (shell_config or 'thin'))
        return None
    if options is None:
        return {}
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import descriptor   # the same check as the descriptor's "options" field
    try:
        return descriptor.parse_options('--options', 'options', options)
    except descriptor.DescriptorError as e:
        raise PackError(str(e))


def make_pack(layout, out, x86_dlls, release, purity=None, fork_commit=None, tools_commit=None, pdb=False,
              major=MAJOR_CIVS_DEFAULT, shell_config=None, options=None):
    """x86_dlls: [{"sha256", "origin"}, ...], the tag's DLL first. major: the x64 DLL's major count (issue 4252);
    recorded as manifest "major_civs" when it is not 22. shell_config: 'thin' | 'i2' | None (issue 2602): checked
    against the build and recorded as manifest "shell_config" when given. options (ticket 0574): the i3 option set,
    {section: {key: int}}, checked and recorded as manifest "options" ({} when None); refused for any other
    configuration."""
    layout = Path(layout)
    if shell_config is not None:
        check_shell_config(layout, shell_config)
    options = check_options(shell_config, options)
    out = Path(out)
    bin_dir = layout / 'bin' if (layout / 'bin').is_dir() else layout
    for f in X64_FILES:
        p = bin_dir / f
        if not p.is_file():
            raise PackError('%s is missing' % p)
        if pe_machine(head(p)) != 0x8664:
            raise PackError('%s is not an x64 PE image' % p)
        if pdb and not p.with_suffix('.pdb').is_file():
            raise PackError('--pdb: %s is missing' % p.with_suffix('.pdb'))
    if out.exists() and (not out.is_dir() or any(out.iterdir())):
        raise PackError('--out %s exists and is not an empty folder' % out)
    if not release:
        raise PackError('no release name (--release or --tag)')
    x64_sha = sha256_file(bin_dir / X64_DLL)
    if purity:
        ptext = Path(purity).read_text(encoding='utf-8')
        if not ptext.startswith(PURITY_HEADER):
            raise PackError('--purity %s does not start with "%s"' % (purity, PURITY_HEADER))
        bound = purity_dll_sha(ptext)
        if bound is not None and bound != x64_sha:
            raise PackError('--purity %s is bound to the x64 DLL %s, the layout\'s is %s' % (purity, bound, x64_sha))
    out.mkdir(parents=True, exist_ok=True)
    files = {}
    for f in X64_FILES:
        to = pack_name(f)   # issue 2606: the x64 DLL as CvGameCore_Expansion2_x64.dll
        copy_writable(bin_dir / f, out / to)
        if pdb:
            # the PDB keeps the build's name: the DLL's debug directory names CvGameCore_Expansion2.pdb
            copy_writable((bin_dir / f).with_suffix('.pdb'), out / Path(f).with_suffix('.pdb').name)
        files[to] = sha256_file(out / to)
    if files[X64_DLL_PACK] != x64_sha:
        raise PackError('the x64 DLL changed while it was copied')
    if purity:
        copy_writable(purity, out / PURITY)
    else:
        # README section 4's header with no entries: every call crosses (issue 3503 writes the real file).
        (out / PURITY).write_bytes(('%s\ndll_sha256\t%s\nderived_from\t-\t%s\n'
                                    % (PURITY_HEADER, x64_sha, fork_commit or '-')).encode('utf-8'))
    m = {
        'format': 1,
        'release': release,
        'x86_dlls': x86_dlls,
        'x64_dll': {'file': X64_DLL_PACK, 'sha256': x64_sha},
        'purity': {'file': PURITY, 'sha256': sha256_file(out / PURITY)},
        'built': {'fork_commit': fork_commit or 'unknown', 'tools_commit': tools_commit or git_head()},
        'files': {f: h for f, h in files.items() if f != X64_DLL_PACK},
    }
    if int(major) != MAJOR_CIVS_DEFAULT:
        m['major_civs'] = int(major)
    if shell_config is not None:
        m['shell_config'] = shell_config
    if options is not None:
        m['options'] = options
    tmp = out / (MANIFEST + '.tmp')
    tmp.write_text(json.dumps(m, indent=1) + '\n', encoding='utf-8', newline='\n')
    os.replace(tmp, out / MANIFEST)
    return m


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--layout', required=True, help='a build output folder with bin/ (or the bin folder itself)')
    ap.add_argument('--out', required=True, help='the pack is written to <out>/vp64pack (new or empty)')
    ap.add_argument('--fork', help='a clone of the DLL fork that has the release tag')
    ap.add_argument('--tag', help='the release tag whose committed 32-bit DLL the pack fits')
    ap.add_argument('--x86-dll', help='the release tag\'s committed 32-bit DLL as a file (instead of --fork/--tag)')
    ap.add_argument('--x86-origin', help='origin text of --x86-dll (default: "tag <release> (1) Community Patch/'
                    'CvGameCore_Expansion2.dll (file <path>)")')
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--published-x86-dll', help='the 32-bit DLL of the release\'s published installer or modpack')
    g.add_argument('--published-x86-sha256', help='its SHA-256, when only the hash is known from an official source')
    ap.add_argument('--published-x86-origin', default='published installer/modpack',
                    help='where the published DLL or hash came from')
    ap.add_argument('--test-x86-dll', action='append', default=[],
                    help='a test-only accepted 32-bit DLL (test instances; never in a shipped pack); repeatable')
    ap.add_argument('--test-x86-origin', default='test only',
                    help='origin text of the --test-x86-dll entries (prefixed "test only: " when it does not say so)')
    ap.add_argument('--purity', help='purity.tsv (issue 3503); default: an empty one of the README section 4 header')
    ap.add_argument('--release', help='the manifest\'s release name (default: --tag)')
    ap.add_argument('--fork-commit', help='the fork commit the x64 DLL was built from')
    ap.add_argument('--tools-commit', help='this repository\'s commit the build used (default: HEAD)')
    ap.add_argument('--pdb', action='store_true', help='copy the PDBs too')
    ap.add_argument('--major-civs', type=int, default=MAJOR_CIVS_DEFAULT, metavar='N',
                    help='the x64 DLL\'s major-civilisation count (issue 4252; default 22): 43 binds the pack to the '
                         'tag\'s "(3b) 43 Civs Community Patch" 32-bit DLL and writes "major_civs" into the manifest')
    ap.add_argument('--shell-config', choices=SHELL_CONFIGS,
                    help="the release's shell configuration (issue 2602; the descriptor's shell_config): written into "
                         'the manifest; refused when the build disagrees (i2 and i3 need the compositor in the build)')
    ap.add_argument('--options', metavar='FILE',
                    help='i3 only (ticket 0574): a JSON file with the option set {"Section": {"Key": int}} the '
                         'installer writes into VP64Options.ini; recorded as the manifest key "options"')
    a = ap.parse_args(argv)
    if a.options and a.shell_config != 'i3':
        ap.error('--options needs --shell-config i3')
    try:
        release = a.release or a.tag
        tdll = tag_dll(a.major_civs)
        if a.x86_dll:
            if a.fork:
                raise PackError('give --x86-dll or --fork/--tag, not both')
            x86_sha, src = x86_from_file(a.x86_dll)
            origin = a.x86_origin or 'tag %s %s (%s)' % (release, tdll, src)
        elif a.fork and a.tag:
            x86_sha, origin = x86_from_tag(a.fork, a.tag, a.major_civs)
        else:
            raise PackError('the 32-bit DLL is needed: --fork and --tag, or --x86-dll')
        x86 = [{'sha256': x86_sha, 'origin': origin}]
        if a.published_x86_dll:
            h, src = x86_from_file(a.published_x86_dll, '--published-x86-dll')
            x86.append({'sha256': h, 'origin': '%s (%s)' % (a.published_x86_origin, src)})
        elif a.published_x86_sha256:
            h = a.published_x86_sha256.strip().lower()
            if len(h) != 64 or any(c not in '0123456789abcdef' for c in h):
                raise PackError('--published-x86-sha256 is not 64 hex digits')
            x86.append({'sha256': h, 'origin': a.published_x86_origin})
        for t in a.test_x86_dll:
            h, src = x86_from_file(t, '--test-x86-dll')
            o = a.test_x86_origin if a.test_x86_origin.startswith('test only') else 'test only: ' + a.test_x86_origin
            x86.append({'sha256': h, 'origin': '%s (%s)' % (o, src)})
        if len(set(e['sha256'] for e in x86)) != len(x86):
            raise PackError('the same 32-bit DLL is listed twice: %s' % [e['sha256'][:12] for e in x86])
        options = None
        if a.options:
            try:
                options = json.loads(Path(a.options).read_text(encoding='utf-8'))
            except (OSError, ValueError) as e:
                raise PackError('--options %s: %s' % (a.options, e))
        out = Path(a.out) / PACK_DIR
        m = make_pack(a.layout, out, x86, release, a.purity, a.fork_commit, a.tools_commit, a.pdb, a.major_civs,
                      a.shell_config, options)
    except PackStop as e:
        print(e.line())
        return 10
    except (PackError, OSError) as e:
        print('pack: FAILED: %s' % e)
        return 1
    print(json.dumps(m, indent=1))
    print('pack: OK %s release %s x86 %s x64 %s purity %s' % (out, m['release'],
                                                              ','.join(e['sha256'][:12] for e in m['x86_dlls']),
                                                              m['x64_dll']['sha256'][:12], m['purity']['sha256'][:12]))
    return 0


if __name__ == '__main__':
    sys.exit(main())
