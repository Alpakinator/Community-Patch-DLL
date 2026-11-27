r"""Native round trip of the generated in/out codecs (issue 0380): plain MSVC and MSVC /fsanitize=address, x86 and x64.

    python -I proxy/tests/inout/test_inout.py --scratch DIR [--no-asan] [-v]

What it does: takes the codec objects of proxy/gen/gen_runtime.py for three real methods (the code lines the generator
emits, nothing retyped), composes for each a caller (the proxy side: caller_enc, then caller_out) and a callee (the
server side: callee_dec, a fake DLL method, the return, callee_out) joined by the real vpipc Writer / Reader
(ipc/src/marshal.cpp), and runs cases with their expected bytes:

  ICvGameContext1::GetDefineSTRING(char*, size_t, const char*, bool)   PCallerBuf + PCap
      untouched (false): the caller's whole buffer unchanged, also the bytes after its NUL; written: "VALUE";
      a buffer of exactly `capacity` bytes without a NUL, untouched: unchanged and nothing written past it (ASan);
      NULL buffer / capacity 0: the callee gets NULL
  ICvTeam1::GetTechs(TechTypes*, uint) const                           POutArray + PCap
      3 of 10 written: the 7 others keep the caller's values; capacity 70,000 (> 65,536): the caller clamps it to
      65,536 and the elements past it are untouched; a count of 70,000 on the wire: the callee refuses
  ICvPreGame1::GetMapOption(const char*, int&)                         PCStr + PScalarRef
      untouched: the caller's -1 comes back; written: 5

Negative control: the same cases built with the caller buffer's and the out array's in halves removed (the copy-out
of before 0380) must fail the untouched cases: the test proves it can see the fault. The ASan build also runs a
canary (a heap-buffer-overflow that must be reported). Exit 0 when every check passes, 1 otherwise.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(REPO / 'proxy' / 'gen'))
import gen_runtime as gr  # noqa: E402

VCVARSALL = Path(r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat")
METHODS = {
    'define': 'ICvGameContext1::GetDefineSTRING(char *, size_t, const char *, bool)',
    'techs': 'ICvTeam1::GetTechs(TechTypes *, uint) const',
    'mapopt': 'ICvPreGame1::GetMapOption(const char *, int &)',
}
RESULTS = []
VERBOSE = False


def check(name, ok, detail=''):
    RESULTS.append((name, bool(ok)))
    print(('ok   ' if ok else 'FAIL ') + name + (('  [' + detail + ']') if (detail and (VERBOSE or not ok)) else ''))
    return ok


def meths():
    m = gr.Model()
    found = {}
    for n in m.mds:
        for x in gr.Iface(m, n).meths:
            for k, key in METHODS.items():
                if x.key == key and k not in found:
                    found[k] = x
    missing = [k for k in METHODS if k not in found]
    if missing:
        raise SystemExit('methods not in the model: %s' % missing)
    return found


def indent(lines, n=4):
    return '\n'.join(' ' * n + s for s in lines)


def old_codecs(x):
    """The pre-0380 halves: no in half for the caller buffer / out array (negative control)."""
    for c in x.params:
        if isinstance(c, gr.PCallerBuf):
            c.caller_enc = (lambda c: lambda side: ['vp_w.u32(%s);' % c.capexpr()])(c)
            dec = c.callee_dec('server')
            c.callee_dec = (lambda d: lambda side: d[:4])(dec)
        if isinstance(c, gr.POutArray):
            c.caller_enc = (lambda c: lambda side: ['vp_w.u32(%s);' % c.capexpr()])(c)
            c.callee_dec = (lambda c: lambda side: [
                'uint32_t %s_cap = args.u32();' % c.a,
                'if (%s_cap > 65536u) %s_cap = 65536u;' % (c.a, c.a),
                'std::vector<%s> %s_buf(%s_cap ? %s_cap : 1u);' % (c.base, c.a, c.a, c.a),
                '%s* %s = %s_cap ? &%s_buf[0] : NULL;' % (c.base, c.a, c.a, c.a)])(c)


def emit_pair(tag, x, impl_call, ret_type, ret_put, ret_get):
    """Caller Call_<tag>(params...) and callee Callee_<tag>(args, ret), from the codec objects' own lines."""
    ps = ', '.join('%s %s' % (gr.qual(gr.norm(p['type'])), x.names[i]) for i, p in enumerate(x.m['params']))
    enc, dec, cout, rout = [], [], [], []
    for c in x.params:
        enc += c.caller_enc('proxy')
        dec += c.callee_dec('server')
        cout += c.callee_out('server')
        rout += c.caller_out('proxy')
    return '''
static bool Callee_%(tag)s(vpipc::Reader& args, vpipc::Writer& ret)
{
    FakeCh ch;
    (void)ch;
%(dec)s
    if (!args.ok()) return false;
    const %(rt)s r_ = %(impl)s;
    %(put)s
%(cout)s
    return true;
}

static %(rt)s Call_%(tag)s(bool* served, %(ps)s)
{
    typedef %(rt)s VpR;
    uint8_t b0_[64];
    vpipc::Writer vp_w;
    vp_w.reset(b0_, sizeof(b0_), true);
%(enc)s
    vpipc::Reader args(vp_w.data(), vp_w.size());
    uint8_t b1_[64];
    vpipc::Writer ret;
    ret.reset(b1_, sizeof(b1_), true);
    *served = Callee_%(tag)s(args, ret);
    if (!*served) return VpR();
    vpipc::Reader vp_r(ret.data(), ret.size());
    VpR vp_ret = %(get)s;
%(rout)s
    if (!vp_r.ok()) { printf("decode failed\\n"); return VpR(); }
    return vp_ret;
}
''' % {'tag': tag, 'dec': indent(dec), 'impl': impl_call, 'rt': ret_type, 'put': ret_put, 'cout': indent(cout),
       'ps': ps, 'enc': indent(enc), 'get': ret_get, 'rout': indent(rout)}


