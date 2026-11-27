"""Tests of tools/exebuild/compiled_by.py (ticket 0605): which test targets and scripts compile or include a source.

    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 && python -I -m pytest -q -p no:cacheprovider tools/exebuild/tests/test_compiled_by.py

Most cases run on a small tree made in tmp_path (no git: the tool walks the folder). One case reads this checkout: the
acceptance of 0605, proxy/src/proxy_farm_guard.cpp names shell/tests/farm_guard_test.cpp and
proxy/tests/test_farm_guard_proxy.py. Nothing is built or run. No case skips.
"""
from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
EXEBUILD = HERE.parent
REPO = EXEBUILD.parents[1]
TOOL = EXEBUILD / 'compiled_by.py'
sys.path.insert(0, str(EXEBUILD))
import compiled_by  # noqa: E402

CPP = '.' + 'cpp'     # spelled apart so that the tool's scan of this checkout never takes this file for a builder


def write(root: Path, rel: str, text: str = '') -> None:
    p = root / rel
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(text, encoding='utf-8')


def make_tree(root: Path) -> None:
    # product sources: a pair (guard + its proxy side), a header, a .cpp included by another, a lone file
    write(root, 'shell/composite/guard.h', '#pragma once\nint Guard();\n')
    write(root, 'shell/composite/guard' + CPP, '#include "guard.h"\nint Guard() { return 1; }\n')
    write(root, 'proxy/src/proxy_guard' + CPP, '#include "guard.h"\nint ProxyGuard() { return Guard(); }\n')
    write(root, 'proxy/src/inc_part' + CPP, 'int Part() { return 2; }\n')
    write(root, 'proxy/src/inc_host' + CPP, '#include "inc_part.cpp"\n')
    write(root, 'proxy/src/lonely' + CPP, 'int Lonely() { return 3; }\n')
    write(root, 'proxy/src/big1' + CPP, '')
    write(root, 'proxy/src/big2' + CPP, '')
    write(root, 'proxy/src/big3' + CPP, '')
    write(root, 'proxy/src/big4' + CPP, '')
    write(root, 'proxy/src/big_unit' + CPP, '')
    write(root, 'proxy/src/big_only' + CPP, '')
    # a batch build: ROOT=%~dp0, one test program with the guard, one product dll
    write(root, 'shell/tests/guard_test' + CPP, '#include "guard.h"\nint main() { return Guard() - 1; }\n')
    write(root, 'shell/build.cmd',
          '@echo off\nset "ROOT=%~dp0"\nset "BIN=%OUT%\\bin"\n'
          'rem guard_test.exe, the unit test of composite\\guard.cpp\n'
          'cl /nologo /I "%ROOT%composite" "%ROOT%tests\\guard_test.cpp" ^\n'
          '   "%ROOT%composite\\guard.cpp" /Fe"%BIN%\\guard_test.exe" /link /DEBUG || exit /b 1\n'
          'cl /nologo /LD "%ROOT%composite\\guard.cpp" /Fe"%BIN%\\shell_composite.dll" || exit /b 1\n')
    # the runner of guard_test.exe
    write(root, 'shell/tests/test_guard.py', 'def test_guard(built):\n    exe = built / "guard_test.exe"\n')
    # a Python test builder: the proxy side with the composite side (a pair: the partner rule)
    write(root, 'proxy/tests/proxy_guard_test' + CPP, 'int main() { return 0; }\n')
    write(root, 'proxy/tests/test_guard_proxy.py',
          'from pathlib import Path\nHERE = Path(__file__).resolve().parent\nREPO = HERE.parents[1]\n'
          "SRC = REPO / 'proxy' / 'src'\nCOMPOSITE = REPO / 'shell' / 'composite'\n"
          "SRCS = [HERE / 'proxy_guard_test.cpp', SRC / 'proxy_guard.cpp', COMPOSITE / 'guard.cpp']\n")
    # a Python product builder with a glob, and one test that compiles many product sources (no partners from it)
    write(root, 'proxy/build_proxy.py',
          'from pathlib import Path\nHERE = Path(__file__).resolve().parent\nREPO = HERE.parent\n'
          "srcs = sorted((REPO / 'proxy' / 'src').glob('*.cpp')) + [REPO / 'shell' / 'composite' / 'guard.cpp']\n")
    write(root, 'proxy/tests/test_big.py',
          'from pathlib import Path\nHERE = Path(__file__).resolve().parent\nS = HERE.parent / "src"\n'
          "L = [HERE / 'proxy_guard_test.cpp', S / 'big_unit.cpp', S / 'big1.cpp', S / 'big2.cpp', S / 'big3.cpp',"
          " S / 'big4.cpp']\n")
    write(root, 'proxy/tests/test_big_only.py',
          "from pathlib import Path\nB = Path(__file__).resolve().parent.parent / 'src' / 'big1.cpp'\n"
          "A = Path(__file__).resolve().parent.parent / 'src' / 'big_only.cpp'\n")
    # a tool builder that loads a test builder's list (one hop), and a product builder loaded for helpers (not counted)
    write(root, 'tools/sceneshot/build_shot.py',
          'import importlib.util\nfrom pathlib import Path\nHERE = Path(__file__).resolve().parent\n'
          'REPO = HERE.parents[1]\n'
          "sbt = importlib.util.spec_from_file_location('t', REPO / 'proxy' / 'tests' / 'test_guard_proxy.py')\n"
          "MAIN = HERE / 'shot.cpp'\n")
    write(root, 'tools/sceneshot/shot' + CPP, '')
    write(root, 'server/build_server.py',
          'import importlib.util\nfrom pathlib import Path\nHERE = Path(__file__).resolve().parent\n'
          'REPO = HERE.parent\n'
          "bp = importlib.util.spec_from_file_location('b', REPO / 'proxy' / 'build_proxy.py')\n"
          "MAIN = HERE / 'srv_main.cpp'\n")
    write(root, 'server/srv_main' + CPP, '')
    # the .cpp included by another, built by a shell script
    write(root, 'proxy/tests/build_inc.sh', 'D=$(dirname "$0")\nclang -c "$D/../src/inc_host.cpp" -o inc_test.exe\n')
    # CMake: a library with a glob, a test function, a foreach over globbed tests, add_test
    write(root, 'render/CMakeLists.txt',
          'project(r)\nadd_library(c5render STATIC)\nadd_subdirectory(src/mod)\n'
          'function(c5r_add_test name)\n  add_executable(${name} ${ARGN})\n'
          '  target_link_libraries(${name} PRIVATE c5r_testkit)\n  add_test(NAME ${name} COMMAND ${name})\nendfunction()\n'
          'add_library(c5r_testkit STATIC tests/common/kit.cpp)\n'
          'target_link_libraries(c5r_testkit PUBLIC c5render)\n'
          'if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tests/mod/CMakeLists.txt)\n  add_subdirectory(tests/mod)\nendif()\n')
    write(root, 'render/src/mod/CMakeLists.txt',
          'file(GLOB MOD_SRC CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)\n'
          'target_sources(c5render PRIVATE ${MOD_SRC})\n')
    write(root, 'render/src/mod/layout' + CPP, '')
    write(root, 'render/tests/common/kit' + CPP, '')
    write(root, 'render/tests/mod/CMakeLists.txt',
          'file(GLOB _t RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp)\n'
          'foreach(t ${_t})\n  string(REPLACE "_test.cpp" "" _n ${t})\n  c5r_add_test(c5r_mod_${_n} ${t})\nendforeach()\n')
    write(root, 'render/tests/mod/layout_test' + CPP, '')
    # a server builder that links the CMake library by name
    write(root, 'server/tests/build_scene.py',
          "from pathlib import Path\nHERE = Path(__file__).resolve().parent\nMAIN = HERE / 'scene.cpp'\n"
          "LIB = 'c5render.lib'\n")
    write(root, 'server/tests/scene' + CPP, '')
    # folders the tool never reads
    write(root, 'proxy/evidence/0001/old_build.sh', 'clang ../../src/lonely.cpp -o lonely_test.exe\n')
    write(root, 'research/notes.py', "X = 'proxy/src/lonely.cpp'\n")


