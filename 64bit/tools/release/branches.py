"""Make the fork branches of a release from the carry series (issue 3505; contract: tools/release/README.md section 3).

    python -I tools/release/branches.py --fork <clone> --base <tag or commit> --series tools/release/series/carry.tsv
        --name <suffix> [--out <dir>] [--push] [--replace] [--compare-i0 <rev>] [--compare-x64 <rev>] [--no-fetch]

Makes two new branches in the fork clone:
- i0-release-<suffix>  = base + every `i0` row of the series, in file order;
- x64-release-<suffix> = base + every `x64` row, in file order, then i0-release-<suffix> merged in (--no-ff), as every
  x64 pin took i0-ship (deps/dll.lock).

Each row is cherry-picked with -x semantics (author kept, "(cherry picked from commit ...)" added). The picks are made
with `git merge-tree --write-tree` and `git commit-tree`: no worktree, no checkout, nothing in the clone's working tree
changes. A row is
- dropped when the base already has its change: an `upstream` commit of the row is an ancestor of the base, an
  `upstream` pull request number appears in the base's history as "(#N)" or "pull request #N", a commit of the base has
  the row's patch id, or the pick changes nothing;
- skipped when it is a `fix` whose pick conflicts, or when a row it `needs` was skipped;
- a stop (exit 10) when a `port` or `identity` row conflicts, when the merge of i0 into x64 conflicts and no `merge`
  row resolves it, when a commit of the series or the base is not in the clone, or when a branch of that name exists
  already (without --replace).
A row with branch `merge` (ticket 0639) is not picked: it names a pushed two-parent merge commit R that resolves a
conflict of the i0 merge into x64. Each conflicted file of that merge takes R's version only when its three blobs
(the merge base's, the x64 side's, the i0 side's) equal R's own (R's merge base, first parent, second parent): an
exact replay of a resolution made once by hand. A conflicted file that no `merge` row resolves this way stays a stop
naming the file and the blob that differs. A series without `merge` rows behaves as before.
Branches are written only when both are complete. --push pushes both to the clone's `origin` (new branches only, never
a force push). <out>/branches.json records every row (applied, dropped, skipped, with the new commit) and the result.

Exit codes: 0 done; 10 stop (one line `take_release: STOP branches (CL step 3): <what>; to unblock: <what>`);
1 failure (git failed in a way the tool does not expect); 2 bad arguments or series file.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

CLASSES = ("port", "identity", "fix")
BRANCHES = ("i0", "x64", "merge")
EXIT_STOP = 10
NAME_RE = re.compile(r"^[A-Za-z0-9._-]+$")
HEX_RE = re.compile(r"^[0-9a-fA-F]{7,40}$")
PR_RE = re.compile(r"^#(\d+)$")
PR_IN_SUBJECT = re.compile(r"\(#(\d+)\)|pull request #(\d+)", re.I)
TRAILER_RE = re.compile(r"^([A-Za-z0-9-]+: .*|\(cherry picked from commit [0-9a-f]+\))$")
DEFAULT_OUT_ROOT = "C:/c5-64bit-work/3500/releases"


class Stop(Exception):
    def __init__(self, what, unblock):
        super().__init__(what)
        self.what = what
        self.unblock = unblock


class Failure(Exception):
    pass


class Git:
    def __init__(self, repo):
        self.repo = str(repo)
        self.env = dict(os.environ, GIT_TERMINAL_PROMPT="0", LC_ALL="C", LANGUAGE="C")

    def run(self, *args, input=None, check=True, env=None):
        p = subprocess.run(["git", "-C", self.repo, *args], input=input, capture_output=True, text=True,
                           encoding="utf-8", errors="replace", env=dict(self.env, **(env or {})))
        if check and p.returncode != 0:
            raise Failure("git %s failed (rc %d): %s" % (" ".join(args), p.returncode, p.stderr.strip()[-2000:]))
        return p

    def out(self, *args, **kw):
        return self.run(*args, **kw).stdout.strip()

    def commit_of(self, rev):
        p = self.run("rev-parse", "--verify", "--quiet", rev + "^{commit}", check=False)
        return p.stdout.strip() if p.returncode == 0 else None

    def tree_of(self, rev):
        return self.out("rev-parse", rev + "^{tree}")

    def is_ancestor(self, a, b):
        return self.run("merge-base", "--is-ancestor", a, b, check=False).returncode == 0


class Row:
    def __init__(self, line_no, cls, branch, commit, subject, upstream, needs):
        self.line_no = line_no
        self.cls = cls
        self.branch = branch
        self.commit = commit
        self.subject = subject
        self.upstream = upstream
        self.needs = needs
        self.status = "pending"
        self.reason = ""
        self.new_commit = None
        self.warnings = []
        self.pid = None

    def as_json(self):
        return {"line": self.line_no, "class": self.cls, "branch": self.branch, "commit": self.commit,
                "subject": self.subject, "upstream": self.upstream, "needs": self.needs, "status": self.status,
                "reason": self.reason, "new_commit": self.new_commit, "warnings": self.warnings}


def parse_series(path):
    """Rows of a series file; raises ValueError naming the line on a malformed row."""
    rows = []
    text = Path(path).read_text(encoding="utf-8")
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.rstrip("\r")
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        f = line.split("\t")
        if len(f) not in (5, 6):
            raise ValueError("%s:%d: %d tab-separated fields, expected 5 or 6" % (path, n, len(f)))
        cls, branch, commit, subject, upstream = (x.strip() for x in f[:5])
        needs = [x.strip() for x in f[5].split(",") if x.strip()] if len(f) == 6 else []
        if cls not in CLASSES:
            raise ValueError("%s:%d: class %r is not one of %s" % (path, n, cls, ", ".join(CLASSES)))
        if branch not in BRANCHES:
            raise ValueError("%s:%d: branch %r is not one of %s" % (path, n, branch, ", ".join(BRANCHES)))
        if not re.fullmatch(r"[0-9a-f]{40}", commit):
            raise ValueError("%s:%d: commit %r is not a full lowercase hash" % (path, n, commit))
        ups = [] if upstream in ("", "-") else upstream.split()
        for u in ups:
            if not (HEX_RE.match(u) or PR_RE.match(u)):
                raise ValueError("%s:%d: upstream token %r is neither a commit hash nor #N" % (path, n, u))
        if branch == "merge" and ups:
            raise ValueError("%s:%d: a merge row resolves the i0 merge and has no upstream (write -)" % (path, n))
        for x in needs:
            if not HEX_RE.match(x):
                raise ValueError("%s:%d: needs token %r is not a commit hash prefix" % (path, n, x))
        rows.append(Row(n, cls, branch, commit, subject, ups, needs))
    seen = {}
    for r in rows:
        if r.commit in seen:
            raise ValueError("%s:%d: commit %s already on line %d" % (path, r.line_no, r.commit, seen[r.commit]))
        seen[r.commit] = r.line_no
    for i, r in enumerate(rows):
        for x in r.needs:
            earlier = [q for q in rows[:i] if q.commit.startswith(x.lower())]
            if len(earlier) != 1:
                raise ValueError("%s:%d: needs %s matches %d earlier rows (must be exactly one)"
                                 % (path, r.line_no, x, len(earlier)))
    return rows


def pick_message(msg, commit):
    """The message `git cherry-pick -x` writes: the original plus the cherry-picked line, directly under a trailer
    block, otherwise after a blank line."""
    body = msg.rstrip()
    last = body.split("\n\n")[-1].splitlines() if body else []
    line = "(cherry picked from commit %s)" % commit
    if last and len(body.split("\n\n")) > 1 and all(TRAILER_RE.match(x) for x in last):
        return body + "\n" + line + "\n"
    return body + "\n\n" + line + "\n"


def merge_tree(git, base, ours, theirs):
    """(rc, tree, conflicted files). rc 0 clean, 1 conflicts."""
    args = ["merge-tree", "--write-tree", "--name-only", "--no-messages"]
    if base:
        args.append("--merge-base=" + base)
    p = git.run(*args, ours, theirs, check=False)
    if p.returncode not in (0, 1):
        raise Failure("git merge-tree failed (rc %d): %s" % (p.returncode, p.stderr.strip()[-2000:]))
    lines = p.stdout.splitlines()
    tree = lines[0].strip() if lines else ""
    files = []
    for x in lines[1:]:
        if not x.strip():
            break
        files.append(x.strip())
    return p.returncode, tree, sorted(set(files))


def blob_at(git, rev, path):
    """(mode, blob) of path in rev, or (None, None) when rev has no such file."""
    out = git.out("ls-tree", "-z", rev, "--", path).rstrip("\0")
    if not out:
        return None, None
    meta, _name = out.split("\t", 1)
    mode, _type, blob = meta.split()
    return mode, blob


def resolve_merge(git, merge_rows, all_rows, tree, files, x64_head, i0_head):
    """The tree of the i0 merge into x64 with its conflicted files taken from the `merge` rows (ticket 0639): a file
    takes R's version only when its blobs at the merge base, the x64 side and the i0 side equal R's at R's merge base,
    first parent and second parent. Raises Stop naming every file no row resolves."""
    base = git.out("merge-base", x64_head, i0_head)
    usable = []
    for r in merge_rows:
        skipped_needs = [q.commit[:9] for q in all_rows for x in r.needs
                         if q.commit.startswith(x.lower()) and q.status == "skipped"]
        if skipped_needs:
            r.status, r.reason = "skipped", "needs %s, which was skipped" % ", ".join(skipped_needs)
            continue
        parents = git.out("log", "-1", "--format=%P", r.commit).split()
        if len(parents) != 2:
            raise Stop("merge row %s (series line %d) is not a merge of two parents" % (r.commit[:9], r.line_no),
                       "name a merge commit (x64 side first parent, i0 side second) in a merge row")
        usable.append((r, parents, git.out("merge-base", parents[0], parents[1])))
    resolved, unresolved = {}, []
    for f in files:
        mine = (blob_at(git, base, f)[1], blob_at(git, x64_head, f)[1], blob_at(git, i0_head, f)[1])
        why = []
        for r, parents, rbase in usable:
            theirs = (blob_at(git, rbase, f)[1], blob_at(git, parents[0], f)[1], blob_at(git, parents[1], f)[1])
            if theirs == mine:
                resolved[f] = (r, blob_at(git, r.commit, f))
                break
            sides = [s for s, a, b in zip(("merge base", "x64 side", "i0 side"), mine, theirs) if a != b]
            why.append("%s: %s blob differs" % (r.commit[:9], ", ".join(sides)))
        if f not in resolved:
            unresolved.append("%s (%s)" % (f, "; ".join(why) if why else "no merge row"))
    if unresolved:
        for r, _p, _b in usable:
            r.status, r.reason = "conflict", "does not resolve: %s" % ", ".join(unresolved)
        raise Stop("the merge of the i0 branch into the x64 branch conflicts: %s" % ", ".join(unresolved),
                   "resolve the merge once by hand: a merge commit of the x64 side (first parent) and the i0 side "
                   "(second parent) pushed to the fork, named in a `merge` row of a release copy of the series "
                   "(tools/release/README.md section 3), then rerun with --series <copy>")
    for r, _p, _b in usable:
        mine = sorted(f for f, (q, _) in resolved.items() if q is r)
        if mine:
            r.status, r.reason = "applied", "resolved %s in the i0 merge" % ", ".join(mine)
        else:
            r.status, r.reason = "dropped", "the i0 merge has no conflict this row resolves"
    idx = Path(os.environ.get("TEMP") or os.environ.get("TMP") or ".") / ("branches-index-%d" % os.getpid())
    env = {"GIT_INDEX_FILE": str(idx)}
    try:
        git.run("read-tree", tree, env=env)
        for f, (_r, (mode, blob)) in sorted(resolved.items()):
            if blob is None:
                git.run("update-index", "--force-remove", "--", f, env=env)
            else:
                git.run("update-index", "--add", "--cacheinfo", "%s,%s,%s" % (mode, blob, f), env=env)
        out = git.out("write-tree", env=env)
    finally:
        try:
            idx.unlink()
        except OSError:
            pass
    return out, sorted(resolved)


def author_env(git, commit):
    name, email, date = git.out("log", "-1", "--format=%an%n%ae%n%ad", "--date=raw", commit).split("\n")
    return {"GIT_AUTHOR_NAME": name, "GIT_AUTHOR_EMAIL": email, "GIT_AUTHOR_DATE": date}


def base_history_prs(git, base):
    prs = set()
    for s in git.out("log", "--format=%s", base).splitlines():
        for m in PR_IN_SUBJECT.finditer(s):
            prs.add(m.group(1) or m.group(2))
    return prs


def patch_ids(git, revs):
    """{commit: patch id} of the given commits (a merge has none)."""
    if not revs:
        return {}
    log = git.run("log", "--no-walk=unsorted", "--stdin", "-p", "--format=commit %H",
                  input="\n".join(revs) + "\n").stdout
    out = git.run("patch-id", "--stable", input=log).stdout
    ids = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 2:
            ids[parts[1]] = parts[0]
    return ids


def apply_rows(git, rows, start, base, base_prs, base_pids, branch_label, all_rows):
    head = start
    for r in rows:
        skipped_needs = [q.commit[:9] for q in all_rows for x in r.needs
                         if q.commit.startswith(x.lower()) and q.status == "skipped"]
        if skipped_needs:
            r.status, r.reason = "skipped", "needs %s, which was skipped" % ", ".join(skipped_needs)
            continue
        dropped = None
        for u in r.upstream:
            m = PR_RE.match(u)
            if m:
                if m.group(1) in base_prs:
                    dropped = "the base's history names upstream pull request #%s" % m.group(1)
                    break
            else:
                uc = git.commit_of(u)
                if uc and git.is_ancestor(uc, base):
                    dropped = "the base contains upstream %s" % uc[:9]
                    break
        if not dropped and r.pid and r.pid in base_pids:
            dropped = "the base has a commit with the same patch id (%s)" % base_pids[r.pid][:9]
        if dropped:
            r.status, r.reason = "dropped", dropped
            continue
        parents = git.out("log", "-1", "--format=%P", r.commit).split()
        if len(parents) != 1:
            raise Stop("row %d (%s) is a merge or root commit, not a single change" % (r.line_no, r.commit[:9]),
                       "list the merge's own commits in the series instead")
        rc, tree, files = merge_tree(git, parents[0], head, r.commit)
        if rc == 1:
            what = "conflict in %s" % ", ".join(files or ["(no file named)"])
            if r.cls == "fix":
                r.status, r.reason = "skipped", what
                continue
            r.status, r.reason = "conflict", what
            raise Stop("%s row %s %s (%s, series line %d) conflicts on %s with the base: %s"
                       % (r.cls, branch_label, r.commit[:9], r.subject[:60], r.line_no, branch_label, what),
                       "pick %s by hand onto the branch's last applied commit %s, resolve %s, and put the resolved "
                       "commit's hash in this row of a release copy of the series (then rerun with --series <copy>)"
                       % (r.commit[:9], head[:9], ", ".join(files)))
        if tree == git.tree_of(head):
            r.status, r.reason = "dropped", "the pick changes nothing (the base already has the change)"
            continue
        msg = pick_message(git.run("log", "-1", "--format=%B", r.commit).stdout, r.commit)
        new = git.out("commit-tree", tree, "-p", head, input=msg, env=author_env(git, r.commit))
        r.status, r.new_commit = "applied", new
        head = new
    return head


def ref_exists(git, name):
    return git.commit_of("refs/heads/" + name)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--fork", required=True, help="the fork clone (a git repository of your own)")
    ap.add_argument("--base", required=True, help="the release tag or commit the branches start from")
    ap.add_argument("--series", required=True, help="the series file (tools/release/series/carry.tsv)")
    ap.add_argument("--name", required=True, help="branch suffix: i0-release-<name>, x64-release-<name>")
    ap.add_argument("--out", help="folder for branches.json (default %s/<name>/branches)" % DEFAULT_OUT_ROOT)
    ap.add_argument("--push", action="store_true", help="push both new branches to the clone's origin")
    ap.add_argument("--replace", action="store_true",
                    help="move local branches of these names that exist already (never on the remote)")
    ap.add_argument("--compare-i0", metavar="REV", help="report whether i0-release's tree equals REV's tree")
    ap.add_argument("--compare-x64", metavar="REV", help="report whether x64-release's tree equals REV's tree")
    ap.add_argument("--no-fetch", action="store_true", help="do not try `git fetch origin <commit>` for a missing row")
    a = ap.parse_args(argv)

    if not NAME_RE.match(a.name):
        print("branches: --name %r: only letters, digits, '.', '_' and '-'" % a.name, file=sys.stderr)
        return 2
    try:
        rows = parse_series(a.series)
    except (OSError, ValueError) as e:
        print("branches: series: %s" % e, file=sys.stderr)
        return 2
    out = Path(a.out) if a.out else Path(DEFAULT_OUT_ROOT) / a.name / "branches"
    out.mkdir(parents=True, exist_ok=True)
    git = Git(a.fork)
    names = {"i0": "i0-release-" + a.name, "x64": "x64-release-" + a.name}
    t0 = time.time()
    rec = {"tool": "tools/release/branches.py", "started": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
           "fork": str(Path(a.fork)), "base": a.base, "series": str(a.series),
           "series_sha256": hashlib.sha256(Path(a.series).read_bytes()).hexdigest(), "name": a.name,
           "branches": {}, "compare": {}, "push": {}, "stop": None, "result": "running"}

    def finish(result, code):
        rec["result"] = result
        rec["rows"] = [r.as_json() for r in rows]
        rec["counts"] = {s: sum(1 for r in rows if r.status == s)
                         for s in ("applied", "dropped", "skipped", "conflict", "pending")}
        rec["seconds"] = round(time.time() - t0, 1)
        (out / "branches.json").write_text(json.dumps(rec, indent=2) + "\n", encoding="utf-8")
        return code

    try:
        if git.run("rev-parse", "--git-dir", check=False).returncode != 0:
            raise Stop("%s is not a git repository" % a.fork, "clone the fork there or pass --fork <clone>")
        base = git.commit_of(a.base)
        if not base:
            raise Stop("base %s is not in the fork clone" % a.base,
                       "fetch it (git -C <clone> fetch upstream --tags) or pass the right --base")
        rec["base_commit"] = base
        missing = [r for r in rows if not git.commit_of(r.commit)]
        if missing and not a.no_fetch and git.run("remote", "get-url", "origin", check=False).returncode == 0:
            git.run("fetch", "-q", "origin", *[r.commit for r in missing], check=False)
            missing = [r for r in rows if not git.commit_of(r.commit)]
        if missing:
            for r in missing:
                r.status, r.reason = "missing", "commit not in the clone"
            raise Stop("%d series commit(s) not in the fork clone: %s"
                       % (len(missing), ", ".join("%s (line %d)" % (r.commit[:9], r.line_no) for r in missing)),
                       "fetch the fork's branches into the clone (git -C <clone> fetch origin) or correct the series. "
                       "If fetching does not bring them, the usual cause is a release copy's commit (a port or a "
                       "hand resolution) that was never pushed before the clone that made it was deleted: name its "
                       "pushed pick in the series instead, or push the commit from a clone that still has it "
                       "(tools/release/README.md section 3, rule of issue 3510)")
        for r in rows:
            s = git.out("log", "-1", "--format=%s", r.commit)
            if s != r.subject:
                r.warnings.append("subject in the commit: %r" % s)
        for k, n in names.items():
            old = ref_exists(git, n)
            if old and not a.replace:
                raise Stop("branch %s exists already in the clone (at %s)" % (n, old[:9]),
                           "pass --replace to move the local branch, or choose another --name")
        # what the base already has: pull request numbers in its history, patch ids of its commits that no row has
        base_prs = base_history_prs(git, base) if any(PR_RE.match(u) for r in rows for u in r.upstream) else set()
        picked = [r for r in rows if r.branch != "merge"]
        own = git.out("rev-list", "--no-merges", base, "--not", *sorted({r.commit for r in picked})).split()
        rec["base_commits_compared"] = len(own)
        base_pids = {pid: c for c, pid in patch_ids(git, own).items()}
        row_pids = patch_ids(git, [r.commit for r in picked])
        for r in picked:
            r.pid = row_pids.get(r.commit)

        i0_rows = [r for r in rows if r.branch == "i0"]
        x64_rows = [r for r in rows if r.branch == "x64"]
        merge_rows = [r for r in rows if r.branch == "merge"]
        i0_head = apply_rows(git, i0_rows, base, base, base_prs, base_pids, "i0", rows)
        x64_head = apply_rows(git, x64_rows, base, base, base_prs, base_pids, "x64", rows)
        rc, tree, files = merge_tree(git, None, x64_head, i0_head)
        resolved = []
        if rc == 1 and not merge_rows:
            raise Stop("the merge of %s into %s conflicts: %s" % (names["i0"], names["x64"], ", ".join(files)),
                       "resolve the x64 rows against the i0 rows (a release copy of the series with a resolved x64 "
                       "commit), or resolve the merge once by hand and name that merge commit in a `merge` row "
                       "(tools/release/README.md section 3), then rerun with --series <copy>")
        if rc == 1:
            tree, resolved = resolve_merge(git, merge_rows, rows, tree, files, x64_head, i0_head)
        else:
            for r in merge_rows:
                r.status, r.reason = "dropped", "the i0 merge into x64 has no conflict"
        rec["merge_resolved"] = resolved
        if i0_head == base:
            x64_final = x64_head
        else:
            msg = "Merge branch '%s' into %s\n" % (names["i0"], names["x64"])
            if resolved:
                used = [r for r in merge_rows if r.status == "applied"]
                msg += "\nConflicts resolved as in %s (the series' merge rows):\n%s" % (
                    ", ".join(r.commit for r in used), "".join("\t%s\n" % f for f in resolved))
            x64_final = git.out("commit-tree", tree, "-p", x64_head, "-p", i0_head, input=msg)
        heads = {"i0": i0_head, "x64": x64_final}
        for k, n in names.items():
            old = ref_exists(git, n)
            git.run("update-ref", "-m", "branches.py %s" % a.name, "refs/heads/" + n, heads[k], old or "0" * 40)
            rec["branches"][k] = {"name": n, "head": heads[k], "tree": git.tree_of(heads[k]),
                                  "replaced": old if old and old != heads[k] else None}
        for k, rev in (("i0", a.compare_i0), ("x64", a.compare_x64)):
            if not rev:
                continue
            c = git.commit_of(rev)
            if not c:
                rec["compare"][k] = {"expected": rev, "error": "not in the clone"}
                continue
            equal = git.tree_of(c) == git.tree_of(heads[k])
            diff = [] if equal else git.out("diff", "--name-status", c, heads[k]).splitlines()
            rec["compare"][k] = {"expected": rev, "expected_commit": c, "equal": equal, "differences": diff}
        if a.push:
            for k, n in names.items():
                remote = git.run("ls-remote", "--heads", "origin", "refs/heads/" + n).stdout.split()
                if remote and remote[0] == heads[k]:
                    rec["push"][k] = "already on origin"
                    continue
                if remote:
                    raise Stop("branch %s exists on origin at %s (a different commit)" % (n, remote[0][:9]),
                               "choose another --name (the tool never force-pushes)")
                git.run("push", "-q", "origin", "%s:refs/heads/%s" % (heads[k], n))
                rec["push"][k] = "pushed"
    except Stop as s:
        rec["stop"] = {"what": s.what, "cl_step": 3, "unblock": s.unblock}
        print("take_release: STOP branches (CL step 3): %s; to unblock: %s" % (s.what, s.unblock))
        return finish("stop", EXIT_STOP)
    except Failure as e:
        print("branches: FAILED: %s" % e, file=sys.stderr)
        rec["error"] = str(e)
        return finish("failed", 1)

    for r in rows:
        if r.status in ("dropped", "skipped") or r.warnings:
            print("  %-8s %-8s %-3s %s %s%s" % (r.status, r.cls, r.branch, r.commit[:9], r.reason,
                                              ("; " + "; ".join(r.warnings)) if r.warnings else ""))
    for k in ("i0", "x64"):
        b = rec["branches"][k]
        line = "branches: %s %s tree %s" % (b["name"], b["head"][:9], b["tree"][:12])
        if k in rec["compare"]:
            c = rec["compare"][k]
            line += "; tree %s %s" % ("equals" if c.get("equal") else "DIFFERS FROM", c["expected"])
            if c.get("differences"):
                line += " (%d files)" % len(c["differences"])
        if k in rec["push"]:
            line += "; " + rec["push"][k]
        print(line)
    code = finish("ok", 0)
    print("branches: %(applied)d applied, %(dropped)d dropped, %(skipped)d skipped; %(json)s"
          % dict(rec["counts"], json=out / "branches.json"))
    return code


if __name__ == "__main__":
    sys.exit(main())
