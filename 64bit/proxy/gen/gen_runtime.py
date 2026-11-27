r"""Runtime code generator for every EXE<->DLL interface, both directions (epic 2300, task 2301).

    python proxy/gen/gen_runtime.py --out <dir> [--hooks-proxy proxy/src] [--hooks-server server/src/remote]
                                    --src <DLL fork copy> [--compile-check --objdir <dir>] [--jobs N]
                                    [--model-dir <dir>]   (issue 3503: the model read; default proxy/model)

Specification: the team-2300 contract (sections A-C), design: research/03-dll-engine-boundary/proxy-design.md
section 11; wire values proxy/include/vp_wire.h; runtime APIs proxy/src/proxy_rt.h (x86) and
server/src/remote/srv_rt.h (x64). Input: proxy/model/interfaces.json (all 75 interfaces) and the `wrappers` table
of proxy/model/model.json. Offline, about 2 s; output only under --out (never in the repository):

  common/vp_ids.gen.h, vp_ids.gen.cpp      kT_<I>, kM_<I>_<ident>, kAppSchemaHash, kVpKindCount, vpk_* tables
  proxy/  (x86)  proxy_ifaces.gen.h, proxy_ifaces_N.gen.cpp, proxy_factory.gen.cpp, proxy_engdispatch.gen.cpp,
                 proxy_hook_defaults.gen.cpp, vp_guids.gen.cpp
  server/ (x64)  server_hooks.gen.h, server_dispatch_N.gen.cpp, server_remote_N.gen.cpp,
                 server_hook_defaults.gen.cpp, vp_guids.gen.cpp

Ids: kT_<I> = 1-based position of I in interfaces.json; kM_<I>_<ident> = (kT_<I> << 16) | index of the method in
I's full method list (inherited methods included; destructors keep their index but get no id); ident = the method
name, or <name>_s<x86 slot> when the name occurs more than once in that list (gen_proxy.Iface). kAppSchemaHash =
FNV-1a 64 as gen_proxy.py (sorted by name, per interface: name, type id, per method key, both slots, id).

MD(I), the most derived interface of I's family: engine -> DLL: the direct interface base of the DLL wrapper class
(model.json `wrappers`) whose chain contains I, the most derived one if several; ICvUnknown is its own MD (every
wrapper contains it; it is the static type of ICvEnumerator::GetCurrent). DLL -> engine: the most derived
dll_to_engine interface whose chain contains I. Classes and dispatchers exist for MD interfaces only; every method
id the generated code sends is kM_<MD>_<ident>.

Wire (contract B): CALL = self handle, then the in-parameters in declaration order; RETURN = the return value,
then the out-parameters in declaration order. In/out parameters appear in both.

Codecs (contract C; each checked against the fork's headers and, where the meaning is not obvious, the DLL's
implementation at the pinned commit e9d0c4837):

  type                                         wire                                          note
  ints, enums, uint, DWORD, short, char        i32 (static_cast both ways)                   size_t narrowed to 32 bits
  bool / float / double / unsigned long long   b / f32 / f64 / u64
  const char*, LPCSTR, LPCTSTR, const TCHAR*   str (null kept)                               returned: interned by the
                                                                                             caller (ProxyRt::Intern;
                                                                                             server: VpGenInternS)
  CvString, const CvString&, std::string       str (c_str, size)                             returned const CvString&:
                                                                                             ProxyRt::InternCv
  const wchar*, const wchar_t*, LPCWSTR        wstr (null kept)                              returned: interned
  GUID, const GUID&                            16 bytes
  IDInfo, const IDInfo&, returned IDInfo       i32 eOwner, i32 iID
  CvColorA, UnitMoveRate,                      fields in declaration order (f32/i32/u32/b)   a returned const T& is
  CvWorldBuilderMapLoaderMapInfo                                                             kept in a member of the
                                                                                             proxy wrapper
  CvMapInitData* (ICvMap1::Init)               b present, then 4 i32 + 2 b                   read only (CvMap::reset)
  const std::vector<bool>&                     u32 n, n x b
  int&, bool&, float& (scalar references)      in: value; out: value                         in/out: CallAccumulator
                                                                                             accumulates
  int*, TechTypes*, int32* (scalar pointers)   in: b present [+ value]; out: value if present in/out with presence:
                                                                                             cursors pIterIdx,
                                                                                             GetCurrentResearchTech,
                                                                                             GetSteamStat
  CvString& (stripSpecialCharacters)           in: str; out: str
  char* + capacity parameter (caller buffer)   in: u32 capacity (0 = no buffer), str of the  callee clamps to 1 MiB;
                                                buffer's current string (in/out, 0380);       caller copies <= cap
                                                out: str (the capacity parameter itself is    bytes and a NUL when
                                                not sent)                                     there is room
  char* of ICvGame1::SetTimeStr                caller buffer of the constant capacity 256    CvDllGame::SetTimeStr
                                                                                             does strcpy_s(256)
  TechTypes* + uint (ICvTeam1::GetTechs)       in: u32 n, n x i32 (current elements, in/out,  out array; n = the
                                                0380); out: u32 n, n x i32                   capacity, <= 65536
  FDataStream& (engine -> DLL only)            Read*/read*: in, Write*/write*: out (vp_wire.h) ProxyRt/SrvRt Stream*
  ICv* / FILogFile* / CvDLLInterfaceIFaceBase*  handle (contract C table: DLL objects HandleOf / Register /
                                                RegisterTemp / Resolve / Wrap / WrapTemp, engine objects
                                                EngineHandleOf / EngineResolve / EngineWrap)

Not mechanical (a hook on the proxy and the server dispatch sides, an overridable loud default in RemoteGen_<I>):
lua_State*, lua_CFunction, void*, HANDLE, Database::Connection*, FIGameIniParser&, FIFile*, FIFile::Allocator&,
FFileErrorHandler*, FFileTimestamp, FAutoArchive&, FMemoryStream&, std::vector<std::pair<...>>&,
EnumeratedFilesList&, CvPopupInfo, const CvCombatMemberEntry*, IDInfo* node walkers, raw int* / const char**
returns of static tables, byte**, char*&, const int* / const bool* arrays (ICvUserInterface2::AddPopup,
TradeVisuals_NewRoute), CvPlotIndexVector (FFastVector allocates through FireWorks' FMALLOCALIGNED, which the
proxy does not link), variadic methods, and an ICvEnumerator the engine implements (returned by
GetSelectedUnits, passed to ICvPreGame1::SetDLCAvailable).

Special methods (generated, never hooks): ICvUnknown::QueryInterface and Destroy, ICvGame1::Update on the server
(SrvRt::EnterUpdate/LeaveUpdate), ICvEngineScriptSystem1::DestroyArgs (EngineForget after the call).

Hooks: the generator scans the hook directories (every *.cpp and *.h, comments stripped) for definitions
`ProxyHook_<I>_<ident>(...) {`, `ServerHook_...` and `EngHook_...`. A method with a hand-written hook calls it on
that side, mechanical or not; a non-mechanical method without one calls a generated loud default
(proxy_hook_defaults.gen.cpp, server_hook_defaults.gen.cpp). For every mechanical method with a hand-written hook
the generated body is also emitted as a forwarder (ProxyFwd_, ServerFwd_, EngFwd_). Every hook is declared (also
for mechanical methods), so a hand-written one compiles against the generated header.

--compile-check compiles every generated .cpp without linking: x86 with clang-cl -m32 in the VS 2008 environment
and the DLL's flags (toolchain.dll_args, include paths under --src), x64 with server/build_server.py's flags for
server/src/*.cpp (/W3 /WX, /Od). Objects under --objdir. Exit code 1 on any compile error.
"""
import argparse
import concurrent.futures
import importlib.util
import json
import os
import re
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gen_proxy as gp  # noqa: E402  Iface (ids, idents), fnv1a64
import marshal_types as mt  # noqa: E402
import toolchain as tc  # noqa: E402

REPO = tc.REPO
INTERFACES = os.path.join(tc.PROXY, 'model', 'interfaces.json')
MODEL_JSON = os.path.join(tc.PROXY, 'model', 'model.json')
HEADER = '// GENERATED by proxy/gen/gen_runtime.py from proxy/model/interfaces.json. Do not edit.\n'
CALLCONV = {'stdcall': 'DLLCALL', 'cdecl': '__cdecl', 'thiscall': ''}
MAX_BUF = 1 << 20     # caller buffers: the callee allocates at most this many bytes
MAX_ARRAY = 1 << 16   # out arrays: at most this many elements
FILES_PROXY = 6       # proxy_ifaces_N.gen.cpp
FILES_DISPATCH = 6    # server_dispatch_N.gen.cpp
FILES_REMOTE = 2      # server_remote_N.gen.cpp

SCALAR_I32 = 'i32'
STRUCT_FIELDS = {
    'CvColorA': [('r', 'f32'), ('g', 'f32'), ('b', 'f32'), ('a', 'f32')],
    'UnitMoveRate': [('m_fTotalMoveRate', 'f32'), ('m_fEaseIn', 'f32'), ('m_fEaseOut', 'f32'),
                     ('m_fIndividualOffset', 'f32'), ('m_fRowOffset', 'f32'), ('m_fCurveRoll', 'f32'),
                     ('m_iPathSubdivision', 'i32')],
    'CvWorldBuilderMapLoaderMapInfo': [('uiWidth', 'u32'), ('uiHeight', 'u32'), ('uiPlayers', 'u32'),
                                       ('uiCityStates', 'u32'), ('uiMajorCivStartingPositions', 'u32'),
                                       ('uiMinorCivStartingPositions', 'u32'), ('bWorldWrap', 'b'),
                                       ('bRandomResources', 'b')],
    'CvMapInitData': [('m_iGridW', 'i32'), ('m_iGridH', 'i32'), ('m_iTopLatitude', 'i32'),
                      ('m_iBottomLatitude', 'i32'), ('m_bWrapX', 'b'), ('m_bWrapY', 'b')],
    'IDInfo': [('eOwner', 'i32:PlayerTypes'), ('iID', 'i32')],
}
# Return types whose loud default cannot be value-initialised (abstract): a zeroed static dummy.
ABSTRACT_RET = {'FAutoArchive'}
# Caller buffers: method name -> (buffer parameter, capacity parameter or a constant).
CALLER_BUFFERS = {
    'GetDefineSTRING': ('szBuffer', 'lenBuffer'),
    'GetEvaluatedMapScriptPath': ('szEvaluatedPath', 'szEvaluatedPathSize'),
    'GetGameOption': ('szOptionNameBuffer', 'nOptionNameBufferSize'),
    'GetMapOption': ('szOptionNameBuffer', 'nOptionNameBuffersize'),
    'GetGreatWorkAudio': ('strSound', 'length'),
    'GetHexDebugLayerString': ('szBuffer', 'uiBufferLength'),
    'SetTimeStr': ('szString', '256'),
}
OUT_ARRAYS = {'GetTechs': ('pkTechArray', 'uiArraySize')}
# In arrays with a count parameter (issue 2503): method name -> {array parameter: count parameter}. The count crosses
# as its own i32 (before the array in both signatures); the array as b present, u32 n, n elements (i32 or b). The
# callee clamps its copy of the count to n. DLL call sites [C, e9d0c4837]: CvTradeClasses.cpp:2319, :2434, :4553
# (x, y of nPlots <= MAX_PLOTS_TO_DISPLAY); CvNotifications.cpp:1129 (5 ints, 2 bools via POPUP_PARAM_*_ARRAY).
IN_ARRAYS = {'TradeVisuals_NewRoute': {'x': 'nPlots', 'y': 'nPlots'},
             'AddPopup': {'pIntArray': 'iIntArraySize', 'pBoolArray': 'iBoolArraySize'}}
IN_ARRAY_TYPES = {'int *': ('int', 'i32'), 'const int *': ('int', 'i32'), 'bool *': ('bool', 'b'),
                  'const bool *': ('bool', 'b')}
STR_PTR = {'const char *', 'LPCSTR', 'LPCTSTR', 'const TCHAR *', 'const char *const'}
SSTR = {'CvString': 'CvString', 'const CvString &': 'CvString', 'const CvString': 'CvString',
        'const std::string &': 'std::string', 'const std::string': 'std::string'}
WSTR = {'const wchar *', 'const wchar_t *', 'LPCWSTR'}
SCALAR_REF = {'int &': ('int', 'i32'), 'bool &': ('bool', 'b'), 'float &': ('float', 'f32')}
SCALAR_PTR = {'int *': 'int', 'TechTypes *': 'TechTypes', 'int32 *': 'int32'}
# Only these parameters are single scalars (the cursors, GetCurrentResearchTech, GetSteamStat); other int* are arrays.
SCALAR_PTR_NAMES = {'pIterIdx', 'pkTech', 'pkTurnsLeft', 'pStat'}
STREAM_IN = re.compile(r'^(Read|read)')
STREAM_OUT = re.compile(r'^(Write|write)')
SEP = '//' + '-' * 117


