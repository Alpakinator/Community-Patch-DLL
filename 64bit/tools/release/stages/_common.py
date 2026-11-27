"""Helpers shared by the stages of tools/release/take_release.py (issue 3502)."""
from __future__ import annotations

import re
import shutil
import time
from pathlib import Path

from take_release import REPO, STOP_LINE_RE, StageResult, not_built_yet, sha256_file

LOCK_KEY_RE = re.compile(r'^([a-z][a-z0-9-]*)\s*=\s?(.*)$')


def read_lock(path: Path) -> dict:
    """key = value lines of a deps/*.lock file, continuation lines (indented) folded into the key's value."""
    out, key = {}, None
    for line in Path(path).read_text(encoding='utf-8').splitlines():
        if not line.strip() or line.lstrip().startswith('#'):
            continue
        m = LOCK_KEY_RE.match(line)
        if m and not line[0].isspace():
            key = m.group(1)
            out[key] = m.group(2).strip()
        elif key and line[0].isspace():
            out[key] += '\n' + line.strip()
    return out


def deps_dir(ctx=None) -> Path:
    """deps/ of the repository (read only); config `deps_dir` is a test hook."""
    return Path((ctx.config.get('deps_dir') if ctx else None) or REPO / 'deps')


def pins(ctx=None):
    """The commits and branches of deps/i0.lock and deps/dll.lock (read only), and the base commit both name."""
    i0 = read_lock(deps_dir(ctx) / 'i0.lock')
    x64 = read_lock(deps_dir(ctx) / 'dll.lock')
    base = re.search(r'\(([0-9a-f]{7,40})\)', x64.get('base', ''))
    return {'i0_commit': i0['commit'], 'i0_branch': i0.get('branch', ''), 'x64_commit': x64['commit'],
            'x64_branch': x64.get('branch', ''), 'base_commit': base.group(1) if base else None,
            'base': x64.get('base', ''), 'fingerprint': x64.get('fingerprint', '').splitlines()[0] if
            x64.get('fingerprint') else ''}


def stop_from_tool(res, default_what, default_unblock):
    """A tool's own stop line (`... STOP <stage> (CL step n): what; to unblock: ...`) passed through, else the default."""
    for line in reversed(res.text.splitlines()):
        m = STOP_LINE_RE.search(line)
        if m:
            return StageResult.stop(m.group('what'), m.group('unblock'), cl_step=m.group('cl'))
    return StageResult.stop(default_what, default_unblock)


def call_entry(ctx, tool, issue, args, *, name, heavy=False, gated=False):
    """Call another issue's entry point: 'not built yet' while its script is missing; exit 10 = its stop passed
    through; another non-zero exit = failed. Returns (StageResult or None, RunResult or None): None result = exit 0."""
    missing = ctx.tool_missing(tool)
    if missing:
        return not_built_yet(issue, f'{missing} missing'), None
    r = ctx.run(ctx.tool(tool) + [str(a) for a in args], name=name, heavy=heavy, gated=gated)
    if r.rc == 10:
        return stop_from_tool(r, f'{tool} stopped (exit 10); see {r.log}', f'read {r.log}'), r
    if r.rc != 0:
        return StageResult.failed(f'{tool} exit {r.rc}; see {r.log}\n{r.tail(8)}'), r
    return None, r


def fresh_dir(p: Path) -> Path:
    """p, made empty: an earlier attempt's folder is kept, renamed <p>.old-<stamp> (never deleted)."""
    p = Path(p)
    if p.exists():
        p.rename(p.with_name(f'{p.name}.old-{time.strftime("%Y%m%d-%H%M%S")}'))
    p.parent.mkdir(parents=True, exist_ok=True)
    return p


def hash_files(folder: Path, rels):
    return {r: {'sha256': sha256_file(folder / r).upper(), 'bytes': (folder / r).stat().st_size} for r in rels}


def write_sha_file(folder: Path, rels):
    lines = [f'{sha256_file(folder / r).upper()}  {r}  {(folder / r).stat().st_size} B' for r in rels]
    (folder / 'SHA256.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')


def copy_into(src: Path, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def shell_config_of(ctx):
    """(StageResult or None, shell_config): the release's shell configuration as the stage's descriptor names it (issue
    2602, the optional shell_config: 'thin' | 'i2'; None = absent = thin). Issue 0464 (MC #3827): fail loud when the
    descriptor the run starts from (releases/<tag>.json, --descriptor or the --base-release one) names a shell_config
    and the stage's descriptor (testdata's candidate after testdata, README 1a) names another or none: before 0464 the
    candidate dropped the field and an i2 release was built and packed as thin without a word. The stage never reads
    the value from the other descriptor."""
    v = getattr(ctx.desc, 'shell_config', None)
    run_desc = getattr(getattr(ctx, '_d', None), 'desc', None)
    want = getattr(run_desc, 'shell_config', None)
    if want is not None and v != want:
        return StageResult.failed(
            f'the run\'s descriptor ({getattr(run_desc, "tag", "?")}) says shell_config {want!r}, the descriptor this '
            f'stage reads ({getattr(ctx.desc, "file", None) or getattr(ctx.desc, "tag", "?")}) says '
            f'{v if v is not None else "nothing (thin)"}: a candidate written before issue 0464, or edited by hand; '
            f'run testdata again (take_release.py {getattr(ctx, "tag", "<tag>")} --from testdata) or give the candidate '
            'the same shell_config'), None
    # ticket 0574: an i3 release's option set must reach the stage the same way (the candidate carries it)
    if want is not None and getattr(run_desc, 'options', None) != getattr(ctx.desc, 'options', None):
        return StageResult.failed(
            f'the run\'s descriptor ({getattr(run_desc, "tag", "?")}) has the option set '
            f'{getattr(run_desc, "options", None)!r}, the descriptor this stage reads '
            f'({getattr(ctx.desc, "file", None) or getattr(ctx.desc, "tag", "?")}) has '
            f'{getattr(ctx.desc, "options", None)!r}: run testdata again (take_release.py '
            f'{getattr(ctx, "tag", "<tag>")} --from testdata) or give the candidate the same options'), None
    return None, v


def tool_cmd(ctx, name, default) -> list:
    """The command of tool `name` from the driver's tool table when it has one (tests put a stand-in there), else
    `default` (issue 0464: shell/build.cmd, which take_release.default_tools does not list)."""
    tools = getattr(getattr(ctx, '_d', None), 'tools', None) or {}
    return [str(x) for x in (tools.get(name) or default)]


def file_sig(p) -> dict | None:
    """size and SHA-256 of a file, for a stage's inputs (None when missing)."""
    p = Path(p)
    if not p.is_file():
        return None
    return {'path': str(p), 'bytes': p.stat().st_size, 'sha256': sha256_file(p)}
