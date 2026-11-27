"""take_release.py: take a Vox Populi release by one command (epic 3500, issue 3502).

    python -I tools/release/take_release.py <tag> [--work DIR] [--from STAGE | --only STAGE]
                                                  [--tier candidate|validated] [--fork CLONE] [--carry series|pins]
                                                  [--jobs N] [--ia-patch FILE|by-base] [--descriptor FILE]
                                                  [--gate-wait MIN] [--who ID]
                                                  [--major-civs N [--unvalidated-major-civs]]
                                                  [--for-owner | --not-for-owner]
                                                  [--series FILE] [--base-commit HASH] [--base-release TAG]
    python -I tools/release/take_release.py <tag> --owner-check [--work DIR] [--build-folder DIR]

--base-release TAG (issue 3512): a new release, with no tools/release/releases/<tag>.json yet, starts from the previous
release's descriptor (releases/TAG.json; `current` = current.json's release). The testdata stage takes it as its base
and writes the new release's candidate descriptor <work>/testdata/<tag>.json (the tag's mods and commit, the database
built from the tag, the tag's game folder, the base's reference saves, empty reference counts). Without the option,
a tag with no releases/<tag>.json and no --descriptor is exit 2 naming it. Recorded in the state and reused.

The run's descriptor (issue 3512): the stages up to testdata read the one the run starts from (releases/<tag>.json,
--descriptor FILE, or the --base-release one); every stage after testdata reads the candidate descriptor testdata
wrote (Driver.desc_for). Every tool the driver starts gets C5_RELEASE naming the descriptor of its stage: the tag when
that is releases/<tag>.json, else the file's path (descriptor.py reads either).

--series FILE (issue 3509): the carry series the branches stage applies instead of tools/release/series/carry.tsv,
for example a release copy with a conflict's resolved commit in its row (the branches stop's unblock). Recorded in the
state's config and reused; its SHA-256 is one of the branches stage's inputs, so a changed copy makes new branches.

--base-commit HASH (issue 3509): the release's base is this upstream commit (7-40 hex digits, e.g. upstream master at
a commit, or a pin batch taken from master) instead of the tag; the first argument is then a release name that need
not be a tag (letters, digits, . _ -, e.g. master-ba8aafdc). The commit must be in the fork clone and be the
descriptor's upstream_commit. Recorded in the state and reused; stages read the base as ctx.ref (the tag without it).

--for-owner (D162, the owner 2026-11-16 15:44; issue 0436): the take is for a build meant for the owner to play. Needs
--tier validated; recorded in the state. The hi_journey stage then gates: the tier is reached only when both HI release
journeys (release-basic, release-large) passed on this build, product and stock (stages/hi_journey.py owner_check());
a failed, incomplete or skipped stage is a STOP (exit 10) naming D162. Without it the stage reports only. Every run of
the validated tier prints one summary line `take_release: D162 HI release journeys of <tag>: verdict ...; for the owner:
yes | NO, refused by D162: ...` and records it as `d162` in the state. --owner-check answers the same question from the
state alone for a hand-out (exit 0 yes, 10 no); --build-folder also requires the staged files to be the bytes the
journeys were played on.

Old option names (ticket 0708): --for-art, --not-for-art and --art-check are hidden aliases of --for-owner,
--not-for-owner and --owner-check; they work as before and print one note naming the new option. A state written before
0708 records the marker as config `for_art` and hi_journey's answer as outputs `art`: both are still read.

The contract is tools/release/README.md section 2; the procedure it automates is
research/06-tooling-and-workflows/new-vp-release-checklist.md (CL step numbers); the design and results are in
research/06-tooling-and-workflows/release-driver.md (section "The driver").

State file <work>/state.json: one entry per stage {name, cl_step, status pending|running|ok|stop|failed|skipped,
started, ended, outputs, stop {what, cl_step, unblock}, inputs, inputs_sha}. A rerun resumes at the first stage that is
not ok (or skipped); an ok stage whose inputs changed (the hash of its declared inputs and of the outputs of the stages
it depends on) runs again, and so does every later stage whose inputs that changes.

Exit codes: 0 the tier asked for is reached (with --only: that stage is ok); 10 a stop (one line
`take_release: STOP <stage> (CL step <n>): <what>; to unblock: <what to do>`); 1 a failure (a tool crashed or failed);
3 a launch gate or the quiet-machine lock refused a heavy start (passed through, said so; the stage stays pending);
2 an argument error.

A stop file <work>/STOP ends the run before the next step (each heavy command and each stage checks it): exit 10,
the stage it stopped in stays pending. Remove the file and run again to resume.

--major-civs N (issue 4252, epic 0334; MC decisions G2, G3): the major-civilisation count of every DLL the run builds,
default 22. 43 builds the x64 and the 32-bit DLLs from the same commits with MOD_GLOBAL_MAX_MAJOR_CIVS=43 on the DLL's
compiler line (stages/build.py), names the layouts and lock keys with it (x64-<c8>-m43-p<t8>, i0ship-<c8>-m43,
release-43) and binds the pack to the tag's `(3b) 43 Civs Community Patch` 32-bit DLL (stages/pack.py). Only 22 and 43
are validated; another value of 2..62 needs --unvalidated-major-civs. The default work folder of a count other than 22
is <root>/<tag>-m<N>, so the two variants of a tag never share a work folder. The value is recorded in the state; a
work folder of one count refuses another (exit 2).

Every heavy command starts through a gated launch point (server/build_server.py, tools/d10/run_d10.py, the fix checks'
server/run_server.py) or through `perf/quietlock/quietlock.py run` (COMMON rule 16); never two at once. Nothing is
written outside --work (default C:/c5-64bit-work/3500/releases/<tag>/): never deps/*.lock, never the MC's pins folder
(the setting pins_dir of tools/release/settings.py; without it the driver refuses to run), never the game install.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import traceback
import types
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
# Stage modules `import take_release` and `from stages import _common`: one module object, also when run as a script.
sys.modules.setdefault('take_release', sys.modules[__name__])
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))
STAGES_DIR = HERE / 'stages'
DEFAULT_WORK_ROOT = Path('C:/c5-64bit-work/3500/releases')
import settings as release_settings  # noqa: E402  (tools/release/settings.py, ticket 4711: this machine's paths)
STATE_NAME = 'state.json'
STOP_NAME = 'STOP'
FORMAT = 1

EXIT_OK, EXIT_FAILED, EXIT_ARGS, EXIT_REFUSED, EXIT_STOP = 0, 1, 2, 3, 10
OK_STATES = ('ok', 'skipped')

# (name, CL step, issue that owns the stage's logic). 3502 owns every module except testdata.py (3501); the stages of
# other issues call their entry points and stop "not built yet (issue N)" while those are missing.
CANDIDATE = [
    ('survey', '1', '3502'),
    ('branches', '3', '3505'),
    ('testdata', '2', '3501'),
    ('lock', '5', '3502'),
    ('build', '6', '3502'),
    ('narrowing', '7', '3502'),
    ('census', '8', '3502'),
    ('model', '10', '3503'),
    ('purity', '11', '3503'),
    ('pack', '-', '3504'),
    ('offline', '12', '3502'),
    ('savetest', '13', '3502'),
    ('drift', '14', '3502'),
    ('fixchecks', '15', '3502'),
]
TIERS = ('candidate', 'validated')


def tier_stages(tier, stages_dir: Path | None = None):
    """The stages of a tier. validated = candidate + the registry `STAGES` [(name, CL step, owner)] of
    stages/validated.py (3506 owns it; this hook means it needs no edit here); while that module is missing, one
    placeholder stage `validated` that stops "not built yet (issue 3506)"."""
    if tier == 'candidate':
        return list(CANDIDATE)
    reg = Path(stages_dir or STAGES_DIR) / 'validated.py'
    if reg.is_file():
        mod = load_stage_module('validated', reg.parent)
        extra = [tuple(s) for s in getattr(mod, 'STAGES', [])]
        if extra:
            return list(CANDIDATE) + extra
    return list(CANDIDATE) + [('validated', '14-17', '3506')]

# Gate refusals print one of these (perf/quietlock/gate.py; quietlock.py run without --take).
REFUSAL_MARKERS = ('launch gate: REFUSED', 'refused, command not started')
STOP_LINE_RE = re.compile(r'STOP (?P<stage>\S+) \(CL step (?P<cl>[^)]*)\): (?P<what>.*?); to unblock: (?P<unblock>.*)$')


def now():
    return time.strftime('%Y-%m-%d %H:%M:%S')


def sha256_bytes(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def sha256_file(p) -> str:
    h = hashlib.sha256()
    with open(p, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def jhash(obj) -> str:
    return sha256_bytes(json.dumps(obj, sort_keys=True, default=str).encode('utf-8'))


class ArgError(Exception):
    """An argument or setup check failed (exit 2)."""


class StopRequested(Exception):
    """The stop file <work>/STOP exists."""


class GateRefused(Exception):
    """A launch gate or the quiet-machine lock refused a heavy start (exit 3)."""


@dataclass
class StageResult:
    status: str                      # ok | stop | failed | skipped
    outputs: dict = field(default_factory=dict)
    what: str = ''                   # stop: what happened; failed: why; skipped: why
    unblock: str = ''                # stop: what a person or agent does
    cl_step: str | None = None       # stop: the CL step the stop belongs to, when not the stage's own

    @classmethod
    def ok(cls, **outputs):
        return cls('ok', outputs)

    @classmethod
    def stop(cls, what, unblock, cl_step=None, **outputs):
        return cls('stop', outputs, what, unblock, cl_step)

    @classmethod
    def failed(cls, why, **outputs):
        return cls('failed', outputs, why)

    @classmethod
    def skipped(cls, why, **outputs):
        return cls('skipped', outputs, why)


def not_built_yet(issue, what=''):
    return StageResult.stop(f'not built yet (issue {issue}){": " + what if what else ""}',
                            f'land issue {issue} on the team branch, merge it, and run again')


@dataclass
class RunResult:
    rc: int
    log: Path
    seconds: float
    text: str            # this run's output (the part of the log it wrote)

    def tail(self, n=20):
        return '\n'.join(self.text.splitlines()[-n:])

    def lines(self, pattern):
        rx = re.compile(pattern)
        return [ln for ln in self.text.splitlines() if rx.search(ln)]


# --------------------------------------------------------------------------------------------------- the descriptor

import descriptor as _descriptor  # noqa: E402  3501's tools/release/descriptor.py (README section 1)

DescriptorError = _descriptor.DescriptorError


def load_descriptor(tag: str, explicit: Path | None, base_release: str | None = None, releases_dir: Path | None = None):
    """The descriptor a run starts from: an explicit file (--descriptor) parsed by 3501's rules, else the previous
    release's (--base-release, issue 3512; `current` = current.json's tag), else 3501's releases/<tag>.json. A tag
    without releases/<tag>.json and neither option is an ArgError that names --base-release."""
    if explicit:
        p = Path(explicit)
        return _descriptor.parse(_descriptor._read_json(p), str(p), expect_tag=tag)
    if base_release:
        rd = Path(releases_dir or _descriptor.RELEASES_DIR)
        if base_release == 'current':
            data = _descriptor._read_json(rd / _descriptor.CURRENT)
            base_release = data.get('tag') if isinstance(data, dict) else None
        return _descriptor.load(base_release, rd)
    p = _descriptor.path_for(tag, releases_dir)
    if not p.is_file():
        raise ArgError(f'no release descriptor {p}: a new release starts from the previous release\'s descriptor, '
                       f'--base-release <its tag> (e.g. --base-release current; the testdata stage writes '
                       f'{tag}\'s candidate descriptor and every later stage reads it; tools/release/README.md section '
                       f'1a), or name a hand-written file with --descriptor FILE')
    return _descriptor.load(tag, releases_dir)


def release_env(desc) -> str | None:
    """The C5_RELEASE value for a tool that must read `desc` (issue 3512): its tag when the file is the repository's
    releases/<tag>.json, else the file's absolute path; None when the descriptor has no file on disk (tests)."""
    tag = getattr(desc, 'tag', None)
    if not tag:
        return None
    repo = _descriptor.RELEASES_DIR / f'{tag}.json'
    f = Path(str(getattr(desc, 'file', '') or ''))
    if not f.is_absolute() or not f.is_file():   # parsed from data (the tests): the repository's file when there is one
        return desc.tag if repo.is_file() else None
    if repo.is_file() and f.resolve() == repo.resolve():
        return desc.tag
    return str(f.resolve())