def norm(t):
    return re.sub(r'\s+', ' ', t.strip())


NESTED = {'TURN_STATUS_TYPE': 'CvDLLInterfaceIFaceBase', 'ArgType': 'ICvEngineScriptSystemArgs1'}


def qual(t):
    """Types nested in an interface are qualified so they resolve outside its class scope."""
    for n, owner in NESTED.items():
        t = re.sub(r'(?<![:\w])%s\b' % n, '%s::%s' % (owner, n), t)
    return t


def iface_includes(m):
    """#include lines of every header that declares a model interface (the proxy's PCH lacks some)."""
    out = []
    for f in sorted({e['file'] for e in m.entries}):
        if f.startswith('FirePlace/include/'):
            out.append('#include <%s>' % f[len('FirePlace/include/'):])
        else:
            out.append('#include "%s"' % f.split('/')[-1])
    return out


def cstr_lit(s):
    return '"%s"' % s.replace('\\', '\\\\').replace('"', '\\"')


# ===================================================================================================== the model
class Model:
    def __init__(self):
        with open(INTERFACES, encoding='utf-8') as f:
            d = json.load(f)
        with open(MODEL_JSON, encoding='utf-8') as f:
            self.wrappers = json.load(f)['wrappers']
        self.entries = d['interfaces']
        self.order = [e['name'] for e in self.entries]
        self.by = {e['name']: e for e in self.entries}
        self.kt = {n: i + 1 for i, n in enumerate(self.order)}
        self.dir = {e['name']: e['direction'] for e in self.entries}
        self.iface = {n: gp.Iface(self.by[n], self.kt[n]) for n in self.order}
        self.md = {n: self._md(n) for n in self.order}
        self.mds = [n for n in self.order if self.md[n] == n]
        self.e2d = [n for n in self.mds if self.dir[n] == 'engine_to_dll']
        self.d2e = [n for n in self.mds if self.dir[n] == 'dll_to_engine']
        self.schema = 0xcbf29ce484222325
        for n in sorted(self.order):
            self.schema = gp.fnv1a64(self.iface[n].schema_text().encode('utf-8') + b'\0', self.schema)

    def chain(self, n):
        return self.by[n]['chain']

    def _md(self, n):
        if n == 'ICvUnknown':
            return n
        if self.dir[n] == 'engine_to_dll':
            cands = {b for w in self.wrappers.values() for b in w['bases'] if b in self.by and n in self.chain(b)}
        else:
            cands = {j for j in self.order if self.dir[j] == 'dll_to_engine' and n in self.chain(j)}
        if not cands:
            return n
        best = max(len(self.chain(c)) for c in cands)
        top = sorted(c for c in cands if len(self.chain(c)) == best)
        if len(top) != 1:
            raise SystemExit('MD(%s) is ambiguous: %s' % (n, top))
        return top[0]


# ============================================================================================ codecs (parameters)
class P:
    """One parameter codec. side: 'proxy' or 'server'. Lines use vp_w / vp_r on the caller, args / ret on the
    callee; the callee's local of parameter n is a_<n>."""
    def __init__(self, p, name):
        self.t = norm(p['type'])
        self.n = name
        self.a = 'a_' + name

    def caller_enc(self, side):
        return []

    def callee_dec(self, side):
        return []

    def callee_arg(self):
        return self.a

    def callee_check(self):
        return None

    def callee_out(self, side):
        return []

    def caller_out(self, side):
        return []


class PScalar(P):
    def __init__(self, p, name, wire, base):
        P.__init__(self, p, name)
        self.wire, self.base = wire, base

    def caller_enc(self, side):
        if self.wire == 'i32':
            return ['vp_w.i32(static_cast<int32_t>(%s));' % self.n]
        if self.wire == 'u64':
            return ['vp_w.u64(static_cast<uint64_t>(%s));' % self.n]
        return ['vp_w.%s(%s);' % (self.wire, self.n)]

    def callee_dec(self, side):
        return ['%s %s = static_cast<%s>(args.%s());' % (self.base, self.a, self.base, self.wire)]


class PCStr(P):
    def caller_enc(self, side):
        return ['vp_w.str(%s);' % self.n]

    def callee_dec(self, side):
        return ['const char* %s = args.cstr();' % self.a]


class PSStr(P):
    def __init__(self, p, name, base):
        P.__init__(self, p, name)
        self.base = base

    def caller_enc(self, side):
        return ['vp_w.str(%s.c_str(), static_cast<uint32_t>(%s.size()));' % (self.n, self.n)]

    def callee_dec(self, side):
        return ['const char* %s_c = args.cstr();' % self.a, '%s %s(%s_c ? %s_c : "");' % (self.base, self.a, self.a,
                                                                                           self.a)]


class PWStr(P):
    def caller_enc(self, side):
        return ['vp_w.wstr(%s);' % self.n]

    def callee_dec(self, side):
        return ['const wchar_t* %s = args.wcstr();' % self.a]


class PGuid(P):
    def caller_enc(self, side):
        return ['vp_w.bytes(&%s, 16u);' % self.n]

    def callee_dec(self, side):
        return ['GUID %s;' % self.a, 'memset(&%s, 0, sizeof(GUID));' % self.a,
                '{ uint32_t n_ = 0; const uint8_t* g_ = args.bytes(&n_); if (g_ && n_ == 16u) memcpy(&%s, g_, 16u); }'
                % self.a]


def struct_enc(w, expr, typ):
    out = []
    for f, wire in STRUCT_FIELDS[typ]:
        wire = wire.split(':')[0]
        if wire == 'i32':
            out.append('%s.i32(static_cast<int32_t>(%s%s));' % (w, expr, f))
        elif wire == 'u32':
            out.append('%s.u32(static_cast<uint32_t>(%s%s));' % (w, expr, f))
        else:
            out.append('%s.%s(%s%s);' % (w, wire, expr, f))
    return out


def struct_dec(r, expr, typ):
    out = []
    for f, wire in STRUCT_FIELDS[typ]:
        parts = wire.split(':')
        if parts[0] == 'b':
            out.append('%s%s = %s.b();' % (expr, f, r))
        elif len(parts) == 2:
            out.append('%s%s = static_cast<%s>(%s.%s());' % (expr, f, parts[1], r, parts[0]))
        else:
            out.append('%s%s = %s.%s();' % (expr, f, r, parts[0]))
    return out


class PStruct(P):
    """IDInfo by value / const reference."""
    def __init__(self, p, name, typ):
        P.__init__(self, p, name)
        self.typ = typ

    def caller_enc(self, side):
        return struct_enc('vp_w', self.n + '.', self.typ)

    def callee_dec(self, side):
        return ['%s %s;' % (self.typ, self.a)] + struct_dec('args', self.a + '.', self.typ)


class PStructPtrIn(P):
    """CvMapInitData*: read-only input pointer with a presence flag."""
    def __init__(self, p, name, typ):
        P.__init__(self, p, name)
        self.typ = typ

    def caller_enc(self, side):
        return ['vp_w.b(%s != NULL);' % self.n, 'if (%s)' % self.n, '{'] + \
            ['    ' + s for s in struct_enc('vp_w', self.n + '->', self.typ)] + ['}']

    def callee_dec(self, side):
        return ['%s %s_v;' % (self.typ, self.a), '%s* %s = NULL;' % (self.typ, self.a), 'if (args.b())', '{'] + \
            ['    ' + s for s in struct_dec('args', self.a + '_v.', self.typ)] + ['    %s = &%s_v;' % (self.a, self.a),
                                                                                  '}']


class PVecBool(P):
    def caller_enc(self, side):
        return ['vp_w.u32(static_cast<uint32_t>(%s.size()));' % self.n,
                'for (size_t i_ = 0; i_ < %s.size(); ++i_) vp_w.b(%s[i_]);' % (self.n, self.n)]

    def callee_dec(self, side):
        return ['std::vector<bool> %s;' % self.a,
                '{ const uint32_t n_ = args.u32(); for (uint32_t i_ = 0; i_ < n_ && args.ok(); ++i_) %s.push_back('
                'args.b()); }' % self.a]


class PInArray(P):
    """const int* / const bool* (or non-const, read only) array with a count parameter (IN_ARRAYS, issue 2503).
    Wire: b present, u32 n (= the count clamped to [0, MAX_ARRAY], 0 for a null pointer), n x (i32 | b). The callee
    passes a pointer to its copy (never NULL when present, also for n = 0) and clamps its count local to n."""
    def __init__(self, p, name, cnt, elem, wire):
        P.__init__(self, p, name)
        self.cnt, self.elem, self.wire = cnt, elem, wire

    def caller_enc(self, side):
        put = 'vp_w.i32(static_cast<int32_t>(%s[i_]))' % self.n if self.wire == 'i32' else 'vp_w.b(%s[i_])' % self.n
        return ['{',
                '    const uint32_t n_ = (%s && (%s) > 0) ? ((%s) > %d ? %du : static_cast<uint32_t>(%s)) : 0u;' % (
                    self.n, self.cnt, self.cnt, MAX_ARRAY, MAX_ARRAY, self.cnt),
                '    vp_w.b(%s != NULL);' % self.n, '    vp_w.u32(n_);',
                '    for (uint32_t i_ = 0; i_ < n_; ++i_) %s;' % put, '}']

    def callee_dec(self, side):
        rt = 'SrvRt' if side == 'server' else 'ProxyRt'
        buf = 'std::vector<int>' if self.wire == 'i32' else 'std::vector<unsigned char>'
        get = 'args.i32()' if self.wire == 'i32' else 'static_cast<unsigned char>(args.b() ? 1 : 0)'
        ptr = '&%s_buf[0]' % self.a if self.wire == 'i32' else 'reinterpret_cast<bool*>(&%s_buf[0])' % self.a
        return ['const bool %s_p = args.b();' % self.a, 'const uint32_t %s_n = args.u32();' % self.a,
                'if (%s_n > %du) return %s::DispatchFail(ch, ret, vpw::kStatusBadArguments, "%s: array over %d elements");'
                % (self.a, MAX_ARRAY, rt, self.n, MAX_ARRAY),
                '%s %s_buf;' % (buf, self.a), '%s_buf.reserve(%s_n + 1u);' % (self.a, self.a),
                'for (uint32_t i_ = 0; i_ < %s_n && args.ok(); ++i_) %s_buf.push_back(%s);' % (self.a, self.a, get),
                'if (%s_buf.empty()) %s_buf.push_back(0);  // a valid pointer also for n = 0' % (self.a, self.a),
                '%s* %s = %s_p ? %s : NULL;' % (self.elem, self.a, self.a, ptr),
                'if (a_%s > static_cast<int>(%s_n)) a_%s = static_cast<int>(%s_n);' % (self.cnt, self.a, self.cnt,
                                                                                       self.a)]


class PHandle(P):
    def __init__(self, p, name, m, call_dir, T, const):
        P.__init__(self, p, name)
        self.m, self.call_dir, self.T, self.const = m, call_dir, T, const
        self.objdir = 'dll' if m.dir[T] == 'engine_to_dll' else 'eng'
        self.CT = ('const ' if const else '') + T
        self.MD = m.md[T]

    def caller_enc(self, side):
        if side == 'proxy':
            e = 'ProxyRt::HandleOf(%s)' % self.n if self.objdir == 'dll' else \
                'ProxyRt::EngineHandleOf(%s, kT_%s)' % (self.n, self.MD)
        else:
            e = 'SrvRt::RegisterTemp(%s, kT_%s)' % (self.n, self.MD) if self.objdir == 'dll' else \
                'SrvRt::EngineHandleOf(%s)' % self.n
        return ['vp_w.handle(%s);' % e]

    def callee_dec(self, side):
        h = '%s_h' % self.a
        if side == 'server':
            e = 'static_cast<%s*>(SrvRt::Resolve(%s, kT_%s))' % (self.CT, h, self.T) if self.objdir == 'dll' else \
                'static_cast<%s*>(static_cast<%s*>(SrvRt::EngineWrap(%s)))' % (self.CT, self.MD, h)
        else:
            e = 'static_cast<%s*>(static_cast<%s*>(ProxyRt::WrapTemp(%s)))' % (self.CT, self.MD, h) \
                if self.objdir == 'dll' else 'static_cast<%s*>(ProxyRt::EngineResolve(%s, kT_%s))' % (self.CT, h,
                                                                                                        self.T)
        return ['const vpipc::Handle %s = args.handle();' % h, '%s* %s = %s;' % (self.CT, self.a, e)]

    def callee_check(self):
        return '(%s_h.type != 0u && !%s)' % (self.a, self.a)


