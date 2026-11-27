"""Ticket 0589 (D200): which installed game the render tests read, and which EXE build they accept (Python side of
server/tests/render/test_install.h, same rules).

Since 2026-11-19 the Steam install is Firaxis' new build; the tests are keyed to build 4390913. No render test reads the
Steam install by default (several did before 0589). The install is, in this order:

1. C5_GAME_DIR;
2. C5R_GAME_DIR (what server/tests/render/run_all.py sets, and the other render suites read);
3. the setting render_game_dir of tools/release/settings.py (C5_RENDER_GAME_DIR, else deps/local-settings.ini);

and with none of them a placeholder path that names the variables, so the test that needs the install fails loud.

An EXE is accepted only when it is build 4390913, by tools/harness/exe_build.py's rule (PE TimeDateStamp and size).
C5_ALLOW_UNKNOWN_EXE_BUILD=1 lets another build through; it is for the migration to the new build only, once that is
approved (ticket 0583). Read-only: nothing is written.

    import gameinstall
    game, origin = gameinstall.game_dir()            # Path, where it came from
    gameinstall.require_install(game)                # AssertionError naming the variables, or the build refused
    gameinstall.require_exe(Path(os.environ["C5R_EXE"]))
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
EXE_NAME = "CivilizationV_DX11.exe"
ALLOW_ENV = "C5_ALLOW_UNKNOWN_EXE_BUILD"
PLACEHOLDER = ("<no install: set C5_GAME_DIR or C5R_GAME_DIR to a pre-update copy (build 4390913), or the setting "
               "render_game_dir of tools/release/settings.py; the Steam install is never the default (ticket 0589)>")


def _load(name, rel):
    spec = importlib.util.spec_from_file_location(name, REPO / rel)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def _exe_build():
    return _load("c5_exe_build", "tools/harness/exe_build.py")


def game_dir(environ=None):
    """(Path, origin) of the installed game the render tests read; the placeholder path when nothing names one."""
    env = os.environ if environ is None else environ
    for var in ("C5_GAME_DIR", "C5R_GAME_DIR"):
        if env.get(var):
            return Path(env[var]), var
    settings = _load("c5_release_settings", "tools/release/settings.py")
    v = settings.get("render_game_dir", environ)
    if v is not None:
        return Path(v), "setting render_game_dir (C5_RENDER_GAME_DIR or the settings file)"
    return Path(PLACEHOLDER), "nothing set"


def allowed(environ=None) -> bool:
    env = os.environ if environ is None else environ
    return env.get(ALLOW_ENV) == "1"


def exe_refusal(exe, environ=None):
    """None when the EXE file is build 4390913 (or C5_ALLOW_UNKNOWN_EXE_BUILD=1); else why it is refused."""
    eb = _exe_build()
    exe = Path(str(exe))
    try:
        eb.check_install(exe.parent, allow=allowed(environ), what=str(exe), exe_name=exe.name)
    except eb.UnknownExeBuild as e:
        return "%s (override: %s=1)" % (e, ALLOW_ENV)
    return None


def require_exe(exe, environ=None):
    """AssertionError unless the EXE file exists and is build 4390913 (or the override is set)."""
    exe = Path(str(exe))
    assert exe.is_file(), "the EXE %s is missing (C5R_EXE, read-only)" % exe
    why = exe_refusal(exe, environ)
    assert why is None, why


def require_install(game=None, environ=None) -> Path:
    """The install (game_dir() when None) after checking that its DX11 EXE exists and is build 4390913; AssertionError
    naming the variables otherwise (a skip counts as failed)."""
    if game is None:
        game, _ = game_dir(environ)
    game = Path(str(game))
    assert (game / EXE_NAME).is_file(), \
        "the installed game is needed (C5_GAME_DIR or C5R_GAME_DIR, a pre-update copy, read-only): %s; " \
        "skipped counts as failed" % game
    why = exe_refusal(game / EXE_NAME, environ)
    assert why is None, why
    return game
