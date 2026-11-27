"""wholegames (CL step 14, the whole-game re-validation; validated tier, issue 3506): whole AI games to the end on the
release's x64 server and on its 32-bit DLL (c5h), on the testbeds, compared table by table.

One job per testbed host (`tools/testbed/tb.py submit`, not --quiet: these are not timings; one job per host keeps to
"one runner job per place held", RECIPE-queue-place.md), each running tools/testbed/jobs/longgames.py with its games
(x64 and x86 run one after the other, then `--compare x64:x86`, tools/d10/statscmp.py over every table) with the
builds of the `build` stage shipped as files: the x64 layout's bin/ and the plain 32-bit DLL (the one the save test
uses). AI logging off, statistics on, the testbed's pinned database (D24 conditions of run_d10.py / longgames.py).
The job ids are recorded as soon as they are submitted: a rerun with the same inputs waits for the same jobs instead of
submitting again. Waiting is `tb.py wait --timeout 170` re-armed (COMMON 21), with the stop file checked between.

Per game the result is "identical", "identical but known" (only the columns in KNOWN_EXCEPTIONS differ, with the same
rows; research/06-tooling-and-workflows/quiet-testbed.md 14.6: ReligionBeliefYields.IsReligionOwner reads memory that
neither build sets, it differs between two machines on the 32-bit DLL alone), or "different". Any "different" is a
stop and the report's verdict is "candidate, parity failed" (decision 1 of the draft, the owner's), never "validated".
A run that does not end with a pass verdict (crash, stall, Lua-error-free end not reached) is a stop too; a job that cannot
be submitted or fetched is a failure.

More than 22 majors (issue 0372; --major-civs N of issue 4252). The GAMES seat the world's default player count (at most
12), so they never use the slots a 43 build adds. When the run's major count N is above 22 the plan also holds every
MAJOR_GAMES game that fits (majors < N: autoplay needs a free major slot for the observer), first in the plan, on both
arms: the x64 arm plays the repository's spec (longgames GAME spec:PATH), the x86 arm a spec resolved for N slots with
c5h's resolver against the descriptor's test database, as tools/d10/run_d10.py x86_spec_for does (c5h's resolver stops
at 21 majors), shipped to the testbed with --file. Every game records the majors it seated on each arm (longgames
run.json gameResult.majors, from stats.db). A run of N > 22 is refused (a stop, never "validated") when its plan has no
such game, when a game seated another count than its spec, or when no game with more than 22 majors on both arms ended
x64 = x86. For N = 22 the plan, the inputs and the commands are exactly those before 0372.

Hosts: C5_RELEASE_TESTBEDS names tb.py hosts, and `local` (or local1, local2, ...: one job each, run at the same time)
runs longgames.py on this machine through quietlock.py run, with the descriptor's database, localization and game folder
and c5h at C5H (for a workstation run of the stage: issue 0372). C5_RELEASE_WHOLEGAMES_GAMES (comma-separated short
names) plays only those games: a subset is always a stop, never "validated", and the report says so.
"""
from __future__ import annotations

import json
import os
import re
import shutil
import sys
import threading
import time
from pathlib import Path

from take_release import HERE, REPO, StageResult, release_settings, sha256_file
from stages import report
from stages import validated as V

DEPENDS = ['build']
USES = ['savetest']
# (short name, longgames GAME). Spread over the hosts in order (round robin). Seeds from 9045's re-validation set
# (upstream-fix-batch-1.md 8f), where every one was x64 = x86 on the D96 pins.
GAMES = [
    ('tc', 'new:TINY:Continents:5051:5253'),
    ('sf', 'new:STANDARD:Fractal:5757:6868'),
    ('sp', 'new:STANDARD:Pangaea:3333:4444'),
    ('mp', 'new:SMALL:Pangaea:1819:2021'),
]
MAJOR_CIVS_DEFAULT = 22
# Issue 0372: (short name, spec file relative to the repository, majors). Played only when the run's major count is
# above 22 and above the game's majors. l30 = 4254's 30-major Large game (player-limits.md 8.3: x64 43 = x86 43, turn
# 387, 3,613,758 rows identical, by hand); run_d10.py's `large30` is the same spec.
MAJOR_GAMES = [
    ('l30', 'server/tests/specs/continents-large-30majors-fixed-seed.json', 30),
]
# c5h, as run_d10.py: its spec resolver and, for `local`, its host. Ticket 4711: the setting c5h of
# tools/release/settings.py (C5_C5H; default <home>/Documents/civ5-headless, the folder named here before).
C5H = release_settings.get('c5h')
GAMES_SETTING = 'C5_RELEASE_WHOLEGAMES_GAMES'
LONGGAMES = [sys.executable, '-I', str(REPO / 'tools' / 'testbed' / 'jobs' / 'longgames.py')]   # a `local` job's command
TO_TURN = 1000            # longgames: past the end = play to the end
JOB_TIMEOUT_S = 5 * 3600
WAIT_S = 170
KNOWN_EXCEPTIONS = {('ReligionBeliefYields', 'IsReligionOwner'):
                    'unset memory on both builds (quiet-testbed.md 14.6; differs on the 32-bit DLL across machines)'}
