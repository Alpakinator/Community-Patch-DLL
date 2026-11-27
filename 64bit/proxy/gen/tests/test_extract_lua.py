"""Offline unit tests of proxy/gen/extract_lua.py's source readers (issue 3519; no compiler, no source tree, < 1 s).

    python -I proxy/gen/tests/test_extract_lua.py [-v]

They pin the two registration forms the extractor reads:
  - the pin's (Release-5.4.8, fork x64 04b1211b8): Method(Name) expands to
    'lua_pushcclosure(L, lName, 0); lua_setfield(L, t, "Name");', Game and Map use 'RegisterMethod(L, lName, "Name");'
  - upstream master's (#13489, Lua/!CvLuaApi*.h): 'luaVp_pushcclosure(L, lName, 0); lua_setfield(L, 1, "Name");'
    (lua_pushcclosure is #defined to luaVp_pushcclosure), master's luaEx_* helper spellings and the luafsq_* stream.
The live checks (extract_lua.py on the pin reproduces proxy/model/lua.json; on master it finds 3,471 registrations)
are in research/06-tooling-and-workflows/new-vp-release-checklist.md step 10 and issue 3519.
"""
import os
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import extract_lua as el  # noqa: E402

SRC = 'C:/x/CvGameCoreDLL_Expansion2/Lua/CvLuaArea.cpp'


class Registrations(unittest.TestCase):
    def test_pin_method_form(self):
        lines = {21: 'lua_pushcclosure(L, lIsNone, 0); lua_setfield(L, t, "IsNone");;'}
        regs, others = el.scan_registrations(lines, 'CvLuaArea', 'Area', SRC)
        self.assertEqual(regs, [('Area', 'IsNone', 'CvLuaArea::lIsNone', SRC, 21, 'Method')])
        self.assertEqual(others, [])

    def test_pin_register_method_form(self):
        lines = {40: 'RegisterMethod(L, lGetGameTurn, "GetGameTurn");;'}
        regs, others = el.scan_registrations(lines, 'CvLuaGame', 'Game', SRC)
        self.assertEqual(regs, [('Game', 'GetGameTurn', 'CvLuaGame::lGetGameTurn', SRC, 40, 'RegisterMethod')])
        self.assertEqual(others, [])

    def test_master_method_form(self):
        # master: '#define lua_pushcclosure luaVp_pushcclosure' and the function table at stack index 1
        lines = {21: 'luaVp_pushcclosure(L, lIsNone, 0); lua_setfield(L, 1, "IsNone");;',
                 22: 'luaVp_pushcclosure(L, lGetID, 0); lua_setfield(L, 1, "GetID");'}
        regs, others = el.scan_registrations(lines, 'CvLuaArea', 'Area', SRC)
        self.assertEqual([r[1] for r in regs], ['IsNone', 'GetID'])
        self.assertTrue(all(r[5] == 'Method' for r in regs))
        self.assertEqual(others, [])

    def test_master_game_without_semicolon(self):
        # CvLuaGame.cpp's macro has no trailing ';' (the call site supplies it)
        lines = {40: 'luaVp_pushcclosure(L, lGetGameTurn, 0); lua_setfield(L, 1, "GetGameTurn");'}
        regs, _ = el.scan_registrations(lines, 'CvLuaGame', 'Game', SRC)
        self.assertEqual(regs, [('Game', 'GetGameTurn', 'CvLuaGame::lGetGameTurn', SRC, 40, 'Method')])

    def test_other_closures_both_forms(self):
        lines = {80: 'lua_pushcclosure(L, lCreate, 0);', 81: 'lua_setfield(L, -2, "Create");',
                 115: 'luaVp_pushcclosure(L, lGetHeight, 1);', 116: 'lua_setfield(L, -2, "GetHeight");'}
        regs, others = el.scan_registrations(lines, 'CvLuaFractal', 'Fractal', SRC)
        self.assertEqual(regs, [])
        self.assertEqual([(o[1], o[2], o[5]) for o in others],
                         [('Create', 'CvLuaFractal::lCreate', 0), ('GetHeight', 'CvLuaFractal::lGetHeight', 1)])

    def test_negative_forms_not_counted(self):
        # another stack index, the unsafe push of !CvLuaApi.h, a different setfield target, an identifier that only
        # ends in lua_pushcclosure: none is a Method() registration
        lines = {1: 'luaVp_pushcclosure(L, lA, 0); lua_setfield(L, 2, "A");',
                 2: 'lua_pushcclosure_unsafe(L, lB, 0); lua_setfield(L, 1, "B");',
                 3: 'lua_pushcclosure(L, lC, 0); lua_setfield(L, tbl, "C");',
                 4: 'xlua_pushcclosure(L, lD, 0); lua_setfield(L, 1, "D");'}
        regs, others = el.scan_registrations(lines, 'CvLuaArea', 'Area', SRC)
        self.assertEqual(regs, [])
        # lines 1 and 3 are closures without a recognised registration; 2 and 4 are not lua(Vp)_pushcclosure at all
        self.assertEqual(sorted(o[2] for o in others), ['CvLuaArea::lA', 'CvLuaArea::lC'])
        self.assertTrue(all(o[1] is None for o in others))


