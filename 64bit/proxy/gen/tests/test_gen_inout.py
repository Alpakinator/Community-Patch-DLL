"""Offline unit tests: every generated parameter that returns a value is in/out (issue 0380; no compiler, < 3 s).

    python -I proxy/gen/tests/test_gen_inout.py [-v]

proxy-design 31: a copy-out parameter hands the caller the callee's fresh value where an in-process callee that does
not write it leaves the caller's own (ICvPreGame1::GetMapOption's unset option). These tests pin the in half of
every such codec in both generators, the server hook of the map-option lookup, and the census
(out_param_census.py): a new copy-out codec in the product fails here.
"""
import json
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
sys.path.insert(0, HERE)
import gen_proxy as gp  # noqa: E402
import gen_runtime as gr  # noqa: E402
import out_param_census as census  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, '..', '..', '..'))


def proof_iface(name, only=None):
    """gen_proxy's Iface of `name`; with `only`, of just the methods whose name is in it (the proof generator has
    no codec for some returns of the larger interfaces, so their whole emission fails)."""
    with open(gp.MODEL, encoding='utf-8') as f:
        model = json.load(f)
    for k, e in enumerate(model['interfaces']):
        if e['name'] == name:
            if only:
                e = dict(e)
                e['methods'] = [m for m in e['methods'] if m['name'] in only]
            return gp.Iface(e, k + 1)
    raise KeyError(name)


def block(text, head):
    """The generated proxy function (closing brace in column 0) or server case (closing brace indented by 4) that
    starts at the line containing head."""
    i = text.index(head)
    line_start = text.rfind('\n', 0, i) + 1
    end = '\n    }\n' if text[line_start:i].lstrip().startswith('case ') else '\n}\n'
    j = text.find(end, i)
    return text[i:j if j >= 0 else len(text)]


class ProofGenerator(unittest.TestCase):
    """gen_proxy.py (proxy/proof): out_scalar and out_str are in/out."""

    def test_scalar_reference(self):
        it = proof_iface('ICvPreGame1', ('GetMapOption',))
        px = block(gp.emit_proxy_cpp(it), 'Proxy_ICvPreGame1::GetMapOption(const char * szOptionName, int & iValue)')
        self.assertIn('w.i32(static_cast<int32_t>(iValue));', px)
        self.assertLess(px.index('w.i32(static_cast<int32_t>(iValue));'), px.index('end_call'))
        self.assertIn('iValue = static_cast<int>(r.i32());', px)
        sv = block(gp.emit_server_cpp(it), 'ICvPreGame1::GetMapOption(const char *, int &)')
        self.assertIn('int iValue = static_cast<int>(args.i32());', sv)
        self.assertNotIn('int iValue = int();', sv)
        self.assertIn('ret.i32(static_cast<int32_t>(iValue));', sv)

    def test_scalar_pointer_keeps_presence(self):
        it = proof_iface('ICvEngineUtility1', ('GetSteamStat', 'stripSpecialCharacters'))
        px = block(gp.emit_proxy_cpp(it), 'Proxy_ICvEngineUtility1::GetSteamStat(')
        self.assertIn('w.b(pStat != NULL); if (pStat) w.i32(static_cast<int32_t>(*pStat));', px)
        self.assertIn('if (pStat) *pStat = static_cast<int32>(r.i32());', px)
        sv = block(gp.emit_server_cpp(it), 'ICvEngineUtility1::GetSteamStat(')
        self.assertIn('int32* pStat = NULL;', sv)
        self.assertIn('if (args.b()) { pStat_v = static_cast<int32>(args.i32()); pStat = &pStat_v; }', sv)
        self.assertIn('if (pStat) ret.i32(static_cast<int32_t>(*pStat));', sv)

    def test_cvstring_reference(self):
        it = proof_iface('ICvEngineUtility1', ('GetSteamStat', 'stripSpecialCharacters'))
        px = block(gp.emit_proxy_cpp(it), 'Proxy_ICvEngineUtility1::stripSpecialCharacters(')
        self.assertIn('w.str(szName.c_str(), static_cast<uint32_t>(szName.size()));', px)
        sv = block(gp.emit_server_cpp(it), 'ICvEngineUtility1::stripSpecialCharacters(')
        self.assertIn('CvString szName(szName_c ? szName_c : "");', sv)

    def test_unchanged_proof_interfaces(self):
        # ICvRandom1 and ICvTeam1 have no out_scalar / out_str: the committed proof output stays byte-identical.
        for name in ('ICvRandom1', 'ICvTeam1'):
            it = proof_iface(name)
            for fn, text in (('proxy_%s.gen.cpp' % name, gp.emit_proxy_cpp(it)),
                             ('server_%s.gen.cpp' % name, gp.emit_server_cpp(it))):
                with open(os.path.join(REPO, 'proxy', 'proof', 'generated', fn), encoding='utf-8') as f:
                    self.assertEqual(f.read().replace('\r\n', '\n'), text, fn)