# ------------------------------------------------------------------------------------------------------- tools

def default_tools():
    py = [sys.executable, '-I']
    ev = REPO / 'research/03-dll-engine-boundary/evidence/4000'
    t = {
        'quietlock': py + [str(REPO / 'perf/quietlock/quietlock.py')],
        'build_server': py + [str(REPO / 'server/build_server.py')],
        'build_all': py + [str(REPO / 'tools/x64-dll/build_all.py')],   # 4252: the x64 DLL of another major count
        'src_fingerprint': py + [str(REPO / 'tools/x64-dll/src_fingerprint.py')],
        'census': py + [str(REPO / 'tools/x64-dll/save-audit/save-width-census.py')],
        'test_proxy_rt': py + [str(REPO / 'proxy/tools/test_proxy_rt.py')],
        'hutest': py + [str(REPO / 'proxy/tools/hutest/build_hutest.py')],
        'run_proxy_game': py + [str(REPO / 'proxy/tools/run_proxy_game.py')],
        'run_d10': py + [str(REPO / 'tools/d10/run_d10.py')],
        'run_pair': py + [str(REPO / 'tools/drift/run_pair.py')],
        'run_server': py + [str(REPO / 'server/run_server.py')],
        'fix0096': py + [str(ev / '0096_run.py')],
        'fix0084': py + [str(ev / '0084_lock_test.py')],
        'fix0103': py + [str(ev / '0103_run.py')],
        'branches': py + [str(HERE / 'branches.py')],
        'pack': py + [str(HERE / 'pack.py')],
        'model': py + [str(REPO / 'proxy/gen/regen_model.py')],        # 3503, README 2a
        'purity': py + [str(REPO / 'proxy/tools/derive_purity.py')],   # 3503, README 2a
        'purity_test': py + [str(REPO / 'proxy/tools/test_purity.py')],   # 0480: the full test in stage purity
        # The fork's own build script runs from its source folder without -I (9045's build9045.py): -I would drop
        # the script's folder from sys.path.
        'build_vp_clang': [sys.executable, 'build_vp_clang.py'],
        'git': ['git'],
    }
    return t


# --------------------------------------------------------------------------------------------------- the state

