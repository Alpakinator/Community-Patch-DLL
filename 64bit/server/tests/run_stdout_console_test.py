"""Issue 0417 (2026-11-16): SrvLog (server/src/srv_main.cpp) does not write to a stdout that is a console without a
window, and writes every other stdout as before.

  python server/tests/run_stdout_console_test.py --bin <build_server.py --out>/bin --out DIR [--timeout S]

Under the proxy, server.exe runs with CREATE_NO_WINDOW and no inherited handles: its stdout is a console that has no
window, which nobody can read, and every write to it waits for the console host. During a huge load such writes took
57-2,512 ms on the render thread (research/05-graphics-composition/terrain-bake-slow-first-use.md 5.2). The server now
skips that stdout and says so once in server.log ("log: stdout is a console without a window (CREATE_NO_WINDOW): lines
go to server.log only (issue 0417)").

The server runs as run_preinit_report_test.py's "lua" case (server.exe --remote with lua_dll missing: it logs, writes
its report and exits 1 before the attach; no proxy, no DLL load) in four arms, each in its own run folder DIR/<arm>/:
  file     stdout redirected to a file: the file has the server's lines; server.log has no skip line;
  pipe     stdout a pipe read by this test: the pipe has the server's lines; no skip line;
  hidden   CREATE_NO_WINDOW, no std handles (as the proxy starts it): server.log has the skip line exactly once and the
           server's lines (server.log is written as before);
  window   CREATE_NEW_CONSOLE on a hidden desktop this test creates (rule 14: no window on the interactive desktop): a
           console with a window, so stdout is written: no skip line in server.log.
In every arm: exit code 1 within the timeout and server.log has "remote: exit code 1 (". Exit 0 when every arm passes,
1 when one fails, 2 on a usage error. Start it through `perf/quietlock/quietlock.py run` (it starts server.exe).
"""
import argparse
import ctypes
import os
import shutil
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