class PScalarRef(P):
    """int& / bool& / float&: in/out."""
    def __init__(self, p, name, base, wire):
        P.__init__(self, p, name)
        self.base, self.wire = base, wire

    def caller_enc(self, side):
        return ['vp_w.%s(%s);' % (self.wire, self.n)]

    def callee_dec(self, side):
        return ['%s %s = static_cast<%s>(args.%s());' % (self.base, self.a, self.base, self.wire)]

    def callee_out(self, side):
        return ['ret.%s(%s);' % (self.wire, self.a)]

    def caller_out(self, side):
        return ['%s = static_cast<%s>(vp_r.%s());' % (self.n, self.base, self.wire)]


class PScalarPtr(P):
    """int* / TechTypes* / int32*: in/out with a presence flag (null allowed)."""
    def __init__(self, p, name, base):
        P.__init__(self, p, name)
        self.base = base

    def caller_enc(self, side):
        return ['vp_w.b(%s != NULL);' % self.n, 'if (%s) vp_w.i32(static_cast<int32_t>(*%s));' % (self.n, self.n)]

    def callee_dec(self, side):
        return ['%s %s_v = %s();' % (self.base, self.a, self.base), '%s* %s = NULL;' % (self.base, self.a),
                'if (args.b())', '{', '    %s_v = static_cast<%s>(args.i32());' % (self.a, self.base),
                '    %s = &%s_v;' % (self.a, self.a), '}']

    def callee_out(self, side):
        return ['if (%s) ret.i32(static_cast<int32_t>(%s_v));' % (self.a, self.a)]

    def caller_out(self, side):
        return ['if (%s) *%s = static_cast<%s>(vp_r.i32());' % (self.n, self.n, self.base)]


class PCvStringRef(P):
    """CvString& (stripSpecialCharacters): in/out string."""
    def caller_enc(self, side):
        return ['vp_w.str(%s.c_str(), static_cast<uint32_t>(%s.size()));' % (self.n, self.n)]

    def callee_dec(self, side):
        return ['const char* %s_c = args.cstr();' % self.a, 'CvString %s(%s_c ? %s_c : "");' % (self.a, self.a, self.a)]

    def callee_out(self, side):
        return ['ret.str(%s.c_str(), static_cast<uint32_t>(%s.size()));' % (self.a, self.a)]

    def caller_out(self, side):
        return ['{ const char* s_ = vp_r.cstr(); %s = s_ ? s_ : ""; }' % self.n]


class PCallerBuf(P):
    """char* caller buffer; cap = the capacity parameter's name or a constant. Sends the capacity (0 = no buffer)
    and, in/out (issue 0380), the buffer's current string (up to the first NUL, at most min(capacity, MAX_BUF)
    bytes): the callee's copy starts from it, so a callee that leaves the buffer alone (GetDefineSTRING and the
    by-index GetGameOption / GetMapOption when they return false, GetHexDebugLayerString on a water plot) hands the
    caller's own bytes back, as in-process."""
    def __init__(self, p, name, cap, cap_is_param):
        P.__init__(self, p, name)
        self.cap, self.cap_is_param = cap, cap_is_param

    def capexpr(self):
        return '((%s && (%s) > 0) ? static_cast<uint32_t>(%s) : 0u)' % (self.n, self.cap, self.cap)

    def caller_enc(self, side):
        return ['{',
                '    const uint32_t c_ = %s;' % self.capexpr(),
                '    vp_w.u32(c_);',
                '    const uint32_t k_ = c_ ? static_cast<uint32_t>(strnlen(%s, c_ < %du ? c_ : %du)) : 0u;' % (
                    self.n, MAX_BUF, MAX_BUF),
                '    vp_w.str(k_ ? %s : "", k_);' % self.n,
                '}']

    def callee_dec(self, side):
        return ['uint32_t %s_cap = args.u32();' % self.a,
                'if (%s_cap > %du) %s_cap = %du;' % (self.a, MAX_BUF, self.a, MAX_BUF),
                'std::vector<char> %s_buf(%s_cap + 1u, 0);' % (self.a, self.a),
                'char* %s = %s_cap ? &%s_buf[0] : NULL;' % (self.a, self.a, self.a),
                '{ uint32_t n_ = 0; const char* s_ = args.cstr(&n_); if (s_ && %s_cap) memcpy(&%s_buf[0], s_, n_ < %s_cap'
                ' ? n_ : %s_cap); }' % (self.a, self.a, self.a, self.a)]

    def callee_out(self, side):
        return ['ret.str(%s);' % self.a]

    def caller_out(self, side):
        return ['{',
                '    const char* s_ = vp_r.cstr();',
                '    const uint32_t c_ = %s;' % self.capexpr(),
                '    if (c_)',
                '    {',
                '        size_t k_ = s_ ? strlen(s_) : 0;',
                '        if (k_ > c_) k_ = c_;  // c_ bytes without a NUL only when the caller sent them so (in/out)',
                '        if (k_) memcpy(%s, s_, k_);' % self.n,
                '        if (k_ < c_) %s[k_] = 0;' % self.n,
                '    }',
                '}']


class PCap(P):
    """The capacity parameter of a caller buffer / out array: not sent; the callee passes its clamped capacity."""
    def __init__(self, p, name, buf):
        P.__init__(self, p, name)
        self.buf = buf
        self.base = mt.strip_top_const(self.t)

    def callee_dec(self, side):
        return ['%s %s = static_cast<%s>(%s_cap);' % (self.base, self.a, self.base, self.buf.a)]


class POutArray(P):
    """Out array with a capacity parameter (ICvTeam1::GetTechs). In/out (issue 0380): the caller sends its capacity
    clamped to MAX_ARRAY and that many current elements; the callee's copy starts from them, so the elements past
    the ones the callee writes (GetTechs writes only the techs it finds) come back unchanged, as in-process."""
    def __init__(self, p, name, cap, base):
        P.__init__(self, p, name)
        self.cap, self.base = cap, base

    def capexpr(self):
        return '((%s && (%s) > 0) ? static_cast<uint32_t>(%s) : 0u)' % (self.n, self.cap, self.cap)

    def caller_enc(self, side):
        return ['{',
                '    uint32_t c_ = %s;' % self.capexpr(),
                '    if (c_ > %du) c_ = %du;' % (MAX_ARRAY, MAX_ARRAY),
                '    vp_w.u32(c_);',
                '    for (uint32_t i_ = 0; i_ < c_; ++i_) vp_w.i32(static_cast<int32_t>(%s[i_]));' % self.n,
                '}']

    def callee_dec(self, side):
        rt = 'SrvRt' if side == 'server' else 'ProxyRt'
        return ['uint32_t %s_cap = args.u32();' % self.a,
                'if (%s_cap > %du) return %s::DispatchFail(ch, ret, vpw::kStatusBadArguments, "%s: array over %d '
                'elements");' % (self.a, MAX_ARRAY, rt, self.n, MAX_ARRAY),
                'std::vector<%s> %s_buf(%s_cap ? %s_cap : 1u);' % (self.base, self.a, self.a, self.a),
                'for (uint32_t i_ = 0; i_ < %s_cap && args.ok(); ++i_) %s_buf[i_] = static_cast<%s>(args.i32());' % (
                    self.a, self.a, self.base),
                '%s* %s = %s_cap ? &%s_buf[0] : NULL;' % (self.base, self.a, self.a, self.a)]

    def callee_out(self, side):
        return ['ret.u32(%s_cap);' % self.a,
                'for (uint32_t i_ = 0; i_ < %s_cap; ++i_) ret.i32(static_cast<int32_t>(%s_buf[i_]));' % (self.a, self.a)]

    def caller_out(self, side):
        return ['{',
                '    const uint32_t n_ = vp_r.u32();',
                '    const uint32_t c_ = %s;' % self.capexpr(),
                '    for (uint32_t i_ = 0; i_ < n_ && vp_r.ok(); ++i_)',
                '    {',
                '        const int32_t v_ = vp_r.i32();',
                '        if (i_ < c_) %s[i_] = static_cast<%s>(v_);' % (self.n, self.base),
                '    }',
                '}']


class PStream(P):
    def __init__(self, p, name, out):
        P.__init__(self, p, name)
        self.out = out

    def caller_enc(self, side):
        return [] if self.out else ['ProxyRt::StreamInArg(vp_w, %s);' % self.n]

    def callee_dec(self, side):
        return ['FDataStream& %s = SrvRt::%s;' % (self.a, 'StreamOut()' if self.out else 'StreamIn(args)')]

    def callee_out(self, side):
        return ['SrvRt::%s(ret, %s);' % ('StreamOutRet' if self.out else 'StreamInRet', self.a)]

    def caller_out(self, side):
        return ['ProxyRt::%s(vp_r, %s);' % ('StreamOutRet' if self.out else 'StreamInRet', self.n)]


# =============================================================================================== codecs (returns)
class R:
    """Return codec. callee_enc writes r_ into ret; caller_dec declares `VpR vp_ret` from vp_r."""
    member = None

    def __init__(self, t):
        self.t = t

    def callee_enc(self, side):
        return []

    def caller_dec(self, side, store=None):
        return []


class RVoid(R):
    pass


class RScalar(R):
    def __init__(self, t, wire):
        R.__init__(self, t)
        self.wire = wire

    def callee_enc(self, side):
        if self.wire == 'i32':
            return ['ret.i32(static_cast<int32_t>(r_));']
        if self.wire == 'u64':
            return ['ret.u64(static_cast<uint64_t>(r_));']
        return ['ret.%s(r_);' % self.wire]

    def caller_dec(self, side, store=None):
        return ['VpR vp_ret = static_cast<VpR>(vp_r.%s());' % self.wire]


class RCStr(R):
    def callee_enc(self, side):
        return ['ret.str(r_);']

    def caller_dec(self, side, store=None):
        f = 'ProxyRt::Intern' if side == 'proxy' else 'VpGenInternS'
        return ['const char* s_ = vp_r.cstr();', 'VpR vp_ret = s_ ? %s(s_) : NULL;' % f]


class RWStr(R):
    def callee_enc(self, side):
        return ['ret.wstr(r_);']

    def caller_dec(self, side, store=None):
        f = 'ProxyRt::InternW' if side == 'proxy' else 'VpGenInternW'
        return ['const wchar_t* s_ = vp_r.wcstr();', 'VpR vp_ret = s_ ? %s(s_) : NULL;' % f]


class RSStr(R):
    def callee_enc(self, side):
        return ['ret.str(r_.c_str(), static_cast<uint32_t>(r_.size()));']

    def caller_dec(self, side, store=None):
        return ['const char* s_ = vp_r.cstr();', 'VpR vp_ret(s_ ? s_ : "");']


class RCvStringRef(R):
    """const CvString& returned to the proxy: interned (ProxyRt::InternCv)."""
    def callee_enc(self, side):
        return ['ret.str(r_.c_str(), static_cast<uint32_t>(r_.size()));']

    def caller_dec(self, side, store=None):
        return ['const char* s_ = vp_r.cstr();', 'VpR vp_ret = ProxyRt::InternCv(s_ ? s_ : "");']


class RGuid(R):
    def callee_enc(self, side):
        return ['ret.bytes(&r_, 16u);']

    def caller_dec(self, side, store=None):
        return ['GUID g_;', 'memset(&g_, 0, sizeof(GUID));',
                '{ uint32_t n_ = 0; const uint8_t* p_ = vp_r.bytes(&n_); if (p_ && n_ == 16u) memcpy(&g_, p_, 16u); }',
                'VpR vp_ret = g_;']


class RStruct(R):
    def __init__(self, t, typ):
        R.__init__(self, t)
        self.typ = typ

    def callee_enc(self, side):
        return struct_enc('ret', 'r_.', self.typ)

    def caller_dec(self, side, store=None):
        return ['%s v_;' % self.typ] + struct_dec('vp_r', 'v_.', self.typ) + ['VpR vp_ret = v_;']


class RStructRef(R):
    """const T& returned to the proxy: kept in a member of the wrapper (stable until the next call)."""
    def __init__(self, t, typ, ident):
        R.__init__(self, t)
        self.typ = typ
        self.member = ('mutable %s' % typ, 'm_vpr_%s' % ident)

    def callee_enc(self, side):
        return struct_enc('ret', 'r_.', self.typ)

    def caller_dec(self, side, store=None):
        return struct_dec('vp_r', store + '.', self.typ) + ['VpR vp_ret = %s;' % store]