class State:
    def __init__(self, path: Path, data: dict):
        self.path = path
        self.data = data

    @classmethod
    def load_or_new(cls, work: Path, tag: str):
        p = work / STATE_NAME
        if p.is_file():
            try:
                data = json.loads(_retry_sharing(lambda: p.read_text(encoding='utf-8')))
            except ValueError as e:
                raise ArgError(f'{p}: not readable JSON ({e}); move it away to start over')
            if data.get('tag') != tag:
                raise ArgError(f'{p} is the state of {data.get("tag")!r}, not {tag!r}: use another --work')
            if data.get('format') != FORMAT:
                raise ArgError(f'{p}: state format {data.get("format")!r}, this driver writes {FORMAT}')
            return cls(p, data)
        return cls(p, {'format': FORMAT, 'tag': tag, 'created': now(), 'config': {}, 'stages': [], 'runs': []})

    def entry(self, name, cl_step):
        for e in self.data['stages']:
            if e['name'] == name:
                return e
        e = {'name': name, 'cl_step': cl_step, 'status': 'pending', 'started': None, 'ended': None, 'outputs': {},
             'stop': None, 'inputs': None, 'inputs_sha': None}
        self.data['stages'].append(e)
        return e

    def get(self, name):
        for e in self.data['stages']:
            if e['name'] == name:
                return e
        return None

    def save(self):
        self.path.parent.mkdir(parents=True, exist_ok=True)
        tmp = self.path.with_suffix('.json.tmp')
        tmp.write_text(json.dumps(self.data, indent=1, default=str) + '\n', encoding='utf-8')
        _retry_sharing(lambda: os.replace(tmp, self.path))


def _retry_sharing(fn, tries: int = 50, wait_s: float = 0.1):
    """fn(), again on a Windows sharing violation (PermissionError) for up to tries * wait_s seconds. A tier take's
    parent replaces state.json while a stage worker it has just started reads it: the read or the replace then fails
    with Errno 13 for a moment (ticket 5024: test_tier_e2e under load, 2026-11-22, 'its stage worker exited 1')."""
    for i in range(tries):
        try:
            return fn()
        except PermissionError:
            if i == tries - 1:
                raise
            time.sleep(wait_s)


# --------------------------------------------------------------------------------------------------- the context

class Ctx:
    """What a stage sees: the descriptor (`desc`), the work folder, the state, the config, and `run`."""

    def __init__(self, driver, stage_name):
        self._d = driver
        self.stage = stage_name
        self.tag = driver.tag
        # the git revision of the release's base in the fork: --base-commit when given (issue 3509), else the tag
        self.ref = driver.config.get('base_commit') or driver.tag
        self.descriptor = driver.desc  # the name 3501's testdata.py reads: the descriptor the run starts from (its base)
        self.work = driver.work
        self.state = driver.state
        self.config = driver.config
        self.repo = REPO
        self.who = driver.config.get('who', '3500')

    @property
    def desc(self):
        """The descriptor this stage reads (issue 3512): up to testdata the one the run starts from; after it the
        candidate descriptor testdata wrote (Driver.desc_for). Read at each use: a DescriptorError (testdata not ok on a
        --base-release run, a candidate file missing or edited into an invalid one) fails the stage."""
        return self._d.desc_for(self.stage)

    def release_env(self) -> str | None:
        """C5_RELEASE for the tools this stage starts: the tag of releases/<tag>.json, else the descriptor file's
        path (issue 3512)."""
        return release_env(self.desc)

    # --- paths and other stages
    def dir(self, *parts) -> Path:
        p = self.work.joinpath(*parts)
        p.mkdir(parents=True, exist_ok=True)
        return p

    def scratch(self, *parts) -> Path:
        """A folder for a stage's large run output (census, offline checks, save test, drift, fix checks): under
        --scratch when given (COMMON 16 / D111: busy scratch on C:), else under the work folder."""
        p = Path(self.config.get('scratch') or self.work).joinpath(*parts)
        p.mkdir(parents=True, exist_ok=True)
        return p

    def outputs(self, stage) -> dict:
        e = self.state.get(stage)
        if not e or e['status'] not in OK_STATES:
            raise RuntimeError(f'stage {self.stage} needs the outputs of {stage}, which is not ok')
        return e['outputs'] or {}

    def outputs_if_ok(self, stage) -> dict | None:
        e = self.state.get(stage)
        return (e['outputs'] or {}) if e and e['status'] in OK_STATES else None

    @property
    def fork(self) -> Path:
        return Path(self.config['fork'])

    # --- progress inside a stage (a stage made of parts records each part as it finishes)
    def partial(self, **outputs):
        e = self.state.get(self.stage)
        e['outputs'] = {**(e.get('outputs') or {}), **outputs}
        self.state.save()

    def previous_outputs(self) -> dict:
        """The outputs this stage recorded in an earlier, unfinished run with the same inputs (for resuming parts)."""
        return dict(self._d.resume_outputs.get(self.stage) or {})

    # --- tools
    def tool(self, name) -> list:
        return [str(x) for x in self._d.tools[name]]

    def tool_missing(self, name) -> Path | None:
        """The tool's script when it does not exist (a stage of another issue not built yet), else None."""
        for x in self._d.tools[name]:
            if str(x).endswith('.py') and os.path.isabs(str(x)):
                return None if Path(x).is_file() else Path(x)
        return None

    def check_stop(self):
        if (self.work / STOP_NAME).exists():
            raise StopRequested(str(self.work / STOP_NAME))

    def say(self, *a):
        self._d.say(f'[{self.stage}]', *a)

    def git(self, *args, cwd=None, check=True, binary=False) -> str | bytes:
        """A read or local git command in the fork (light: not gated)."""
        cmd = self.tool('git') + (['-C', str(cwd or self.fork)] if cwd is not False else []) + [str(a) for a in args]
        r = subprocess.run(cmd, capture_output=True)
        if check and r.returncode != 0:
            raise RuntimeError(f'git {" ".join(map(str, args))}: exit {r.returncode}: '
                               f'{r.stderr.decode("utf-8", "replace").strip()[:400]}')
        if binary:
            return r.stdout if r.returncode == 0 else None
        return r.stdout.decode('utf-8', 'replace').strip() if r.returncode == 0 else None

    def run(self, cmd, *, name=None, what=None, gated=False, heavy=True, cwd=None, env=None, timeout=None, **kw):
        """Run one command, its output appended to <work>/logs/<stage>-<name>.log.

        Without `name` (3501's testdata.py calls ctx.run(cmd, capture_output=True, text=True, ...) as its runner): the
        stop file is checked, the driver's environment added, and subprocess.run(cmd, **kw) returned (light commands:
        git, c5h snapshot / dbbuild of a few seconds).

        heavy=True and gated=False: started through `quietlock.py run --who <who> --what <what> -- cmd` (registered as a
        running job, refused while the lock is held). gated=True: the command is a gated launch point itself (it calls
        perf/quietlock/gate.py at each start) and is started directly. heavy=False: a light command (git, a hash).
        Checks the stop file first. A refusal (a gate's or the lock's message and a non-zero exit) waits up to
        --gate-wait minutes in `quietlock.py wait` loops and starts again; past that it raises GateRefused."""
        cmd = [str(c) for c in cmd]
        if name is None:  # subprocess.run-compatible runner (testdata.py)
            self.check_stop()
            e2 = dict(os.environ if env is None else env)
            e2.setdefault('C5_JOB_OWNER', self.who)
            e2['VSCMD_SKIP_SENDTELEMETRY'] = '1'
            self._set_release(e2)
            return subprocess.run(cmd, cwd=cwd, env=e2, timeout=timeout, **kw)
        if heavy and not gated:
            cmd = self.tool('quietlock') + ['run', '--who', self.who, '--what',
                                            what or f'{self.who} take_release {self.tag} {self.stage} {name}', '--'] + cmd
        full_env = dict(os.environ if env is None else env)
        full_env.setdefault('C5_JOB_OWNER', self.who)
        full_env['VSCMD_SKIP_SENDTELEMETRY'] = '1'
        self._set_release(full_env)
        log = self.dir('logs') / f'{self.stage}-{name}.log'
        deadline = time.time() + 60 * float(self.config.get('gate_wait', 0) or 0)
        while True:
            self.check_stop()
            start = log.stat().st_size if log.exists() else 0
            t0 = time.time()
            self.say('start', name, '->', log)
            with open(log, 'a', encoding='utf-8', errors='replace') as f:
                f.write(f'\n=== {now()} cwd={cwd or os.getcwd()}\n=== {subprocess.list2cmdline(cmd)}\n')
                f.flush()
                try:
                    rc = subprocess.run(cmd, cwd=cwd, env=full_env, stdout=f, stderr=subprocess.STDOUT,
                                        timeout=timeout).returncode
                except subprocess.TimeoutExpired:
                    rc = -9
                    f.write(f'\n=== take_release: timeout after {timeout} s\n')
                except OSError as e:
                    rc = 127
                    f.write(f'\n=== take_release: could not start: {e}\n')
                f.write(f'=== {now()} exit {rc} in {time.time() - t0:.0f} s\n')
            with open(log, 'rb') as f:
                f.seek(start)
                text = f.read().decode('utf-8', 'replace')
            res = RunResult(rc, log, time.time() - t0, text)
            self.say(f'{name}: exit {rc} in {res.seconds:.0f} s')
            if rc != 0 and any(m in text for m in REFUSAL_MARKERS):
                if time.time() < deadline:
                    self.say(f'{name}: refused by a gate; waiting (quietlock.py wait, at most 170 s per call)')
                    self._wait_free(deadline)
                    continue
                raise GateRefused(f'{name}: ' + next(ln for ln in text.splitlines()
                                                     if any(m in ln for m in REFUSAL_MARKERS)).strip())
            return res

    def _set_release(self, env: dict):
        """The tools read this stage's descriptor (3501's descriptor.load()): C5_RELEASE = the tag of
        releases/<tag>.json, else the descriptor file (issue 3512: --descriptor, or the candidate after testdata).
        An env given by the stage that already names C5_RELEASE is left as it is."""
        if env.get('C5_RELEASE') and env.get('C5_RELEASE') != os.environ.get('C5_RELEASE'):
            return
        v = self.release_env()
        if v:
            env['C5_RELEASE'] = v

    def _wait_free(self, deadline):
        while time.time() < deadline:
            self.check_stop()
            left = max(1, min(170, int(deadline - time.time())))
            r = subprocess.run(self.tool('quietlock') + ['wait', '--timeout', str(left)], capture_output=True)
            if r.returncode == 0:
                time.sleep(2)  # the lock is free: let the holder's own follow-up start first
                return


