"""survey (CL step 1): what the release changes in the parts the patch reads or carries. Read-only git in the fork.

Old = the base of the pins (deps/dll.lock `base`), new = the release tag (or the --base-commit, issue 3509). Writes <work>/survey/survey.json and
survey.md with the flags of CL step 1: boundary headers changed, new engine calls, new and deleted source files, the
build list, serialization headers, the Lua surface and the overlap with the carry (files the pins change that the
release also changes). A report, never a stop by itself: the stages after it act on what changed (model, narrowing,
census, branches). Stops only when the tag or the pins' commits are missing in the fork or the tag is not the
descriptor's commit. A run started from the previous release's descriptor (take_release.py --base-release, issue 3512)
has no descriptor of this release yet: the check is left out (the candidate descriptor testdata writes takes its
upstream_commit from the same tag or commit), and the report says so (`descriptor_check`).
"""
from __future__ import annotations

import json
import re

from take_release import StageResult
from stages import _common

DEPENDS = []
BOUNDARY = ['CvGameCoreDLLUtil/include/CvDll*', 'CvGameCoreDLLUtil/include/ICv*', 'CvGameCoreDLL_Expansion2/CvDll*.h',
            'FirePlace/include/FireWorks/*']
SERIALIZATION = ['CvSerialize.h', 'FStlContainerSerialization.h', 'CvInfosSerializationHelper.h', 'FDataStream.h']
SOURCE_DIRS = ['CvGameCoreDLL_Expansion2', 'CvGameCoreDLLUtil', 'FirePlace']
ENGINE_CALL_RE = re.compile(r'(?:gDLL->|DLLUI->|GC\.getDLLIFace\(\)->|GC\.GetEngineUserInterface\(\)->)([A-Za-z0-9_]+)\(')
ENGINE_GREP = r'(gDLL->|DLLUI->|GC\.getDLLIFace\(\)->|GC\.GetEngineUserInterface\(\)->)[A-Za-z0-9_]+\('
LUA_GREP = r'Method\([A-Za-z0-9_]*\)'


def inputs(ctx):
    p = _common.pins(ctx)
    inp = {'tag': ctx.tag, 'upstream_commit': ctx.desc.upstream_commit, 'base': p['base_commit'],
           'x64_pin': p['x64_commit'], 'fork': str(ctx.fork)}
    if ctx.config.get('base_commit'):  # issue 3509; absent without it, so a tag's inputs hash is as before
        inp['base_commit'] = ctx.config['base_commit']
    return inp


def _ref(ctx) -> str:
    return getattr(ctx, 'ref', None) or ctx.tag


def _names(ctx, *args):
    out = ctx.git(*args)
    return sorted(x for x in out.splitlines() if x.strip()) if out else []


def _engine_calls(ctx, rev):
    out = ctx.git('grep', '-h', '-o', '-E', ENGINE_GREP, rev, '--', 'CvGameCoreDLL_Expansion2', check=False) or ''
    return sorted({m.group(1) for m in ENGINE_CALL_RE.finditer(out)})


def _lua_methods(ctx, rev):
    out = ctx.git('grep', '-h', '-o', '-E', LUA_GREP, rev, '--', 'CvGameCoreDLL_Expansion2/Lua/*.cpp', check=False) or ''
    return len([x for x in out.splitlines() if x.strip()])


