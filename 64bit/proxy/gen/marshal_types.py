"""Marshalling classes for the types in the EXE<->DLL interfaces (epic 2100, task 2103).

One table, used by classify.py (model and policy) and by gen_proxy.py (code generation), so the two cannot
disagree. Conventions are ipc/PROTOCOL.md 1.0 section 5 (little-endian, 4-byte aligned, no type tags):
i32/bool/f32/f64/u64 values, narrow and wide strings as u32 length + bytes + NUL, byte buffers, and
handles as (u32 type, i32 id).

classify_type(t, role) -> (codec, mechanical, note)
  role: 'param' or 'ret'
  codec:      the wire encoding (or the class of problem when not mechanical)
  mechanical: True when the generator can emit the stub and the dispatch from the signature alone
"""
import re

INT_LIKE = {'int', 'unsigned int', 'uint', 'dword', 'int32', 'short', 'unsigned short', 'char', 'byte',
            'unsigned char', 'unsigned long', 'long', 'DWORD', 'ArgType', 'FileErr', 'SlotClaim', 'SlotStatus',
            'StorageLocation', 'GameMode', 'EAchievement', 'ESteamStat', 'BandwidthType', 'TURN_STATUS_TYPE',
            'CvDLLInterfaceIFaceBase::TURN_STATUS_TYPE', 'FIFile::OpenMode', 'WonderCommandType',
            'ArchaeologyChoiceType', 'TradeConnectionType', 'InterfaceDirtyBits', 'size_t'}
STRINGS = {'const char *', 'LPCSTR', 'LPCTSTR', 'const TCHAR *', 'const char *const', 'CvString', 'const CvString &',
           'const std::string &', 'const CvString', 'const std::string'}
WSTRINGS = {'const wchar *', 'const wchar_t *', 'LPCWSTR'}
GUIDS = {'GUID', 'const GUID &', 'const GUID'}
STREAMS = {'FDataStream &', 'FMemoryStream &', 'FAutoArchive &'}
# Plain structs passed by value / const reference / pointer: mechanical once a per-type codec exists (fixed
# fields; layouts in interfaces.json "records" where the compiler laid them out).
PODS = {'IDInfo', 'const IDInfo &', 'const IDInfo *', 'IDInfo *', 'UnitMoveRate', 'FFileTimestamp',
        'const CvColorA &', 'const CvPopupInfo &', 'const CvWorldBuilderMapLoaderMapInfo &',
        'const CvCombatMemberEntry *', 'CvMapInitData *'}
STL = {'const std::vector<bool> &', 'const std::vector<std::pair<std::string, std::string>> &',
       'const CvPlotIndexVector &', 'EnumeratedFilesList &'}
OUT_SCALARS = {'int &', 'bool &', 'float &', 'size_t *', 'int32 *', 'CvString &'}
LUA = {'lua_State *', 'lua_CFunction'}
# Objects of the EXE (or of closed libraries) that only make sense in the process that owns them.
ENGINE_OBJECTS = {'Database::Connection *', 'FILogFile *', 'FIFile *', 'FFileErrorHandler *',
                  'CvDLLInterfaceIFaceBase *', 'ICvEngineScriptSystemArgs1 *', 'ICvEngineScriptSystem1 *',
                  'ICvEngineUtility1 *', 'ICvUserInterface2 *', 'FIGameIniParser &', 'FIFile::Allocator &', 'HANDLE'}
RAW = {'void *', 'byte **', 'char *', 'char *&', 'const char **', 'int *', 'const int *', 'const bool *',
       'TechTypes *', 'const ICvPlot1 *'}


def strip_top_const(t):
    t = t.strip()
    if t.startswith('const ') and not t.endswith(('*', '&')):
        return t[6:].strip()
    return t


