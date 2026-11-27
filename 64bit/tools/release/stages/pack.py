"""pack (README section 5): the DLL pack vp64pack/ from the x64 layout and the purity file, bound to the player's
32-bit DLL, i.e. the tag's committed `(1) Community Patch/CvGameCore_Expansion2.dll` (3504's tools/release/pack.py;
README section 2a). The player's DLL is taken from the fork at the tag (git show) into <work>/inputs/player-x86/ and
checked against the descriptor's player_dll_sha256 when it has one.

Issue 4252 (--major-civs 43): the player's DLL is the tag's `(3b) 43 Civs Community Patch/CvGameCore_Expansion2.dll`
(the DLL upstream's installer writes into (1) for a 43-civ install, research/03-dll-engine-boundary/player-limits.md
1.3), checked against the descriptor's player_dll_43_sha256 when it has one; pack.py gets --major-civs 43.

Issue 2602 (MC #3197): when the descriptor has the optional shell_config ("thin" | "i2"), pack.py gets
--shell-config with it (written into manifest.json; a build whose configuration disagrees is refused, a failure of
this stage). Without it every command, input and output is the one before 2602.

Issue 0464 (MC #3827): the stage fails when the descriptor the run starts from names a shell_config and the one this
stage reads (testdata's candidate) names another or none (_common.shell_config_of); testdata's candidate carries the
field since 0464.

Ticket 0574 (D195, release 0.1b): shell_config "i3" (the i2 build and an option set). The descriptor's options are
written to <work>/inputs/options.json and pack.py gets --options with it; the set is part of the stage's inputs and
outputs. thin and i2 runs are unchanged.
"""
from __future__ import annotations

from take_release import StageResult, sha256_bytes
import json
from stages import _common

DEPENDS = ['survey', 'build', 'purity']
PLAYER_DLL = '(1) Community Patch/CvGameCore_Expansion2.dll'
PLAYER_DLL_BY_MAJOR = {22: PLAYER_DLL, 43: '(3b) 43 Civs Community Patch/CvGameCore_Expansion2.dll'}   # issue 4252
DESC_FIELD_BY_MAJOR = {22: 'player_dll_sha256', 43: 'player_dll_43_sha256'}


def major_of(ctx) -> int:
    return int(ctx.config.get('major_civs') or 22)


def shell_config_of(ctx):
    """The descriptor's shell_config (issue 2602), or None when it has none."""
    return getattr(ctx.desc, 'shell_config', None)


def options_of(ctx):
    """The i3 option set (ticket 0574): the descriptor's options for shell_config 'i3' ({} when it has none), else
    None."""
    if shell_config_of(ctx) != 'i3':
        return None
    return getattr(ctx.desc, 'options', None) or {}


def inputs(ctx):
    inp = {'player_dll_sha256': getattr(ctx.desc, 'player_dll_sha256', None)}
    if major_of(ctx) != 22:   # absent for 22: the inputs hash of a 22 run is unchanged
        inp['major_civs'] = major_of(ctx)
        inp['player_dll_43_sha256'] = getattr(ctx.desc, 'player_dll_43_sha256', None)
    if shell_config_of(ctx):   # issue 2602; absent: the inputs hash of a run without it is unchanged
        inp['shell_config'] = shell_config_of(ctx)
    if options_of(ctx) is not None:   # ticket 0574; i3 only
        inp['options'] = options_of(ctx)
    return inp


def run(ctx):
    res, _ = _common.shell_config_of(ctx)   # issue 0464: refuse a candidate that lost the run's shell_config
    if res:
        return res
    tag_commit = ctx.outputs('survey')['tag_commit']
    b, pu = ctx.outputs('build'), ctx.outputs('purity')
    major = major_of(ctx)
    if major not in PLAYER_DLL_BY_MAJOR:
        return StageResult.stop(f'upstream ships no 32-bit DLL for {major} major civilizations (known: '
                                f'{", ".join(map(str, sorted(PLAYER_DLL_BY_MAJOR)))})',
                                'name the 32-bit DLL a player of that count installs (pack.py TAG_DLL_BY_MAJOR and this '
                                'stage\'s PLAYER_DLL_BY_MAJOR), then run again')
    if int(b.get('major_civs', 22)) != major:
        return StageResult.failed(f'the build stage built {b.get("major_civs", 22)} major civilizations, the run asks '
                                  f'for {major}')
    PLAYER_DLL = PLAYER_DLL_BY_MAJOR[major]
    blob = ctx.git('show', f'{tag_commit}:{PLAYER_DLL}', binary=True)
    if not blob:
        return StageResult.stop(f'the tag {ctx.tag} has no {PLAYER_DLL} in the fork',
                                'find the 32-bit DLL players install with this release and name it in the descriptor '
                                '(issue 3501), then run again')
    got = sha256_bytes(blob)
    field = DESC_FIELD_BY_MAJOR[major]
    want = getattr(ctx.desc, field, None)
    if want and got != want.lower():
        return StageResult.stop(f'the tag\'s {PLAYER_DLL} is {got[:16]}..., the descriptor says {want[:16]}...',
                                f'fix the descriptor\'s {field} (issue 3501) or the fork\'s tag, then run '
                                'again')
    d = ctx.dir('inputs', 'player-x86')
    (d / 'CvGameCore_Expansion2.dll').write_bytes(blob)
    opts = options_of(ctx)
    opt_args = []
    if opts is not None:   # ticket 0574: the i3 option set, as a file for pack.py --options
        of = ctx.dir('inputs') / 'options.json'
        of.write_bytes((json.dumps(opts, indent=1, sort_keys=True) + chr(10)).encode('utf-8'))
        opt_args = ['--options', of]
    out = _common.fresh_dir(ctx.work / 'pack')
    res, r = _common.call_entry(ctx, 'pack', '3504',
                                ['--layout', b['x64_layout'], '--purity', pu['purity_tsv'], '--x86-dll',
                                 d / 'CvGameCore_Expansion2.dll', '--release', ctx.tag, '--out', out,
                                 '--fork-commit', b['x64_commit'], '--tools-commit', b['tools_commit']]
                                + (['--major-civs', major] if major != 22 else [])
                                + (['--shell-config', shell_config_of(ctx)] if shell_config_of(ctx) else [])
                                + opt_args,
                                name='pack', heavy=False)
    if res:  # exit 10 = a stop (README 2a: the tag's own 32-bit DLL is missing)
        return res
    manifest = out / 'vp64pack' / 'manifest.json'
    if not manifest.is_file():
        return StageResult.failed(f'pack.py exit 0 but {manifest} is missing')
    return StageResult.ok(pack=str(out / 'vp64pack'), manifest=str(manifest), player_dll_sha256=got,
                          **({'major_civs': major, 'player_dll': PLAYER_DLL} if major != 22 else {}),
                          **({'shell_config': shell_config_of(ctx)} if shell_config_of(ctx) else {}),
                          **({'options': opts} if opts is not None else {}))