# --------------------------------------------------------------------------------------------------- the driver

def load_stage_module(name, stages_dir: Path):
    p = stages_dir / f'{name}.py'
    if not p.is_file():
        return None
    # compiled from the source every time (no __pycache__: a byte-cached module of the same size and second would hide
    # an edit)
    mod = types.ModuleType(f'take_release_stage_{name}')
    mod.__file__ = str(p)
    sys.modules[mod.__name__] = mod  # dataclasses and pickling look a module up by name
    exec(compile(p.read_text(encoding='utf-8'), str(p), 'exec'), mod.__dict__)
    return mod


class Driver:
    def __init__(self, tag, work: Path, desc, config: dict, stages, tools=None, stages_dir=STAGES_DIR, out=None):
        out = out or _say_flush
        self.tag = tag
        self.work = Path(work)
        self.desc = desc
        self.config = config
        self.stages = stages            # [(name, cl_step, owner)]
        self.tools = tools or default_tools()
        self.stages_dir = Path(stages_dir)
        self.out = out
        self.state = State.load_or_new(self.work, tag)
        self.resume_outputs = {}
        self.mods = {}
        self._cand = None   # ((path, size, mtime_ns), Descriptor): the candidate descriptor as last read

    def desc_for(self, stage):
        """The descriptor `stage` reads (issue 3512). Stages up to and including testdata: the one the run starts from
        (`desc`: releases/<tag>.json, --descriptor, or the --base-release one, testdata's base). Every stage after
        testdata: the candidate testdata wrote (<work>/testdata/<tag>.json: the tag's mods and commit, the built or kept
        database, the tag's game folder, the base's saves). When testdata is ok without a candidate (an older state, a
        test's stand-in stage), the run's own descriptor if it is this tag's; else a DescriptorError."""
        names = [s[0] for s in self.stages]
        if 'testdata' not in names or stage not in names or names.index(stage) <= names.index('testdata'):
            return self.desc
        return self.candidate()

    def candidate(self):
        e = self.state.get('testdata') or {}
        p = (e.get('outputs') or {}).get('descriptor') if e.get('status') in OK_STATES else None
        if not p:
            if self.desc.tag == self.tag:
                return self.desc
            raise DescriptorError(self.work / 'testdata', 'descriptor',
                                  f'the run starts from {self.desc.tag}\'s descriptor (--base-release) and the stages '
                                  f'after testdata read {self.tag}\'s candidate descriptor, which testdata writes: '
                                  f'testdata is {e.get("status") or "not run"}; run it (take_release.py {self.tag} '
                                  f'--from testdata)')
        p = Path(p)
        try:
            st = p.stat()
        except OSError:
            raise DescriptorError(p, '', 'the candidate descriptor testdata wrote is missing; run testdata again '
                                  f'(take_release.py {self.tag} --from testdata)') from None
        key = (str(p), st.st_size, st.st_mtime_ns)
        if self._cand is None or self._cand[0] != key:
            self._cand = (key, _descriptor.load_file(p, expect_tag=self.tag))
        return self._cand[1]

    def say(self, *a):
        line = ' '.join(str(x) for x in a)
        self.out(f'{time.strftime("%H:%M:%S")} take_release: {line}')
        try:
            with open(self.work / 'driver.log', 'a', encoding='utf-8') as f:
                f.write(f'{now()} {line}\n')
        except OSError:
            pass

    def module(self, name):
        if name not in self.mods:
            f = (self.config.get('stage_files') or {}).get(name)  # test hook: one stage from another file
            self.mods[name] = load_stage_module(name, Path(f).parent) if f and Path(f).stem == name else \
                load_stage_module(name, self.stages_dir)
        return self.mods[name]

    def inputs_of(self, name, mod, ctx):
        """The recorded inputs of a stage: its own declared inputs and the outputs of the stages it depends on."""
        own = mod.inputs(ctx) if hasattr(mod, 'inputs') else {}
        deps = {}
        for d in getattr(mod, 'DEPENDS', []):
            deps[d] = ctx.outputs(d)
        for d in getattr(mod, 'USES', []):  # optional: counted when ok
            o = ctx.outputs_if_ok(d)
            if o is not None:
                deps[d] = o
        return {'own': own, 'deps_sha': {k: jhash(v) for k, v in deps.items()}}

    def stop_line(self, name, cl, what, unblock):
        return f'take_release: STOP {name} (CL step {cl}): {what}; to unblock: {unblock}'

    def run(self, frm=None, only=None) -> int:
        names = [s[0] for s in self.stages]
        lock = self._take_driver_lock()
        try:
            return self._run(names, frm, only)
        finally:
            self._release_driver_lock(lock)

    def _take_driver_lock(self):
        self.work.mkdir(parents=True, exist_ok=True)
        p = self.work / 'driver.pid'
        if p.exists():
            try:
                pid = int(p.read_text().split()[0])
            except (ValueError, IndexError, OSError):
                pid = None
            if pid and pid != os.getpid() and _pid_alive(pid):
                raise ArgError(f'another take_release (pid {pid}) runs in {self.work}; {p}')
        p.write_text(f'{os.getpid()} {now()}\n')
        return p

    def _release_driver_lock(self, p):
        try:
            if p.read_text().split()[0] == str(os.getpid()):
                p.unlink()
        except (OSError, IndexError):
            pass

    def _run(self, names, frm, only) -> int:
        st = self.state
        run_rec = {'started': now(), 'argv': sys.argv[1:], 'from': frm, 'only': only}
        st.data['runs'].append(run_rec)
        st.data['config'] = {k: v for k, v in self.config.items() if k != 'tools'}
        st.data['tier_stages'] = names
        for name, cl, _owner in self.stages:
            st.entry(name, cl)
        st.save()
        if only:
            todo = [only]
        elif frm:
            todo = names[names.index(frm):]
        else:
            todo = names
        forced = set(todo) if (only or frm) else set()

        def finish(code):
            self.d162_summary()
            run_rec.update(ended=now(), exit=code)
            st.save()
            return code

        for name, cl, owner in self.stages:
            if name not in todo:
                continue
            e = st.entry(name, cl)
            ctx = Ctx(self, name)
            try:
                ctx.check_stop()
            except StopRequested as s:
                self.out(self.stop_line(name, cl, f'stop file {s} present', f'remove {s} and run again'))
                return finish(EXIT_STOP)
            mod = self.module(name)
            if mod is None:
                res = not_built_yet(owner, f'stage module {self.stages_dir / (name + ".py")} missing')
                return finish(self._record(e, name, cl, res, None))
            missing = [d for d in getattr(mod, 'DEPENDS', []) if (st.get(d) or {}).get('status') not in OK_STATES]
            if missing:
                self.out(f'take_release: error: {"--only " if only else ""}{name} needs {", ".join(missing)}, '
                         'not ok yet; run those first')
                return finish(EXIT_ARGS)
            try:
                inputs = self.inputs_of(name, mod, ctx)
            except Exception as ex:
                return finish(self._fail(e, name, f'computing inputs: {type(ex).__name__}: {ex}'))
            sha = jhash(inputs)
            if name not in forced and e['status'] in OK_STATES and e.get('inputs_sha') == sha:
                # a stage's own reason to run again although its inputs are unchanged (hi_journey on a take for the
                # owner: a recorded result that would not pass D162)
                why_again = mod.again(ctx, e) if hasattr(mod, 'again') else None
                if not why_again:
                    self.say(f'{name} {e["status"]} (inputs unchanged), not run again')
                    continue
                self.say(f'{name}: {why_again}; running again')
            if e['status'] in OK_STATES and e.get('inputs_sha') and e.get('inputs_sha') != sha:
                self.say(f'{name}: inputs changed since it was {e["status"]}; running again')
            # parts finished in an earlier, unfinished run with the same inputs may be reused by the stage
            self.resume_outputs[name] = (e.get('outputs') if e.get('inputs_sha') == sha and
                                         e['status'] in ('running', 'pending', 'failed', 'stop') else {}) or {}
            e.update(status='running', started=now(), ended=None, stop=None, error=None, inputs=inputs,
                     inputs_sha=sha, outputs=dict(self.resume_outputs[name]))
            st.save()
            self.say(f'stage {name} (CL step {cl}) started')
            try:
                res = mod.run(ctx)
            except StopRequested as s:
                e.update(status='pending', ended=now())
                self.out(self.stop_line(name, cl, f'stop file {s} present', f'remove {s} and run again'))
                return finish(EXIT_STOP)
            except GateRefused as g:
                e.update(status='pending', ended=now(), error=f'gate refusal: {g}')
                self.out(f'take_release: REFUSED {name} (CL step {cl}): a launch gate or the quiet-machine lock '
                         f'refused a heavy start ({g}); exit 3 passed through; run again when the lock is free '
                         f'(or with --gate-wait MIN)')
                return finish(EXIT_REFUSED)
            except Exception as ex:
                tb = traceback.format_exc()
                try:
                    (self.work / 'logs').mkdir(parents=True, exist_ok=True)
                    with open(self.work / 'logs' / f'{name}-traceback.log', 'a', encoding='utf-8') as f:
                        f.write(f'\n=== {now()}\n{tb}')
                except OSError:
                    pass
                return finish(self._fail(e, name, f'{type(ex).__name__}: {ex}'))
            code = self._record(e, name, cl, res, sha)
            if code != EXIT_OK:
                return finish(code)
        if only:
            self.say(f'--only {only}: {st.get(only)["status"]}')
            return finish(EXIT_OK)
        tier = self.config.get('tier', 'candidate')
        for name, cl, _owner in self.stages:  # --from: the stages before it count too
            e = st.get(name)
            if e['status'] not in OK_STATES:
                s = e.get('stop') or {}
                self.out(self.stop_line(name, s.get('cl_step', cl), f'the {tier} tier is not reached: {name} is '
                                        f'{e["status"]}' + (f' ({s.get("what")})' if s.get('what') else ''),
                                        s.get('unblock') or f'run {name} (take_release.py {self.tag} --from {name})'))
                return finish(EXIT_STOP)
        if self.config.get('for_owner'):
            # D162 at the tier's end too: a take for the owner reaches its tier only with the journeys passed on this
            # build
            chk = self.d162_check()
            if chk is None or not chk['allowed']:
                why = '; '.join(chk['why']) if chk else 'the tier has no hi_journey stage'
                self.out(self.stop_line('hi_journey', '-', f'D162: a take for the owner and the HI release journeys '
                                        f'did not pass on this build: {why}',
                                        f'run take_release.py {self.tag} --tier validated --for-owner --from '
                                        'hi_journey'))
                return finish(EXIT_STOP)
        self.say(f'tier {tier} reached for {self.tag}; state {st.path}')
        st.data['reached'] = {'tier': tier, 'at': now()}
        return finish(EXIT_OK)

    def d162_check(self):
        """hi_journey's owner_check() on the state as it is (D162), or None when the tier has no hi_journey stage."""
        if 'hi_journey' not in [s[0] for s in self.stages]:
            return None
        mod = self.module('hi_journey')
        if mod is None or not hasattr(mod, 'owner_check'):
            return None
        b = self.state.get('build')
        return mod.owner_check(self.state.get('hi_journey'),
                               (b.get('outputs') or {}) if b and b.get('status') in OK_STATES else None)

    def d162_summary(self):
        """The summary's D162 line (every run of a tier with hi_journey), recorded in the state as `d162`."""
        try:
            chk = self.d162_check()
        except Exception as ex:  # noqa: BLE001 - a summary never changes the run's result
            self.out(f'take_release: D162 HI release journeys: not readable ({type(ex).__name__}: {ex}); '
                     'for the owner: NO, refused by D162')
            return None
        if chk is None:
            return None
        mod = self.module('hi_journey')
        line = mod.owner_line(chk, self.tag)
        if not self.config.get('for_owner'):
            line += ' (not a take for the owner: the stage reported, it did not gate)'
        self.out(f'take_release: {line}')
        self.state.data['d162'] = dict(chk, at=now(), for_owner=bool(self.config.get('for_owner')))
        return chk

    def _fail(self, e, name, why):
        e.update(status='failed', ended=now(), error=why)
        self.state.save()
        self.out(f'take_release: FAILED {name}: {why}')
        return EXIT_FAILED

    def _record(self, e, name, cl, res: StageResult, sha):
        if isinstance(res, dict) and res.get('status') in ('ok', 'stop', 'failed', 'skipped'):
            # a stage that returns {'status', 'outputs', 'stop': {what, unblock, cl_step?}, 'error'} (3501's testdata)
            s = res.get('stop') or {}
            res = StageResult(res['status'], dict(res.get('outputs') or {}),
                              s.get('what') or res.get('error') or '', s.get('unblock') or '', s.get('cl_step'))
        if not isinstance(res, StageResult):
            return self._fail(e, name, f'stage returned {type(res).__name__}, not a StageResult')
        e['outputs'] = {**(e.get('outputs') or {}), **(res.outputs or {})}
        e['ended'] = now()
        if sha is not None:
            e['inputs_sha'] = sha
        if res.status in OK_STATES:
            e['status'] = res.status
            if res.status == 'skipped':
                e['outputs']['skipped_why'] = res.what
            self.state.save()
            self.say(f'stage {name} {res.status}' + (f' ({res.what})' if res.what else ''))
            return EXIT_OK
        if res.status == 'stop':
            stop_cl = res.cl_step or cl
            e.update(status='stop', stop={'what': res.what, 'cl_step': stop_cl, 'unblock': res.unblock})
            self.state.save()
            self.out(self.stop_line(name, stop_cl, res.what, res.unblock))
            return EXIT_STOP
        if res.status == 'failed':
            return self._fail(e, name, res.what or 'failed')
        return self._fail(e, name, f'unknown stage status {res.status!r}')