FINAL = ('done', 'failed', 'cancelled')


def hosts():
    return [h.strip() for h in V.env_settings()['testbeds'].split(',') if h.strip()]


def is_local(host) -> bool:
    """`local`, `local1`, `local2`, ...: longgames.py on this machine (issue 0372), not a tb.py host."""
    return re.fullmatch(r'local\d*', host) is not None


def major_of(ctx) -> int:
    return int(ctx.config.get('major_civs') or MAJOR_CIVS_DEFAULT)


def subset():
    """The short names of C5_RELEASE_WHOLEGAMES_GAMES, or None when it is not set."""
    v = V.setting(GAMES_SETTING, None)
    return [x.strip() for x in v.split(',') if x.strip()] if v else None


def major_games(n):
    """The MAJOR_GAMES entries (name, 'spec:PATH', majors) a run of n major slots plays."""
    if n <= MAJOR_CIVS_DEFAULT:
        return []
    return [(name, f'spec:{spec}', majors) for name, spec, majors in MAJOR_GAMES if majors < n]


def games_for(n=MAJOR_CIVS_DEFAULT, only=None):
    """The games of a run of n major slots, the major games first (so that they go to different hosts)."""
    gs = major_games(n) + list(GAMES)
    return [g for g in gs if only is None or g[0] in only]


def plan(n=MAJOR_CIVS_DEFAULT, only=None):
    """{host: [(name, game[, majors]), ...]} for the configured hosts. For n = 22 and no subset: the plan before 0372."""
    hs = hosts()
    out = {h: [] for h in hs}
    for i, g in enumerate(games_for(n, only)):
        out[hs[i % len(hs)]].append(g)
    return {h: gs for h, gs in out.items() if gs}


def inputs(ctx):
    n, only = major_of(ctx), subset()
    d = {'plan': plan(n, only), 'to': TO_TURN, 'exceptions': sorted(f'{t}.{c}' for t, c in KNOWN_EXCEPTIONS)}
    if n != MAJOR_CIVS_DEFAULT:   # the 22 inputs hash stays the one before 0372
        d['major_civs'] = n
    if only is not None:
        d['games'] = only
    return d


def x86_game(name, game, local_specs=None):
    """The x86 arm's GAME: a spec: game plays the spec resolved for the run's slots (shipped as rel/specs/, or the
    local file), every other game the same GAME as x64."""
    if not game.startswith('spec:'):
        return game
    if local_specs is not None:
        return f'spec:{Path(local_specs[name]).as_posix()}'
    return f'spec:%TESTBED_BIN%/rel/specs/{name}-x86.json'


def job_command(ctx, games, label, local=None):
    """The longgames.py command of one job. local: None for a testbed (%VARS% expanded by the host's cmd.exe), else
    {'x64_bin', 'i0_dll', 'specs'} with this machine's paths."""
    if local is None:
        cmd = ['%C5H_PYTHON%', 'tools/testbed/jobs/longgames.py', '--x64-bin', '%TESTBED_BIN%/rel/x64/bin',
               '--x86-dll', '%TESTBED_BIN%/rel/x86/CvGameCore_Expansion2.dll', '--label', label]
    else:
        cmd = [str(x) for x in LONGGAMES] + [
               '--x64-bin', str(local['x64_bin']), '--x86-dll', str(local['i0_dll']), '--label', label]
    for g in games:
        name, game = g[0], g[1]
        g86 = x86_game(name, game, None if local is None else local['specs'])
        cmd += ['--run', f'{name}-x64,x64,{game},{TO_TURN}', '--run', f'{name}-x86,x86,{g86},{TO_TURN}',
                '--compare', f'{name}-x64:{name}-x86']
    return cmd


