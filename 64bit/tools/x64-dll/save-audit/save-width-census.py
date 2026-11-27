"""Save-width census for the x64 DLL port (issue 1004, decision D10).

Finds every place where the DLL writes or reads a 64-bit integer through FDataStream (saves, the
multiplayer sync archive, anything else that uses the stream), in the x86 build (clang-cl -m32 with the VC9
headers, as build_vp_clang.py builds it) and in the x64 build (clang-cl -m64 with the MSVC 14.43 STL).
A site that is 64-bit only on x64 changes the save bytes: size_t, ptrdiff_t and friends.

How: the fork's sources are copied to a scratch tree (the fork is never modified), and two headers are
instrumented there:
  - FDataStream.h gets non-template, deprecated operator<< / operator>> overloads for (unsigned) long long,
    and its 64-bit Write/Read members are marked deprecated;
  - CvSerialize.h gets the same non-template deprecated overloads on CvStreamSaveVisitor and
    CvStreamLoadVisitor.
A non-template exact match beats the member templates, so every direct `stream << x` and `visitor(x)` of a
64-bit value warns at its own line. Values that reach the stream through a container or wrapper template
(std::vector<size_t>, CvSyncVar<...>) warn once per template instantiation per translation unit, with the
instantiation chain naming the first requester; the type in that chain identifies the member (grep for it).
Every TU is compiled with /Zs (syntax only), no PCH.

Usage:
  python save-width-census.py <fork worktree> <out dir> [--arch x64|x86|both] [--jobs N]
Writes <out dir>/<arch>.log (raw diagnostics) and <out dir>/<arch>-sites.txt (unique sites), and
<out dir>/x64-only.txt (sites present on x64 and not on x86).
"""
import argparse
import concurrent.futures as cf
import os
import re
import shutil
import subprocess
import sys

CLANG = r'C:\Program Files\LLVM\bin\clang-cl.exe'
VCVARS64 = r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
VS90 = os.path.join(os.environ.get('VS90COMNTOOLS', r'C:\Program Files (x86)\Microsoft Visual Studio 9.0\Common7\Tools' + '\\'), 'vsvars32.bat')

INCLUDE_DIRS = ['CvGameCoreDLL_Expansion2', 'CvWorldBuilderMap\\include', 'CvGameCoreDLLUtil\\include',
                'CvLocalization\\include', 'CvGameDatabase\\include', 'FirePlace\\include',
                'FirePlace\\include\\FireWorks', 'ThirdPartyLibs\\Lua51\\include']
COPY_DIRS = ['CvGameCoreDLL_Expansion2', 'CvWorldBuilderMap\\include', 'CvGameCoreDLLUtil\\include',
             'CvLocalization\\include', 'CvGameDatabase\\include', 'FirePlace\\include',
             'ThirdPartyLibs\\Lua51\\include']
PREDEFS = ['FXS_IS_DLL', 'WIN32', '_WINDOWS', '_USRDLL', 'EXTERNAL_PAUSING', 'CVGAMECOREDLL_EXPORTS',
           'FINAL_RELEASE', '_CRT_SECURE_NO_WARNINGS', '_WINDLL', 'VPDEBUG', 'FDATASTREAM_WIDTH_AUDIT']
COMMON = ['/Zs', '/EHsc', '/fp:precise', '/Zc:wchar_t', '/clang:-fno-caret-diagnostics', '/clang:-ftemplate-backtrace-limit=0',
          '-Wno-invalid-offsetof', '-Wno-tautological-constant-out-of-range-compare', '-Wno-comment',
          '-Wno-enum-constexpr-conversion']
ARCH_ARGS = {
    'x86': ['-m32', '-msse3', '/MD'],
    'x64': ['-m64', '/MD', '/std:c++14', '-Wno-c++11-narrowing', '/D_HAS_AUTO_PTR_ETC=1',
            '/D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS', '/D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING'],
}
ENV_BAT = {'x86': VS90, 'x64': VCVARS64}

