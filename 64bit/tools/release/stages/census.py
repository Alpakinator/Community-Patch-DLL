"""census (CL step 8): the save-width (size_t) census of the x64 source, both architectures
(tools/x64-dll/save-audit/save-width-census.py, under quietlock.py run; about 8 min with --jobs 16). Pass = x64-only.txt
empty: no place where the DLL writes or reads a 64-bit integer through FDataStream only on x64. A new site is a stop:
it changes the save bytes and breaks D10.
"""
from __future__ import annotations

from take_release import StageResult
from stages import _common

DEPENDS = ['build']
JOBS = 16


def _sites(p):
    if not p.is_file():
        return None
    return [ln for ln in p.read_text(encoding='utf-8', errors='replace').splitlines()
            if ln.strip() and not ln.lstrip().startswith('#')]


def run(ctx):
    src = ctx.outputs('build')['x64_src']
    out = _common.fresh_dir(ctx.scratch() / 'census')
    r = ctx.run(ctx.tool('census') + [src, out, '--arch', 'both', '--jobs', JOBS], name='census',
                what=f'{ctx.who} take_release {ctx.tag} save-width census')
    if r.rc != 0:
        return StageResult.failed(f'save-width-census.py exit {r.rc}; {r.log}\n{r.tail(8)}')
    only, x86, x64 = (_sites(out / n) for n in ('x64-only.txt', 'x86-sites.txt', 'x64-sites.txt'))
    if only is None:
        return StageResult.failed(f'save-width-census.py exit 0 but {out / "x64-only.txt"} is missing')
    if only:
        return StageResult.stop(
            f'{len(only)} x64-only save-width site(s) ({only[0][:120]}{" ..." if len(only) > 1 else ""}), '
            f'listed in {out / "x64-only.txt"}',
            'pin each new site as in tools/x64-dll/save-layout-audit.md (FStlSerializedCount(), 32-bit members) in '
            'the carry, then run again from branches', x64_only=len(only))
    return StageResult.ok(out=str(out), x64_only=0, x86_sites=len(x86 or []), x64_sites=len(x64 or []),
                          seconds=round(r.seconds))
