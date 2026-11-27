"""The render units' guard of server/build_server.py in both layouts (ticket 4706).

Run: PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider server/tests/test_render_args_upstream.py

The guard (render_args_see_dll) refuses a build whose render units could see the DLL's headers. It used to be a
substring test of the whole --src folder, which is true for every render include path in the upstream layout, where
this repository is `64bit/` inside the DLL source. No compiler runs here.
"""
from __future__ import annotations

import importlib.util
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location('build_server_4706', REPO / 'server' / 'build_server.py')
bs = importlib.util.module_from_spec(_spec)
sys.modules['build_server_4706'] = bs
_spec.loader.exec_module(bs)

OUT = Path('C:/vp64-test-out')


def _args():
    return bs.render_compile_args('release', OUT)


def test_this_repositorys_layout_passes():
    src = Path('C:/elsewhere/Community-Patch-DLL')   # the DLL source outside this folder
    assert not bs.render_args_see_dll(_args(), src)


def test_upstream_layout_passes():
    src = REPO.parent   # this folder is 64bit/ inside the DLL source
    args = _args()
    assert any(str(src) in a for a in args), 'precondition: the old substring test would have refused this layout'
    assert not bs.render_args_see_dll(args, src)


def test_a_dll_header_folder_is_caught_in_both_layouts():
    for src in (Path('C:/elsewhere/Community-Patch-DLL'), REPO.parent):
        for d in bs.x64build.INCLUDE_DIRS:
            assert bs.render_args_see_dll(_args() + [f'/I{src / d}'], src), (src, d)
        # other case and forward slashes, as an /external:I argument
        bad = '/external:I' + str(src / 'FirePlace' / 'include').replace('\\', '/').upper()
        assert bs.render_args_see_dll(_args() + [bad], src)


def test_the_source_folder_itself_is_not_a_header_folder():
    src = Path('C:/elsewhere/Community-Patch-DLL')
    assert not bs.render_args_see_dll(_args() + [f'-ffile-prefix-map={src}=VPSRC'], src)