def _pid_alive(pid):
    try:
        import psutil
        return psutil.pid_exists(pid)
    except ImportError:  # never os.kill(pid, 0) on Windows: signal 0 is CTRL_C_EVENT there
        return True


# --------------------------------------------------------------------------------------------------- the command line

TAG_RE = re.compile(r'^Release-\d+\.\d+\.\d+$')
NAME_RE = re.compile(r'^[A-Za-z0-9][A-Za-z0-9._-]*$')   # a release name with --base-commit (descriptor.py's tag rule)
COMMIT_RE = re.compile(r'^[0-9a-f]{7,40}$')


def _under(p: Path, root: Path) -> bool:
    try:
        p.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def _required_setting(key: str, why: str) -> Path:
    try:
        return release_settings.require(key, why)
    except release_settings.SettingError as e:
        raise ArgError(str(e)) from None


def forbidden_work() -> list:
    """Folders --work and --scratch must not be under: the MC's pins folder (setting pins_dir) and this repository.
    Fail-closed (ticket 4711): without the setting this is an ArgError (exit 2), never a shorter list.
    (Replaced: the constant FORBIDDEN_WORK, which named the pins folder of the workstation.)"""
    return [_required_setting('pins_dir', 'the driver never writes under the pins folder'), REPO]


def own_clones() -> Path:
    """The folder of the user's own clones (setting clones_dir; default the repository's parent when it is named
    GitHub): --fork must not be under it. Fail-closed as forbidden_work. (Replaced: an older constant that named the
    workstation's folder.)"""
    return _required_setting('clones_dir', 'a --fork under your own clones is refused')