class RHandle(R):
    def __init__(self, t, m, T, const):
        R.__init__(self, t)
        self.m, self.T = m, T
        self.objdir = 'dll' if m.dir[T] == 'engine_to_dll' else 'eng'
        self.CT = ('const ' if const else '') + T
        self.MD = m.md[T]

    # A DLL object returned by an engine call (DLL -> engine: GetHeadSelectedUnit, getSelectionPlot,
    # getHeadSelectedCity, getLastSelectedUnit, GetScratchDeal) is a reference the caller owns: every DLL call site
    # wraps it in CvInterfacePtr and Destroys it [C, e9d0c4837: CvGame.cpp:2018-3729, CvDealAI.cpp:6157, ...]. The
    # engine's reference is on a proxy wrapper, the DLL's Destroy acts on the server object: VpGenGiveDll (proxy)
    # makes the server take a DLL reference for the caller (QueryInterface forwarded on the handle) and then releases
    # the engine's reference; VpGenAdoptDll (server) hands that reference to the caller and drops its registration.
    # Before issue 2503 the server returned the registered object itself and the DLL's Destroy freed the object a
    # live proxy wrapper still referred to (never reached headless: the observer has no selection).
    def callee_enc(self, side):
        if side == 'server':
            e = 'SrvRt::Register(r_, kT_%s)' % self.MD if self.objdir == 'dll' else 'SrvRt::EngineHandleOf(r_)'
        else:
            e = 'VpGenGiveDll(const_cast<%s*>(r_))' % self.T if self.objdir == 'dll' else \
                'ProxyRt::EngineHandleOf(r_, kT_%s)' % self.MD
        return ['ret.handle(%s);' % e]

    def caller_dec(self, side, store=None):
        if side == 'proxy':
            e = 'static_cast<%s*>(static_cast<%s*>(ProxyRt::Wrap(h_)))' % (self.CT, self.MD) \
                if self.objdir == 'dll' else 'static_cast<%s*>(ProxyRt::EngineResolve(h_, kT_%s))' % (self.CT, self.T)
        else:
            e = 'static_cast<%s*>(VpGenAdoptDll(h_, kT_%s))' % (self.CT, self.T) if self.objdir == 'dll' else \
                'static_cast<%s*>(static_cast<%s*>(SrvRt::EngineWrap(h_)))' % (self.CT, self.MD)
        return ['const vpipc::Handle h_ = vp_r.handle();', 'VpR vp_ret = %s;' % e]


# ==================================================================================================== analysis
def iface_ptr(m, t):
    mm = re.match(r'^(const )?(\w+) \*$', t)
    if mm and mm.group(2) in m.by:
        return mm.group(2), bool(mm.group(1))
    return None, False


def param_codec(m, meth, call_dir, i, p, name, params_by_name):
    """Returns (codec, None) or (None, reason)."""
    t = norm(p['type'])
    b = mt.strip_top_const(t)
    mname = meth['name']
    if mname in CALLER_BUFFERS:
        buf, cap = CALLER_BUFFERS[mname]
        if p['name'] == buf and t == 'char *':
            return PCallerBuf(p, name, cap, cap in params_by_name), None
    if mname in OUT_ARRAYS:
        arr, cap = OUT_ARRAYS[mname]
        if p['name'] == arr:
            return POutArray(p, name, cap, t[:-2].strip()), None
    if mname in IN_ARRAYS and p['name'] in IN_ARRAYS[mname] and t in IN_ARRAY_TYPES:
        cnt = IN_ARRAYS[mname][p['name']]
        names = [q['name'] for q in meth['params']]
        if cnt not in names or names.index(cnt) > i or mt.strip_top_const(norm(meth['params'][names.index(cnt)]['type'])) != 'int':
            raise SystemExit('%s: the count %s of the in array %s must be an int parameter before it' % (
                meth['key'], cnt, p['name']))
        elem, wire = IN_ARRAY_TYPES[t]
        return PInArray(p, name, cnt, elem, wire), None
    if t == 'void':
        return None, 'void parameter'
    if b == 'bool':
        return PScalar(p, name, 'b', 'bool'), None
    if b == 'float':
        return PScalar(p, name, 'f32', 'float'), None
    if b == 'double':
        return PScalar(p, name, 'f64', 'double'), None
    if b in ('unsigned long long', 'uint64', 'long long'):
        return PScalar(p, name, 'u64', qual(b)), None
    c = mt.classify_type(t, 'param')[0]
    if c == 'i32' and not t.endswith(('*', '&')):
        return PScalar(p, name, 'i32', qual(b)), None
    if t in STR_PTR:
        return PCStr(p, name), None
    if t in SSTR:
        return PSStr(p, name, SSTR[t]), None
    if t in WSTR:
        return PWStr(p, name), None
    if t in ('GUID', 'const GUID &', 'const GUID'):
        return PGuid(p, name), None
    if t in ('IDInfo', 'const IDInfo &', 'const IDInfo'):
        return PStruct(p, name, 'IDInfo'), None
    if t == 'CvMapInitData *':
        return PStructPtrIn(p, name, 'CvMapInitData'), None
    if t == 'const std::vector<bool> &':
        return PVecBool(p, name), None
    T, const = iface_ptr(m, t)
    if T:
        if T == 'ICvEnumerator' and call_dir == 'e2d':
            return None, 'ICvEnumerator argument from the engine: an engine-implemented list (SetDLCAvailable)'
        return PHandle(p, name, m, call_dir, T, const), None
    if t in SCALAR_REF:
        base, wire = SCALAR_REF[t]
        return PScalarRef(p, name, base, wire), None
    if t in SCALAR_PTR and p['name'] in SCALAR_PTR_NAMES:
        return PScalarPtr(p, name, SCALAR_PTR[t]), None
    if t == 'CvString &':
        return PCvStringRef(p, name), None
    if t == 'FDataStream &':
        if call_dir != 'e2d':
            return None, 'FDataStream& of a DLL -> engine call'
        if STREAM_IN.match(mname):
            return PStream(p, name, False), None
        if STREAM_OUT.match(mname):
            return PStream(p, name, True), None
        return None, 'FDataStream& in a method named neither Read*/read* nor Write*/write*'
    if t == 'const CvPlotIndexVector &':
        return None, 'CvPlotIndexVector: FFastVector allocates through FMALLOCALIGNED (FireWorks), not linked by the proxy'
    return None, '%s %s: %s' % (t, p['name'], mt.classify_type(t, 'param')[2] or 'no codec')


def ret_codec(m, meth, call_dir, ident):
    t = norm(meth['ret'])
    b = mt.strip_top_const(t)
    caller = 'proxy' if call_dir == 'e2d' else 'server'
    if t == 'void':
        return RVoid(t), None
    if b == 'bool':
        return RScalar(t, 'b'), None
    if b == 'float':
        return RScalar(t, 'f32'), None
    if b == 'double':
        return RScalar(t, 'f64'), None
    if b in ('unsigned long long', 'uint64', 'long long'):
        return RScalar(t, 'u64'), None
    c = mt.classify_type(t, 'ret')[0]
    if c == 'i32' and not t.endswith(('*', '&')):
        return RScalar(t, 'i32'), None
    if t in ('const char *', 'const char *const', 'LPCSTR'):
        return RCStr(t), None
    if t in WSTR:
        return RWStr(t), None
    if t in ('CvString', 'const CvString', 'const std::string', 'std::string'):
        return RSStr(t), None
    if t == 'const CvString &':
        if caller != 'proxy':
            return None, 'const CvString& returned to the server'
        return RCvStringRef(t), None
    if t in ('GUID', 'const GUID'):
        return RGuid(t), None
    if b in ('IDInfo', 'UnitMoveRate'):
        return RStruct(t, b), None
    mm = re.match(r'^const (CvColorA|CvWorldBuilderMapLoaderMapInfo) &$', t)
    if mm:
        if caller != 'proxy':
            return None, 'struct reference returned to the server'
        return RStructRef(t, mm.group(1), ident), None
    T, const = iface_ptr(m, t)
    if T:
        if T == 'ICvEnumerator' and call_dir == 'd2e':
            return None, 'returns an engine-implemented ICvEnumerator (GetSelectedUnits)'
        return RHandle(t, m, T, const), None
    return None, 'returns %s: %s' % (t, mt.classify_type(t, 'ret')[2] or 'no codec')


class Meth:
    def __init__(self, m, md, x):
        self.x = x
        self.m = x['m']
        mm = self.m
        self.md = md
        self.ident = x['ident']
        self.kM = x['const_name']
        self.key = mm['key']
        self.name = mm['name']
        self.decl = mm['declared_in']
        self.const = mm['const']
        self.cc = CALLCONV[mm['callconv']]
        self.ret = qual(norm(mm['ret']))
        self.call_dir = 'e2d' if m.dir[md] == 'engine_to_dll' else 'd2e'
        self.names = [p['name'] if p['name'] else 'vp_p%d' % i for i, p in enumerate(mm['params'])]
        self.special = None
        if self.decl == 'ICvUnknown' and self.name in ('QueryInterface', 'Destroy'):
            self.special = self.name
        elif self.key == 'ICvGame1::Update()':
            self.special = 'Update'
        elif self.key.startswith('ICvEngineScriptSystem1::DestroyArgs('):
            self.special = 'DestroyArgs'
        self.reasons = []
        self.params = []
        self.retc = None
        if mm['variadic']:
            self.reasons.append('variadic (...)')
        pbn = {p['name'] for p in mm['params']}
        for i, p in enumerate(mm['params']):
            c, why = param_codec(m, mm, self.call_dir, i, p, self.names[i], pbn)
            if why:
                self.reasons.append(why)
            self.params.append(c)
        # capacity parameters of caller buffers / out arrays
        for i, c in enumerate(self.params):
            if isinstance(c, (PCallerBuf, POutArray)) and getattr(c, 'cap_is_param', True):
                capn = c.cap
                for j, p in enumerate(mm['params']):
                    if p['name'] == capn:
                        if j < i:
                            raise SystemExit('%s: capacity before its buffer' % self.key)
                        self.params[j] = PCap(p, self.names[j], c)
        self.retc, why = ret_codec(m, mm, self.call_dir, self.ident)
        if why:
            self.reasons.append(why)
        self.mechanical = not self.reasons

    def sig(self, names=True):
        ps = []
        for i, p in enumerate(self.m['params']):
            t = qual(norm(p['type']))
            ps.append('%s %s' % (t, self.names[i]) if names else t)
        if self.m['variadic']:
            ps.append('...')
        return ', '.join(ps)

    def head(self, cls):
        cc = (' ' + self.cc) if self.cc else ''
        return '%s%s %s::%s(%s)%s' % (self.ret, cc, cls, self.name, self.sig(), ' const' if self.const else '')

    def decl_line(self, override=True):
        cc = (self.cc + ' ') if self.cc else ''
        return 'virtual %s %s%s(%s)%s%s;' % (self.ret, cc, self.name, self.sig(), ' const' if self.const else '',
                                            ' override' if override else '')


class Iface:
    def __init__(self, m, name):
        self.name = name
        self.dir = m.dir[name]
        self.chain = m.chain(name)
        self.meths = [Meth(m, name, x) for x in m.iface[name].methods]


# =================================================================================================== hook scan
HOOK_DEF = re.compile(r'\b((?:ProxyHook|ServerHook|EngHook)_\w+)\s*\((?:[^;{}()]|\([^()]*\))*\)\s*(?:const\s*)?\{')


def scan_hooks(dirs):
    found = {}
    for d in dirs:
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if not fn.endswith(('.cpp', '.h')) or '.gen.' in fn:
                continue
            path = os.path.join(d, fn)
            with open(path, encoding='utf-8', errors='replace') as f:
                text = f.read()
            text = re.sub(r'/\*.*?\*/', ' ', text, flags=re.S)
            text = re.sub(r'//[^\n]*', ' ', text)
            for mm in HOOK_DEF.finditer(text):
                found.setdefault(mm.group(1), os.path.relpath(path, REPO).replace('\\', '/'))
    return found


# ================================================================================================ emit helpers
def lit_key(x):
    return cstr_lit(x.key)


def hook_name(kind, it, x):
    return '%s_%s_%s' % (kind, it.name, x.ident)


def proxy_hook_sig(it, x, fn):
    self_t = ('const ' if x.const else '') + 'Proxy_%s& self' % it.name
    ps = x.sig()
    return '%s %s(%s%s)' % (x.ret, fn, self_t, (', ' + ps) if ps else '')


def disp_sig(it, fn):
    return 'bool %s(%s* self, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)' % (fn, it.name)


