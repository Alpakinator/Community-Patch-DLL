"""narrowing (CL step 7): the -Wshorten-64-to-32 narrowing gate of the x64 build, read from its build_all.json
(tools/x64-dll/narrowing-baseline.tsv). The build stage already stops (CL step 7) when the gate cut the build; this
stage records the result and stops when the build ran without the gate or with new sites.
"""
from __future__ import annotations

from take_release import StageResult

DEPENDS = ['build']


def run(ctx):
    b = ctx.outputs('build')
    gate = b.get('narrowing_gate')
    if not isinstance(gate, dict):
        return StageResult.stop(f'the x64 build has no narrowing gate result ({b.get("build_all")})',
                                'build with the gate (server/build_server.py runs build.py --shorten), then run '
                                'again from build')
    if not gate.get('ok') or gate.get('new'):
        return StageResult.stop(
            f'the narrowing gate reports {gate.get("new")} new -Wshorten-64-to-32 site(s) ({b.get("build_all")})',
            'triage each site (tools/x64-dll/truncation-triage.md), fix it in the carry or write the baseline '
            '(tools/x64-dll/build.py --shorten --write-narrowing-baseline), then run again from build')
    return StageResult.ok(sites=b.get('narrowing_sites'), new=gate.get('new', 0), gone=gate.get('gone', 0),
                          baseline=gate.get('baseline'))