def resolve_x86_spec(ctx, spec_rel, majors, n, out: Path) -> Path:
    """The spec the x86 arm plays for a game of more than 22 majors: c5h's resolver with n major slots against the
    descriptor's test database (tools/d10/run_d10.py x86_spec_for, which resolves against the current release's)."""
    sys.path.insert(0, str(C5H / 'python'))
    from civ5headless import spec as gamespec  # noqa: E402  (civ5-headless, used as a tool)
    old = gamespec.MAX_MAJOR_CIVS
    gamespec.MAX_MAJOR_CIVS = n
    try:
        r = gamespec.resolve(json.loads((REPO / spec_rel).read_text(encoding='utf-8')), ctx.desc.test_db)
    finally:
        gamespec.MAX_MAJOR_CIVS = old
    if r.get('majors') != majors or len(r.get('slots') or []) != n:
        raise ValueError(f"{spec_rel}: resolved spec has majors {r.get('majors')} and {len(r.get('slots') or [])} "
                         f"slots, expected {majors} and {n}")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(r, indent=1), encoding='utf-8')
    return out


def submit(ctx, host, games, b, specs=None):
    label = f'{ctx.tag} validated'
    args = ['--host', host, 'submit', '--name', f'{ctx.who} take_release {ctx.tag} wholegames {host}',
            '--timeout', JOB_TIMEOUT_S, '--file', f'{b["x64_bin"]}=rel/x64/bin',
            '--file', f'{b["i0_dll"]}=rel/x86/CvGameCore_Expansion2.dll']
    for g in games:
        if g[1].startswith('spec:'):
            args += ['--file', f'{specs[g[0]]}=rel/specs/{g[0]}-x86.json']
    # longgames.py reads this run's descriptor (mods): the repository's releases/<tag>.json by its tag, else (issue
    # 3512: a new release's candidate, a --descriptor file) the file shipped with the job and named by its path there
    # (descriptor.py expands %TESTBED_BIN%: the host's runner does not)
    rel = ctx.release_env() if hasattr(ctx, 'release_env') else (
        ctx.tag if (HERE / 'releases' / f'{ctx.tag}.json').is_file() else None)
    if rel == ctx.tag:
        args += ['--env', f'C5_RELEASE={ctx.tag}']
    elif rel:
        args += ['--file', f'{rel}=rel/release/{ctx.tag}.json',
                 '--env', f'C5_RELEASE=%TESTBED_BIN%/rel/release/{ctx.tag}.json']
    r = ctx.run(V.tool(ctx, 'tb') + args + ['--'] + job_command(ctx, games, label), name=f'submit-{host}',
                heavy=False)
    m = re.search(r'^submitted (\S+) to', r.text, re.M)
    if r.rc != 0 or not m:
        return None, f'tb.py submit to {host}: exit {r.rc}; {r.log}\n{r.tail(6)}'
    return m.group(1), None


def first_json(text):
    i = text.find('{')
    if i < 0:
        return None
    try:
        return json.JSONDecoder().raw_decode(text[i:])[0]
    except ValueError:
        return None


def job_state(ctx, host, job):
    r = ctx.run(V.tool(ctx, 'tb') + ['--host', host, '--json', 'status', job], name=f'status-{host}', heavy=False)
    st = first_json(r.text) or {}
    for j in st.get('jobs') or []:
        if j.get('id') == job:
            return j.get('state'), j
    return None, {}


def wait_all(ctx, jobs, deadline):
    """Wait until every job is final. {host: state}"""
    states = {}
    while True:
        ctx.check_stop()
        for host, job in jobs.items():
            if states.get(host) in FINAL:
                continue
            states[host], _ = job_state(ctx, host, job)
        if all(s in FINAL for s in states.values()):
            return states
        if time.time() > deadline:
            return states
        host = next(h for h in jobs if states.get(h) not in FINAL)
        ctx.run(V.tool(ctx, 'tb') + ['--host', host, 'wait', jobs[host], '--timeout', WAIT_S], name=f'wait-{host}',
                heavy=False)