HEAD = r'''// GENERATED by proxy/tests/inout/test_inout.py from proxy/gen/gen_runtime.py's codec objects (issue 0380).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <vector>
#include "vpipc/marshal.h"

typedef int TechTypes;
typedef unsigned int uint;
struct FakeCh {};
namespace vpw { enum { kStatusBadArguments = 3 }; }
static int g_refusals = 0;
struct SrvRt { static bool DispatchFail(FakeCh&, vpipc::Writer&, int, const char* why) { ++g_refusals; printf("refused: %s\n", why); return false; } };
struct ProxyRt { static bool DispatchFail(FakeCh&, vpipc::Writer&, int, const char* why) { ++g_refusals; printf("refused: %s\n", why); return false; } };

// The fake DLL (the behaviour of the pin's methods, CvDllContext.cpp / CvDllTeam.cpp / CvPreGame.cpp).
static int g_mode = 0;          // 0: leave the out values alone and return false; 1: write them
static const char* g_seenBuf = (const char*)1;
static bool Impl_Define(char* szBuffer, size_t lenBuffer, const char*, bool)
{
    g_seenBuf = szBuffer;
    if (g_mode == 1 && szBuffer && lenBuffer > 0) { strcpy_s(szBuffer, lenBuffer, "VALUE"); return true; }
    return false;
}
static int Impl_Techs(TechTypes* p, uint n)
{
    int found = 0;
    for (int t = 0; t < 3 && n--; ++t) { *p++ = 100 + t; ++found; }
    return found;
}
static bool Impl_MapOpt(const char*, int& v)
{
    if (g_mode == 1) { v = 5; return true; }
    return false;
}
'''