class ProductGenerator(unittest.TestCase):
    """gen_runtime.py: scalar references, caller buffers and the out array start from the caller's value."""

    @classmethod
    def setUpClass(cls):
        cls.m = gr.Model()
        cls.meths = {}
        for n in cls.m.mds:
            for x in gr.Iface(cls.m, n).meths:
                cls.meths[x.key] = x

    def codec(self, key, pname):
        x = self.meths[key]
        for c in x.params:
            if c is not None and c.n == pname:
                return x, c
        raise KeyError(pname)

    def test_map_option_by_name(self):
        x, c = self.codec('ICvPreGame1::GetMapOption(const char *, int &)', 'iValue')
        self.assertIsInstance(c, gr.PScalarRef)
        self.assertEqual(c.caller_enc('proxy'), ['vp_w.i32(iValue);'])
        self.assertEqual(c.callee_dec('server'), ['int a_iValue = static_cast<int>(args.i32());'])

    def test_caller_buffer_sends_current_string(self):
        x, c = self.codec('ICvGameContext1::GetDefineSTRING(char *, size_t, const char *, bool)', 'szBuffer')
        self.assertIsInstance(c, gr.PCallerBuf)
        enc = '\n'.join(c.caller_enc('proxy'))
        self.assertIn('vp_w.u32(c_);', enc)
        self.assertIn('strnlen(szBuffer, c_ < 1048576u ? c_ : 1048576u)', enc)
        self.assertIn('vp_w.str(k_ ? szBuffer : "", k_);', enc)
        dec = '\n'.join(c.callee_dec('server'))
        self.assertIn('args.cstr(&n_)', dec)
        self.assertIn('memcpy(&a_szBuffer_buf[0], s_, n_ < a_szBuffer_cap ? n_ : a_szBuffer_cap)', dec)
        out = '\n'.join(c.caller_out('proxy'))
        self.assertIn('if (k_ > c_) k_ = c_;', out)
        self.assertIn('if (k_ < c_) szBuffer[k_] = 0;', out)

    def test_out_array_sends_current_elements(self):
        x, c = self.codec('ICvTeam1::GetTechs(TechTypes *, uint) const', 'pkTechArray')
        self.assertIsInstance(c, gr.POutArray)
        enc = '\n'.join(c.caller_enc('proxy'))
        self.assertIn('if (c_ > 65536u) c_ = 65536u;', enc)
        self.assertIn('vp_w.i32(static_cast<int32_t>(pkTechArray[i_]));', enc)
        dec = '\n'.join(c.callee_dec('server'))
        self.assertIn('return SrvRt::DispatchFail(ch, ret, vpw::kStatusBadArguments', dec)
        self.assertIn('a_pkTechArray_buf[i_] = static_cast<TechTypes>(args.i32());', dec)

    def test_no_product_copy_out(self):
        rows = census.product_rows()
        self.assertGreater(len(rows), 50)
        bad = [r for r in rows if r[8] in ('COPY-OUT', 'not reviewed')]
        self.assertEqual(bad, [])
        self.assertTrue(all(r[7] == 'yes' for r in rows if r[6] != 'PStream'))

    def test_census_catches_copy_out(self):
        # Negative: a codec that loses its in half (the fault of proxy-design 31.1) is reported COPY-OUT and the
        # census exits 1.
        saved = gr.PCallerBuf.caller_enc
        gr.PCallerBuf.caller_enc = lambda self, side: []
        try:
            rows = census.product_rows()
            bad = [r for r in rows if r[8] == 'COPY-OUT']
            self.assertEqual(len(bad), 7)  # the seven caller buffers of the census
            self.assertTrue(all(r[6] == 'PCallerBuf' for r in bad))
            old = sys.argv
            sys.argv = ['out_param_census.py', '--out', os.devnull]
            try:
                self.assertEqual(census.main(), 1)
            finally:
                sys.argv = old
        finally:
            gr.PCallerBuf.caller_enc = saved


class ServerHook(unittest.TestCase):
    """The map-option lookup has its server hook (fault 2 of proxy-design 31), found by the generator's hook scan."""

    def test_hook_found(self):
        found = gr.scan_hooks([os.path.join(REPO, 'server', 'src', 'remote')])
        self.assertIn('ServerHook_ICvPreGame2_GetMapOption_s37', found)
        self.assertEqual(found['ServerHook_ICvPreGame2_GetMapOption_s37'], 'server/src/remote/srv_remote_db.cpp')


if __name__ == '__main__':
    unittest.main()