FDS_BLOCK = r'''
#if defined(FDATASTREAM_WIDTH_AUDIT)
public:
	__declspec(deprecated("W64AUDIT")) FDataStream& operator<<(const unsigned long long& v) { Write(v); return *this; }
	__declspec(deprecated("W64AUDIT")) FDataStream& operator<<(const long long& v) { Write(v); return *this; }
	__declspec(deprecated("R64AUDIT")) FDataStream& operator>>(unsigned long long& v) { Read(v); return *this; }
	__declspec(deprecated("R64AUDIT")) FDataStream& operator>>(long long& v) { Read(v); return *this; }
#endif
'''
VIS_SAVE_BLOCK = r'''
#if defined(FDATASTREAM_WIDTH_AUDIT)
	__declspec(deprecated("V64AUDIT")) void operator()(const unsigned long long& v) { m_stream << v; }
	__declspec(deprecated("V64AUDIT")) void operator()(const long long& v) { m_stream << v; }
	__declspec(deprecated("V64AUDIT")) CvStreamSaveVisitor& operator<<(const unsigned long long& v) { m_stream << v; return *this; }
	__declspec(deprecated("V64AUDIT")) CvStreamSaveVisitor& operator<<(const long long& v) { m_stream << v; return *this; }
#endif
'''
VIS_LOAD_BLOCK = r'''
#if defined(FDATASTREAM_WIDTH_AUDIT)
	__declspec(deprecated("V64AUDIT")) void operator()(unsigned long long& v) { m_stream >> v; }
	__declspec(deprecated("V64AUDIT")) void operator()(long long& v) { m_stream >> v; }
	__declspec(deprecated("V64AUDIT")) CvStreamLoadVisitor& operator>>(unsigned long long& v) { m_stream >> v; return *this; }
	__declspec(deprecated("V64AUDIT")) CvStreamLoadVisitor& operator>>(long long& v) { m_stream >> v; return *this; }
#endif
'''
INNER = '__declspec(deprecated("INNER64AUDIT")) '


def sub1(text, old, new, what):
    n = text.count(old)
    if n != 1:
        sys.exit(f'anchor for {what} found {n} times; update the census script')
    return text.replace(old, new)


def instrument(tree):
    p = os.path.join(tree, 'FirePlace', 'include', 'FireWorks', 'FDataStream.h')
    t = open(p, encoding='latin-1').read()
    t = sub1(t, 'protected:\n\t// only access through shift operators', FDS_BLOCK + 'protected:\n\t// only access through shift operators', 'FDataStream block')
    for decl in ['void Write(const UINT64 &);', 'void Write(const INT64 &);', 'void Read(UINT64 &);', 'void Read(INT64 &);',
                 'void Write(int, const INT64 values[]);', 'void Write(int, const UINT64 values[]);',
                 'void Read(int, INT64 values[]);', 'void Read(int, UINT64 values[]);']:
        t = sub1(t, '\t' + decl, '\t' + INNER + decl, decl)
    open(p, 'w', encoding='latin-1').write(t)
    p = os.path.join(tree, 'CvGameCoreDLL_Expansion2', 'CvSerialize.h')
    t = open(p, encoding='latin-1').read()
    t = sub1(t, '\tCvStreamSaveVisitor(FDataStream& stream)\n\t\t: m_stream(stream)\n\t{}\n',
             '\tCvStreamSaveVisitor(FDataStream& stream)\n\t\t: m_stream(stream)\n\t{}\n' + VIS_SAVE_BLOCK, 'save visitor')
    t = sub1(t, '\tCvStreamLoadVisitor(FDataStream& stream)\n\t\t: m_stream(stream)\n\t{}\n',
             '\tCvStreamLoadVisitor(FDataStream& stream)\n\t\t: m_stream(stream)\n\t{}\n' + VIS_LOAD_BLOCK, 'load visitor')
    open(p, 'w', encoding='latin-1').write(t)


def env_for(bat, scratch):
    # A wrapper batch file avoids cmd's quote stripping on paths with spaces and parentheses.
    wrapper = os.path.join(scratch, 'env-' + os.path.basename(bat))
    with open(wrapper, 'w') as f:
        f.write(f'@call "{bat}" >NUL\n@set\n')
    # VSCMD_SKIP_SENDTELEMETRY: vcvars otherwise starts a background PowerShell for its telemetry (bug 0108; 0147)
    out = subprocess.run([os.environ.get('COMSPEC', 'cmd.exe'), '/c', wrapper], capture_output=True, text=True,
                         env=dict(os.environ, VSCMD_SKIP_SENDTELEMETRY='1')).stdout
    env = dict(l.split('=', 1) for l in out.splitlines() if '=' in l)
    if 'INCLUDE' not in {k.upper() for k in env}:
        sys.exit(f'no INCLUDE after {bat}')
    return env


def cpp_list(fork):
    src = open(os.path.join(fork, 'build_vp_clang.py'), encoding='utf-8').read()
    return [c.replace('\\\\', '\\') for c in re.findall(r"'(CvGameCoreDLL_Expansion2[^']+\.cpp)'", src)]


DIAG = re.compile(r'^(?P<file>.+?)\((?P<line>\d+),\d+\) ?: (?P<kind>warning|note|error): (?P<msg>.*)$')
# Headers whose templates relay values to the stream; the root of a record is the first requester outside them.
RELAY = ('FStlContainerSerialization.h', 'FDataStream.h', 'CvSerialize.h', 'CvSpanSerialization.h',
         'CvEnumMapSerialization.h', 'FAutoVariable.h', 'FAutoVector.h', 'FAutoArchiveClassContainer.h')


