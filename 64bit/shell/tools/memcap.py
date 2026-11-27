"""Run a command with a memory cap on it and every process it starts (issue 3013; COMMON rule 16 / EFFECTIVENESS E1:
a sanitizer run has a memory cap and prints progress).

    python shell/tools/memcap.py --mb 4096 -- python -m pytest shell/tests -v

This process puts itself into a Windows job object with JOB_OBJECT_LIMIT_JOB_MEMORY (committed memory of all processes in
the job together) and KILL_ON_JOB_CLOSE, then runs the command; children inherit the job. An allocation beyond the cap
fails in the process that asked (ASan reports it as an out-of-memory error, which the shell tests turn into a failure).
Prints the job's peak committed memory at the end. Exit code: the command's, or 2 when the job cannot be set up.
"""
from __future__ import annotations

import argparse
import ctypes
import subprocess
import sys
from ctypes import wintypes

JobObjectExtendedLimitInformation = 9
JOB_OBJECT_LIMIT_JOB_MEMORY = 0x00000200
JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE = 0x00002000


class IO_COUNTERS(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ("ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
                                                    "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]


class JOBOBJECT_BASIC_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_longlong), ("PerJobUserTimeLimit", ctypes.c_longlong),
                ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD)]


class JOBOBJECT_EXTENDED_LIMIT_INFORMATION(ctypes.Structure):
    _fields_ = [("BasicLimitInformation", JOBOBJECT_BASIC_LIMIT_INFORMATION), ("IoInfo", IO_COUNTERS),
                ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mb", type=int, required=True, help="cap on the committed memory of the whole process tree (MB)")
    ap.add_argument("command", nargs=argparse.REMAINDER)
    a = ap.parse_args()
    cmd = a.command[1:] if a.command[:1] == ["--"] else a.command
    if not cmd or a.mb <= 0:
        print("memcap: need --mb > 0 and a command after --", file=sys.stderr)
        return 2
    k = ctypes.WinDLL("kernel32", use_last_error=True)
    k.CreateJobObjectW.restype = wintypes.HANDLE
    k.GetCurrentProcess.restype = wintypes.HANDLE
    k.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    k.QueryInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p]
    k.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    job = k.CreateJobObjectW(None, None)
    info = JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
    info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_JOB_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
    info.JobMemoryLimit = a.mb << 20
    if not job or not k.SetInformationJobObject(job, JobObjectExtendedLimitInformation, ctypes.byref(info), ctypes.sizeof(info)) \
            or not k.AssignProcessToJobObject(job, k.GetCurrentProcess()):
        print("memcap: job object setup failed: %d" % ctypes.get_last_error(), file=sys.stderr)
        return 2
    print("memcap: job memory cap %d MB on %s" % (a.mb, " ".join(cmd)), flush=True)
    rc = subprocess.call(cmd)
    k.QueryInformationJobObject(job, JobObjectExtendedLimitInformation, ctypes.byref(info), ctypes.sizeof(info), None)
    print("memcap: exit %d, peak committed memory of the job %.0f MB (cap %d MB)" % (rc, info.PeakJobMemoryUsed / 1048576.0, a.mb),
          flush=True)
    return rc


if __name__ == "__main__":
    sys.exit(main())
