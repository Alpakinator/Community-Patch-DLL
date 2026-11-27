"""Ticket 0385: shell_composite.dll's default log (<VPSHELL_OUT_DIR or the DLL's folder>/shell_composite.log) starts per EXE
process. A log left by an earlier process is renamed to shell_composite.<its last write, yyyymmdd-hhmmss>[-n].log before
the new process opens the name; when the rename is refused (another process has the file open), the new process logs to
shell_composite.<pid>.log and says why; an empty old log is removed; VPSHELL_LOG (a path the caller names) still appends.

test_host.exe with the composite on the hidden desktop (rule 14), without VPSHELL_LOG unless a test sets it.
Needs C5_SHELL_BUILD (shell/build.cmd). Written 2026-11-16 for this repository (GPL-3.0-or-later).
"""
import os
import re
import time
from datetime import datetime
from pathlib import Path

import shellrun

HOST = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after", "--fake-exe")
BANNER = re.compile(r"==== shell_composite\.dll \(inline hooks, MinHook [\d.]+\) loaded into pid (\d+) ")


def start(run, out, extra_env=None, seconds=3):
    env = {"VPSHELL_OUT_DIR": str(out), "VPSHELL_SECTION": run.section, "VPSHELL_SHOT_DIR": str(run.dir)}
    env.update(extra_env or {})
    args = [str(shellrun.bin_dir() / "test_host.exe"), "--dll", str(run.dll), "--seconds", str(seconds),
            "--log", str(run.host_log), "--pos", "0,0", *HOST]
    unset = () if extra_env and "VPSHELL_LOG" in extra_env else ("VPSHELL_LOG",)
    run.host = shellrun.launch(args, run.desktop, env=env, unset=unset)
    assert run.host.wait(seconds + 30) == 0, shellrun.tail(run.host_log, 10)
    pid = run.host.pid
    run.host.close()
    run.host = None
    return pid


def banners(p):
    return [int(m.group(1)) for m in BANNER.finditer(Path(p).read_text(encoding="utf-8", errors="replace"))]


def kept(out):
    return sorted(p for p in Path(out).glob("shell_composite.*.log"))


def stamp_of(p):
    return datetime.fromtimestamp(os.stat(p).st_mtime).strftime("%Y%m%d-%H%M%S")


def test_old_log_is_renamed_and_the_new_one_holds_this_process_only(run):
    out = run.dir / "out"
    out.mkdir()
    old = out / "shell_composite.log"
    old.write_text("an earlier process's line\n", encoding="utf-8")
    t = time.mktime((2026, 11, 15, 22, 11, 7, 0, 0, -1))
    os.utime(old, (t, t))
    pid = start(run, out)
    text = old.read_text(encoding="utf-8")
    assert banners(old) == [pid] and "an earlier process's line" not in text, text[:400]
    renamed = out / "shell_composite.20261115-221107.log"
    assert kept(out) == [renamed], kept(out)
    assert renamed.read_text(encoding="utf-8") == "an earlier process's line\n"
    assert re.search(r"log: the previous log kept as .*shell_composite\.20261115-221107\.log$", text, re.M), text[:600]
    # a second process: the first one's log is kept the same way (its own write time), nothing is lost
    first = text
    st = stamp_of(old)
    pid2 = start(run, out)
    assert banners(old) == [pid2]
    names = {p.name for p in kept(out)}
    assert names == {"shell_composite.20261115-221107.log", "shell_composite.%s.log" % st}, names
    assert (out / ("shell_composite.%s.log" % st)).read_text(encoding="utf-8") == first


def test_a_taken_name_gets_a_number(run):
    out = run.dir / "out"
    out.mkdir()
    old = out / "shell_composite.log"
    old.write_text("old\n", encoding="utf-8")
    t = time.mktime((2026, 11, 16, 1, 2, 3, 0, 0, -1))
    os.utime(old, (t, t))
    (out / "shell_composite.20261116-010203.log").write_text("someone else's\n", encoding="utf-8")
    (out / "shell_composite.20261116-010203-2.log").write_text("and another\n", encoding="utf-8")
    pid = start(run, out)
    assert (out / "shell_composite.20261116-010203-3.log").read_text(encoding="utf-8") == "old\n"
    assert (out / "shell_composite.20261116-010203.log").read_text(encoding="utf-8") == "someone else's\n"
    assert (out / "shell_composite.20261116-010203-2.log").read_text(encoding="utf-8") == "and another\n"
    assert banners(old) == [pid]


def test_log_held_open_by_another_process_is_left_alone(run):
    """Negative: the old log is open in another process (no FILE_SHARE_DELETE, as the composite itself opens it): the
    rename is refused, the held file is not touched, and this process logs to shell_composite.<pid>.log, saying why."""
    out = run.dir / "out"
    out.mkdir()
    old = out / "shell_composite.log"
    old.write_text("held line\n", encoding="utf-8")
    with open(old, "a", encoding="utf-8") as held:          # the CRT opens without FILE_SHARE_DELETE
        pid = start(run, out)
        assert old.read_text(encoding="utf-8") == "held line\n"
    own = out / ("shell_composite.%d.log" % pid)
    assert own.exists(), sorted(p.name for p in out.iterdir())
    text = own.read_text(encoding="utf-8")
    assert banners(own) == [pid]
    assert re.search(r"log: .*shell_composite\.log could not be renamed \(error 32: another process has it open\): this "
                     r"process logs here$", text, re.M), text[:600]
    assert [p.name for p in kept(out)] == [own.name]       # nothing renamed