MAIN = r'''
static int g_fail = 0, g_n = 0;
static void expect(const char* name, bool ok) { ++g_n; if (!ok) ++g_fail; printf("%s %s\n", ok ? "ok  " : "FAIL", name); }

int main()
{
    bool served = false;
    // GetDefineSTRING, untouched: every byte of the caller's buffer stays, also after its NUL.
    {
        char* b = (char*)malloc(16);
        memset(b, 'Z', 16); memcpy(b, "keepme", 7);
        char ref[16]; memcpy(ref, b, 16);
        g_mode = 0;
        const bool r = Call_define(&served, b, 16, "NO_SUCH_DEFINE", false);
        expect("define untouched: false", served && !r);
        expect("define untouched: buffer unchanged (all 16 bytes)", memcmp(b, ref, 16) == 0);
        free(b);
    }
    // GetDefineSTRING, written.
    {
        char* b = (char*)malloc(16);
        memset(b, 'Z', 16); memcpy(b, "keepme", 7);
        g_mode = 1;
        const bool r = Call_define(&served, b, 16, "SOME_DEFINE", false);
        expect("define written: true and \"VALUE\"", served && r && strcmp(b, "VALUE") == 0);
        free(b);
    }
    // A buffer of exactly its capacity with no NUL, untouched: unchanged, nothing written past it (ASan watches).
    {
        char* b = (char*)malloc(8);
        memset(b, 'A', 8);
        g_mode = 0;
        Call_define(&served, b, 8, "NO_SUCH_DEFINE", false);
        bool same = true;
        for (int i = 0; i < 8; ++i) same = same && b[i] == 'A';
        expect("define untouched, no NUL inside the capacity: unchanged", served && same);
        free(b);
    }
    // NULL buffer and capacity 0: the callee gets NULL.
    {
        g_mode = 1; g_seenBuf = (const char*)1;
        const bool r = Call_define(&served, NULL, 0, "SOME_DEFINE", false);
        expect("define NULL buffer: the callee gets NULL, false", served && !r && g_seenBuf == NULL);
        char one[1] = {'Q'};
        g_seenBuf = (const char*)1;
        Call_define(&served, one, 0, "SOME_DEFINE", false);
        expect("define capacity 0: the callee gets NULL, buffer unchanged", served && g_seenBuf == NULL && one[0] == 'Q');
    }
    // GetTechs: 3 of 10 written, the 7 others keep the caller's values.
    {
        TechTypes* a = (TechTypes*)malloc(10 * sizeof(TechTypes));
        for (int i = 0; i < 10; ++i) a[i] = 77;
        const int n = Call_techs(&served, a, 10);
        bool ok = served && n == 3 && a[0] == 100 && a[1] == 101 && a[2] == 102;
        for (int i = 3; i < 10; ++i) ok = ok && a[i] == 77;
        expect("techs: 3 written, 7 kept", ok);
        free(a);
    }
    // GetTechs with a capacity over 65,536: the caller clamps it, the callee serves.
    {
        TechTypes* a = (TechTypes*)malloc(70000 * sizeof(TechTypes));
        for (int i = 0; i < 70000; ++i) a[i] = 7;
        const int r0 = g_refusals;
        const int n = Call_techs(&served, a, 70000);
        expect("techs capacity 70000: clamped by the caller, served, element 65535 kept, 65536 untouched",
               served && n == 3 && g_refusals == r0 && a[65535] == 7 && a[65536] == 7);
        free(a);
    }
    // GetMapOption(const char*, int&): untouched -1 stays, written 5.
    {
        int v = -1;
        g_mode = 0;
        bool r = Call_mapopt(&served, "SomeOption", v);
        expect("mapopt untouched: false, -1 kept", served && !r && v == -1);
        g_mode = 1;
        r = Call_mapopt(&served, "SomeOption", v);
        expect("mapopt written: true, 5", served && r && v == 5);
    }
    printf("inout: %d/%d checks passed\n", g_n - g_fail, g_n);
    return g_fail ? 1 : 0;
}
'''

CALLEE_REFUSES = r'''
// The callee's refusal of a count over 65,536 (a caller that does not clamp: hand-made args).
static int RefusalCase()
{
    uint8_t b0[64];
    vpipc::Writer w;
    w.reset(b0, sizeof(b0), true);
    w.u32(70000u);
    vpipc::Reader args(w.data(), w.size());
    uint8_t b1[64];
    vpipc::Writer ret;
    ret.reset(b1, sizeof(b1), true);
    const int r0 = g_refusals;
    const bool served = Callee_techs(args, ret);
    printf("%s techs: a count of 70000 on the wire is refused\n", (!served && g_refusals == r0 + 1) ? "ok  " : "FAIL");
    return (!served && g_refusals == r0 + 1) ? 0 : 1;
}
'''


def source(negative):
    xs = meths()
    if negative:
        for k in ('define', 'techs'):
            old_codecs(xs[k])
    parts = [HEAD,
             emit_pair('define', xs['define'], 'Impl_Define(a_szBuffer, a_lenBuffer, a_szName, a_bReportErrors)', 'bool',
                       'ret.b(r_);', 'vp_r.b()'),
             emit_pair('techs', xs['techs'], 'Impl_Techs(a_pkTechArray, a_uiArraySize)', 'int',
                       'ret.i32(static_cast<int32_t>(r_));', 'static_cast<VpR>(vp_r.i32())'),
             emit_pair('mapopt', xs['mapopt'], 'Impl_MapOpt(a_szOptionName, a_iValue)', 'bool', 'ret.b(r_);', 'vp_r.b()')]
    main = MAIN
    if not negative:
        parts.append(CALLEE_REFUSES)
        main = main.replace('    printf("inout: %d/%d', '    ++g_n; if (RefusalCase()) ++g_fail;\n    printf("inout: %d/%d')
    parts.append(main)
    return '\n'.join(parts)