def query(root: Path, f: str, partners=True):
    ix = compiled_by.Index(root)
    rep = compiled_by.report(ix, [f], partners=partners)
    return rep[f]


def targets(r, **kw):
    out = []
    for e in r['targets']:
        if all(e[k] == v for k, v in kw.items()):
            out.append(e)
    return out


def labels(r, **kw):
    return sorted(e['target'] for e in targets(r, **kw))


def test_batch_line_and_its_runner(tmp_path):
    make_tree(tmp_path)
    r = query(tmp_path, 'shell/composite/guard' + CPP)
    hit = [e for e in r['targets'] if e['target'].startswith('shell/build.cmd:5 guard_test.exe')]
    assert hit, labels(r)
    e = hit[0]
    assert e['test'] and e['level'] == 1 and e['reason'] == 'compiles it'
    assert e['programs'] == ['shell/tests/guard_test' + CPP]
    assert e['runners'] == ['shell/tests/test_guard.py']
    product = labels(r, test=False)
    assert 'shell/build.cmd:7 shell_composite.dll' in product
    assert 'proxy/build_proxy.py' in product


def test_python_builder_and_partner(tmp_path):
    """The acceptance shape: the proxy side's tests include the composite side's unit test as a partner."""
    make_tree(tmp_path)
    r = query(tmp_path, 'proxy/src/proxy_guard' + CPP)
    assert labels(r, level=1, test=True) == ['proxy/tests/test_guard_proxy.py']
    assert targets(r, level=1, test=True)[0]['reason'] == 'names it'      # a script: it may build or read it
    partner = targets(r, level=2)
    assert [e['target'] for e in partner] == ['shell/build.cmd:5 guard_test.exe']
    assert partner[0]['programs'] == ['shell/tests/guard_test' + CPP]
    assert partner[0]['runners'] == ['shell/tests/test_guard.py']
    assert 'shell/composite/guard' + CPP in partner[0]['reason']
    # build_proxy.py by its glob; the tool builder that loads the test builder's list, one hop
    assert labels(r, test=False) == ['proxy/build_proxy.py', 'tools/sceneshot/build_shot.py']
    shot = [e for e in r['targets'] if e['script'] == 'tools/sceneshot/build_shot.py'][0]
    assert shot['reason'].startswith('loads builder proxy/tests/test_guard_proxy.py')
    text = compiled_by.render_text({'proxy/src/proxy_guard' + CPP: r})
    assert 'shell/tests/guard_test' + CPP in text and 'proxy/tests/test_guard_proxy.py' in text