# ticket 0708: (old option, new option, dest, value) of the options renamed to say "the owner"
OLD_OPTIONS = (('--for-art', '--for-owner', 'for_owner', True),
               ('--not-for-art', '--not-for-owner', 'for_owner', False),
               ('--art-check', '--owner-check', 'owner_check', True))


class _OldOption(argparse.Action):
    """An old option name (OLD_OPTIONS): sets what its new option sets and records itself for old_option_notes()."""

    def __init__(self, option_strings, dest, new=None, **kw):
        self.new = new
        super().__init__(option_strings, dest, **kw)

    def __call__(self, parser, namespace, values, option_string=None):
        setattr(namespace, self.dest, self.const)
        used = list(getattr(namespace, 'old_options', None) or [])
        if (option_string, self.new) not in used:
            used.append((option_string, self.new))
        namespace.old_options = used


def old_option_notes(a, prog='take_release') -> list:
    """One note per old option name used (ticket 0708), once each however often it was given."""
    return [f'{prog}: note: {old} is the old name of {new}; it still works, use {new}'
            for old, new in (getattr(a, 'old_options', None) or [])]


def parse_args(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('tag', help='the release tag, e.g. Release-5.4.8; with --base-commit any release name, e.g. '
                                'master-ba8aafdc')
    ap.add_argument('--work', type=Path, help='work folder (default C:/c5-64bit-work/3500/releases/<tag>)')
    g = ap.add_mutually_exclusive_group()
    g.add_argument('--from', dest='frm', metavar='STAGE', help='run again from this stage on (it and every later one)')
    g.add_argument('--only', metavar='STAGE', help='run only this stage (again); the stages it needs must be ok')
    ap.add_argument('--tier', choices=TIERS, default='candidate')
    ap.add_argument('--fork', type=Path, help='a clone of the DLL fork with the release tag and the pins (recorded in '
                                              'the state; needed on the first run). Never a clone of the owner\'s.')
    ap.add_argument('--carry', choices=['series', 'pins'], default=None,
                    help='series (default): the branches stage makes the release branches from '
                         'tools/release/series/carry.tsv; pins: build the commits of deps/i0.lock and deps/dll.lock '
                         '(the branches stage is skipped; for the pins\' own release)')
    ap.add_argument('--jobs', type=int, default=None, help='parallel compiles and host processes (default 6)')
    ap.add_argument('--ia-patch', help='the instance-aware instruments patch for the -ia 32-bit build (default "by-base": '
                                       'chosen by the i0 commit\'s base, stages/build.py IA_PATCHES: 9045\'s for '
                                       '5.4.8-based pins, 3515\'s when the i0 tree is upstream master\'s; issue 0310)')
    ap.add_argument('--skip-ia', action='store_true', default=None,
                    help='do not build the instance-aware 32-bit DLL (no second 32-bit source copy; D110); recorded')
    ap.add_argument('--with-ia', dest='skip_ia', action='store_false', default=None, help='build it again (the default)')
    ap.add_argument('--series', type=Path, help='the carry series for the branches stage instead of '
                                                'tools/release/series/carry.tsv (a release copy: the branches stop\'s '
                                                'unblock); recorded (issue 3509)')
    ap.add_argument('--base-commit', metavar='HASH',
                    help='the release\'s base is this upstream commit, not the tag; the first argument is then a '
                         'release name that need not be a tag; must be the descriptor\'s upstream_commit; recorded '
                         '(issue 3509)')
    ap.add_argument('--descriptor', type=Path, help='a descriptor JSON file instead of tools/release/releases/<tag>.json')
    ap.add_argument('--base-release', metavar='TAG',
                    help='a new release (no releases/<tag>.json yet) starts from this release\'s descriptor '
                         '(releases/TAG.json; "current" = current.json\'s): testdata builds the candidate descriptor '
                         'from it and every later stage reads the candidate; recorded (issue 3512)')
    ap.add_argument('--gate-wait', type=float, default=None, metavar='MIN',
                    help='on a gate refusal, wait for the lock up to MIN minutes and start again (default 0: exit 3)')
    ap.add_argument('--who', default=None, help='owner id for the lock and the job registry (default 3500)')
    ap.add_argument('--scratch', type=Path, help='folder for the large run outputs of census, offline, savetest, drift '
                                                 'and fixchecks (default: the work folder); recorded')
    ap.add_argument('--major-civs', type=int, default=None, metavar='N',
                    help='major civilizations of every DLL built (issue 4252; default 22; 43 is the upstream 43-civ '
                         'variant: the define on the DLL compile, -m43 names, the (3b) player DLL); recorded')
    ap.add_argument('--unvalidated-major-civs', action='store_true', default=None,
                    help='allow a --major-civs value other than 22 and 43 (2..62); recorded')
    ap.add_argument('--for-owner', dest='for_owner', action='store_true', default=None,
                    help='a take for a build meant for the owner to play (D162, the owner 2026-11-16; issue 0436): the '
                         'validated tier is reached only when both HI release journeys passed on this build, product '
                         'and stock; recorded in the state')
    ap.add_argument('--not-for-owner', dest='for_owner', action='store_false', default=None,
                    help='take the recorded --for-owner back (the journeys report again and do not gate)')
    ap.add_argument('--owner-check', action='store_true', default=False,
                    help='only answer D162 for the work folder\'s state (reads, writes nothing): exit 0 when the '
                         'build may go to the owner, 10 when not, 2 when there is no state')
    # ticket 0708: the old names, hidden; each still works and main() prints one note naming the new option
    for old, new, dest, const in OLD_OPTIONS:
        ap.add_argument(old, dest=dest, action=_OldOption, nargs=0, const=const, new=new, help=argparse.SUPPRESS)
    ap.add_argument('--build-folder', type=Path, metavar='DIR',
                    help='with --owner-check: a staged build folder whose files must carry the SHA-256 of the build\'s '
                         'proxy DLL, x64 DLL and server.exe that the journeys were played on')
    ap.add_argument('--tools', type=Path, help=argparse.SUPPRESS)       # test hook: JSON {name: [command prefix]}
    ap.add_argument('--stages-dir', type=Path, help=argparse.SUPPRESS)  # test hook
    return ap.parse_args(argv)


def build_config(a, work: Path):
    """The run's configuration: arguments given now, else the values recorded in the state, else defaults."""
    rec = {}
    p = work / STATE_NAME
    if p.is_file():
        try:
            # ticket 5024 (2026-11-23): a tier take's stage worker reads it while its parent replaces it
            rec = json.loads(_retry_sharing(lambda: p.read_text(encoding='utf-8'))).get('config') or {}
        except ValueError:
            rec = {}

    def pick(key, given, default):
        return given if given is not None else rec.get(key, default)

    cfg = {
        'tier': a.tier,
        'fork': str(a.fork.resolve()) if a.fork else rec.get('fork'),
        'carry': pick('carry', a.carry, 'series'),
        'jobs': pick('jobs', a.jobs, 6),
        'ia_patch': _ia_patch_config(a.ia_patch, rec.get('ia_patch')),
        'ia': (not a.skip_ia) if a.skip_ia is not None else rec.get('ia', True),
        'descriptor': str(a.descriptor.resolve()) if a.descriptor else rec.get('descriptor'),
        'gate_wait': pick('gate_wait', a.gate_wait, 0),
        'who': pick('who', a.who, '3500'),
        'scratch': str(a.scratch.resolve()) if a.scratch else rec.get('scratch'),
        'major_civs': pick('major_civs', a.major_civs, MAJOR_CIVS_DEFAULT),
        'unvalidated_major_civs': bool(pick('unvalidated_major_civs', a.unvalidated_major_civs, False)),
        'for_owner': bool(pick('for_owner', a.for_owner, rec.get('for_art', False))),   # for_art: before 0708
        'series': str(a.series.resolve()) if a.series else rec.get('series'),                    # issue 3509
        'base_commit': a.base_commit.strip().lower() if a.base_commit else rec.get('base_commit'),  # issue 3509
        'base_release': a.base_release.strip() if a.base_release else rec.get('base_release'),       # issue 3512
    }
    if a.descriptor and a.base_release:
        raise ArgError('--descriptor and --base-release: give one (a hand-written descriptor of the new release, or '
                       'the previous release the candidate is built from)')
    if a.descriptor:
        cfg['base_release'] = None       # a descriptor file given now replaces a recorded base release
    elif a.base_release:
        cfg['descriptor'] = None         # and the other way round
    if rec and a.major_civs is not None and int(rec.get('major_civs', MAJOR_CIVS_DEFAULT)) != a.major_civs:
        raise ArgError(f'--major-civs {a.major_civs}: the work folder {work} holds a run of '
                       f'{rec.get("major_civs", MAJOR_CIVS_DEFAULT)} major civilizations; use another --work (the '
                       f'default for {a.major_civs} is <root>/<tag>{major_work_suffix(a.major_civs)})')
    return cfg


MAJOR_CIVS_DEFAULT = 22


def major_work_suffix(n) -> str:
    """'' for 22, '-m43' for 43: the default work folder <root>/<tag><suffix> (issue 4252)."""
    return '' if n is None or int(n) == MAJOR_CIVS_DEFAULT else f'-m{int(n)}'


def _check_major(cfg):
    """tools/x64-dll/build.py's check (22 and 43 validated, 2..62 with --unvalidated-major-civs); ArgError otherwise."""
    import importlib.util
    name = 'take_release_x64dll_build'
    if name not in sys.modules:
        spec = importlib.util.spec_from_file_location(name, str(REPO / 'tools' / 'x64-dll' / 'build.py'))
        mod = importlib.util.module_from_spec(spec)
        sys.modules[name] = mod
        spec.loader.exec_module(mod)
    xb = sys.modules[name]
    try:
        xb.check_major_civs(cfg['major_civs'], cfg['unvalidated_major_civs'])
    except xb.MajorCivsError as e:
        raise ArgError(str(e))


IA_BY_BASE = 'by-base'
# the default before issue 0310, as earlier work folders recorded it: read as "by base" (for a 5.4.8-based i0 commit
# the same patch; for one cut from upstream master 3515's)
IA_OLD_DEFAULT = REPO / 'tools/d10/evidence/9045/ia-ee893bc18-440878983.patch'


def _ia_patch_config(given, recorded):
    if given and given != IA_BY_BASE:
        return str(Path(given).resolve())
    if given == IA_BY_BASE or not recorded:
        return IA_BY_BASE
    if recorded != IA_BY_BASE and Path(recorded).resolve() == IA_OLD_DEFAULT.resolve():
        return IA_BY_BASE
    return recorded


def check_config(cfg, work: Path):
    if cfg.get('for_owner') and cfg['tier'] != 'validated':
        raise ArgError(f'--for-owner (given or recorded in the state) needs --tier validated: the HI release journeys '
                       f'that D162 requires before a build goes to the owner are its hi_journey stage (--not-for-owner '
                       f'takes the marker back)')
    if not cfg['fork']:
        raise ArgError('--fork <clone of the DLL fork> is needed on the first run (it is recorded in the state)')
    fork = Path(cfg['fork'])
    clones = own_clones()
    if _under(fork, clones):
        raise ArgError(f'--fork {fork} is under {clones} (your own clones, setting clones_dir): use a clone of the '
                       'driver\'s own under '
                       'C:/c5-64bit-work/<ticket>/ (COMMON 16, D111)')
    if not fork.is_dir():
        raise ArgError(f'--fork {fork}: no such folder')
    r = subprocess.run(['git', '-C', str(fork), 'rev-parse', '--git-dir'], capture_output=True)
    if r.returncode != 0:
        raise ArgError(f'--fork {fork}: not a git clone')
    if not isinstance(cfg['jobs'], int) or cfg['jobs'] < 1 or cfg['jobs'] > 32:
        raise ArgError(f'--jobs {cfg["jobs"]}: must be 1-32')
    if cfg['gate_wait'] < 0:
        raise ArgError('--gate-wait must be >= 0')
    if not re.match(r'^[A-Za-z0-9_.-]+$', str(cfg['who'])):
        raise ArgError(f'--who {cfg["who"]!r}: letters, digits, _ . - only')
    if cfg['ia_patch'] and cfg['ia_patch'] != IA_BY_BASE and not Path(cfg['ia_patch']).is_file():
        raise ArgError(f'--ia-patch {cfg["ia_patch"]}: no such file')
    if cfg.get('series') and not Path(cfg['series']).is_file():
        raise ArgError(f'--series {cfg["series"]} (given or recorded in the state): no such file')
    if cfg.get('base_commit') and not COMMIT_RE.match(cfg['base_commit']):
        raise ArgError(f'--base-commit {cfg["base_commit"]!r}: 7-40 hex digits')
    _check_major(cfg)


def _say_flush(line):
    print(line, flush=True)


def owner_check_main(tag, work: Path, build_folder, out, stages_dir=STAGES_DIR) -> int:
    """--owner-check (D162, issue 0436): may the build of this work folder go to the owner? Reads <work>/state.json
    only (no lock, no tool, nothing written). With --build-folder, the staged folder's files must carry the SHA-256 of the
    build's proxy DLL, x64 DLL and server.exe that hi_journey recorded. Exit 0 yes, 10 no, 2 no state."""
    p = work / STATE_NAME
    if not p.is_file():
        raise ArgError(f'--owner-check: no state {p} (the take of {tag} runs in its --work folder)')
    try:
        data = json.loads(_retry_sharing(lambda: p.read_text(encoding='utf-8')))
    except ValueError as e:
        raise ArgError(f'--owner-check: {p}: not readable JSON ({e})')
    if data.get('tag') != tag:
        raise ArgError(f'--owner-check: {p} is the state of {data.get("tag")!r}, not {tag!r}')
    mod = None
    tier = (data.get('config') or {}).get('tier')
    if tier and tier not in TIERS:
        # a take of a validation tier (epic 0630, tools/release/tiers/): its own hi_journey answers D162 (it knows a
        # carried stock route); the candidate and validated tiers read stages/ exactly as before
        tdir = HERE / 'tiers' / 'stages'
        if (tdir / 'hi_journey.py').is_file():
            mod = load_stage_module('hi_journey', tdir)
    if mod is None:
        mod = load_stage_module('hi_journey', Path(stages_dir))
    if mod is None:
        raise ArgError(f'--owner-check: no hi_journey stage module in {stages_dir}')
    ents = {e.get('name'): e for e in data.get('stages') or []}
    b = ents.get('build') or {}
    chk = mod.owner_check(ents.get('hi_journey'), (b.get('outputs') or {}) if b.get('status') in OK_STATES else None)
    conf = data.get('config') or {}
    if not conf.get('for_owner', conf.get('for_art')):                   # for_art: a state written before 0708
        chk['notes'] = ['the take was not marked --for-owner (the journeys reported, they did not gate)']
    if build_folder is not None:
        bf = Path(build_folder)
        recorded = (((ents.get('hi_journey') or {}).get('outputs') or {}).get('candidate') or {}).get('sha256') or {}
        if not bf.is_dir():
            chk['why'].append(f'--build-folder {bf}: no such folder')
        else:
            have = {sha256_file(f).lower() for f in bf.rglob('*') if f.is_file()}
            for role in ('proxy_dll', 'x64_dll', 'server'):
                if not recorded.get(role):
                    chk['why'].append(f'the journeys recorded no {role} SHA-256 to compare with {bf}')
                elif recorded[role] not in have:
                    chk['why'].append(f'{bf} has no file with the {role} SHA-256 the journeys were played on '
                                      f'({recorded[role][:16]}...): not the build that passed')
        chk['allowed'] = not chk['why']
    mod_line = mod.owner_line(chk, tag)
    out(f'take_release: {mod_line}')
    for n in chk.get('notes') or []:
        out(f'take_release: note: {n}')
    cand = (((ents.get('hi_journey') or {}).get('outputs') or {}).get('candidate') or {})
    for role, h in sorted((cand.get('sha256') or {}).items()):
        out(f'take_release: played on {role} {h}')
    return EXIT_OK if chk['allowed'] else EXIT_STOP


def main(argv=None, out=_say_flush) -> int:
    try:
        a = parse_args(sys.argv[1:] if argv is None else argv)
    except SystemExit as e:
        return EXIT_ARGS if e.code not in (0, None) else EXIT_OK
    for ln in old_option_notes(a):
        out(ln)
    try:
        if not NAME_RE.match(a.tag):
            raise ArgError(f'tag {a.tag!r}: expected Release-<major>.<minor>.<patch> (or, with --base-commit, a '
                           'release name of letters, digits, . _ -)')
        stages = tier_stages(a.tier, a.stages_dir)
        names = [s[0] for s in stages]
        for opt, v in (('--from', a.frm), ('--only', a.only)):
            if v and v not in names:
                raise ArgError(f'{opt} {v}: not a stage of the {a.tier} tier ({", ".join(names)})')
        work = (a.work or DEFAULT_WORK_ROOT / (a.tag + major_work_suffix(a.major_civs))).resolve()
        if a.owner_check:
            return owner_check_main(a.tag, work, a.build_folder, out, a.stages_dir or STAGES_DIR)
        if a.build_folder:
            raise ArgError('--build-folder goes with --owner-check')
        forbidden = forbidden_work()
        for bad in forbidden:
            if _under(work, bad):
                raise ArgError(f'--work {work} is under {bad}: the driver writes only under its own work folder '
                               '(COMMON 15; never the repository, never the pins folder)')
        cfg = build_config(a, work)
        if not cfg['base_commit'] and not TAG_RE.match(a.tag):
            raise ArgError(f'tag {a.tag!r}: expected Release-<major>.<minor>.<patch>; a release name that is not a tag '
                           'needs --base-commit <upstream commit> (issue 3509)')
        if cfg['scratch']:
            for bad in forbidden:
                if _under(Path(cfg['scratch']), bad):
                    raise ArgError(f'--scratch {cfg["scratch"]} is under {bad} (never the repository, never the '
                                   'pins folder)')
        check_config(cfg, work)
        try:
            desc = load_descriptor(a.tag, Path(cfg['descriptor']) if cfg['descriptor'] else None,
                                   cfg.get('base_release'))
        except DescriptorError as e:
            raise ArgError(f'descriptor: {e}' + (' (--base-release names the previous release\'s tag, or current)'
                                                 if cfg.get('base_release') else ''))
        tools = None
        if a.tools:
            tools = {**default_tools(), **json.loads(a.tools.read_text(encoding='utf-8'))}
        work.mkdir(parents=True, exist_ok=True)
        drv = Driver(a.tag, work, desc, cfg, stages, tools=tools, stages_dir=a.stages_dir or STAGES_DIR, out=out)
        return drv.run(frm=a.frm, only=a.only)
    except ArgError as e:
        out(f'take_release: error: {e}')
        return EXIT_ARGS


if __name__ == '__main__':
    sys.exit(main())