def default_ret(side, ret):
    """Expression of a loud default return value; abstract references get a zeroed static dummy."""
    b = ret[:-1].strip() if ret.endswith('&') else None
    if b is not None and mt.strip_top_const(b) in ABSTRACT_RET:
        return 'VpGenDummyRef< %s >::Get()' % mt.strip_top_const(b)
    return '%s< %s >::Get()' % ('ProxyDefault' if side == 'proxy' else 'SrvDefault', ret)


def caller_body(side, x, hexpr, store=None, ind='    '):
    """Generated caller (proxy for engine -> DLL, server RemoteGen for DLL -> engine). Uses the method's own
    parameter names."""
    rt = 'ProxyRt' if side == 'proxy' else 'SrvRt'
    L = []
    if x.ret != 'void':
        L.append('typedef %s VpR;' % x.ret)
    L += ['vpipc::Channel& vp_ch = %s::Chan();' % rt, 'vpipc::Writer& vp_w = %s::Begin(vp_ch, %s);' % (rt, x.kM),
          'vp_w.handle(%s);' % hexpr]
    for c in x.params:
        L += c.caller_enc(side)
    L += ['vpipc::Reader vp_r;', 'if (!%s::End(vp_ch, %s, vp_r)) return %s;' % (rt, x.kM, default_ret(side, x.ret))]
    L += x.retc.caller_dec(side, store)
    for c in x.params:
        L += c.caller_out(side)
    L.append('%s::DecodeCheck(%s, vp_r);' % (rt, x.kM))
    if x.ret != 'void':
        L.append('return vp_ret;')
    return [ind + s for s in L]


def callee_body(side, it, x, ind='    '):
    """Generated callee after `self` is resolved (server Dispatch for engine -> DLL, proxy EngDispatch for
    DLL -> engine)."""
    rt = 'SrvRt' if side == 'server' else 'ProxyRt'
    L = []
    for c in x.params:
        L += c.callee_dec(side)
    L.append('if (!args.ok()) return %s::DispatchFail(ch, ret, vpw::kStatusBadArguments, %s);' % (
        rt, cstr_lit(x.key + ': bad arguments')))
    checks = [c.callee_check() for c in x.params if c.callee_check()]
    if checks:
        L.append('if (%s) return %s::DispatchFail(ch, ret, vpw::kStatusBadHandle, %s);' % (
            ' || '.join(checks), rt, cstr_lit(x.key + ': bad handle argument')))
    call = 'static_cast<%s*>(self)->%s(%s)' % (x.decl, x.name, ', '.join(c.callee_arg() for c in x.params))
    if x.ret == 'void':
        L.append(call + ';')
    else:
        L.append('%s r_ = %s;' % (x.ret, call))
        L += x.retc.callee_enc(side)
    for c in x.params:
        L += c.callee_out(side)
    L.append('return true;')
    return [ind + s for s in L]


def chain_guid_checks(chain, var, ind):
    return ['%sif (memcmp(&%s, &guid%s, sizeof(GUID)) == 0) return static_cast<%s*>(this);' % (ind, var, c, c)
            for c in chain]


def proxy_chain_guid_checks(chain, var, ind, me):
    # A local answer is one more reference to the same wrapper: the engine may Destroy both pointers (the DLL's
    # singletons, e.g. the net message handler, have a no-op Destroy). Integration 2026-11-10.
    # Issue 2700 item 4 (issue 2503 F6, adopted by the MC): a temporary wrapper (a DLL object passed to an engine
    # call) that the engine QueryInterfaces is kept: KeepIfTemp promotes it (a forwarded QueryInterface gives the
    # server a persistent registration and a DLL reference). Issue 2708: the engine's reference is counted like any
    # other (WrapperAddRef); the dispatch scope keeps its own until the call returns, because the EXE goes on using the
    # borrowed argument after Destroying what it QueryInterfaced (GameplayUnitCreated 0x557CF5 / 0x557D1E / 0x557D20).
    return ['%sif (memcmp(&%s, &guid%s, sizeof(GUID)) == 0) { ProxyRt::KeepIfTemp(static_cast<%s*>(this)); '
            'ProxyRt::WrapperAddRef(static_cast<%s*>(this)); return static_cast<%s*>(this); }' % (ind, var, c, me, me, c)
            for c in chain]


def split(items, n, weight):
    """Greedy split into n buckets by weight, keeping the model order inside a bucket."""
    buckets = [[] for _ in range(n)]
    loads = [0] * n
    for it in sorted(items, key=lambda i: -weight(i)):
        k = loads.index(min(loads))
        buckets[k].append(it)
        loads[k] += weight(it)
    return [b for b in buckets if b]


# ================================================================================================= common files
def emit_ids_h(m):
    L = [HEADER, '// Shared by the x86 proxy and the x64 server (C++03): interface kinds, method ids, schema hash.',
         '// Design: research/03-dll-engine-boundary/proxy-design.md section 11; contract 2300 section A.',
         '#pragma once', '#include <stdint.h>', '',
         '// FNV-1a 64 over every interface (sorted by name): name, type id, per method key, x86 slot, x64 slot, id.',
         'static const uint64_t kAppSchemaHash = 0x%016xULL;' % m.schema,
         'static const uint32_t kVpKindCount = %du;' % len(m.order), '',
         '// Interface kinds: 1-based position in proxy/model/interfaces.json. [MD] = most derived (own classes).']
    for n in m.order:
        L.append('static const uint32_t kT_%s = %du;  // %s%s' % (n, m.kt[n], m.dir[n],
                                                                  '' if m.md[n] != n else ' [MD]') if m.md[n] == n else
                 'static const uint32_t kT_%s = %du;  // %s, MD %s' % (n, m.kt[n], m.dir[n], m.md[n]))
    L.append('')
    for n in m.order:
        L.append('// %s (kT %d): (kT << 16) | index in the full method list' % (n, m.kt[n]))
        for x in m.iface[n].methods:
            L.append('static const uint32_t %s = 0x%08xu;  // %s' % (x['const_name'], x['id'], x['m']['key']))
    L += ['',
          'uint32_t vpk_most_derived(uint32_t kind);                  // 0 for an unknown kind',
          'bool vpk_in_chain(uint32_t haveKind, uint32_t wantKind);   // wantKind in the chain of haveKind (itself incl.)',
          'bool vpk_is_engine_iface(uint32_t kind);                   // direction dll_to_engine',
          'const char* vpk_iface_name(uint32_t kind);                 // NULL for an unknown kind',
          'const char* vpk_method_key(uint32_t method);               // the model key; NULL if unknown',
          'uint32_t vpk_method_count(uint32_t kind);                  // size of the method index space of kind']
    return '\n'.join(L) + '\n'


def emit_ids_cpp(m):
    L = [HEADER, '// Tables behind the vpk_* functions (C++03, no runtime dependency).', '#include "vp_ids.gen.h"',
         '#include <string.h>', '', 'namespace', '{']
    for n in m.order:
        L.append('const uint32_t kChain_%s[] = {%s};' % (n, ', '.join('%du' % m.kt[c] for c in m.chain(n))))
    for n in m.order:
        keys = []
        ms = {x['index']: x for x in m.iface[n].methods}
        for i in range(len(m.by[n]['methods'])):
            keys.append(cstr_lit(ms[i]['m']['key']) if i in ms else 'NULL')
        L.append('const char* const kKeys_%s[] = {' % n)
        L += ['    %s,' % k for k in keys]
        L.append('};')
    L += ['struct VpKindInfo', '{', '    const char* name;', '    uint32_t md;', '    bool engine;',
          '    uint32_t nchain;', '    const uint32_t* chain;', '    uint32_t nmethods;', '    const char* const* keys;',
          '};', 'const VpKindInfo kKinds[%d] = {' % len(m.order)]
    for n in m.order:
        L.append('    {"%s", %du, %s, %du, kChain_%s, %du, kKeys_%s},' % (
            n, m.kt[m.md[n]], 'true' if m.dir[n] == 'dll_to_engine' else 'false', len(m.chain(n)), n,
            len(m.by[n]['methods']), n))
    L += ['};', '', 'const VpKindInfo* Info(uint32_t kind)', '{',
          '    return (kind >= 1u && kind <= kVpKindCount) ? &kKinds[kind - 1u] : 0;', '}', '}  // namespace', '',
          'uint32_t vpk_most_derived(uint32_t kind)', '{', '    const VpKindInfo* k = Info(kind);',
          '    return k ? k->md : 0u;', '}', '',
          'bool vpk_in_chain(uint32_t haveKind, uint32_t wantKind)', '{', '    const VpKindInfo* k = Info(haveKind);',
          '    if (!k) return false;', '    for (uint32_t i = 0; i < k->nchain; ++i)',
          '        if (k->chain[i] == wantKind) return true;', '    return false;', '}', '',
          'bool vpk_is_engine_iface(uint32_t kind)', '{', '    const VpKindInfo* k = Info(kind);',
          '    return k ? k->engine : false;', '}', '',
          'const char* vpk_iface_name(uint32_t kind)', '{', '    const VpKindInfo* k = Info(kind);',
          '    return k ? k->name : 0;', '}', '',
          'const char* vpk_method_key(uint32_t method)', '{', '    const VpKindInfo* k = Info(method >> 16);',
          '    const uint32_t i = method & 0xFFFFu;', '    return (k && i < k->nmethods) ? k->keys[i] : 0;', '}', '',
          'uint32_t vpk_method_count(uint32_t kind)', '{', '    const VpKindInfo* k = Info(kind);',
          '    return k ? k->nmethods : 0u;', '}']
    return '\n'.join(L) + '\n'


def emit_guids(m, side):
    hdr = 'proxy_ifaces.gen.h' if side == 'proxy' else 'server_hooks.gen.h'
    L = [HEADER, '// GUID -> most derived kind (%s side).' % side, '#include "vp_gen_common.h"', '#include "vp_ids.gen.h"',
         '#include "%s"' % hdr, '#include <string.h>', '', 'uint32_t vpk_kind_of_guid(const GUID& g)', '{']
    for n in m.order:
        if n in m.guids:
            L.append('    if (memcmp(&g, &guid%s, sizeof(GUID)) == 0) return kT_%s;' % (n, m.md[n]))
    L += ['    return 0u;', '}']
    return '\n'.join(L) + '\n'


# ================================================================================================== proxy (x86)
def emit_proxy_h(m, ifs, hooks, plan):
    L = [HEADER, '// x86 proxy side (clang-cl -m32, VC9 headers, C++11 at most): wrappers of the DLL objects the engine',
         '// holds (engine -> DLL), hooks, forwarders, and the dispatchers of the engine objects (DLL -> engine).',
         '#pragma once', '#include "vp_gen_common.h"', '#include "vp_ids.gen.h"', '#include <string.h>',
         '#include <vector>'] + iface_includes(m) + ['',
         'uint32_t vpk_kind_of_guid(const GUID& g);  // vp_guids.gen.cpp: MD kind of the interface with that GUID',
         'void* VpProxyNewWrapper(vpipc::Handle h);   // proxy_factory.gen.cpp: new Proxy_<MD> as MD*, NULL if none',
         'bool EngDispatchGenerated(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch);',
         '// A DLL object the engine returned to the server with a reference (proxy_engdispatch.gen.cpp): the server',
         '// takes its own DLL reference (QueryInterface forwarded on the handle), then the engine\'s reference is',
         '// released. Returns the handle for the RETURN (null for NULL or a pointer that is not a wrapper). Makes a',
         '// nested call: use it before anything is written into the RETURN. Issue 2503.',
         'vpipc::Handle VpGenGiveDll(ICvUnknown* p);',
         '// Human-game runtime methods (vp_wire.h kRtTypeServerToProxyHuman, issue 2503): hand-written in',
         '// proxy/src/proxy_hooks_human.cpp.',
         'bool VpHumanServe(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch);', '',
         '// Loud default of a method returning a reference to an abstract type: a zeroed static object.',
         'template<class T> struct VpGenDummyRef', '{', '    static T& Get()', '    {',
         '        static double s_[(sizeof(T) + sizeof(double) - 1) / sizeof(double)];',
         '        return *reinterpret_cast<T*>(s_);', '    }', '};', '']
    for n in m.e2d:
        it = ifs[n]
        L += [SEP, '// %s (kT %d), chain %s' % (n, m.kt[n], ' > '.join(it.chain)),
              'class Proxy_%s : public %s' % (n, n), '{', 'public:',
              '    explicit Proxy_%s(vpipc::Handle h) : m_h(h) { ProxyRt::AddWrapper(static_cast<%s*>(this), h); }' % (
                  n, n),
              '    vpipc::Handle ProxyHandle() const { return m_h; }']
        for x in it.meths:
            L.append('    ' + x.decl_line())
        mem = [x.retc.member for x in it.meths if x.retc is not None and x.retc.member]
        if mem:
            L.append('    // values returned by const reference (stable until the next call of the method)')
            L += ['    %s %s;' % mm for mm in mem]
        L += ['', 'private:', '    Proxy_%s(const Proxy_%s&);' % (n, n), '    Proxy_%s& operator=(const Proxy_%s&);' % (n, n),
              '    vpipc::Handle m_h;', '};']
        for x in it.meths:
            if x.special:
                continue
            L.append('%s;' % proxy_hook_sig(it, x, hook_name('ProxyHook', it, x)))
            if x.mechanical and hook_name('ProxyHook', it, x) in hooks:
                L.append('%s;' % proxy_hook_sig(it, x, hook_name('ProxyFwd', it, x)))
        L.append('')
    for n in m.d2e:
        it = ifs[n]
        L += [SEP, '// %s (kT %d): engine object, served for the server by EngDispatch_%s' % (n, m.kt[n], n),
              'bool EngDispatch_%s(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch);' % n]
        for x in it.meths:
            if x.special:
                continue
            L.append('%s;' % disp_sig(it, hook_name('EngHook', it, x)))
            if x.mechanical and hook_name('EngHook', it, x) in hooks:
                L.append('%s;' % disp_sig(it, hook_name('EngFwd', it, x)))
        L.append('')
    return '\n'.join(L) + '\n'


