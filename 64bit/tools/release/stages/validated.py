"""The validated tier of tools/release/take_release.py (issue 3506; README section 2b): the registry of its stages and
the helpers they share. take_release.tier_stages('validated') = the candidate stages + STAGES below. This module
defines no run(): it is a registry, not a stage.

Stages, in order (CL = research/06-tooling-and-workflows/new-vp-release-checklist.md):
  refsave     CL 2   the huge reference save: the descriptor's save loads and plays two turns on the release's x64
                     server (premise P10), else a new fixed-seed huge game played to its turn (hours, headless)
  instance    CL 16-17  a claimed Goldberg instance gets the release's 32-bit instance-aware DLL, the proxy and a test
                     pack (VP64\\ with manifest.json, pack=1); 10 AI turns in verify mode on a Tiny save and on the
                     reference save: 0 mismatches, 0 lock errors, no fallback; restored by hash afterwards
  wholegames  CL 14  whole games x64 against x86 on the testbeds (tb.py submit, longgames.py --compare x64:x86,
                     statscmp over every table); a difference is a stop and the result is "candidate, parity failed"
                     (decision 1 of the draft, the owner's), never "validated"
  hi_journey  -      (4506) the HI release journeys on the candidate and on stock; reports only, except on a take for
                     the owner (--for-owner): then D162 (the owner, 2026-11-16; issue 0436) makes it a stop unless
                     both journeys passed on this build, product and stock (stages/hi_journey.py owner_check)
  report      -      <work>/validated-report.md and .json: every run, its inputs and its result, and the verdict

Every validated stage rewrites the report as it ends (report.finish), so a stop leaves a report that says why the
tier was not reached. Settings that the driver's command line does not carry come from the environment and are part
of the stages' inputs (a change runs the stage again):
  C5_RELEASE_REFSAVE       auto (default: the descriptor's save if it loads, else a new game) | new (always a new game)
  C5_RELEASE_REFSAVE_RUN   a finished run_server.py run folder of the same spec and turn to adopt instead of playing
                           the new game again (its server-report.txt must say passed and its save must exist)
  C5_RELEASE_INSTANCE      the instance to claim, or a comma list tried in order (default: the pool's choice; since
                           ticket 0180 never a D: instance, D133)
  C5_RELEASE_INSTANCE_WAIT minutes the instance stage waits for one of them to come free (default 90)
  C5_RELEASE_TESTBEDS      comma-separated testbed hosts (default oldpc,laptop)
"""
from __future__ import annotations

import os
import sys
from pathlib import Path

STAGES = [
    ('refsave', '2', '3506'),
    ('instance', '16-17', '3506'),
    ('wholegames', '14', '3506'),
    ('hi_journey', '-', '4506'),
    ('report', '-', '3506'),
]

REPO = Path(__file__).resolve().parents[3]


def _py(*parts):
    return [sys.executable, '-I', str(REPO.joinpath(*parts))]


# Tools of the validated stages that take_release.default_tools() does not name. A test passes its own `tools` dict to
# the Driver; a name it gives wins.
TOOLS = {
    'run_server': _py('server', 'run_server.py'),
    'tb': [sys.executable, '-I', '-X', 'utf8', str(REPO / 'tools' / 'testbed' / 'tb.py')],
    'pool': _py('tools', 'harness', 'pool.py'),
    'provision': _py('tools', 'instances', 'provision.py'),
    'arm': _py('proxy', 'tools', 'live', 'arm.py'),
    # Not -I: -I implies safe-path, so the script's own folder is not on sys.path and frontend.py cannot import its
    # sibling modules (backup, instance; the 2026-11-14 00:34 live run: "No module named 'backup'"). arm.py runs the
    # harness scripts the same way.
    'frontend': [sys.executable, str(REPO / 'tools' / 'harness' / 'frontend.py')],
    'visualcheck': _py('tools', 'visualcheck', 'visualcheck.py'),
    'pack': _py('tools', 'release', 'pack.py'),
}


def tool(ctx, name) -> list:
    """The command of a tool: the driver's when it names it, else TOOLS."""
    tools = getattr(ctx._d, 'tools', {}) or {}
    if name in tools:
        return ctx.tool(name)
    return [str(x) for x in TOOLS[name]]


def setting(name, default):
    v = os.environ.get(name)
    return v.strip() if v and v.strip() else default


def env_settings() -> dict:
    """The environment settings of the validated stages (recorded in their inputs)."""
    return {'refsave': setting('C5_RELEASE_REFSAVE', 'auto'), 'refsave_run': setting('C5_RELEASE_REFSAVE_RUN', None),
            'instance': setting('C5_RELEASE_INSTANCE', None),
            'testbeds': setting('C5_RELEASE_TESTBEDS', 'oldpc,laptop')}
