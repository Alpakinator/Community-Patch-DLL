"""Start the shell programs (shell_server.exe, test_host.exe with shell_composite.dll) on a hidden desktop, and read
their logs and back-buffer dumps (issues 3001, 3006).

Every process here is started by `launch()` with STARTUPINFO.lpDesktop set to a desktop the test created and holds
(tools/harness/tests/desktops.py `HiddenDesktop`). Before anything starts, `launch()` applies the harness's own check
(`desktops.check_desktop`): no desktop, "Default" or "input" is refused with `InteractiveDesktopRefused` unless the
MC's permission variable is set (rule 14). Nothing here touches the game or a game install; the only processes ever
terminated are the ones this module started (by handle).
"""
from __future__ import annotations

import ctypes
import os
import re
import shutil
import struct
import sys
import time
from ctypes import wintypes
from pathlib import Path
from typing import Dict, Iterable, List, Optional

HERE = Path(__file__).resolve().parent
SHELL = HERE.parent
REPO = SHELL.parent
HARNESS_TESTS = REPO / "tools" / "harness" / "tests"
if str(HARNESS_TESTS) not in sys.path:
    sys.path.insert(0, str(HARNESS_TESTS))

import desktops  # noqa: E402  (tools/harness/tests/desktops.py, not modified here)

_k = ctypes.WinDLL("kernel32", use_last_error=True)

CREATE_UNICODE_ENVIRONMENT = 0x00000400
CREATE_NO_WINDOW = 0x08000000
STARTF_USESHOWWINDOW = 0x00000001
SW_SHOWNOACTIVATE = 4
WAIT_TIMEOUT = 0x102
PROCESS_VM_READ = 0x0010
PROCESS_QUERY_INFORMATION = 0x0400


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("lpReserved", wintypes.LPWSTR), ("lpDesktop", wintypes.LPWSTR),
                ("lpTitle", wintypes.LPWSTR), ("dwX", wintypes.DWORD), ("dwY", wintypes.DWORD),
                ("dwXSize", wintypes.DWORD), ("dwYSize", wintypes.DWORD), ("dwXCountChars", wintypes.DWORD),
                ("dwYCountChars", wintypes.DWORD), ("dwFillAttribute", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                ("wShowWindow", wintypes.WORD), ("cbReserved2", wintypes.WORD), ("lpReserved2", ctypes.c_void_p),
                ("hStdInput", wintypes.HANDLE), ("hStdOutput", wintypes.HANDLE), ("hStdError", wintypes.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wintypes.HANDLE), ("hThread", wintypes.HANDLE), ("dwProcessId", wintypes.DWORD),
                ("dwThreadId", wintypes.DWORD)]


class SECURITY_ATTRIBUTES(ctypes.Structure):
    _fields_ = [("nLength", wintypes.DWORD), ("lpSecurityDescriptor", ctypes.c_void_p), ("bInheritHandle", wintypes.BOOL)]


STARTF_USESTDHANDLES = 0x00000100
GENERIC_WRITE = 0x40000000
FILE_SHARE_READ, FILE_SHARE_WRITE = 1, 2
CREATE_ALWAYS = 2
INVALID_HANDLE_VALUE = ctypes.c_void_p(-1).value
_k.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
                           wintypes.DWORD, wintypes.HANDLE]
_k.CreateFileW.restype = wintypes.HANDLE
_k.CreateProcessW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wintypes.BOOL,
                              wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(STARTUPINFOW),
                              ctypes.POINTER(PROCESS_INFORMATION)]
_k.CreateProcessW.restype = wintypes.BOOL
_k.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
_k.WaitForSingleObject.restype = wintypes.DWORD
_k.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
_k.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
_k.CloseHandle.argtypes = [wintypes.HANDLE]
_k.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
_k.OpenProcess.restype = wintypes.HANDLE
_k.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                 ctypes.POINTER(ctypes.c_size_t)]
_k.ReadProcessMemory.restype = wintypes.BOOL


def build_dir() -> Path:
    """Where shell/build.cmd put its output: C5_SHELL_BUILD, required like in build.cmd since 2026-11-11 (MC order
    after a build overwrote the formerly shared default D:/c5-64bit-work/3000/shell-build)."""
    env = os.environ.get("C5_SHELL_BUILD")
    if env:
        return Path(env)
    raise RuntimeError("C5_SHELL_BUILD is not set: set it to the output folder of this checkout's shell\\build.cmd")