def classify_type(t, role='param'):
    t = re.sub(r'\s+', ' ', t.strip())
    if role == 'ret' and t == 'void':
        return 'void', True, ''
    b = strip_top_const(t)
    if b == 'bool':
        return 'bool', True, ''
    if b == 'float':
        return 'f32', True, ''
    if b == 'double':
        return 'f64', True, ''
    if b in ('unsigned long long', 'uint64', 'long long'):
        return 'u64', True, ''
    if b in ('size_t',):
        return 'i32', True, 'size_t is 4 bytes in the EXE and 8 in the server: narrowed on the wire [I]'
    if b in INT_LIKE or re.match(r'^[A-Z]\w*Types$', b) or b in ('CitySizeTypes',):
        return 'i32', True, ''
    if t in GUIDS:
        return 'guid', True, '16-byte buffer'
    if t in STRINGS:
        if role == 'ret' and t in ('const char *', 'const char *const', 'LPCSTR'):
            return 'str_ret_interned', True, 'returned pointer must outlive the call: the proxy interns it'
        return 'str', True, ''
    if t in WSTRINGS:
        if role == 'ret':
            return 'wstr_ret_interned', True, 'returned pointer must outlive the call: the proxy interns it'
        return 'wstr', True, ''
    m = re.match(r'^(?:const )?(ICv\w+) \*$', t)
    if m and t not in ENGINE_OBJECTS and m.group(1) not in ('ICvUnknown',):
        return 'handle', True, 'wrapper object <-> (interface type, id); proxy owns client-side wrappers'
    if t == 'ICvUnknown *':
        return 'handle', True, 'QueryInterface result: a handle whose type is the requested interface'
    if t in OUT_SCALARS:
        if t == 'CvString &':
            return 'out_str', True, 'in/out string (issue 0380), re-assigned into the EXE\'s VC9 CvString'
        return 'out_scalar', True, 'in/out: the callee starts from the caller\'s value (issue 0380)'
    if t in STREAMS:
        return 'stream', False, 'EXE stream object (save/load, net sync)'
    if t in PODS:
        return 'pod', False, 'plain struct: needs a per-type field codec (layout in records)'
    if t in STL:
        return 'stl', False, 'VC9 STL container: element-wise codec, never by layout'
    if t in LUA:
        return 'lua', False, 'Lua state or C function of one VM: cannot cross processes'
    if t in ENGINE_OBJECTS:
        return 'engine_object', False, 'object of the process that owns it; server-local or proxy-local'
    if t in RAW or t.endswith(('*', '&')):
        return 'raw_pointer', False, 'pointer into the caller\'s memory; length/ownership not in the signature'
    return 'unknown', False, 'type not classified'


def classify_method(m):
    """m: a method dict of interfaces.json. Returns dict with per-param codecs and the method verdict."""
    params = []
    reasons = []
    for p in m['params']:
        codec, mech, note = classify_type(p['type'], 'param')
        params.append({'name': p['name'], 'type': p['type'], 'codec': codec, 'mechanical': mech})
        if not mech:
            reasons.append('%s %s: %s' % (p['type'], p['name'], note))
    rc, rmech, rnote = classify_type(m['ret'], 'ret')
    if not rmech:
        reasons.append('returns %s: %s' % (m['ret'], rnote))
    if m.get('variadic'):
        reasons.append('variadic (...): argument types not in the signature')
    # Known pointer+count pairs (an output array whose length is the next argument).
    for i, p in enumerate(m['params'][:-1]):
        nxt = m['params'][i + 1]
        if p['type'] in ('TechTypes *', 'int *') and strip_top_const(nxt['type']) in ('uint', 'int', 'unsigned int'):
            params[i]['codec'] = 'out_array_i32'
            params[i]['mechanical'] = True
            params[i]['count_param'] = nxt['name']
            reasons = [r for r in reasons if not r.startswith(p['type'] + ' ' + p['name'] + ':')]
    return {'params': params, 'ret_codec': rc, 'mechanical': not reasons, 'reasons': reasons}