class MasterSpellings(unittest.TestCase):
    API_H = ('LuaTypes_DeclareStatic( 0x47414D45 /*GAME*/, Game, CvGame );\n'
             'LuaTypes_DeclareInstanced( 0x43495459 /*CITY*/, City, CvCity );\n'
             'LuaTypes_DeclareInstanced( 0x504C5952 /*PLYR*/, PlayerBase, CvPlayer );\n'
             'LuaTypes_DeclareInstanced( 0x54455445 /*TETE*/, TeamTechs, CvTeamTechs );\n'
             '#define LuaTypes_DeclareInstanced( Tag, Name, CLASS ) class CLASS ;\n')
    CPP = ('LuaTypes_DefineInstanced( CvCity, CvLuaCity );\n'
           'LuaTypes_DefineInstanced( CvPlayer, CvLuaPlayer );\n'
           'LuaTypes_DefineInstanced( CvTeamTechs, CvLuaTeamTech );\n'
           'LuaTypes_DefineStatic( CvGame, CvLuaGame );\n')

    def test_type_map(self):
        tm = el.build_type_map({'!CvLuaApi.h': el.strip_comments(self.API_H), 'x.cpp': self.CPP})
        self.assertEqual(tm, {'City': 'CvLuaCity', 'PlayerBase': 'CvLuaPlayer', 'TeamTechs': 'CvLuaTeamTech'})

    def test_type_map_pin_is_empty(self):
        self.assertEqual(el.build_type_map({'CvLuaCity.cpp': 'int CvLuaCity::lGetID(lua_State* L) { return 1; }'}),
                         {})

    def test_normalise_body(self):
        tm = {'City': 'CvLuaCity', 'Unit': 'CvLuaUnit', 'TeamTechs': 'CvLuaTeamTech'}
        body = ('{ CvCity* c = luaEx_toCity(L, 2, false); luaEx_pushUnit(L, u); luaEx_pushTeamTechs(L, t);'
                ' int b = luaEx_to<BuildingTypes>(L, 3); return luaEx_exec<UnitTypes>(L, &CvCity::getX); }')
        self.assertEqual(el.normalise_body(body, tm),
                         '{ CvCity* c = CvLuaCity::GetInstance(L, 2, false); CvLuaUnit::Push(L, u); '
                         'CvLuaTeamTech::Push(L, t); int b = CvLuaArgs::toValue<BuildingTypes>(L, 3); '
                         'return BasicLuaMethod<UnitTypes>(L, &CvCity::getX); }')

    def test_normalise_body_unknown_type_untouched(self):
        body = '{ luaEx_toNoSuchType(L, 2); return 1; }'
        self.assertEqual(el.normalise_body(body, {'City': 'CvLuaCity'}), body)

    def test_normalise_body_pin_untouched(self):
        body = '{ CvCity* c = CvLuaCity::GetInstance(L, 2); return BasicLuaMethod<int, BuildingTypes>(L, &CvCity::f); }'
        self.assertIs(el.normalise_body(body, {'City': 'CvLuaCity'}), body)

    def test_normalised_body_parses_like_the_pin(self):
        tm = {'City': 'CvLuaCity', 'Plot': 'CvLuaPlot'}
        master = '{ CvCity* c = GetInstance(L); CvPlot* p = luaEx_toPlot(L, 2); luaEx_pushCity(L, c); return 1; }'
        pin = '{ CvCity* c = GetInstance(L); CvPlot* p = CvLuaPlot::GetInstance(L, 2); CvLuaCity::Push(L, c); return 1; }'
        self.assertEqual(el.parse_shape(el.normalise_body(master, tm), 'Unit', 'instance', 'Unit'),
                         el.parse_shape(pin, 'Unit', 'instance', 'Unit'))

    def test_tovalue_qualified_and_unqualified(self):
        # Issue 0309: the pin spells the scalar read both ways; both give the same argument record.
        qual = '{ CvCity* c = GetInstance(L); const UnitTypes e = CvLuaArgs::toValue<UnitTypes>(L, 2); ' \
               'const int i = CvLuaArgs::toValue<int>(L, 3); return 0; }'
        unq = '{ CvCity* c = GetInstance(L); const UnitTypes e = toValue<UnitTypes>(L, 2); ' \
              'const int i = toValue< int >(L, 3); return 0; }'
        a_q = {a['pos']: a for a in el.parse_shape(qual, 'City', 'instance', 'City')[0]}
        a_u = {a['pos']: a for a in el.parse_shape(unq, 'City', 'instance', 'City')[0]}
        self.assertEqual(sorted(a_u), [1, 2, 3])
        self.assertEqual(a_u[2]['accessor'], 'CvLuaArgs::toValue<UnitTypes>')
        self.assertEqual(a_u[2]['kind'], 'integer')
        self.assertEqual(a_u[3]['accessor'], 'CvLuaArgs::toValue<int>')
        self.assertEqual(a_u, a_q)
        self.assertEqual(el.parse_shape(unq, 'City', 'instance', 'City'),
                         el.parse_shape(qual, 'City', 'instance', 'City'))

    def test_tovalue_master_spelling_equals_pin_unqualified(self):
        master = '{ CvCity* c = GetInstance(L); const BuildingTypes e = luaEx_to<BuildingTypes>(L, 2); return 0; }'
        pin = '{ CvCity* c = GetInstance(L); const BuildingTypes e = toValue<BuildingTypes>(L, 2); return 0; }'
        self.assertEqual(el.parse_shape(el.normalise_body(master, {}), 'City', 'instance', 'City'),
                         el.parse_shape(pin, 'City', 'instance', 'City'))

    def test_tovalue_member_access_not_counted(self):
        for body in ('{ CvCity* c = GetInstance(L); int x = args.toValue<int>(L, 2); return 0; }',
                     '{ CvCity* c = GetInstance(L); int x = p->toValue<int>(L, 2); return 0; }',
                     '{ CvCity* c = GetInstance(L); int x = Other::toValue<int>(L, 2); return 0; }',
                     '{ CvCity* c = GetInstance(L); int x = mytoValue<int>(L, 2); return 0; }'):
            args = [a['pos'] for a in el.parse_shape(body, 'City', 'instance', 'City')[0]]
            self.assertEqual(args, [1], body)

    def test_luaex_callees_are_plumbing(self):
        for name in ('luaEx_toUnit', 'luaEx_pushCity', 'luaEx_exec', 'luaVp_pushcclosure', 'BasicLuaMethod',
                     'TrackDllFinalizer'):
            self.assertEqual(el.callee_effect('func', '', name, 'void (lua_State *)', True, False)[0], 'plumbing',
                             name)
        # a game free function still is not
        self.assertNotEqual(el.callee_effect('func', '', 'setGameTurn', 'void (int)', True, False)[0], 'plumbing')
        for cls in ('CvLuaDynamicInstance', 'CvLuaStaticInstance', 'LuaTaggedAutoPointer', 'TypeGlobals'):
            self.assertEqual(el.cls_kind(cls), 'lua_wrapper', cls)

    def test_luafsq_stream(self):
        text = ('void f(lua_State* L) {\n lua_newtable(L);\n luafsq_start(L,1);\n'
                ' luafsq_func(HelloWorld) { lua_pushstring(L,"x"); }\n'
                ' luafsq_func(SetFaith,CvPlayer* in,int faith) { }\n'
                ' luafsq_wrap(SetFaithT100,CvPlayer::SetFaithTimes100);\n'
                ' luafsq_end();\n lua_setglobal(L,"vputils");\n}\n')
        self.assertEqual(el.scan_luafsq(text),
                         [('func', 'HelloWorld', None, 4, 'vputils'), ('func', 'SetFaith', None, 5, 'vputils'),
                          ('wrap', 'SetFaithT100', 'CvPlayer::SetFaithTimes100', 6, 'vputils')])

    def test_luafsq_none_at_pin(self):
        self.assertEqual(el.scan_luafsq('void f(lua_State* L) { lua_pushcclosure(L, lX, 0); }'), [])


if __name__ == '__main__':
    unittest.main(verbosity=2 if '-v' in sys.argv else 1, argv=[sys.argv[0]])