def proxy_method(m, it, x, hooks):
    L = ['// %s  x86 slot %d, x64 slot %d' % (x.key, x.m['slot']['x86'], x.m['slot']['x64']), x.head('Proxy_' + it.name),
         '{']
    hk = hook_name('ProxyHook', it, x)
    if x.special == 'QueryInterface':
        g = x.names[0]
        L += proxy_chain_guid_checks(it.chain, g, '    ', it.name)
        L += ['    // Any other GUID: the DLL object may answer it (forwarded; the server registers the result).',
              '    vpipc::Channel& vp_ch = ProxyRt::Chan();', '    vpipc::Writer& vp_w = ProxyRt::Begin(vp_ch, %s);' % x.kM,
              '    vp_w.handle(m_h);', '    vp_w.bytes(&%s, 16u);' % g, '    vpipc::Reader vp_r;',
              '    if (!ProxyRt::End(vp_ch, %s, vp_r)) return NULL;' % x.kM,
              '    const vpipc::Handle h_ = vp_r.handle();', '    ProxyRt::DecodeCheck(%s, vp_r);' % x.kM,
              '    // Wrap returns the wrapper as the MD interface of the handle\'s kind; every chain is single',
              '    // inheritance from ICvUnknown at offset 0, so the address is the requested interface\'s too.',
              '    return ProxyRt::Wrap(h_);']
    elif x.special == 'Destroy':
        L += ['    if (ProxyRt::IsPinned(static_cast<%s*>(this))) return;  // the context singleton (issue 2500)' % it.name,
              '    if (!ProxyRt::WrapperRelease(static_cast<%s*>(this))) return;  // other references remain' % it.name,
              '    // issue 2700: a local wrapper over a canonical object: no crossing (its lock, if any, released); the',
              '    // wrapper goes back to its thread\'s pool, it is never deleted',
              '    if (ProxyRt::LocalRelease(static_cast<%s*>(this), %s)) return;' % (it.name, x.kM),
              '    vpipc::Channel& vp_ch = ProxyRt::Chan();', '    vpipc::Writer& vp_w = ProxyRt::Begin(vp_ch, %s);' % x.kM,
              '    vp_w.handle(m_h);', '    vpipc::Reader vp_r;',
              '    if (ProxyRt::End(vp_ch, %s, vp_r)) ProxyRt::DecodeCheck(%s, vp_r);' % (x.kM, x.kM),
              '    ProxyRt::ForgetWrapper(static_cast<%s*>(this));' % it.name, '    delete this;']
    elif hk in hooks or not x.mechanical:
        if not x.mechanical:
            L.append('    // Not mechanical: %s' % '; '.join(x.reasons))
        args = ', '.join(['*this'] + x.names)
        L.append('    %s%s(%s);' % ('' if x.ret == 'void' else 'return ', hk, args))
    else:
        store = x.retc.member[1] if x.retc.member else None
        L += caller_body('proxy', x, 'm_h', store)
    L += ['}', '']
    if not x.special and x.mechanical and hk in hooks:
        L += ['// Forwarder: the generated body of %s, for the hand-written hook (%s).' % (x.key, hooks[hk]),
              proxy_hook_sig(it, x, hook_name('ProxyFwd', it, x)), '{']
        store = ('self.' + x.retc.member[1]) if x.retc.member else None
        L += caller_body('proxy', x, 'self.ProxyHandle()', store)
        L += ['}', '']
    return L


def emit_proxy_cpp(m, ifs, names, hooks, k):
    L = [HEADER, '// x86 proxy: wrapper bodies (part %d).' % k, '#include "vp_gen_common.h"', '#include "vp_ids.gen.h"',
         '#include "proxy_ifaces.gen.h"', '']
    for n in names:
        L.append(SEP)
        for x in ifs[n].meths:
            L += proxy_method(m, ifs[n], x, hooks)
    return '\n'.join(L) + '\n'


def emit_proxy_factory(m):
    L = [HEADER, '// x86 proxy: a new wrapper for a DLL object handle the server returned.', '#include "vp_gen_common.h"',
         '#include "vp_ids.gen.h"', '#include "proxy_ifaces.gen.h"', '', 'void* VpProxyNewWrapper(vpipc::Handle h)', '{',
         '    switch (vpw::Kind(h.type))', '    {']
    for n in m.e2d:
        L.append('    case kT_%s: return static_cast<void*>(static_cast<%s*>(new Proxy_%s(h)));' % (n, n, n))
    L += ['    default: return NULL;', '    }', '}']
    return '\n'.join(L) + '\n'


def eng_dispatch(m, it, hooks):
    n = it.name
    L = [SEP, 'bool EngDispatch_%s(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)' % n,
         '{', '    switch (method)', '    {']
    fwd = []
    for x in it.meths:
        L.append('    case %s:  // %s' % (x.kM, x.key))
        L.append('    {')
        if x.special == 'QueryInterface':
            L += ['        // RemoteGen_%s answers its chain locally and never sends QueryInterface.' % n,
                  '        return ProxyRt::DispatchFail(ch, ret, vpw::kStatusUnknownMethod, %s);' % cstr_lit(
                      x.key + ': never sent'),
                  '    }']
            continue
        L += ['        const vpipc::Handle vp_h = args.handle();',
              '        %s* self = static_cast<%s*>(ProxyRt::EngineResolve(vp_h, kT_%s));' % (n, n, n),
              '        if (!self) return ProxyRt::DispatchFail(ch, ret, vpw::kStatusBadHandle, %s);' % cstr_lit(
                  x.key + ': bad handle')]
        hk = hook_name('EngHook', it, x)
        if x.special == 'Destroy':
            L += ['        static_cast<ICvUnknown*>(self)->Destroy();', '        ProxyRt::EngineForget(vp_h);',
                  '        return true;']
        elif x.special == 'DestroyArgs':
            body = callee_body('proxy', it, x, '        ')
            body.insert(len(body) - 1, '        ProxyRt::EngineForget(%s_h);' % x.params[0].a)
            L += body
        elif hk in hooks or not x.mechanical:
            L.append('        return %s(self, args, ret, ch);' % hk)
            if x.mechanical:
                fwd.append(x)
        else:
            L += callee_body('proxy', it, x, '        ')
        L.append('    }')
    L += ['    default:', '        return false;', '    }', '}', '']
    for x in fwd:
        L += ['// Forwarder for the hand-written %s (%s).' % (hook_name('EngHook', it, x), hooks[hook_name('EngHook', it, x)]),
              disp_sig(it, hook_name('EngFwd', it, x)), '{']
        L += callee_body('proxy', it, x)
        L += ['}', '']
    return L


def qi_missing(n):
    raise SystemExit('%s: QueryInterface is not the first method of its chain' % n)


def emit_proxy_engdispatch(m, ifs, hooks):
    L = [HEADER, '// x86 proxy: serves the server\'s calls on engine objects (DLL -> engine interfaces).',
         '#include "vp_gen_common.h"', '#include "vp_ids.gen.h"', '#include "proxy_ifaces.gen.h"', '']
    L += [SEP, '// QueryInterface method id of a DLL object kind (its most derived interface).',
          'static uint32_t VpGenQiMethod(uint32_t kind)', '{', '    switch (vpk_most_derived(kind))', '    {']
    for n in m.e2d:
        L.append('    case kT_%s: return %s;' % (n, ifs[n].meths[0].kM if ifs[n].meths[0].special == 'QueryInterface'
                                                   else qi_missing(n)))
    L += ['    default: return 0u;', '    }', '}', '',
          'vpipc::Handle VpGenGiveDll(ICvUnknown* p)', '{', '    vpipc::Handle h = ProxyRt::HandleOf(p);',
          '    if (!p || (h.type == 0u && h.id == 0)) return h;  // NULL, or not a wrapper (logged by HandleOf)',
          '    const uint32_t qi = VpGenQiMethod(vpw::Kind(h.type));', '    vpipc::Handle h2 = {0u, 0};', '    if (qi)',
          '    {', '        vpipc::Channel& vp_ch = ProxyRt::Chan();', '        vpipc::Writer& vp_w = ProxyRt::Begin(vp_ch, qi);',
          '        vp_w.handle(h);', '        vp_w.bytes(&guidICvUnknown, 16u);', '        vpipc::Reader vp_r;',
          '        if (ProxyRt::End(vp_ch, qi, vp_r))', '        {', '            h2 = vp_r.handle();',
          '            ProxyRt::DecodeCheck(qi, vp_r);', '        }', '    }',
          '    p->Destroy();  // the engine\'s reference (forwards Destroy only when it was the wrapper\'s last)',
          '    return h2;', '}', '']
    for n in m.d2e:
        L += eng_dispatch(m, ifs[n], hooks)
    L += [SEP, 'bool EngDispatchGenerated(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)',
          '{', '    switch (method >> 16)', '    {']
    for n in m.d2e:
        L.append('    case kT_%s: return EngDispatch_%s(method, args, ret, ch);' % (n, n))
    L += ['    case vpw::kRtTypeServerToProxyHuman: return VpHumanServe(method, args, ret, ch);  // issue 2503',
          '    default: return false;', '    }', '}']
    return '\n'.join(L) + '\n'


def emit_proxy_defaults(m, ifs, hooks):
    L = [HEADER, '// x86 proxy: loud defaults of the hooks nobody wrote (counted, logged once; strict=1 fails).',
         '#include "vp_gen_common.h"', '#include "vp_ids.gen.h"', '#include "proxy_ifaces.gen.h"', '']
    for n in m.e2d:
        it = ifs[n]
        for x in it.meths:
            hk = hook_name('ProxyHook', it, x)
            if x.special or x.mechanical or hk in hooks:
                continue
            L += ['// %s: %s' % (x.key, '; '.join(x.reasons)), proxy_hook_sig(it, x, hk), '{', '    (void)self;',
                  '    ProxyRt::Unimplemented(%s, %s);' % (x.kM, lit_key(x)),
                  '    return %s;' % default_ret('proxy', x.ret), '}', '']
    for n in m.d2e:
        it = ifs[n]
        for x in it.meths:
            hk = hook_name('EngHook', it, x)
            if x.special or x.mechanical or hk in hooks:
                continue
            L += ['// %s: %s' % (x.key, '; '.join(x.reasons)), disp_sig(it, hk), '{', '    (void)self;', '    (void)args;',
                  '    ProxyRt::Unimplemented(%s, %s);' % (x.kM, lit_key(x)),
                  '    return ProxyRt::DispatchFail(ch, ret, vpw::kStatusUnimplemented, %s);' % cstr_lit(
                      x.key + ': not implemented'), '}', '']
    return '\n'.join(L) + '\n'


