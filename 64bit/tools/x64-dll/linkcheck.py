"""Checks shared by the build wrappers (one-off 9023, auditor cycle 1 proposal A6, pattern P10: 23 incidents, 84 min).

A wrapper that links a binary and then copies or reports it must:
- fail when the link did not succeed: exit code non-zero, an error line in the link log, no artifact, or an artifact
  older than the link's start (a stale file from an earlier build that the failed link did not replace);
- fail when a sub-build it ran exits 0 but its output lacks the sub-build's own success line (`build_all: OK`,
  `build_proxy: OK`; I-0379: a wrapper printed "completed" with exit 0 after an out-of-memory link and copied the
  previous DLL);
- never copy a file it did not check, and print the SHA-256, size and timestamp of each file it copied.

lld-link writes nothing on success, so "the success line" of a raw link is: exit 0, no error line, a fresh artifact.
Used by tools/x64-dll/build.py, tools/x64-dll/build_all.py, server/build_server.py and proxy/build_proxy.py.
Tests: tools/x64-dll/tests/test_linkcheck.py (fake link logs and artifacts in temporary folders).
"""
from __future__ import annotations

import hashlib
import os
import re
import shutil
import stat
import subprocess
import sys
import time
from pathlib import Path
from typing import Iterable, List, Optional, Pattern, Tuple, Union

#: Lines that mean the link failed whatever its exit code: lld-link's and MSVC link's error lines, LLVM's fatal
#: errors, and the allocator's out-of-memory messages (I-0379).
LINK_ERROR_RE = re.compile(
    r"^(?:lld-link|lld|link(?:\.exe)?)\s*:\s*(?:fatal\s+)?error\b"   # lld-link: error: ... / LINK : fatal error LNK1104
    r"|\berror LNK\d+\b"                                              # foo.obj : error LNK2019: ...
    r"|^LLVM ERROR\b"
    r"|\bout of memory\b|\bstd::bad_alloc\b|\bnot enough space\b",
    re.I | re.M)
#: Seconds of clock difference allowed between the link's start and the artifact's mtime (file-system granularity).
MTIME_SLACK_S = 2.0


class LinkCheckError(Exception):
    pass


def link_failure(rc: int, log_text: str, artifact: Union[str, Path], started: float,
                 check_log: bool = True) -> Optional[str]:
    """None when the link succeeded; otherwise the reason. `started` is time.time() taken just before the link."""
    if rc != 0:
        return "linker exit code %d" % rc
    if check_log:
        m = LINK_ERROR_RE.search(log_text or "")
        if m:
            line = (log_text[log_text.rfind("\n", 0, m.start()) + 1:].split("\n", 1)[0]).strip()
            return "linker exit code 0 but the link log has an error line: %s" % line[:300]
    p = Path(artifact)
    if not p.is_file():
        return "linker exit code 0 but %s was not written" % p
    mt = p.stat().st_mtime
    if mt < started - MTIME_SLACK_S:
        return ("linker exit code 0 but %s is older than the link (written %s, link started %s): a stale file "
                "from an earlier build" % (p, _stamp(mt), _stamp(started)))
    return None


def require_link(rc: int, log_text: str, artifact: Union[str, Path], started: float, check_log: bool = True) -> None:
    why = link_failure(rc, log_text, artifact, started, check_log)
    if why:
        raise LinkCheckError(why)


def _stamp(t: float) -> str:
    return time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(t))


def sha256_of(path: Union[str, Path]) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def artifact_info(path: Union[str, Path]) -> dict:
    p = Path(path)
    st = p.stat()
    return {"path": str(p), "sha256": sha256_of(p), "size": st.st_size, "mtime": _stamp(st.st_mtime)}


def artifact_line(path: Union[str, Path], what: str = "artifact") -> str:
    i = artifact_info(path)
    return "%s: %s sha256 %s size %d mtime %s" % (what, i["path"], i["sha256"], i["size"], i["mtime"])


def make_writable(path: Union[str, Path]) -> None:
    """Ticket 0629: clear the read-only attribute (Windows) / add the owner's write bit of one file. Used on COPIES only:
    sources such as the shared pins folder C:/c5-64bit-work/keep/pins-0.1b stay read-only and are never written."""
    mode = os.stat(path).st_mode
    if not mode & stat.S_IWRITE:
        os.chmod(path, stat.S_IMODE(mode) | stat.S_IWRITE)


def copy_checked(files: Iterable[Tuple[Union[str, Path], bool]], dst_dir: Union[str, Path],
                 since: Optional[float] = None, say=print) -> List[dict]:
    """Copy (src, required) pairs into dst_dir. Checks everything before copying anything: a required source that
    is missing, or any source older than `since` (time.time(); None = no age check) raises LinkCheckError and
    nothing is copied. An optional source that is missing has its old copy in dst_dir removed (so dst_dir never
    holds a file of an earlier build next to this build's). Each copy is hash checked; one line per file is printed
    with its SHA-256, size and timestamp. Returns the artifact_info of each copy."""
    dst = Path(dst_dir)
    pairs = [(Path(s), bool(r)) for s, r in files]
    bad = []
    for s, req in pairs:
        if not s.is_file():
            if req:
                bad.append("missing %s" % s)
            continue
        if since is not None and s.stat().st_mtime < since - MTIME_SLACK_S:
            bad.append("%s is older than this build (written %s, build started %s)"
                       % (s, _stamp(s.stat().st_mtime), _stamp(since)))
    if bad:
        raise LinkCheckError("nothing copied into %s: %s" % (dst, "; ".join(bad)))
    dst.mkdir(parents=True, exist_ok=True)
    out = []
    for s, _req in pairs:
        d = dst / s.name
        if not s.is_file():
            if d.exists():
                make_writable(d)
                d.unlink()
                say("removed %s (an earlier build's; this build has no %s)" % (d, s.name))
            continue
        if d.exists():
            make_writable(d)   # ticket 0629: an earlier copy made read-only cannot be overwritten
        shutil.copy2(str(s), str(d))
        make_writable(d)       # ticket 0629: copy2 keeps a read-only source's attribute; the copy is ours to replace
        info = artifact_info(d)
        if info["sha256"] != sha256_of(s):
            raise LinkCheckError("copy of %s into %s does not match its source" % (s, dst))
        say("copied %s sha256 %s size %d mtime %s" % (d, info["sha256"], info["size"], info["mtime"]))
        out.append(info)
    return out


def run_sub_build(cmd: List[str], success: Union[str, Pattern], label: str, say=print,
                  env=None) -> Tuple[int, str, Optional[str]]:
    """Run a sub-build, streaming its output and keeping it. Returns (rc, output, None) when it exited 0 AND printed
    its success line (`success`, a regex matched per line); otherwise (rc, output, reason)."""
    pat = re.compile(success) if isinstance(success, str) else success
    p = subprocess.Popen([str(c) for c in cmd], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                         errors="replace", env=env)
    lines = []
    assert p.stdout is not None
    for line in p.stdout:
        sys.stdout.write(line)
        sys.stdout.flush()
        lines.append(line)
    rc = p.wait()
    text = "".join(lines)
    if rc != 0:
        return rc, text, "%s exited %d" % (label, rc)
    if not any(pat.search(x) for x in text.splitlines()):
        return rc, text, "%s exited 0 but printed no success line (/%s/)" % (label, pat.pattern)
    return rc, text, None