def parity(cmp: dict) -> tuple[str, list]:
    """('identical' | 'identical but known' | 'different', [what differs]) of a statscmp report."""
    if cmp.get('error'):
        return 'different', [f"compare failed: {cmp['error']}"]
    if cmp.get('identical'):
        return 'identical', []
    diffs, known = [], []
    for t in ('onlyInA', 'onlyInB'):
        if cmp.get(t):
            diffs.append(f'tables only in {t[-1]}: {cmp[t]}')
    for p in cmp.get('plausibility') or []:
        diffs.append(f'implausible: {p}')
    for t in cmp.get('tables') or []:
        if t.get('identical'):
            continue
        cols = t.get('columnsDiffering') or {}
        unknown = [c for c in cols if (t['table'], c) not in KNOWN_EXCEPTIONS]
        if t.get('schemaDiffers') or t.get('rowsA') != t.get('rowsB') or unknown or not cols:
            first = next(iter((cols[c] for c in unknown)), None) if unknown else None
            diffs.append(f"{t['table']}: rows {t.get('rowsA')}/{t.get('rowsB')}, columns {unknown or list(cols)}"
                         + (f", first turn {first.get('firstTurn')}" if first else
                            f", first differing turn {t.get('firstTurnDiffering')}"))
        else:
            known += [f"{t['table']}.{c} ({cols[c]['rows']} rows)" for c in cols]
    if not cmp.get('tablesCompared') or not cmp.get('rowsCompared'):
        diffs.append('nothing compared')
    if diffs:
        return 'different', diffs
    return 'identical but known', known


def collect(ctx, host, job, games):
    """Fetch the job and read its runs and comparisons. A list of game results."""
    out = ctx.work / 'wholegames' / f'{host}-{job}'
    r = ctx.run(V.tool(ctx, 'tb') + ['--host', host, 'fetch', job, '--out', out], name=f'fetch-{host}', heavy=False)
    lg = out / 'out' / 'longgames.json'
    if r.rc != 0 or not lg.is_file():
        return None, f'tb.py fetch {job} from {host}: exit {r.rc}, {lg} missing; {r.log}\n{r.tail(6)}'
    return read_results(host, job, out, games), None


def local_out(ctx, host) -> Path:
    return ctx.work / 'wholegames' / f'{host}-local'


def local_done(path) -> bool:
    """A local job's longgames.json that ended (meta.ended) with every run passed: reused by a rerun with the same
    inputs. A job with a failed run is played again (the stop's unblock is "fix the cause and run again")."""
    lg = Path(path) / 'out' / 'longgames.json'
    try:
        data = json.loads(lg.read_text(encoding='utf-8'))
        runs = data.get('runs') or []
        return bool(data.get('meta', {}).get('ended') and runs and
                    all(str(r.get('verdict') or '').startswith('pass') for r in runs))
    except (OSError, ValueError, AttributeError):
        return False


def c5h_exe():
    """This machine's c5h host, found as c5h's own config.find_exe does: <C5H>/out/latest.txt (a repository, the
    workstation), else <C5H>/bin/civ5headless.exe (a kit, a testbed). None when neither exists."""
    marker = C5H / 'out' / 'latest.txt'
    if marker.is_file():
        p = Path(marker.read_text(encoding='utf-8').strip())
        p = p / 'civ5headless.exe' if p.is_dir() else p
        if p.is_file():
            return p
    p = C5H / 'bin' / 'civ5headless.exe'
    return p if p.is_file() else None