# ================================================================================================= server (x64)
def emit_server_h(m, ifs, hooks):
    L = [HEADER, '// x64 server side (clang-cl -m64, MSVC 14 STL, C++14): hooks and forwarders of the engine -> DLL',
         '// dispatchers, and RemoteGen_<I>, the remote stand-ins of the engine objects (DLL -> engine).',
         '#pragma once', '#include "vp_gen_common.h"', '#include "vp_ids.gen.h"', '#include <string.h>', '#include <vector>'] + \
        iface_includes(m) + ['',
         'uint32_t vpk_kind_of_guid(const GUID& g);  // vp_guids.gen.cpp: MD kind of the interface with that GUID',
         'bool DispatchGenerated(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch);',
         '// Strings returned by pointer from an engine call outlive it (server_remote_1.gen.cpp; never freed).',
         'const char* VpGenInternS(const char* s);', 'const wchar_t* VpGenInternW(const wchar_t* s);',
         '// A DLL object an engine call returned (the proxy took a server reference for it, VpGenGiveDll): the object,',
         '// whose DLL reference now belongs to the caller; the registration made for it is dropped. NULL for a null or',
         '// unknown handle. server_remote_1.gen.cpp; issue 2503.',
         'void* VpGenAdoptDll(vpipc::Handle h, uint32_t kind);', '']
    for n in m.e2d:
        it = ifs[n]
        L += [SEP, '// %s (kT %d)' % (n, m.kt[n]),
              'bool Dispatch_%s(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch);' % n]
        for x in it.meths:
            if x.special:
                continue
            L.append('%s;' % disp_sig(it, hook_name('ServerHook', it, x)))
            if x.mechanical and hook_name('ServerHook', it, x) in hooks:
                L.append('%s;' % disp_sig(it, hook_name('ServerFwd', it, x)))
        L.append('')
    for n in m.d2e:
        it = ifs[n]
        L += [SEP, '// %s (kT %d), chain %s. Mechanical methods forward; the others log SrvRt::Unserved and return a' % (
            n, m.kt[n], ' > '.join(it.chain)),
              '// default: a hand-written subclass overrides them.',
              'class RemoteGen_%s : public %s' % (n, n), '{', 'public:',
              '    explicit RemoteGen_%s(vpipc::Handle h) : m_h(h) {}' % n,
              '    vpipc::Handle RemoteHandle() const { return m_h; }']
        for x in it.meths:
            L.append('    ' + x.decl_line())
        L += ['', 'protected:', '    vpipc::Handle m_h;', '};', '']
    return '\n'.join(L) + '\n'


def server_dispatch(m, it, hooks):
    n = it.name
    L = [SEP, 'bool Dispatch_%s(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)' % n, '{',
         '    switch (method)', '    {']
    fwd = []
    for x in it.meths:
        L += ['    case %s:  // %s, x64 slot %d' % (x.kM, x.key, x.m['slot']['x64']), '    {',
              '        const vpipc::Handle vp_h = args.handle();',
              '        %s* self = static_cast<%s*>(SrvRt::Resolve(vp_h, kT_%s));' % (n, n, n),
              '        if (!self) return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadHandle, %s);' % cstr_lit(
                  x.key + ': bad handle')]
        hk = hook_name('ServerHook', it, x)
        if x.special == 'QueryInterface':
            L += ['        GUID g_;', '        memset(&g_, 0, sizeof(GUID));',
                  '        { uint32_t n_ = 0; const uint8_t* p_ = args.bytes(&n_); if (p_ && n_ == 16u) memcpy(&g_, p_, 16u); }',
                  '        if (!args.ok()) return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments, %s);' % cstr_lit(
                      x.key + ': bad arguments'),
                  '        void* p_ = static_cast<ICvUnknown*>(self)->QueryInterface(g_);',
                  '        const uint32_t k_ = vpk_kind_of_guid(g_);',
                  '        const vpipc::Handle null_ = {0u, 0};',
                  '        ret.handle((p_ && k_) ? SrvRt::Register(p_, k_) : null_);', '        return true;']
        elif x.special == 'Destroy':
            L += ['        static_cast<ICvUnknown*>(self)->Destroy();', '        SrvRt::Unregister(vp_h);',
                  '        return true;']
        elif x.special == 'Update' and hk not in hooks:
            L += ['        SrvRt::EnterUpdate();', '        static_cast<%s*>(self)->Update();' % x.decl,
                  '        SrvRt::LeaveUpdate();', '        return true;']
        elif hk in hooks or not x.mechanical:
            L.append('        return %s(self, args, ret, ch);' % hk)
            if x.mechanical:
                fwd.append(x)
        else:
            L += callee_body('server', it, x, '        ')
        L.append('    }')
    L += ['    default:', '        return false;', '    }', '}', '']
    for x in fwd:
        L += ['// Forwarder for the hand-written %s (%s).' % (hook_name('ServerHook', it, x),
                                                              hooks[hook_name('ServerHook', it, x)]),
              disp_sig(it, hook_name('ServerFwd', it, x)), '{']
        L += callee_body('server', it, x)
        L += ['}', '']
    return L


def emit_server_dispatch(m, ifs, names, hooks, k, last):
    L = [HEADER, '// x64 server: dispatchers of the engine -> DLL calls (part %d).' % k, '#include "vp_gen_common.h"',
         '#include "vp_ids.gen.h"', '#include "server_hooks.gen.h"', '']
    for n in names:
        L += server_dispatch(m, ifs[n], hooks)
    if last:
        L += [SEP, 'bool DispatchGenerated(uint32_t method, vpipc::Reader& args, vpipc::Writer& ret, vpipc::Channel& ch)',
              '{', '    switch (method >> 16)', '    {']
        for n in m.e2d:
            L.append('    case kT_%s: return Dispatch_%s(method, args, ret, ch);' % (n, n))
        L += ['    default: return false;', '    }', '}']
    return '\n'.join(L) + '\n'


def remote_method(m, it, x):
    cls = 'RemoteGen_' + it.name
    L = ['// %s  x64 slot %d' % (x.key, x.m['slot']['x64']), x.head(cls), '{']
    if x.special == 'QueryInterface':
        g = x.names[0]
        L += chain_guid_checks(it.chain, g, '    ')
        L += ['    SrvRt::Unserved(%s, %s);' % (x.kM, lit_key(x)), '    return NULL;']
    elif x.special == 'Destroy':
        L += ['    vpipc::Channel& vp_ch = SrvRt::Chan();', '    vpipc::Writer& vp_w = SrvRt::Begin(vp_ch, %s);' % x.kM,
              '    vp_w.handle(m_h);', '    vpipc::Reader vp_r;',
              '    if (SrvRt::End(vp_ch, %s, vp_r)) SrvRt::DecodeCheck(%s, vp_r);' % (x.kM, x.kM),
              '    SrvRt::EngineForget(m_h);  // whatever the status; the object itself is not deleted']
    elif x.special == 'DestroyArgs':
        a = x.names[0]
        L += ['    const vpipc::Handle vp_ah = SrvRt::EngineHandleOf(%s);' % a,
              '    vpipc::Channel& vp_ch = SrvRt::Chan();', '    vpipc::Writer& vp_w = SrvRt::Begin(vp_ch, %s);' % x.kM,
              '    vp_w.handle(m_h);', '    vp_w.handle(vp_ah);', '    vpipc::Reader vp_r;',
              '    if (SrvRt::End(vp_ch, %s, vp_r)) SrvRt::DecodeCheck(%s, vp_r);' % (x.kM, x.kM),
              '    SrvRt::EngineForget(vp_ah);']
    elif not x.mechanical:
        L += ['    // Not mechanical: %s' % '; '.join(x.reasons), '    SrvRt::Unserved(%s, %s);' % (x.kM, lit_key(x)),
              '    return %s;' % default_ret('server', x.ret)]
    else:
        L += caller_body('server', x, 'm_h')
    L += ['}', '']
    return L


def emit_server_remote(m, ifs, names, k):
    L = [HEADER, '// x64 server: RemoteGen_<I> bodies (part %d).' % k, '#include "vp_gen_common.h"',
         '#include "vp_ids.gen.h"', '#include "server_hooks.gen.h"', '']
    if k == 1:
        L += ['#include <mutex>', '#include <set>', '#include <string>', '',
              'const char* VpGenInternS(const char* s)', '{', '    if (!s) return NULL;', '    static std::mutex s_lock;',
              '    static std::set<std::string> s_strings;', '    std::lock_guard<std::mutex> g(s_lock);',
              '    return s_strings.insert(std::string(s)).first->c_str();', '}', '',
              'const wchar_t* VpGenInternW(const wchar_t* s)', '{', '    if (!s) return NULL;',
              '    static std::mutex s_lock;', '    static std::set<std::wstring> s_strings;',
              '    std::lock_guard<std::mutex> g(s_lock);', '    return s_strings.insert(std::wstring(s)).first->c_str();',
              '}', '',
              'void* VpGenAdoptDll(vpipc::Handle h, uint32_t kind)', '{', '    void* p = SrvRt::Resolve(h, kind);',
              '    if (p) SrvRt::Unregister(h);  // the reference VpGenGiveDll took is now the caller\'s', '    return p;',
              '}', '']
    for n in names:
        L.append(SEP)
        L.append('static_assert(!std::is_abstract<RemoteGen_%s>::value, "RemoteGen_%s must override every pure virtual");'
                 % (n, n))
        for x in ifs[n].meths:
            L += remote_method(m, ifs[n], x)
    return '\n'.join(L) + '\n'


def emit_server_defaults(m, ifs, hooks):
    L = [HEADER, '// x64 server: loud defaults of the dispatch hooks nobody wrote (SrvRt::Unserved: counted, listed).',
         '#include "vp_gen_common.h"', '#include "vp_ids.gen.h"', '#include "server_hooks.gen.h"', '']
    for n in m.e2d:
        it = ifs[n]
        for x in it.meths:
            hk = hook_name('ServerHook', it, x)
            if x.special or x.mechanical or hk in hooks:
                continue
            L += ['// %s: %s' % (x.key, '; '.join(x.reasons)), disp_sig(it, hk), '{', '    (void)self;', '    (void)args;',
                  '    SrvRt::Unserved(%s, %s);' % (x.kM, lit_key(x)),
                  '    return SrvRt::DispatchFail(ch, ret, vpw::kStatusUnimplemented, %s);' % cstr_lit(
                      x.key + ': not implemented'), '}', '']
    return '\n'.join(L) + '\n'


# ======================================================================================================== main
def find_guids(m, src):
    """guid<I> constants defined in the fork's interface headers."""
    text = ''
    for d in (os.path.join(src, 'CvGameCoreDLLUtil', 'include'),):
        for root, _, files in os.walk(d):
            for fn in files:
                if fn.endswith('.h'):
                    with open(os.path.join(root, fn), encoding='latin-1') as f:
                        text += f.read()
    return {n for n in m.order if re.search(r'\bGUID\s+guid%s\s*=' % re.escape(n), text)}


def write(path, text):
    bad = sorted({c for c in text if ord(c) < 32 and c != '\n'})
    if bad:
        raise RuntimeError('control characters in generated %s: %r' % (path, bad))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    return text.count('\n')


def generate(out, hook_dirs, src):
    m = Model()
    m.guids = find_guids(m, src)
    ifs = {n: Iface(m, n) for n in m.mds}
    hooks = scan_hooks(hook_dirs)
    known = set()
    for n in m.mds:
        it = ifs[n]
        for x in it.meths:
            for kind in (('ProxyHook', 'ServerHook') if it.dir == 'engine_to_dll' else ('EngHook',)):
                known.add(hook_name(kind, it, x))
    stale = sorted(h for h in hooks if h not in known)
    special_hooked = sorted(hook_name(k, ifs[n], x) for n in m.mds for x in ifs[n].meths if x.special
                            for k in ('ProxyHook', 'ServerHook', 'EngHook') if hook_name(k, ifs[n], x) in hooks
                            and not (x.special == 'Update' and k == 'ServerHook'))
    files = {}
    files['common/vp_ids.gen.h'] = emit_ids_h(m)
    files['common/vp_ids.gen.cpp'] = emit_ids_cpp(m)
    files['proxy/proxy_ifaces.gen.h'] = emit_proxy_h(m, ifs, hooks, None)
    w = lambda n: len(ifs[n].meths) + 5  # noqa: E731
    for k, names in enumerate(split(m.e2d, FILES_PROXY, w), 1):
        files['proxy/proxy_ifaces_%d.gen.cpp' % k] = emit_proxy_cpp(m, ifs, sorted(names, key=m.order.index), hooks, k)
    files['proxy/proxy_factory.gen.cpp'] = emit_proxy_factory(m)
    files['proxy/proxy_engdispatch.gen.cpp'] = emit_proxy_engdispatch(m, ifs, hooks)
    files['proxy/proxy_hook_defaults.gen.cpp'] = emit_proxy_defaults(m, ifs, hooks)
    files['proxy/vp_guids.gen.cpp'] = emit_guids(m, 'proxy')
    files['server/server_hooks.gen.h'] = emit_server_h(m, ifs, hooks)
    parts = split(m.e2d, FILES_DISPATCH, w)
    for k, names in enumerate(parts, 1):
        files['server/server_dispatch_%d.gen.cpp' % k] = emit_server_dispatch(
            m, ifs, sorted(names, key=m.order.index), hooks, k, k == len(parts))
    for k, names in enumerate(split(m.d2e, FILES_REMOTE, w), 1):
        files['server/server_remote_%d.gen.cpp' % k] = emit_server_remote(m, ifs, sorted(names, key=m.order.index), k)
    files['server/server_hook_defaults.gen.cpp'] = emit_server_defaults(m, ifs, hooks)
    files['server/vp_guids.gen.cpp'] = emit_guids(m, 'server')
    lines = {}
    for rel, text in files.items():
        lines[rel] = write(os.path.join(out, *rel.split('/')), text)
    return m, ifs, hooks, stale, special_hooked, lines


