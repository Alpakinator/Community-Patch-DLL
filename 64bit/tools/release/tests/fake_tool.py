"""A stand-in for every tool the release driver's stages call (tests of issue 3502). Usage: fake_tool.py NAME ARGS...

Each call appends "NAME ARGS" to $FAKE_CALLS. Behaviour switches are environment variables (FAKE_*), set by the tests.
Writes only below the folders its arguments name (inside the test's temporary folder).
"""
import json
import os
import subprocess
import sys
from pathlib import Path

name, args = sys.argv[1], sys.argv[2:]
calls = os.environ.get('FAKE_CALLS')
if calls:
    with open(calls, 'a', encoding='utf-8') as f:
        extra = (' C5_RELEASE_TEST_DB=' + os.environ.get('C5_RELEASE_TEST_DB', '-')) if name == 'build_server' else ''
        f.write(name + ' ' + ' '.join(args) + extra + '\n')
env_calls = os.environ.get('FAKE_ENV_CALLS')   # issue 3512: the descriptor each tool is pointed at
if env_calls and name != 'quietlock':
    with open(env_calls, 'a', encoding='utf-8') as f:
        f.write(name + '\t' + os.environ.get('C5_RELEASE', '-') + '\n')


def opt(flag, default=None):
    return args[args.index(flag) + 1] if flag in args else default


def fail_if(var):
    if os.environ.get(var):
        print(f'{name}: failing because {var} is set')
        sys.exit(int(os.environ.get(var)))


if name == 'quietlock':
    held = os.environ.get('FAKE_QL_HELD')
    if args[0] == 'run':
        if held and os.path.exists(held):
            print('refused, command not started (x: y); lock held')
            sys.exit(3)
        cmd = args[args.index('--') + 1:]
        sys.exit(subprocess.call(cmd))
    if args[0] == 'wait':
        if held and os.path.exists(held):
            os.remove(held)
        sys.exit(0)
    sys.exit(0)

if name == 'src_fingerprint':
    print('fingerprint = ' + os.environ.get('FAKE_FINGERPRINT', 'v1 ' + 'a' * 64 + ' 3 files'))
    sys.exit(0)

if name == 'build_all':   # issue 4252: tools/x64-dll/build_all.py --major-civs N (the dll of another count)
    out = Path(opt('--out'))
    fail_if('FAKE_BUILD_ALL_FAIL')
    n = int(opt('--major-civs', '22'))
    for rel in ('dll/CvGameCore_Expansion2.dll', 'stubs/lua51_x64.dll'):
        p = out / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(f'{rel} m{n} of {opt("--src")}'.encode())
    (out / 'build_all.json').write_text(json.dumps(
        {'rc': 0, 'config': opt('--config'), 'major_civs': n,
         'major_civs_define': f'/DMOD_GLOBAL_MAX_MAJOR_CIVS={n}',
         'smoke_major_civs': {'major_civs': n, 'version': f'Release-9.9.9-1-gabc+m{n} Copy'},
         'steps': [{'step': s, 'rc': 0} for s in ('stubs', 'dll', 'smoke')],
         'dll': {'unresolved': 0, 'narrowing_sites': 794, 'narrowing_gate': {'ok': True, 'new': 0, 'gone': 4}}}))
    (out / 'dll' / 'build-summary.json').write_text(json.dumps({'config': opt('--config'), 'major_civs': n}))
    print('build_all: OK')
    sys.exit(0)