def run(ctx):
    p = _common.pins(ctx)
    ref = _ref(ctx)
    by_commit = bool(ctx.config.get('base_commit'))
    tag_commit = ctx.git('rev-parse', '--verify', '--quiet', f'{ref}^{{commit}}', check=False)
    if not tag_commit:
        if by_commit:
            return StageResult.stop(f'base commit {ref} of {ctx.tag} (--base-commit) is not in the fork clone {ctx.fork}',
                                    f'git -C {ctx.fork} fetch <upstream remote> master (or the branch that holds '
                                    f'{ref}), then run again')
        return StageResult.stop(f'tag {ctx.tag} is not in the fork clone {ctx.fork}',
                                f'git -C {ctx.fork} fetch <upstream or fork remote> tag {ctx.tag}, then run again '
                                f'(a base that is not a tag: --base-commit <hash>)')
    want = str(ctx.desc.upstream_commit or '')
    from_base = getattr(ctx.desc, 'tag', ctx.tag) != ctx.tag   # issue 3512: --base-release, no descriptor of the tag yet
    if from_base:
        ctx.say(f'the run starts from {ctx.desc.tag}\'s descriptor (--base-release): its upstream_commit is not '
                f'{ctx.tag}\'s; not compared (testdata writes the candidate from {tag_commit[:12]})')
    elif not tag_commit.startswith(want.lower()):
        if by_commit:
            return StageResult.stop(f'base commit {ref} of {ctx.tag} is {tag_commit[:12]} in the fork, the descriptor '
                                    f'says {want}',
                                    'fix the descriptor\'s upstream_commit (issue 3501) or --base-commit, then run again')
        return StageResult.stop(f'tag {ctx.tag} is {tag_commit[:12]} in the fork, the descriptor says {want}',
                                'fix the descriptor (issue 3501) or the clone\'s tag, then run again')
    missing = [c for c in (p['base_commit'], p['x64_commit'], p['i0_commit'])
               if not c or not ctx.git('rev-parse', '--verify', '--quiet', f'{c}^{{commit}}', check=False)]
    if missing:
        return StageResult.stop(f'commits of deps/*.lock missing in the fork clone: {", ".join(map(str, missing))}',
                                f'git -C {ctx.fork} fetch origin (the project\'s fork has the pins), then run again')
    old, new = p['base_commit'], tag_commit
    rep = {'old': old, 'new': new, 'tag': ctx.tag, 'x64_pin': p['x64_commit'], 'i0_pin': p['i0_commit'],
           'base_ref': 'commit' if by_commit else 'tag',
           'descriptor_check': f'not made: the run starts from {ctx.desc.tag}\'s descriptor (--base-release)'
           if from_base else 'the tag is the descriptor\'s upstream_commit'}
    rep['commits'] = int(ctx.git('rev-list', '--count', f'{old}..{new}') or 0)
    rep['boundary_headers_changed'] = _names(ctx, 'diff', '--name-only', old, new, '--', *BOUNDARY)
    rep['serialization_headers_changed'] = [f for f in _names(ctx, 'diff', '--name-only', old, new, '--', *SOURCE_DIRS)
                                            if f.rsplit('/', 1)[-1] in SERIALIZATION]
    status = _names(ctx, 'diff', '--name-status', old, new, '--', *SOURCE_DIRS)
    rep['new_sources'] = sorted(ln.split('\t')[-1] for ln in status if ln[:1] in ('A', 'R'))
    rep['deleted_sources'] = sorted(ln.split('\t')[1] for ln in status if ln[:1] in ('D', 'R'))
    rep['dll_files_changed'] = len(status)
    rep['build_list_changed'] = bool(_names(ctx, 'diff', '--name-only', old, new, '--', 'build_vp_clang.py'))
    calls_old, calls_new = _engine_calls(ctx, old), _engine_calls(ctx, new)
    rep['engine_calls'] = {'old': len(calls_old), 'new': len(calls_new),
                           'added': sorted(set(calls_new) - set(calls_old)),
                           'removed': sorted(set(calls_old) - set(calls_new))}
    rep['lua_methods'] = {'old': _lua_methods(ctx, old), 'new': _lua_methods(ctx, new)}
    carried = set(_names(ctx, 'diff', '--name-only', old, p['x64_commit']))
    changed = set(_names(ctx, 'diff', '--name-only', old, new))
    rep['carry_overlap'] = sorted(carried & changed)
    flags = {
        'boundary_headers_changed': bool(rep['boundary_headers_changed']),
        'new_engine_call': bool(rep['engine_calls']['added']),
        'new_source_file': bool(rep['new_sources']),
        'carry_overlap': bool(rep['carry_overlap']),
        'serialization_headers_changed': bool(rep['serialization_headers_changed']),
        'build_list_changed': rep['build_list_changed'],
        'lua_surface_changed': rep['lua_methods']['old'] != rep['lua_methods']['new'],
    }
    rep['flags'] = flags
    d = ctx.dir('survey')
    (d / 'survey.json').write_text(json.dumps(rep, indent=1) + '\n', encoding='utf-8')
    md = [f'# Survey of {ctx.tag} ({new[:9]}) against the pins\' base {old[:9]} (CL step 1)', '',
          f'{rep["commits"]} commits, {rep["dll_files_changed"]} files in {", ".join(SOURCE_DIRS)}.', '',
          '| flag | value |', '|---|---|'] + [f'| {k} | {"YES" if v else "no"} |' for k, v in flags.items()] + ['']
    for k in ('boundary_headers_changed', 'serialization_headers_changed', 'new_sources', 'deleted_sources',
              'carry_overlap'):
        md += [f'## {k} ({len(rep[k])})', ''] + [f'- `{x}`' for x in rep[k]] + ['']
    md += [f'## engine calls: {rep["engine_calls"]["old"]} -> {rep["engine_calls"]["new"]}', '',
           f'added: {", ".join(rep["engine_calls"]["added"]) or "none"}; removed: '
           f'{", ".join(rep["engine_calls"]["removed"]) or "none"}', '',
           f'## Lua Method() tokens: {rep["lua_methods"]["old"]} -> {rep["lua_methods"]["new"]}', '']
    (d / 'survey.md').write_text('\n'.join(md), encoding='utf-8')
    ctx.say('flags:', ', '.join(k for k, v in flags.items() if v) or 'none')
    return StageResult.ok(tag_commit=new, old_base=old, report=str(d / 'survey.md'), flags=flags,
                          counts={'commits': rep['commits'], 'dll_files': rep['dll_files_changed'],
                                  'carry_overlap': len(rep['carry_overlap'])})