def summary(m, ifs, hooks, stale, special_hooked, lines):
    def side(kinds):
        st = {'interfaces': 0, 'methods': 0, 'mechanical': 0, 'hooked': 0, 'loud': 0}
        hooked, loud = [], []
        for n, hk_kind, has_hooks in kinds:
            it = ifs[n]
            st['interfaces'] += 1
            for x in it.meths:
                st['methods'] += 1
                if x.mechanical or x.special:
                    st['mechanical'] += 1
                hk = hook_name(hk_kind, it, x)
                if not x.special and has_hooks and hk in hooks:
                    st['hooked'] += 1
                    hooked.append(hk)
                elif not x.special and not x.mechanical:
                    st['loud'] += 1
                    loud.append('%s  [%s]' % (x.key, '; '.join(x.reasons)))
        return st, hooked, loud
    print('gen_runtime: %d interfaces in the model, %d MD interfaces (engine -> DLL %d, DLL -> engine %d), '
          'kAppSchemaHash 0x%016x' % (len(m.order), len(m.mds), len(m.e2d), len(m.d2e), m.schema))
    print('MD interfaces, engine -> DLL: ' + ', '.join(m.e2d))
    print('MD interfaces, DLL -> engine: ' + ', '.join(m.d2e))
    res = {}
    for label, kinds in (('proxy', [(n, 'ProxyHook', True) for n in m.e2d] + [(n, 'EngHook', True) for n in m.d2e]),
                         ('server', [(n, 'ServerHook', True) for n in m.e2d] + [(n, 'RemoteGen', False)
                                                                               for n in m.d2e])):
        st, hooked, loud = side(kinds)
        res[label] = (st, hooked, loud)
        print('%-6s interfaces %d, methods %d, mechanical %d (special included), hooked (hand-written) %d, '
              'loud defaults %d' % (label, st['interfaces'], st['methods'], st['mechanical'], st['hooked'], st['loud']))
        if hooked:
            print('       hand-written hooks: ' + ', '.join(hooked))
    if stale:
        print('WARNING: hand-written hooks that match no generated method: ' + ', '.join(stale))
    if special_hooked:
        print('WARNING: hooks of special (always generated) methods are ignored: ' + ', '.join(special_hooked))
    print('files: %d, lines %d' % (len(lines), sum(lines.values())))
    return res


# =============================================================================================== compile check
def capture_env(bat):
    cp = subprocess.run('cmd /d /s /c ""%s" >NUL && set"' % bat, capture_output=True, text=True, errors='replace')
    env = {}
    for line in cp.stdout.splitlines():
        if '=' in line:
            k, v = line.split('=', 1)
            env[k] = v
    if 'INCLUDE' not in env:
        raise SystemExit('could not read the environment of %s' % bat)
    return env


def load_x64build():
    spec = importlib.util.spec_from_file_location('x64build', os.path.join(REPO, 'tools', 'x64-dll', 'build.py'))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def compile_check(out, src, objdir, jobs, hooks=()):
    t0 = time.perf_counter()
    common, pdir, sdir = (os.path.join(out, d) for d in ('common', 'proxy', 'server'))
    os.makedirs(objdir, exist_ok=True)
    # x86: the DLL's flags (toolchain.dll_args) with the include paths under --src
    x86_args = tc.ARCH_ARGS['x86'] + ['/MD', '/EHsc', '/Zc:wchar_t', '-msse3', '-Wno-everything', '/nologo', '/c'] + \
        ['/D' + d for d in tc.DEFINES] + ['/I' + os.path.join(src, i) for i in tc.INCLUDES] + \
        ['/I' + os.path.join(REPO, 'proxy', 'src'), '/I' + os.path.join(REPO, 'proxy', 'include'),
         '/I' + os.path.join(REPO, 'ipc', 'include'), '/I' + common, '/I' + pdir]
    # x64: server/build_server.py's flags for server/src/*.cpp (debug optimisation level: compile-only check)
    xb = load_x64build()
    not_for_host = {'_USRDLL', '_WINDLL', 'CVGAMECOREDLL_EXPORTS'}
    x64_args = ['-m64', '-msse3', '/c', '/nologo', '/MD', '/GS', '/EHsc', '/fp:precise', '/Zc:wchar_t', '/std:c++14',
                '-fdiagnostics-absolute-paths', '/W3', '/WX', '/Od', '/Oy-']
    x64_args += ['/D' + d for d in xb.PREDEFS['debug'] if d not in not_for_host and d != 'VPDEBUG']
    x64_args += ['/D_SILENCE_TR1_NAMESPACE_DEPRECATION_WARNING']
    x64_args += ['/external:W0'] + ['/external:I' + os.path.join(src, d) for d in xb.INCLUDE_DIRS]
    x64_args += ['/I' + os.path.join(REPO, 'server', 'src'), '/I' + os.path.join(REPO, 'server', 'src', 'remote'),
                 '/I' + os.path.join(REPO, 'proxy', 'include'), '/I' + os.path.join(REPO, 'ipc', 'include'),
                 '/I' + common, '/I' + sdir]
    x64_args += ['-Wno-' + w for w in xb.CL_SUPPRESS] + ['-Wno-deprecated-declarations', '-Wno-microsoft-include',
                                                         '-Wno-pragma-pack', '-Wno-unused-value',
                                                         '-Wno-overloaded-virtual', '-Wno-invalid-offsetof']
    env86 = capture_env(tc.ENV_BAT['x86'])
    env64 = xb.load_vcvars_env(lambda s, echo=True: None,xb.Path(objdir) / 'vcvars-env.json')
    tus = [('x86', os.path.join(common, 'vp_ids.gen.cpp')), ('x64', os.path.join(common, 'vp_ids.gen.cpp'))]
    tus += [('x86', os.path.join(pdir, f)) for f in sorted(os.listdir(pdir)) if f.endswith('.cpp')]
    tus += [('x64', os.path.join(sdir, f)) for f in sorted(os.listdir(sdir)) if f.endswith('.cpp')]

    def one(tu):
        arch, path = tu
        od = os.path.join(objdir, arch)
        os.makedirs(od, exist_ok=True)
        obj = os.path.join(od, os.path.splitext(os.path.basename(path))[0] + '.obj')
        if arch == 'x86':
            cmd, env = [tc.CLANG] + x86_args + ['/Fo' + obj, path], env86
        else:
            cmd, env = [str(xb.CLANG_CL)] + x64_args + ['/Fo' + obj, path], env64
        t = time.perf_counter()
        cp = subprocess.run(cmd, capture_output=True, text=True, errors='replace', env=env)
        return arch, path, cp.returncode, (cp.stdout + cp.stderr).strip(), time.perf_counter() - t

    results = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        for r in ex.map(one, tus):
            results.append(r)
    errors = 0
    log = []
    for arch, path, rc, text, dt in results:
        n_err = len(re.findall(r'\berror\b', text)) if rc else 0
        errors += n_err if rc else 0
        print('  %s %-36s rc=%d %5.1f s%s' % (arch, os.path.basename(path), rc, dt,
                                               '' if rc == 0 else '  (%d error lines)' % n_err))
        if rc != 0 or text:
            log.append('==== %s %s rc=%d\n%s\n' % (arch, path, rc, text))
    with open(os.path.join(objdir, 'compile-check.log'), 'w', encoding='utf-8') as f:
        f.write(''.join(log))
    failed = [r for r in results if r[2] != 0]
    n86 = sum(1 for r in results if r[0] == 'x86')
    if not failed:
        missing = symbol_check(objdir, [(r[0], r[1]) for r in results], hooks)
        for arch, names in sorted((k, v) for k, v in missing.items() if v):
            print('symbol-check %s: %d generated names referenced but defined nowhere: %s' % (
                arch, len(names), ', '.join(sorted(names)[:20])))
        if any(missing.values()):
            failed = [('symbols',)]
        else:
            print('symbol-check: every generated name the objects reference is defined in them (x86, x64)')
    print('compile-check: %d TUs (x86 %d, x64 %d), %d failed, error lines %d, %.0f s; log %s' % (
        len(results), n86, len(results) - n86, len(failed), errors, time.perf_counter() - t0,
        os.path.join(objdir, 'compile-check.log')))
    return not failed


GEN_NAME = re.compile(r'(ProxyHook_|ServerHook_|EngHook_|ProxyFwd_|ServerFwd_|EngFwd_|Dispatch_|DispatchGenerated|'
                      r'vpk_|VpGen|VpProxyNewWrapper|Proxy_|RemoteGen_)')


def symbol_check(objdir, tus, hooks=()):
    """Undefined symbols of the generated objects that name generated functions must be defined by one of them
    (hook defaults included), so a link of the generated code plus the two runtimes cannot miss one. Runtime
    (ProxyRt::, SrvRt::, vpipc::) and library symbols are not checked here."""
    nm = os.path.join(os.path.dirname(tc.CLANG), 'llvm-nm.exe')
    missing = {}
    for arch in ('x86', 'x64'):
        defined, undefined = set(), set()
        for a, path in tus:
            if a != arch:
                continue
            obj = os.path.join(objdir, arch, os.path.splitext(os.path.basename(path))[0] + '.obj')
            cp = subprocess.run([nm, '--no-sort', obj], capture_output=True, text=True, errors='replace')
            for line in cp.stdout.splitlines():
                parts = line.split()
                if len(parts) >= 2 and parts[-2] == 'U':
                    undefined.add(parts[-1])
                elif len(parts) >= 3:
                    defined.add(parts[-1])
        # hand-written hooks (found by the scan) are defined outside the generated code
        missing[arch] = {s for s in undefined - defined if GEN_NAME.search(s) and
                         re.sub(r'^\?([A-Za-z_]\w*)@@.*$', r'\1', s) not in hooks}
    return missing


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--out', required=True, help='output directory (outside the repository)')
    ap.add_argument('--hooks-proxy', default=os.path.join(REPO, 'proxy', 'src'))
    ap.add_argument('--hooks-server', default=os.path.join(REPO, 'server', 'src', 'remote'))
    ap.add_argument('--src', required=True,
                    help='DLL fork copy at the pinned commit (GUIDs, compile check; required, no default since 9008)')
    ap.add_argument('--compile-check', action='store_true', help='compile every generated .cpp (no link)')
    ap.add_argument('--objdir', help='objects of --compile-check (required with it; outside the repository)')
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--list-loud', action='store_true', help='print every method left to a loud default')
    ap.add_argument('--model-dir', default=os.path.join(tc.PROXY, 'model'),
                    help='issue 3503: where interfaces.json and model.json are read (default proxy/model)')
    a = ap.parse_args()
    global INTERFACES, MODEL_JSON
    INTERFACES = os.path.join(os.path.abspath(a.model_dir), 'interfaces.json')
    MODEL_JSON = os.path.join(os.path.abspath(a.model_dir), 'model.json')
    out = os.path.abspath(a.out)
    if (out + os.sep).lower().startswith(REPO.lower() + os.sep):
        raise SystemExit('--out %s is inside the repository' % out)
    t0 = time.perf_counter()
    m, ifs, hooks, stale, special_hooked, lines = generate(out, [a.hooks_proxy, a.hooks_server], a.src)
    res = summary(m, ifs, hooks, stale, special_hooked, lines)
    if a.list_loud:
        for label in ('proxy', 'server'):
            print('loud defaults, %s:' % label)
            for s in res[label][2]:
                print('  ' + s)
    print('generated into %s in %.1f s' % (out, time.perf_counter() - t0))
    if a.compile_check:
        if not a.objdir:
            raise SystemExit('gen_runtime: --compile-check needs --objdir <dir>')
        if not compile_check(out, a.src, a.objdir, a.jobs, hooks):
            sys.exit(1)


if __name__ == '__main__':
    main()