def test_no_partners_flag(tmp_path):
    make_tree(tmp_path)
    r = query(tmp_path, 'proxy/src/proxy_guard' + CPP, partners=False)
    assert not targets(r, level=2)


def test_partner_limit_and_product_loader(tmp_path):
    """A test that compiles many product sources gives no partners; a product builder loaded for its helpers
    (build_server.py loads build_proxy.py) does not make the loader a builder of build_proxy.py's sources."""
    make_tree(tmp_path)
    r = query(tmp_path, 'proxy/src/big_unit' + CPP)
    assert labels(r, level=1, test=True) == ['proxy/tests/test_big.py']
    assert not targets(r, level=2), labels(r, level=2)      # test_big_only.py is not pulled in through big1.cpp
    r = query(tmp_path, 'proxy/src/lonely' + CPP)
    assert labels(r) == ['proxy/build_proxy.py']          # not server/build_server.py, not evidence/ or research/


def test_header_and_included_cpp(tmp_path):
    make_tree(tmp_path)
    r = query(tmp_path, 'shell/composite/guard.h')
    got = labels(r, test=True)
    assert 'proxy/tests/test_guard_proxy.py' in got
    assert 'shell/build.cmd:5 guard_test.exe' in got
    r = query(tmp_path, 'proxy/src/inc_part' + CPP)
    hit = [e for e in r['targets'] if e['script'] == 'proxy/tests/build_inc.sh']
    assert hit and hit[0]['reason'] == 'compiles proxy/src/inc_host.cpp, which includes it'


def test_cmake_library_glob_function_and_foreach(tmp_path):
    make_tree(tmp_path)
    r = query(tmp_path, 'render/src/mod/layout' + CPP)
    cm = targets(r, kind='cmake', test=True)
    assert [e['target'].split(' ')[0] for e in cm] == ['c5r_mod_layout']
    assert 'links library c5render' in cm[0]['reason']
    assert [s['script'] for s in r['library_scripts']] == ['server/tests/build_scene.py']
    r = query(tmp_path, 'render/tests/mod/layout_test' + CPP)
    assert [e['reason'] for e in targets(r, kind='cmake')] == ['compiles it']


def test_nothing_compiles_it(tmp_path):
    make_tree(tmp_path)
    write(tmp_path, 'proxy/src/unused.h', '')
    r = query(tmp_path, 'proxy/src/unused.h')
    assert r['targets'] == [] and r['library_scripts'] == []
    assert 'nothing in the tree compiles or includes it' in compiled_by.render_text({'proxy/src/unused.h': r})


def test_cli_missing_file_exits_2(tmp_path):
    make_tree(tmp_path)
    p = subprocess.run([sys.executable, '-I', str(TOOL), '--root', str(tmp_path), 'proxy/src/nope' + CPP],
                       capture_output=True, text=True, timeout=120)
    assert p.returncode == 2
    assert 'is not a file of' in p.stderr and p.stdout == ''


def test_cli_json(tmp_path):
    make_tree(tmp_path)
    p = subprocess.run([sys.executable, '-I', str(TOOL), '--root', str(tmp_path), '--json',
                        str(tmp_path / 'proxy' / 'src' / ('proxy_guard' + CPP))],
                       capture_output=True, text=True, timeout=120)
    assert p.returncode == 0, p.stderr
    rep = json.loads(p.stdout)
    assert list(rep) == ['proxy/src/proxy_guard' + CPP]


def test_this_checkout_farm_guard_acceptance():
    """Ticket 0605's acceptance: the proxy's farm guard names both farm guard tests."""
    f = 'proxy/src/proxy_farm_guard' + CPP
    p = subprocess.run([sys.executable, '-I', str(TOOL), '--json', f], capture_output=True, text=True, timeout=300,
                       cwd=str(REPO))
    assert p.returncode == 0, p.stderr
    r = json.loads(p.stdout)[f]
    tests = [e for e in r['targets'] if e['test']]
    names = set()
    for e in tests:
        names.add(e['script'])
        names.update(e['programs'])
        names.update(e['runners'])
    assert 'shell/tests/farm_guard_test' + CPP in names, sorted(names)
    assert 'proxy/tests/test_farm_guard_proxy.py' in names, sorted(names)
    assert 'shell/tests/test_farm_guard.py' in names, sorted(names)