def bin_dir() -> Path:
    return build_dir() / "bin"


class Proc:
    """A process this module started; only ever terminated through its own handle."""

    def __init__(self, pi: PROCESS_INFORMATION, cmd: str):
        self.h = pi.hProcess
        self.pid = pi.dwProcessId
        self.cmd = cmd
        _k.CloseHandle(pi.hThread)

    def wait(self, timeout_s: float) -> Optional[int]:
        if _k.WaitForSingleObject(self.h, int(timeout_s * 1000)) == WAIT_TIMEOUT:
            return None
        code = wintypes.DWORD()
        _k.GetExitCodeProcess(self.h, ctypes.byref(code))
        return code.value

    def alive(self) -> bool:
        return _k.WaitForSingleObject(self.h, 0) == WAIT_TIMEOUT

    def kill(self) -> None:
        if self.h and self.alive():
            _k.TerminateProcess(self.h, 1)
            _k.WaitForSingleObject(self.h, 5000)

    def close(self) -> None:
        if self.h:
            _k.CloseHandle(self.h)
            self.h = None

    def read_memory(self, addr: int, n: int) -> bytes:
        """ReadProcessMemory of n bytes at addr (used to check hook prologues from outside)."""
        hp = _k.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, False, self.pid)
        if not hp:
            raise OSError("OpenProcess(%d) failed: %d" % (self.pid, ctypes.get_last_error()))
        try:
            buf = ctypes.create_string_buffer(n)
            got = ctypes.c_size_t()
            if not _k.ReadProcessMemory(hp, ctypes.c_void_p(addr), buf, n, ctypes.byref(got)):
                raise OSError("ReadProcessMemory(0x%X) failed: %d" % (addr, ctypes.get_last_error()))
            return buf.raw[:got.value]
        finally:
            _k.CloseHandle(hp)


def _quote(a: str) -> str:
    a = str(a)
    return '"%s"' % a if (" " in a or "\t" in a or not a) else a


def launch(args: List[str], desktop: Optional[str], *, env: Optional[Dict[str, str]] = None,
           cwd: Optional[str] = None, console: bool = False, out_path: Optional[Path] = None,
           unset: Iterable[str] = ()) -> Proc:
    """Start args[0] with args[1:] on `desktop` (a hidden desktop's name). Refused before anything starts when
    `desktop` is the interactive desktop (or None) and the MC's permission is not set. `out_path` (issue 3013): the
    child's stdout and stderr go to that file (refusals printed before a program opens its log). `unset`: variables
    removed from the child's environment."""
    desktops.check_desktop(desktop, "%s (shell/tests)" % os.path.basename(str(args[0])))
    cmd = " ".join(_quote(a) for a in args)
    full_env = dict(os.environ)
    full_env.update(env or {})
    for k in unset:
        for have in [h for h in full_env if h.upper() == k.upper()]:
            del full_env[have]
    block = "".join("%s=%s\0" % (k, v) for k, v in sorted(full_env.items(), key=lambda kv: kv[0].upper())) + "\0"
    env_buf = ctypes.create_unicode_buffer(block, len(block))
    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(si)
    si.lpDesktop = desktop
    si.dwFlags = STARTF_USESHOWWINDOW
    si.wShowWindow = SW_SHOWNOACTIVATE
    out_h = None
    if out_path is not None:
        sa = SECURITY_ATTRIBUTES(ctypes.sizeof(SECURITY_ATTRIBUTES), None, True)
        out_h = _k.CreateFileW(str(out_path), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, ctypes.byref(sa),
                               CREATE_ALWAYS, 0, None)
        if not out_h or out_h == INVALID_HANDLE_VALUE:
            raise OSError("CreateFileW(%s) failed: %d" % (out_path, ctypes.get_last_error()))
        si.dwFlags |= STARTF_USESTDHANDLES
        si.hStdOutput = si.hStdError = out_h
    pi = PROCESS_INFORMATION()
    cmd_buf = ctypes.create_unicode_buffer(cmd)
    flags = CREATE_UNICODE_ENVIRONMENT | (CREATE_NO_WINDOW if console else 0)
    try:
        if not _k.CreateProcessW(str(args[0]), cmd_buf, None, None, out_h is not None, flags, env_buf, cwd,
                                 ctypes.byref(si), ctypes.byref(pi)):
            raise OSError("CreateProcessW(%s) failed: %d" % (cmd, ctypes.get_last_error()))
    finally:
        if out_h is not None:
            _k.CloseHandle(out_h)
    return Proc(pi, cmd)