def vcvars_env(arch):
    out = subprocess.run('call "%s" %s >nul && set' % (VCVARSALL, arch), shell=True, capture_output=True, text=True,
                         env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1'))
    if out.returncode:
        return None
    env = {}
    for line in out.stdout.splitlines():
        k, sep, v = line.partition('=')
        if sep and k:
            env[k] = v
    return env


def compile_run(d, name, src_text, env, asan):
    d.mkdir(parents=True, exist_ok=True)
    src = d / (name + '.gen.cpp')
    src.write_text(src_text, encoding='utf-8', newline='\n')
    cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
    flags = ['/nologo', '/EHsc', '/W3', '/Zi', '/Od', '/D_CRT_SECURE_NO_WARNINGS', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
             '/I' + str(REPO / 'ipc' / 'include')] + (['/fsanitize=address'] if asan else [])
    exe = d / (name + '.exe')
    cp = subprocess.run([cl] + flags + [str(src), str(REPO / 'ipc' / 'src' / 'marshal.cpp'), '/Fe' + str(exe),
                                        '/Fo' + str(d) + '\\', '/Fd' + str(d / (name + '.pdb'))],
                        cwd=str(d), env=env, capture_output=True, text=True, errors='replace')
    if cp.returncode != 0:
        return None, (cp.stdout + cp.stderr)[-3000:]
    memcap = [sys.executable, '-I', str(REPO / 'shell' / 'tools' / 'memcap.py'), '--mb', '1024', '--']
    r = subprocess.run(memcap + [str(exe)], cwd=str(d), env=env, capture_output=True, text=True, errors='replace',
                       timeout=300)
    return r, None


def main():
    global VERBOSE
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--scratch', required=True, type=Path, help='build folder outside the repository')
    ap.add_argument('--no-asan', action='store_true')
    ap.add_argument('-v', action='store_true')
    a = ap.parse_args()
    VERBOSE = a.v
    if REPO in a.scratch.resolve().parents:
        raise SystemExit('--scratch must be outside the repository')
    pos, neg = source(False), source(True)
    for arch in ('x86', 'x64'):
        env = vcvars_env(arch)
        if not check('%s: vcvarsall' % arch, env is not None):
            continue
        for asan in ([False] if a.no_asan else [False, True]):
            label = '%s %s' % (arch, 'asan' if asan else 'plain')
            d = a.scratch / ('%s-%s' % (arch, 'asan' if asan else 'plain'))
            r, err = compile_run(d, 'inout', pos, env, asan)
            if not check('%s: built' % label, r is not None, err or ''):
                continue
            out = r.stdout + r.stderr
            m = re.search(r'inout: (\d+)/(\d+) checks passed', out)
            check('%s: %s, exit %d%s' % (label, m.group(0) if m else 'no summary', r.returncode,
                                          ', no AddressSanitizer report' if asan else ''),
                  r.returncode == 0 and m and m.group(1) == m.group(2) and 'AddressSanitizer' not in out, out[-3000:])
            r, err = compile_run(d, 'inout_old', neg, env, asan)
            if not check('%s negative: built' % label, r is not None, err or ''):
                continue
            out = r.stdout + r.stderr
            fails = [ln for ln in out.splitlines() if ln.startswith('FAIL')]
            want = {'FAIL define untouched: buffer unchanged (all 16 bytes)',
                    'FAIL define untouched, no NUL inside the capacity: unchanged', 'FAIL techs: 3 written, 7 kept',
                    'FAIL techs capacity 70000: clamped by the caller, served, element 65535 kept, 65536 untouched'}
            check('%s negative (copy-out of before 0380): exactly the untouched cases fail, exit 1' % label,
                  r.returncode == 1 and set(fails) == want and 'AddressSanitizer' not in out, out[-2000:])
            if asan:
                (d / 'canary.cpp').write_text('#include <stdio.h>\n#include <stdlib.h>\nint main() { char* volatile p = '
                                              '(char*)malloc(16); p[16] = 1; printf("canary NOT caught\\n"); free(p); '
                                              'return 0; }\n', encoding='ascii')
                cl = shutil.which('cl', path=env.get('PATH', env.get('Path', '')))
                subprocess.run([cl, '/nologo', '/Zi', '/Od', '/fsanitize=address', str(d / 'canary.cpp'),
                                '/Fe' + str(d / 'canary.exe'), '/Fo' + str(d) + '\\'], cwd=str(d), env=env,
                               capture_output=True, text=True)
                c = subprocess.run([str(d / 'canary.exe')], cwd=str(d), env=env, capture_output=True, text=True,
                                   errors='replace')
                check('%s (negative): the canary\'s heap-buffer-overflow is reported (ASan active)' % label,
                      c.returncode != 0 and 'heap-buffer-overflow' in c.stdout + c.stderr)
    passed = sum(1 for _, ok in RESULTS if ok)
    print('test_inout: %d/%d checks passed' % (passed, len(RESULTS)))
    return 0 if passed == len(RESULTS) else 1


if __name__ == '__main__':
    sys.exit(main())