if name == 'build_server':
    out = Path(opt('--out'))
    if os.environ.get('FAKE_NARROW'):
        d = out / 'deps' / 'dll'
        d.mkdir(parents=True, exist_ok=True)
        (d / 'narrowing-new.txt').write_text('# gate\nNEW\tCvGameCoreDLL_Expansion2/a.cpp:12:5\tmsg\tcode\t(+1)\n')
        print('build_all: FAILED at step dll (exit 3): narrowing gate')
        sys.exit(3)
    fail_if('FAKE_BUILD_SERVER_FAIL')
    deps = opt('--deps')   # as the real script: --deps reuses a build_all folder, whose DLL goes into bin/
    for rel in ('bin/CvGameCore_Expansion2.dll', 'bin/server.exe', 'bin/lua51_x64.dll',
                'proxy/bin/CvGameCore_Expansion2.dll'):
        p = out / rel
        p.parent.mkdir(parents=True, exist_ok=True)
        if deps and rel == 'bin/CvGameCore_Expansion2.dll':
            p.write_bytes((Path(deps) / 'dll' / 'CvGameCore_Expansion2.dll').read_bytes())
        else:
            p.write_bytes(f'{rel} of {opt("--src")}'.encode())
    (out / 'proxy' / 'build-proxy.json').write_text('{}')
    if deps:
        print('build_all: OK (--deps)')
        sys.exit(0)
    (out / 'deps').mkdir(exist_ok=True)
    gate = {'ok': True, 'new': 0, 'gone': 4, 'baseline': 'narrowing-baseline.tsv'}
    if os.environ.get('FAKE_NO_GATE'):
        gate = None
    (out / 'deps' / 'build_all.json').write_text(json.dumps(
        {'rc': 0, 'steps': [{'step': s, 'rc': 0} for s in ('stubs', 'dll', 'smoke')],
         'dll': {'unresolved': 0, 'narrowing_sites': 794, 'narrowing_gate': gate}}))
    (out / 'deps' / 'dll').mkdir(exist_ok=True)
    (out / 'deps' / 'dll' / 'build-summary.json').write_text(json.dumps({'config': opt('--config')}))
    print('build_all: OK')
    sys.exit(0)