# ---------------------------------------------------------------------------------------------------------------
# One run: a private copy of the DLL + ini in run_dir (so triggers, ini edits and logs are per test), a section name
# of its own (no collision with another team's server), server and host on the hidden desktop.

class Run:
    def __init__(self, run_dir: Path, desktop: str, tag: str):
        self.dir = Path(run_dir)
        self.dir.mkdir(parents=True, exist_ok=True)
        self.desktop = desktop
        self.section = "Local\\VPShellTest_%d_%s" % (os.getpid(), tag)
        self.texname = "Local\\VPShellTestWorld_%d_%s" % (os.getpid(), tag)
        self.dll = self.dir / "shell_composite.dll"
        self.ini = self.dir / "shell_composite.ini"
        self.composite_log = self.dir / "composite.log"
        self.host_log = self.dir / "host.log"
        self.server_log = self.dir / "server.log"
        b = bin_dir()
        for name in ("shell_composite.dll", "shell_composite.pdb"):
            if (b / name).exists():
                shutil.copy2(b / name, self.dir / name)
        if not self.dll.exists():
            raise FileNotFoundError("%s missing: run `cmd /c shell\\build.cmd` first" % (b / "shell_composite.dll"))
        shutil.copy2(SHELL / "composite" / "shell_composite.ini", self.ini)
        self.server: Optional[Proc] = None
        self.host: Optional[Proc] = None

    # -- config ---------------------------------------------------------------------------------------------------
    def set_ini(self, **kv) -> None:
        """Set keys in [composite] of the run's ini (the DLL re-reads it when its timestamp changes)."""
        lines = self.ini.read_text(encoding="utf-8").splitlines()
        out, seen = [], set()
        for ln in lines:
            m = re.match(r"^\s*([A-Za-z0-9_]+)\s*=", ln)
            if m and m.group(1) in kv:
                out.append("%s=%s" % (m.group(1), kv[m.group(1)]))
                seen.add(m.group(1))
            else:
                out.append(ln)
        for k, v in kv.items():
            if k not in seen:
                out.append("%s=%s" % (k, v))
        old = self.ini.stat().st_mtime_ns
        self.ini.write_text("\n".join(out) + "\n", encoding="utf-8")
        # make sure the timestamp moves even on a coarse clock
        if self.ini.stat().st_mtime_ns == old:
            os.utime(self.ini, ns=(old + 20_000_000, old + 20_000_000))

    # -- processes --------------------------------------------------------------------------------------------------
    def start_server(self, *extra: str, seconds: float = 30, log: Optional[Path] = None, ready_s: float = 15.0) -> Proc:
        """Start the server and wait until it has published its first texture (issue 3013: the DLL maps the section at
        most once per second, so a host started before the section exists loses up to 1 s; under ASan the server needs
        ~0.7 s to its section and tests that count frames from the host's start failed). ready_s=0: do not wait."""
        lp = Path(log or self.server_log)
        n0 = len(lp.read_text(errors="replace").splitlines()) if lp.exists() else 0
        args = [str(bin_dir() / "shell_server.exe"), "--section", self.section, "--name", self.texname,
                "--seconds", str(seconds), "--log", str(lp), *extra]
        self.server = launch(args, self.desktop, console=True)
        t_end = time.time() + ready_s
        while ready_s > 0:
            if lp.exists() and any("texture generation" in ln for ln in lp.read_text(errors="replace").splitlines()[n0:]):
                break
            if not self.server.alive() or time.time() > t_end:
                raise TimeoutError("server not ready within %.0f s (alive %s): %s" % (ready_s, self.server.alive(), tail(lp, 8)))
            time.sleep(0.05)
        return self.server

    def start_host(self, *extra: str, seconds: float = 8, env: Optional[Dict[str, str]] = None) -> Proc:
        e = {"VPSHELL_LOG": str(self.composite_log), "VPSHELL_SECTION": self.section, "VPSHELL_SHOT_DIR": str(self.dir)}
        e.update(env or {})
        args = [str(bin_dir() / "test_host.exe"), "--dll", str(self.dll), "--seconds", str(seconds),
                "--log", str(self.host_log), "--pos", "0,0", *extra]
        self.host = launch(args, self.desktop, env=e)
        return self.host

    def stop(self) -> None:
        for p in (self.host, self.server):
            if p:
                p.kill()
                p.close()

    # -- triggers and logs ------------------------------------------------------------------------------------------
    def trigger(self, name: str, content: str = "") -> None:
        (self.dir / name).write_text(content, encoding="utf-8")

    def shot(self, timeout_s: float = 5.0) -> Path:
        """Ask the DLL for a back-buffer dump and wait for the BMP."""
        before = set(self.dir.glob("backbuffer_*.bmp"))
        self.trigger("shell_composite.shot")
        t_end = time.time() + timeout_s
        while time.time() < t_end:
            new = set(self.dir.glob("backbuffer_*.bmp")) - before
            if new:
                p = sorted(new)[-1]
                time.sleep(0.3)  # let the writer close it
                return p
            time.sleep(0.1)
        raise TimeoutError("no back-buffer dump within %.0f s (composite log tail: %s)" % (
            timeout_s, tail(self.composite_log, 8)))

    def log_lines(self, which: str = "composite") -> List[str]:
        p = {"composite": self.composite_log, "host": self.host_log, "server": self.server_log}[which]
        return p.read_text(encoding="utf-8", errors="replace").splitlines() if p.exists() else []

    def wait_log(self, pattern: str, which: str = "composite", timeout_s: float = 10.0, start: int = 0) -> str:
        """First line matching pattern (searching from line index `start`), waiting up to timeout_s."""
        rx = re.compile(pattern)
        t_end = time.time() + timeout_s
        while time.time() < t_end:
            for ln in self.log_lines(which)[start:]:
                if rx.search(ln):
                    return ln
            time.sleep(0.1)
        raise TimeoutError("no %r in %s log within %.0f s; tail: %s" % (pattern, which, timeout_s,
                                                                        tail(self.composite_log if which == "composite" else self.host_log, 10)))