CREATE_NO_WINDOW = 0x08000000
CREATE_NEW_CONSOLE = 0x00000010
STARTF_USESHOWWINDOW = 0x00000001
SW_HIDE = 0
SKIP_LINE = 'log: stdout is a console without a window (CREATE_NO_WINDOW): lines go to server.log only (issue 0417)'
EXIT_LINE = 'remote: exit code 1 ('
GENERIC_ALL = 0x10000000
WAIT_TIMEOUT = 0x102


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [('cb', wintypes.DWORD), ('lpReserved', wintypes.LPWSTR), ('lpDesktop', wintypes.LPWSTR),
                ('lpTitle', wintypes.LPWSTR), ('dwX', wintypes.DWORD), ('dwY', wintypes.DWORD),
                ('dwXSize', wintypes.DWORD), ('dwYSize', wintypes.DWORD), ('dwXCountChars', wintypes.DWORD),
                ('dwYCountChars', wintypes.DWORD), ('dwFillAttribute', wintypes.DWORD), ('dwFlags', wintypes.DWORD),
                ('wShowWindow', wintypes.WORD), ('cbReserved2', wintypes.WORD), ('lpReserved2', ctypes.c_void_p),
                ('hStdInput', wintypes.HANDLE), ('hStdOutput', wintypes.HANDLE), ('hStdError', wintypes.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [('hProcess', wintypes.HANDLE), ('hThread', wintypes.HANDLE), ('dwProcessId', wintypes.DWORD),
                ('dwThreadId', wintypes.DWORD)]


k32 = ctypes.WinDLL('kernel32', use_last_error=True)
u32 = ctypes.WinDLL('user32', use_last_error=True)
k32.CreateProcessW.argtypes = [wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wintypes.BOOL,
                               wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(STARTUPINFOW),
                               ctypes.POINTER(PROCESS_INFORMATION)]
k32.CreateProcessW.restype = wintypes.BOOL
k32.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
k32.WaitForSingleObject.restype = wintypes.DWORD
k32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
k32.TerminateProcess.argtypes = [wintypes.HANDLE, wintypes.UINT]
k32.CloseHandle.argtypes = [wintypes.HANDLE]
u32.CreateDesktopW.restype = wintypes.HANDLE
u32.CreateDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
                               ctypes.c_void_p]
u32.CloseDesktop.argtypes = [wintypes.HANDLE]


def write_cfg(run: Path, b: Path):
    cfg = {'run_dir': str(run), 'game_dir': str(run), 'dll': str(b / 'CvGameCore_Expansion2.dll'),
           'lua_dll': str(run / 'no-such-lua51_x64.dll'), 'db_dll': str(b / 'CvGameDatabase_x64.dll'),
           'loc_dll': str(b / 'CvLocalization_x64.dll'), 'loc_db': '', 'dll_logs': 0, 'fail_on_unexpected': 1,
           'connect_timeout_ms': 1500}
    path = run / 'server.cfg'
    path.write_text('# issue 0417 stdout test (server/tests/run_stdout_console_test.py)\n' +
                    ''.join('%s=%s\n' % (k, v) for k, v in cfg.items()), encoding='utf-8')
    return path


def raw_start(cmd, cwd, flags, desktop=None, timeout=30.0):
    """CreateProcessW without inherited handles (as the proxy starts the server); returns (exit code or None, seconds)."""
    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(si)
    if desktop:
        si.lpDesktop = desktop
        si.dwFlags = STARTF_USESHOWWINDOW
        si.wShowWindow = SW_HIDE
    pi = PROCESS_INFORMATION()
    line = ctypes.create_unicode_buffer(subprocess.list2cmdline([str(c) for c in cmd]))
    t0 = time.time()
    if not k32.CreateProcessW(None, line, None, None, False, flags, None, str(cwd), ctypes.byref(si), ctypes.byref(pi)):
        raise OSError('CreateProcessW failed: %d' % ctypes.get_last_error())
    try:
        if k32.WaitForSingleObject(pi.hProcess, int(timeout * 1000)) == WAIT_TIMEOUT:
            k32.TerminateProcess(pi.hProcess, 99)
            k32.WaitForSingleObject(pi.hProcess, 5000)
            return None, time.time() - t0
        rc = wintypes.DWORD()
        k32.GetExitCodeProcess(pi.hProcess, ctypes.byref(rc))
        return rc.value, time.time() - t0
    finally:
        k32.CloseHandle(pi.hThread)
        k32.CloseHandle(pi.hProcess)


def run_arm(arm, b: Path, out: Path, timeout: float, desktop: str):
    run = out / arm
    if run.exists():
        shutil.rmtree(run)
    run.mkdir(parents=True)
    cfg = write_cfg(run, b)
    section = 'Local\\VP64StdoutTest_%d_%s' % (os.getpid(), arm)
    cmd = [b / 'server.exe', '--remote', '--section', section, '--config', cfg]
    captured = None
    t0 = time.time()
    if arm == 'file':
        with (run / 'server-stdout.txt').open('w', encoding='utf-8') as so:
            p = subprocess.Popen([str(c) for c in cmd], stdout=so, stderr=subprocess.STDOUT, cwd=str(run),
                                 creationflags=CREATE_NO_WINDOW)
            try:
                rc = p.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                p.kill()
                rc = None
        captured = (run / 'server-stdout.txt').read_text(encoding='utf-8', errors='replace')
        dt = time.time() - t0
    elif arm == 'pipe':
        p = subprocess.Popen([str(c) for c in cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=str(run),
                             creationflags=CREATE_NO_WINDOW)
        try:
            data, _ = p.communicate(timeout=timeout)
            rc = p.returncode
        except subprocess.TimeoutExpired:
            p.kill()
            data, _ = p.communicate()
            rc = None
        captured = data.decode('utf-8', errors='replace')
        dt = time.time() - t0
    elif arm == 'hidden':
        rc, dt = raw_start(cmd, run, CREATE_NO_WINDOW, timeout=timeout)
    else:   # window: a console with a window, on the hidden desktop
        rc, dt = raw_start(cmd, run, CREATE_NEW_CONSOLE, desktop=desktop, timeout=timeout)
    log = run / 'server.log'
    ltext = log.read_text(encoding='utf-8', errors='replace') if log.exists() else ''
    fails = []
    if rc is None:
        fails.append('no exit within %.0f s (killed)' % timeout)
    elif rc != 1:
        fails.append('exit code %d (0x%08X), expected 1' % (rc, rc & 0xFFFFFFFF))
    if EXIT_LINE not in ltext:
        fails.append('server.log has no "%s"' % EXIT_LINE)
    skips = ltext.count(SKIP_LINE)
    if arm == 'hidden':
        if skips != 1:
            fails.append('server.log has the skip line %d times, expected once' % skips)
    elif skips:
        fails.append('server.log has the skip line (%d), expected none: stdout is %s' % (skips, arm))
    if captured is not None and EXIT_LINE not in captured:
        fails.append('the %s stdout has no "%s" (%d bytes)' % (arm, EXIT_LINE, len(captured)))
    print('%-7s %s  exit %s in %.1f s; skip lines %d; stdout %s' % (
        arm, 'PASS' if not fails else 'FAIL', rc, dt, skips,
        '-' if captured is None else '%d bytes' % len(captured)), flush=True)
    for f in fails:
        print('        - ' + f, flush=True)
    return not fails


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--bin', type=Path, required=True, help='server/build_server.py --out <dir>/bin')
    ap.add_argument('--out', type=Path, required=True, help='scratch folder outside the repository')
    ap.add_argument('--timeout', type=float, default=30.0)
    a = ap.parse_args()
    b = a.bin.resolve()
    if not (b / 'server.exe').is_file():
        print('run_stdout_console_test: %s has no server.exe' % b)
        return 2
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    name = 'C5STDOUT_%d' % os.getpid()
    hdesk = u32.CreateDesktopW(name, None, None, 0, GENERIC_ALL, None)
    if not hdesk:
        print('run_stdout_console_test: CreateDesktopW failed: %d (nothing started)' % ctypes.get_last_error())
        return 2
    try:
        print('server.exe %s; hidden desktop %s' % (b / 'server.exe', name), flush=True)
        ok = [run_arm(arm, b, out, a.timeout, name) for arm in ('file', 'pipe', 'hidden', 'window')]
    finally:
        u32.CloseDesktop(hdesk)
    print('run_stdout_console_test: %d arms, %d passed, %d failed: %s' % (len(ok), sum(ok), len(ok) - sum(ok),
                                                                          'PASS' if all(ok) else 'FAIL'), flush=True)
    return 0 if all(ok) else 1


if __name__ == '__main__':
    sys.exit(main())