def test_empty_old_log_is_removed(run):
    out = run.dir / "out"
    out.mkdir()
    (out / "shell_composite.log").write_bytes(b"")
    pid = start(run, out)
    assert kept(out) == [] and banners(out / "shell_composite.log") == [pid]
    assert not re.search(r"\] log: ", (out / "shell_composite.log").read_text(encoding="utf-8"))


FOREIGN = ("shell_composite.12345.log", "shell_composite.lockstep.csv", "shell_composite.2026-bad.log",
           "shell_composite.20261101-000000-01.log", "shell_composite.20261101-000000-1.log", "other.log",
           "shell_composite.20261101-000000.txt")


def plant_kept(out, count):
    """count stamped kept logs, oldest first: 2026-11-01 00:00:00 + i minutes, every third one with a '-2' twin; and the
    foreign files a prune must never touch. Returns the stamped names, oldest first (stamp, then n)."""
    names = []
    for i in range(count):
        st = "202611%02d-%02d%02d00" % (1 + i // 60, (i // 60) % 24, i % 60)
        names.append("shell_composite.%s.log" % st)
        if i % 3 == 2:
            names.append("shell_composite.%s-2.log" % st)
    for n in names:
        (out / n).write_text(n + "\n", encoding="utf-8")
    for n in FOREIGN:
        (out / n).write_text("foreign\n", encoding="utf-8")
    return names


def stamped(out):
    return sorted(p.name for p in kept(out) if re.match(r"^shell_composite\.\d{8}-\d{6}(-[1-9]\d?)?\.log$", p.name)
                  and not p.name.endswith("-1.log"))


def test_kept_logs_limited_to_vpshell_keep_logs(run):
    """Ticket 0747 (D255 G5b): with VPSHELL_KEEP_LOGS=3 only the newest 3 stamped logs stay, the one this start kept
    among them; the older ones are deleted, the foreign names (a pid log, the CSV, malformed stamps, other files) stay."""
    out = run.dir / "out"
    out.mkdir()
    names = plant_kept(out, 12)   # 16 names with the '-2' twins
    old = out / "shell_composite.log"
    old.write_text("the last process's line\n", encoding="utf-8")
    t = time.mktime((2026, 11, 20, 8, 0, 0, 0, 0, -1))   # newer than every planted one
    os.utime(old, (t, t))
    pid = start(run, out, extra_env={"VPSHELL_KEEP_LOGS": "3"})
    want = sorted(names[-2:] + ["shell_composite.20261120-080000.log"])
    assert stamped(out) == want, stamped(out)
    for n in FOREIGN:
        assert (out / n).read_text(encoding="utf-8") == "foreign\n", n
    text = old.read_text(encoding="utf-8")
    assert banners(old) == [pid]
    assert re.search(r"log: older logs: %d deleted, 0 could not be deleted; the newest 3 kept \(VPSHELL_KEEP_LOGS; "
                     r"ticket 0747\)$" % (len(names) + 1 - 3), text, re.M), text[:900]


def test_kept_logs_default_ten(run):
    """Ticket 0747: without VPSHELL_KEEP_LOGS the limit is 10 (the proxy's default); a bad value gives 10 as well."""
    out = run.dir / "out"
    out.mkdir()
    names = plant_kept(out, 14)   # 18 names
    start(run, out)
    assert stamped(out) == sorted(names[-10:]), stamped(out)
    text = (out / "shell_composite.log").read_text(encoding="utf-8")
    assert re.search(r"log: older logs: 8 deleted, 0 could not be deleted; the newest 10 kept \(the default; "
                     r"ticket 0747\)$", text, re.M), text[:900]
    for bad in ("0", "101", "x"):
        start(run, out, extra_env={"VPSHELL_KEEP_LOGS": bad})
        text = (out / "shell_composite.log").read_text(encoding="utf-8")
        assert len(stamped(out)) == 10, (bad, stamped(out))
        assert "VPSHELL_KEEP_LOGS is not a whole number from 1 to 100" in text, (bad, text[:900])


def test_kept_logs_under_the_limit_untouched(run):
    """Ticket 0747 (negative): at or under the limit nothing is deleted, and the line says how many are kept."""
    out = run.dir / "out"
    out.mkdir()
    names = plant_kept(out, 2)   # 2 names
    start(run, out, extra_env={"VPSHELL_KEEP_LOGS": "2"})
    assert stamped(out) == sorted(names), stamped(out)
    text = (out / "shell_composite.log").read_text(encoding="utf-8")
    assert re.search(r"log: older logs: 2 kept, at most 2 \(VPSHELL_KEEP_LOGS; ticket 0747\)$", text, re.M), text[:900]


def test_vpshell_log_still_appends(run):
    """VPSHELL_LOG names a file the caller owns (the tests' per-run log): it is appended to, never renamed."""
    out = run.dir / "out"
    out.mkdir()
    own = run.dir / "mine.log"
    own.write_text("caller's line\n", encoding="utf-8")
    pid = start(run, out, extra_env={"VPSHELL_LOG": str(own)})
    text = own.read_text(encoding="utf-8")
    assert text.startswith("caller's line\n") and banners(own) == [pid], text[:300]
    assert list(out.glob("shell_composite*.log")) == []