def tail(p: Path, n: int) -> str:
    if not Path(p).exists():
        return "<no log>"
    return " | ".join(Path(p).read_text(encoding="utf-8", errors="replace").splitlines()[-n:])


def stats(lines: Iterable[str]) -> List[Dict[str, float]]:
    """The DLL's per-second `stats` lines as dicts of their numeric key=value fields (plus 'ins' and 'mode' text)."""
    rows = []
    for ln in lines:
        if " stats " not in ln:
            continue
        d: Dict[str, float] = {}
        for k, v in re.findall(r"(\w+)=([-\d\.]+)(?=\s|$)", ln):
            try:
                d[k] = float(v)
            except ValueError:
                pass
        for k in ("mode", "ins", "rule"):
            m = re.search(r"\b%s=(\S+)" % k, ln)
            if m:
                d[k] = m.group(1)  # type: ignore[assignment]
        d["disabled"] = 1.0 if "DISABLED" in ln else 0.0
        rows.append(d)
    return rows


# ---------------------------------------------------------------------------------------------------------------
# Back-buffer dumps (32-bit top-down BMP written by the DLL, BGRA)

class Image:
    def __init__(self, path: Path):
        data = Path(path).read_bytes()
        off = struct.unpack_from("<I", data, 10)[0]
        self.w, h = struct.unpack_from("<ii", data, 18)
        self.h = abs(h)
        self.top_down = h < 0
        self.px = data[off:off + self.w * self.h * 4]

    def at(self, x: int, y: int):
        if not self.top_down:
            y = self.h - 1 - y
        i = (y * self.w + x) * 4
        b, g, r = self.px[i], self.px[i + 1], self.px[i + 2]
        return r, g, b

    def region(self, x0: int, y0: int, x1: int, y1: int, step: int = 2):
        for y in range(y0, y1, step):
            for x in range(x0, x1, step):
                yield self.at(x, y)


def is_server(c) -> bool:
    """A pixel of the test server's world (shaders.hlsl PSServer): land greens/browns (G > R or close, G >= 70),
    water blues (B clearly above R and G), light hex edges. Not the host's clear grey (38,38,38), not its UI."""
    r, g, b = c
    land = g >= 70 and g >= r - 20 and b <= g
    water = b >= 80 and b > r + 40 and b > g + 25
    edge = r >= 100 and g >= 100 and b >= 90 and max(c) - min(c) <= 60
    return land or water or edge


def fraction(pixels, pred) -> float:
    n = k = 0
    for c in pixels:
        n += 1
        k += 1 if pred(c) else 0
    return k / n if n else 0.0