def run_local(ctx, host, games, b, specs):
    """One job on this machine (issue 0372): longgames.py through quietlock.py run (ctx.run, heavy), with the
    environment a testbed runner gives it. (results, None) or (None, why)."""
    base = local_out(ctx, host)
    job = f'local-{host}'
    if local_done(base) and str(base) in (ctx.previous_outputs().get('local_done') or []):
        ctx.say(f'{host}: the local job of an earlier attempt ended; reading it ({base})')
        return read_results(host, job, base, games), None
    out = base / 'out'
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True, exist_ok=True)
    exe = c5h_exe()
    if exe is None:
        return None, (f'{host}: no civ5headless.exe in {C5H} (out/latest.txt of a repository, or bin/ of a kit): '
                      'the local x86 arm cannot start')
    work = ctx.scratch('wholegames', f'{host}-work')
    d = ctx.desc
    env = dict(os.environ, TESTBED_OUT=str(out), TESTBED_WORK=str(work), TESTBED_SAVES=str(work),
               TESTBED_DB=str(d.test_db), TESTBED_LOC_DB=str(d.loc_db), TESTBED_GAME_TOOLKIT=str(d.game_dir),
               TESTBED_C5H=str(C5H), TESTBED_C5H_EXE=str(exe), C5H_PYTHON=sys.executable, TESTBED_HOST=host,
               TESTBED_JOB=f'{job}-{time.strftime("%Y%m%d-%H%M%S")}', PYTHONIOENCODING='utf-8')
    cmd = job_command(ctx, games, f'{ctx.tag} validated', local={'x64_bin': b['x64_bin'], 'i0_dll': b['i0_dll'],
                                                                   'specs': specs})
    r = ctx.run(cmd, name=job, what=f'{ctx.who} take_release {ctx.tag} wholegames {host} (local longgames)',
                env=env, cwd=str(REPO))
    if not (out / 'longgames.json').is_file():
        return None, f'{host}: local longgames.py exit {r.rc}, {out / "longgames.json"} missing; {r.log}\n{r.tail(6)}'
    return read_results(host, job, base, games), None


def read_results(host, job, out, games):
    """The runs and comparisons of a job's <out>/out/longgames.json (fetched, or written locally)."""
    lg = Path(out) / 'out' / 'longgames.json'
    data = json.loads(lg.read_text(encoding='utf-8'))
    runs = {x['name']: x for x in data.get('runs') or []}
    res = []
    for entry in games:
        name, game = entry[0], entry[1]
        g = {'host': host, 'job': job, 'game': game, 'name': name}
        if len(entry) > 2:
            g['majors_planned'] = entry[2]
        for arm in ('x64', 'x86'):
            x = runs.get(f'{name}-{arm}') or {}
            g[arm] = {k: x.get(k) for k in ('verdict', 'endTurn', 'rc', 'wallSeconds', 'luaErrors')}
            g[arm]['victory'] = ((x.get('gameResult') or {}).get('victory') or [None])[-1]
            g[arm]['majors'] = (x.get('gameResult') or {}).get('majors')
        seated = [g[a]['majors'] for a in ('x64', 'x86')]
        g['majors'] = min(seated) if all(isinstance(m, int) for m in seated) else None
        cj = Path(out) / 'out' / f'cmp-{name}-x64-{name}-x86' / 'compare.json'
        runs_pass = all(str((g[a] or {}).get('verdict') or '').startswith('pass') for a in ('x64', 'x86'))
        cmp = json.loads(cj.read_text(encoding='utf-8')) if cj.is_file() else None
        if cmp is not None and cmp.get('error') and not runs_pass:
            # issue 0372: a run that failed before it wrote stats.db is an unfinished game, not a parity result
            # (the 2026-11-15 live run: x86 host not found, "compare failed: no stats.db" read as "parity failed")
            g['parity'], g['differences'] = 'not compared', [f"compare failed: {cmp['error']}"]
        elif cmp is not None:
            g['parity'], g['differences'] = parity(cmp)
            g['rows'] = cmp.get('rowsCompared')
            tabs = cmp.get('tables') or []
            if tabs:   # issue 0372: each arm's own row count, so the report shows them side by side
                g['rows_x64'] = sum(t.get('rowsA') or 0 for t in tabs)
                g['rows_x86'] = sum(t.get('rowsB') or 0 for t in tabs)
                g['tables'] = len(tabs)
            g['compare'] = str(cj.with_name('compare.txt'))
        else:
            g['parity'], g['differences'] = 'not compared', [f'{cj} missing']
        g['runs_pass'] = runs_pass
        res.append(g)
    return {'meta': data.get('meta'), 'games': res, 'out': str(out)}


def run(ctx):
    return report.finish(ctx, _run(ctx))