if name == 'build_shell':   # issue 0464: shell/build.cmd (C5_SHELL_BUILD = the output folder)
    out = Path(os.environ['C5_SHELL_BUILD'])
    with open(calls, 'a', encoding='utf-8') as f:
        f.write(f'build_shell_env C5_SHELL_BUILD={out}\n')
    fail_if('FAKE_BUILD_SHELL_FAIL')
    import struct
    b = bytearray(0x200)
    b[:2] = b'MZ'
    struct.pack_into('<I', b, 0x3C, 0x80)
    b[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<H', b, 0x84, 0x8664 if os.environ.get('FAKE_BUILD_SHELL_X64') else 0x14C)
    (out / 'bin').mkdir(parents=True, exist_ok=True)
    if not os.environ.get('FAKE_BUILD_SHELL_NO_DLL'):
        (out / 'bin' / 'shell_composite.dll').write_bytes(bytes(b) + f'compositor of {out}'.encode())
    (out / 'bin' / 'shell_composite.pdb').write_bytes(b'compositor pdb')
    (out / 'bin' / 'shell_server.exe').write_bytes(b'not laid out')
    print('shell build: OK')
    sys.exit(0)

if name == 'census':
    out = Path(args[1])
    out.mkdir(parents=True, exist_ok=True)
    (out / 'x86-sites.txt').write_text('s1\ns2\n')
    (out / 'x64-sites.txt').write_text('s1\ns2\n')
    (out / 'x64-only.txt').write_text(os.environ.get('FAKE_CENSUS_SITE', ''))
    sys.exit(0)

if name == 'model':
    Path(opt('--out')).mkdir(parents=True, exist_ok=True)
    sys.exit(int(os.environ.get('FAKE_MODEL_RC', '0')))

if name == 'purity':
    out = Path(opt('--out'))
    out.mkdir(parents=True, exist_ok=True)
    (out / 'purity.tsv').write_text('# vp64-purity 1\ndll_sha256\tx\nmethod\tA::B()\nmethod\tA::C()\nlua\tPlot.GetX\n')
    (out / 'report.tsv').write_text('kind\tkey\tdecision\treason\nmethod\tA::B()\tkept\t-\n' +
                                    ('' if os.environ.get('FAKE_PURITY_NODROP') else
                                     'method\tA::D()\tdropped\tcallee changed\n'))
    sys.exit(0)

if name == 'purity_test':   # issue 0480: proxy/tools/test_purity.py in full
    mode = os.environ.get('FAKE_PURITY_TEST', 'pass')
    if mode == 'fail':      # the 0478 failure: the lists grew, a derive check fails
        print('ok   derive: exit 0 on --src')
        print('FAIL derive: every listed row kept at the pin (137 method, 29 lua)  [derived 137, the lists 138]')
        print('test_purity: 65/66 checks passed; FAILED: derive: every listed row kept at the pin')
        sys.exit(1)
    if mode == 'nosummary':
        print('ok   unit: something')
        sys.exit(0)
    print('ok   derive: exit 0 on --src')
    print('test_purity: 66/66 checks passed')
    sys.exit(0)

if name == 'pack':
    if os.environ.get('FAKE_PACK_STOP'):
        print('take_release: STOP pack (CL step -): the accepted DLL x cannot be fetched; to unblock: fetch it')
        sys.exit(10)
    out = Path(opt('--out')) / 'vp64pack'
    out.mkdir(parents=True, exist_ok=True)
    (out / 'manifest.json').write_text(json.dumps({'format': 1, 'release': opt('--release')}))
    sys.exit(0)

if name in ('test_proxy_rt', 'hutest', 'run_proxy_game'):
    fail_if('FAKE_' + name.upper() + '_FAIL')
    print(f'{name}: PASS')
    sys.exit(0)

if name == 'run_d10':
    out = Path(opt('--out'))
    out.mkdir(parents=True)
    rows = json.loads(os.environ.get('FAKE_ROWS', '{"tiny": 30666, "standard": 351496, "huge": 683123}'))
    rep = {'pass': True, 'failures': [], 'wallSeconds': 1,
           'games': {g: {'parts': {'2': {'comparisons': [{'label': 'x86 direct vs x64 direct', 'ok': True, 'rows': n}]}}}
                     for g, n in rows.items()}}
    (out / 'report.json').write_text(json.dumps(rep))
    (out / 'report.txt').write_text('PASS\n')
    sys.exit(0)

if name == 'run_pair':
    out = Path(opt('--out'))
    out.mkdir(parents=True, exist_ok=True)
    (out / 'tiny-compare.txt').write_text('civ_turn_era   404   404  identical\ntotal rows: A 30666, B 30666\n'
                                          'RESULT: IDENTICAL\n')
    print('x86: exit 0\nx64: exit 0\ncompare: exit 0 -> x')
    print('first differing turn:', os.environ.get('FAKE_DRIFT', 'None'))
    sys.exit(0)

if name == 'fix0096':
    print('pin: rc 0: 0096-RESULT PASS message [a]')
    print('fix: rc 0: ' + ('0096-RESULT FAIL empty notification' if os.environ.get('FAKE_0096_FAIL') else
                           '0096-RESULT PASS message [a]'))
    print('VERDICT FAIL')  # both builds carry the fix: the script's own verdict wants an unfixed pin
    sys.exit(1)

if name == 'fix0084':
    print('VERDICT PASS')
    sys.exit(0)

if name == 'fix0103':
    print('CMP new-off vs new-on: IDENTICAL (statscmp rc 0) -> x')
    sys.exit(0)

if name == 'run_server':
    fail_if('FAKE_RUN_SERVER_FAIL')
    sys.exit(0)

if name == 'branches':
    fork, base, nm, out = opt('--fork'), opt('--base'), opt('--name'), Path(opt('--out'))
    if os.environ.get('FAKE_BRANCHES_STOP') == 'series':  # branches.py's own conflict unblock text (issue 3509)
        print('take_release: STOP branches (CL step 3): identity row 5201480f1 conflicts on i0: conflict in '
              'CvGameCoreDLL_Expansion2/SqliteLogger.cpp; to unblock: pick 5201480f1 by hand onto the branch\'s last '
              'applied commit 0123456789, resolve CvGameCoreDLL_Expansion2/SqliteLogger.cpp, and put the resolved '
              'commit\'s hash in this row of a release copy of the series (then rerun with --series <copy>)')
        sys.exit(10)
    if os.environ.get('FAKE_BRANCHES_STOP'):
        print('take_release: STOP branches (CL step 3): port row b25fd45c0 conflicts; to unblock: resolve it')
        sys.exit(10)
    heads = json.loads(os.environ['FAKE_BRANCH_HEADS'])
    for b, c in heads.items():
        subprocess.run(['git', '-C', fork, 'branch', '-f', f'{b}-release-{nm}', c], check=True)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'branches.json').write_text(json.dumps({'rows': [{'status': 'applied'}] * 3, 'compare': {}}))
    sys.exit(0)

print(f'fake_tool: unknown tool {name}')
sys.exit(99)
