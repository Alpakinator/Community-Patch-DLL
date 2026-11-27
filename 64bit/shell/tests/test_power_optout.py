"""Bug 0118 (tracker 4000, 2026-11-13): shell_server.exe opts out of Windows power throttling at start-up.

Without the opt-out, Windows 11 stops honouring timeBeginPeriod(1) of a process without a visible window a few seconds
after it starts, and every wait on the system tick runs at 64 Hz (server/tests/run_power_test.py --rate:
Sleep(1) loop 670 Hz for ~3 s, then 64 Hz; with the opt-out 666 Hz throughout; 2026-11-13, workstation). The test
starts the server on the hidden desktop and reads its power-throttling masks from outside
(GetProcessInformation(ProcessPowerThrottling) on the process handle): control 0x5 (EXECUTION_SPEED |
IGNORE_TIMER_RESOLUTION), state 0x0. The unfixed server reads back control 0x0 state 0x0 and fails here.
"""
import ctypes
import time
from ctypes import wintypes

import shellrun

_k = ctypes.WinDLL("kernel32", use_last_error=True)
PROCESS_POWER_THROTTLING = 4   # PROCESS_INFORMATION_CLASS ProcessPowerThrottling (processthreadsapi.h)


class PROCESS_POWER_THROTTLING_STATE(ctypes.Structure):
    _fields_ = [("Version", wintypes.ULONG), ("ControlMask", wintypes.ULONG), ("StateMask", wintypes.ULONG)]


def power_state(h):
    st = PROCESS_POWER_THROTTLING_STATE(1, 0, 0)
    _k.GetProcessInformation.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    if not _k.GetProcessInformation(h, PROCESS_POWER_THROTTLING, ctypes.byref(st), ctypes.sizeof(st)):
        raise OSError("GetProcessInformation failed: %d" % ctypes.get_last_error())
    return st.ControlMask, st.StateMask


def test_shell_server_opts_out_of_power_throttling(tmp_path, hidden_desktop, built):
    log = tmp_path / "shell_server.log"
    p = shellrun.launch([str(built / "shell_server.exe"), "--section", "C5PowerTest%d" % time.time_ns(),
                         "--name", "C5PowerTex%d" % time.time_ns(), "--seconds", "6", "--log", str(log)],
                        hidden_desktop, console=True)
    try:
        deadline = time.time() + 10
        while time.time() < deadline and not (log.exists() and "shell_server start" in log.read_text(errors="replace")):
            time.sleep(0.1)
        time.sleep(0.5)
        assert p.alive(), "shell_server.exe ended early (exit %s); log: %s" % (
            p.wait(0), log.read_text(errors="replace") if log.exists() else "none")
        control, state = power_state(p.h)
        text = log.read_text(errors="replace")
        assert (control, state) == (0x5, 0x0), (
            "FAIL: shell_server.exe start-up did not apply the opt-out: control 0x%X state 0x%X (expected 0x5 0x0)"
            % (control, state))
        assert "power throttling opt-out (bug 0118)" in text and "reads back control 0x5 state 0x0" in text, \
            "FAIL: no opt-out line in shell_server.log"
    finally:
        p.wait(15)
        p.kill()
        p.close()
