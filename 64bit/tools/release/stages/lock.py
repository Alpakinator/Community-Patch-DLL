"""lock (CL step 5): candidate lock texts <work>/deps/dll.lock and <work>/deps/i0.lock for the release's commits
(branch, commit, describe, base, fingerprint), never deps/ (the pins are the MC's, COMMON 18). The build stage adds
tools, epoch, verified and release to the same files. src_fingerprint.py --git is read-only (about 1 s).
"""
from __future__ import annotations

import time

from take_release import StageResult, release_settings
from stages import _common

DEPENDS = ['survey', 'branches']
FORK_URL_SETTING = 'fork_url'   # ticket 4711: the fork's address is a setting (tools/release/settings.py, C5_FORK_URL)


def repository():
    """The DLL fork's address for the lock texts' `repository =` key: the setting fork_url (environment variable
    C5_FORK_URL, else deps/local-settings.ini), None when it is not set. (Replaced: the constant REPOSITORY, which
    named the fork's address; the setting's value here is the same address.)"""
    return release_settings.get(FORK_URL_SETTING)


def inputs(ctx):
    return {'tag': ctx.tag}


def lock_text(kind, info: dict, extra: dict | None = None) -> str:
    """The text of a candidate lock (deps/*.lock key = value format) from the lock stage's (and the build's) outputs."""
    head = (f'# Candidate {kind} written by tools/release/take_release.py (issue 3502) for {info["tag"]}, '
            f'{time.strftime("%Y-%m-%d %H:%M")}.\n# A candidate, not a pin: deps/{kind} is the MC\'s (COMMON 18).\n')
    url = repository()
    if not url:
        raise release_settings.SettingError(f'setting {FORK_URL_SETTING} is not set: set the environment variable '
                                            f'{release_settings.ENV[FORK_URL_SETTING]} or `{FORK_URL_SETTING} = '
                                            '<address>` in deps/local-settings.ini (tools/release/settings.py)')
    keys = [('repository', url), ('branch', info['branch']), ('commit', info['commit']),
            ('describe', info['describe']), ('base', f'{info["tag"]} ({info["tag_commit"][:9]})')]
    if info.get('fingerprint'):
        keys.append(('fingerprint', info['fingerprint']))
    keys.append(('source', f'git -C <fork clone> archive -o <tmp>/src.tar {info["commit"][:9]}  then  tar -x'))
    for k, v in (extra or {}).items():
        keys.append((k, v))
    w = max(len(k) for k, _ in keys)
    lines = []
    for k, v in keys:
        vl = str(v).splitlines() or ['']
        lines.append(f'{k.ljust(w)} = {vl[0]}')
        lines += [' ' * (w + 3) + x for x in vl[1:]]
    return head + '\n'.join(lines) + '\n'


def run(ctx):
    try:
        if not repository():
            return StageResult.failed(f'setting {FORK_URL_SETTING} is not set (the fork\'s address for the candidate '
                                      f'locks): set {release_settings.ENV[FORK_URL_SETTING]} or `{FORK_URL_SETTING} = '
                                      '<address>` in deps/local-settings.ini (tools/release/settings.py)')
    except release_settings.SettingError as e:
        return StageResult.failed(str(e))
    br = ctx.outputs('branches')
    tag_commit = ctx.outputs('survey')['tag_commit']
    out = {}
    for kind, b in (('dll.lock', 'x64'), ('i0.lock', 'i0')):
        commit = ctx.git('rev-parse', '--verify', f'{br[f"{b}_commit"]}^{{commit}}', check=False)
        if not commit:
            return StageResult.stop(f'{b} commit {br[f"{b}_commit"]} is not in the fork clone {ctx.fork}',
                                    f'git -C {ctx.fork} fetch origin, then run again')
        info = {'tag': ctx.tag, 'tag_commit': tag_commit, 'branch': br.get(f'{b}_branch', ''), 'commit': commit,
                'describe': ctx.git('describe', '--tags', '--long', commit, check=False) or f'No-Tag {commit[:9]}'}
        if b == 'x64':
            r = ctx.run(ctx.tool('src_fingerprint') + ['--git', ctx.fork, '--commit', commit], name='fingerprint',
                        heavy=False)
            fp = [ln for ln in r.text.splitlines() if ln.startswith('fingerprint = ')]
            if r.rc != 0 or not fp:
                return StageResult.failed(f'src_fingerprint.py exit {r.rc}; {r.log}\n{r.tail(5)}')
            info['fingerprint'] = fp[-1].split('=', 1)[1].strip()
        out[b] = info
    d = ctx.dir('deps')
    for kind, b in (('dll.lock', 'x64'), ('i0.lock', 'i0')):
        (d / kind).write_text(lock_text(kind, out[b]), encoding='utf-8')
    p = _common.pins(ctx)
    pin_fp = p['fingerprint'].split()[1] if len(p['fingerprint'].split()) > 1 else ''
    return StageResult.ok(x64=out['x64'], i0=out['i0'], dll_lock=str(d / 'dll.lock'), i0_lock=str(d / 'i0.lock'),
                          fingerprint_matches_pin=bool(pin_fp) and out['x64']['fingerprint'].split()[1] == pin_fp,
                          same_commits_as_pins=(out['x64']['commit'] == p['x64_commit'] and
                                                out['i0']['commit'] == p['i0_commit']))