def norm(chain):
    # VC9 prints std::tr1:: and stdext:: where MSVC 14 prints std::; compare across builds without them.
    return re.sub(r'std::tr1::|tr1::|stdext::', 'std::', chain).replace(' >', '>')


def parse(log_text, tree):
    """Return a set of (kind, site, root) records. site = file:line of the warning, root = outermost
    requester outside the instrumented headers (from the instantiation notes), or the site itself."""
    recs = set()
    cur = None
    def flush():
        if cur:
            recs.add((cur['tag'], cur['site'], cur['root'], cur['chain']))
    for line in log_text.splitlines():
        m = DIAG.match(line.strip())
        if not m:
            continue
        f = os.path.relpath(m['file'], tree) if m['file'].lower().startswith(tree.lower()) else m['file']
        loc = f'{f}:{m["line"]}'.replace('/', os.sep)
        if m['kind'] in ('warning', 'error'):
            flush()
            cur = None
            t = re.search(r'(W64|R64|V64|INNER64)AUDIT', m['msg'])
            if m['kind'] == 'error':
                cur = {'tag': 'ERROR', 'site': loc, 'root': loc, 'chain': m['msg'][:200]}
            elif t:
                cur = {'tag': t.group(1), 'site': loc, 'root': loc, 'chain': ''}
        elif cur and m['kind'] == 'note' and 'requested here' in m['msg']:
            if cur['root'] == cur['site'] or os.path.basename(cur['root']).split(':')[0] in RELAY:
                cur['root'] = loc
            what = re.search(r"in instantiation of [^']*'([^']*)'", m['msg'])
            if what and not cur['chain']:
                cur['chain'] = norm(what.group(1))[:300]
    flush()
    return recs


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('fork')
    ap.add_argument('out')
    ap.add_argument('--arch', default='both')
    ap.add_argument('--jobs', type=int, default=16)
    ap.add_argument('--only', default='', help='compile only TUs whose path contains this text')
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    tree = os.path.join(a.out, 'tree')
    if os.path.exists(tree):
        shutil.rmtree(tree)
    for d in COPY_DIRS:
        shutil.copytree(os.path.join(a.fork, d), os.path.join(tree, d),
                        ignore=shutil.ignore_patterns('*.lib', '*.obj', '*.pdb', '*.dll'))
    instrument(tree)
    # CvGame.cpp includes ../commit_id.inc, which build_vp_clang.py generates; the census needs only a stub.
    with open(os.path.join(tree, 'commit_id.inc'), 'w') as f:
        f.write('const char CURRENT_GAMECORE_VERSION[] = "census";\n')
    cpps = [c for c in cpp_list(a.fork) if a.only in c]
    archs = ['x86', 'x64'] if a.arch == 'both' else [a.arch]
    results = {}
    for arch in archs:
        env = env_for(ENV_BAT[arch], a.out)
        args = COMMON + ARCH_ARGS[arch] + [f'/D{p}' for p in PREDEFS] + [f'/I"{os.path.join(tree, d)}"' for d in INCLUDE_DIRS]
        base = f'"{CLANG}" ' + ' '.join(args)

        def run(c):
            r = subprocess.run(f'{base} "{os.path.join(tree, c)}"', capture_output=True, text=True, env=env, shell=True)
            return c, r.returncode, r.stdout + r.stderr
        fails = 0
        with open(os.path.join(a.out, f'{arch}.log'), 'w', encoding='utf-8') as f, cf.ThreadPoolExecutor(a.jobs) as ex:
            text = []
            for c, rc, out in ex.map(run, cpps):
                f.write(f'==== {c} rc={rc}\n{out}\n')
                text.append(out)
                fails += rc != 0
        recs = parse('\n'.join(text), tree)
        results[arch] = recs
        with open(os.path.join(a.out, f'{arch}-sites.txt'), 'w', encoding='utf-8') as f:
            for r in sorted(recs, key=lambda r: (r[2], r[1], r[0])):
                f.write(f'{r[0]}\t{r[2]}\t{r[1]}\t{r[3]}\n')
        print(f'{arch}: {len(cpps)} TUs, {fails} failed, {len(recs)} unique records')
    if len(archs) == 2:
        only = results['x64'] - results['x86']
        with open(os.path.join(a.out, 'x64-only.txt'), 'w', encoding='utf-8') as f:
            for r in sorted(only, key=lambda r: (r[2], r[1], r[0])):
                f.write(f'{r[0]}\t{r[2]}\t{r[1]}\t{r[3]}\n')
        print(f'x64-only records: {len(only)}')


if __name__ == '__main__':
    main()