def _run(ctx):
    b = ctx.outputs('build')
    n, only = major_of(ctx), subset()
    if only is not None:
        known = [g[0] for g in major_games(n) + list(GAMES)]
        bad = [x for x in only if x not in known]
        if bad or not only:
            return StageResult.failed(f'{GAMES_SETTING}={",".join(only)}: unknown or no games {bad}; the games of '
                                      f'this run are {known}')
    p = plan(n, only)
    if not p:
        return StageResult.failed('C5_RELEASE_TESTBEDS names no host')
    planned = [g for gs in p.values() for g in gs]
    if n > MAJOR_CIVS_DEFAULT and not any(len(g) > 2 and g[2] > MAJOR_CIVS_DEFAULT for g in planned):
        return StageResult.stop(
            f'--major-civs {n}: no whole game of the plan seats more than {MAJOR_CIVS_DEFAULT} majors '
            f'({[g[0] for g in planned]}; MAJOR_GAMES needs majors < {n}{f", {GAMES_SETTING} chose a subset" if only else ""})',
            f'play a game of more than {MAJOR_CIVS_DEFAULT} majors: unset {GAMES_SETTING} or add a game to MAJOR_GAMES '
            '(tools/release/stages/wholegames.py) and run the stage again', parity='incomplete', major_civs=n, plan=p)
    specs = {}
    for g in planned:
        if g[1].startswith('spec:'):
            dst = ctx.work / 'wholegames' / 'specs' / f'{g[0]}-x86.json'
            try:
                specs[g[0]] = str(resolve_x86_spec(ctx, g[1][5:], g[2], n, dst))
            except Exception as e:  # noqa: BLE001 - c5h missing, a database without the civs, a bad count
                return StageResult.failed(f'{g[0]}: the x86 spec for {n} major slots could not be resolved: '
                                          f'{type(e).__name__}: {e}', plan=p)
    if specs:
        ctx.partial(x86_specs={k: {'path': v, 'sha256': sha256_file(v).lower()} for k, v in specs.items()})
    jobs = dict(ctx.previous_outputs().get('jobs') or {})
    for host, games in p.items():
        if is_local(host):
            continue
        if host in jobs:
            ctx.say(f'{host}: job {jobs[host]} of an earlier attempt; waiting for it')
            continue
        job, err = submit(ctx, host, games, b, specs)
        if err:
            return StageResult.failed(err, jobs=jobs, plan=p)
        jobs[host] = job
        ctx.partial(jobs=jobs, plan=p)
    local = run_locals(ctx, {h: gs for h, gs in p.items() if is_local(h)}, b, specs)
    for host, (got, err) in local.items():
        if err:
            return StageResult.failed(err, jobs=jobs, plan=p)
    states = wait_all(ctx, jobs, time.time() + JOB_TIMEOUT_S + 3600)
    if not all(s in FINAL for s in states.values()):
        return StageResult.failed(f'testbed jobs not finished within {JOB_TIMEOUT_S + 3600} s: {states}', jobs=jobs,
                                  plan=p, states=states)
    results, db = [], {}
    dll = Path(b['x64_bin']) / 'CvGameCore_Expansion2.dll'
    local_dll = sha256_file(dll).lower() if dll.is_file() else ''
    collected = []
    for host, job in jobs.items():
        got, err = collect(ctx, host, job, p[host])
        if err:
            return StageResult.failed(err, jobs=jobs, plan=p, states=states)
        collected.append((host, job, got))
    collected += [(host, f'local-{host}', got) for host, (got, _e) in local.items()]
    for host, job, got in collected:
        results += got['games']
        meta = got.get('meta') or {}
        db[host] = meta.get('db_sha256')
        shipped = (meta.get('x64_bin') or {}).get('CvGameCore_Expansion2.dll')
        if shipped and shipped.lower() != local_dll:
            return StageResult.failed(f'{host} job {job} ran x64 DLL {shipped[:16]}..., the build stage\'s is '
                                      f'{local_dll[:16]}...', jobs=jobs, plan=p, states=states)
    outputs = dict(jobs=jobs, plan=p, states=states, games=results, testbed_db_sha256=db)
    if local:
        outputs['local'] = {h: str(local_out(ctx, h)) for h in local}
    if n != MAJOR_CIVS_DEFAULT or any(g.get('majors') is not None for g in results):
        outputs['major_civs'] = n
        outputs['majors_played'] = max((g['majors'] for g in results if isinstance(g.get('majors'), int)),
                                       default=None)
    if only is not None:
        outputs['subset'] = only
    want_db = (getattr(ctx.desc, 'test_db_sha256', None) or '').lower()
    other_db = {h: v for h, v in db.items() if want_db and v and v.lower() != want_db}
    differ = [g for g in results if g['parity'] == 'different']
    unrun = [g for g in results if not g['runs_pass'] or g['parity'] == 'not compared']
    summary = '; '.join(f"{g['host']} {g['game']}: {g['parity']}" + (f" ({g['majors']} majors)" if g.get('majors')
                                                                      else '') for g in results)
    if differ:
        outputs['parity'] = 'failed'
        return StageResult.stop(
            f'x64 != x86 in {len(differ)} of {len(results)} whole games: ' +
            '; '.join(f"{g['host']} {g['game']}: {g['differences'][:2]}" for g in differ),
            'the result is "candidate, parity failed" (decision 1 of the draft, the owner\'s): find the cause '
            '(tools/drift/, research/03-dll-engine-boundary/stl-drift-sites.md), file a bug, fix it and run the tier again', **outputs)
    if unrun:
        outputs['parity'] = 'incomplete'
        return StageResult.stop(
            f'{len(unrun)} of {len(results)} whole games did not finish or were not compared: ' +
            '; '.join(f"{g['host']} {g['game']}: x64 {g['x64'].get('verdict')}, x86 {g['x86'].get('verdict')}, "
                      f"{g['parity']}" for g in unrun),
            'read the jobs\' logs (tb.py --host H logs JOB), fix the cause and run the tier again', **outputs)
    if n > MAJOR_CIVS_DEFAULT and not any(isinstance(g.get('majors'), int) and g['majors'] > MAJOR_CIVS_DEFAULT
                                          for g in results):
        outputs['parity'] = 'incomplete'
        return StageResult.stop(
            f'--major-civs {n}: every whole game played seated {MAJOR_CIVS_DEFAULT} majors or fewer on an arm '
            f'({summary}); a run of {n} major slots is not validated on 22-slot games',
            f'play a game of more than {MAJOR_CIVS_DEFAULT} majors on both arms (MAJOR_GAMES) and run the stage again',
            **outputs)
    wrong = [g for g in results if g.get('majors_planned') is not None and
             any(g[a].get('majors') != g['majors_planned'] for a in ('x64', 'x86'))]
    if wrong:
        outputs['parity'] = 'incomplete'
        return StageResult.stop(
            'a game seated another number of majors than its spec: ' +
            '; '.join(f"{g['host']} {g['name']}: x64 {g['x64'].get('majors')}, x86 {g['x86'].get('majors')}, spec "
                      f"{g['majors_planned']}" for g in wrong),
            'read the runs\' spec.json and stats.db (civ_turn_era): the x86 spec must be resolved for the build\'s '
            'slots and the DLL must be the --major-civs build; fix and run the stage again', **outputs)
    if other_db:
        outputs['parity'] = 'incomplete'
        return StageResult.stop(
            f'the testbeds played on another database than the release\'s ({other_db}, descriptor {want_db[:16]}...)',
            'ship the release\'s database to the testbeds (CL 17: commission.py ship-bases) and run again', **outputs)
    outputs['summary'] = summary
    if only is not None:
        outputs['parity'] = 'passed on a subset'
        return StageResult.stop(
            f'only the games {only} were played ({GAMES_SETTING}), every one x64 = x86: {summary}; a subset is never '
            'validated',
            f'unset {GAMES_SETTING} and run the stage again', **outputs)
    outputs['parity'] = 'passed'
    return StageResult.ok(**outputs)


def run_locals(ctx, local_plan, b, specs):
    """Run the local jobs at the same time (one thread each). {host: (results, why)}. A stop or gate refusal in a
    job is raised again here once every job has ended."""
    out, errors = {}, []

    def go(host, games):
        try:
            out[host] = run_local(ctx, host, games, b, specs)
        except BaseException as e:  # noqa: BLE001 - StopRequested / GateRefused go to the driver
            errors.append(e)

    th = [threading.Thread(target=go, args=(h, gs), daemon=True) for h, gs in local_plan.items()]
    for t in th:
        t.start()
    for t in th:
        t.join()
    if errors:
        raise errors[0]
    done = [str(local_out(ctx, h)) for h, (got, err) in out.items() if not err and local_done(local_out(ctx, h))]
    if done:
        ctx.partial(local_done=sorted(set((ctx.previous_outputs().get('local_done') or []) + done)))
    return out
