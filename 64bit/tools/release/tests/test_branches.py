"""Tests of tools/release/branches.py (issue 3505) on scratch git repositories: every case runs the tool as its own
process, as the release driver does.

    python -I -X utf8 -m unittest discover -s tools/release/tests -p "test_branches.py"
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
TOOL = HERE.parent / "branches.py"
sys.path.insert(0, str(HERE.parent))
import branches  # noqa: E402

STOP_PREFIX = "take_release: STOP branches (CL step 3): "


class Scratch:
    """A scratch fork: `rel` (the release), commits on side branches, and an empty bare `origin`."""

    def __init__(self, root):
        self.root = Path(root)
        self.repo = self.root / "fork"
        self.origin = self.root / "origin.git"
        cfg = self.root / "gitconfig"
        cfg.write_text("", encoding="utf-8")
        self.env = dict(os.environ, GIT_CONFIG_NOSYSTEM="1", GIT_CONFIG_GLOBAL=str(cfg),
                        GIT_AUTHOR_NAME="Test Author", GIT_AUTHOR_EMAIL="author@example.invalid",
                        GIT_COMMITTER_NAME="Test Committer", GIT_COMMITTER_EMAIL="committer@example.invalid",
                        GIT_TERMINAL_PROMPT="0")
        self.git("init", "-q", "-b", "main", str(self.repo), cwd=self.root)
        self.git("init", "-q", "--bare", str(self.origin), cwd=self.root)
        self.git("remote", "add", "origin", str(self.origin))
        for name in "abc":
            self.write("%s.txt" % name, "".join("%s%d\n" % (name.upper(), i) for i in range(1, 8)))
        self.commit("Release")
        self.git("tag", "rel")

    def git(self, *args, cwd=None):
        p = subprocess.run(["git", *args], cwd=str(cwd or self.repo), env=self.env, capture_output=True, text=True)
        if p.returncode != 0:
            raise AssertionError("git %s: %s" % (" ".join(args), p.stderr))
        return p.stdout.strip()

    def write(self, name, text):
        (self.repo / name).write_text(text, encoding="utf-8", newline="\n")

    def edit_line(self, name, old, new):
        p = self.repo / name
        p.write_text(p.read_text(encoding="utf-8").replace(old + "\n", new + "\n", 1), encoding="utf-8", newline="\n")

    def commit(self, msg):
        self.git("add", "-A")
        self.git("commit", "-q", "-m", msg)
        return self.git("rev-parse", "HEAD")

    def branch_from(self, name, start):
        self.git("checkout", "-q", "-b", name, start)

    def tree(self, rev):
        return self.git("rev-parse", rev + "^{tree}")

    def ref(self, name):
        p = subprocess.run(["git", "rev-parse", "--verify", "--quiet", "refs/heads/" + name], cwd=str(self.repo),
                           env=self.env, capture_output=True, text=True)
        return p.stdout.strip() or None

    def series(self, rows, name="series.tsv"):
        p = self.root / name
        p.write_text("# test series\n" + "".join("\t".join(r) + "\n" for r in rows), encoding="utf-8")
        return p

    def run(self, series, base, name="t", *extra):
        out = self.root / ("out-" + name)
        p = subprocess.run([sys.executable, "-I", str(TOOL), "--fork", str(self.repo), "--base", base, "--series",
                            str(series), "--name", name, "--out", str(out), *extra],
                           env=self.env, capture_output=True, text=True)
        rec = None
        if (out / "branches.json").exists():
            rec = json.loads((out / "branches.json").read_text(encoding="utf-8"))
        return p, rec


class BranchesTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="branches-test-")
        s = self.s = Scratch(self.tmp)
        # the carry: three i0 commits in a row (fix, identity, a fix that needs the first) and one x64 port commit
        s.branch_from("work", "rel")
        s.edit_line("a.txt", "A1", "A1 fix")
        self.f_a = s.commit("Fix a")
        s.edit_line("b.txt", "B1", "B1 identity")
        self.id_b = s.commit("Identity b")
        s.edit_line("a.txt", "A1 fix", "A1 fix two")
        self.f_dep = s.commit("Fix a again")
        s.branch_from("x64work", "rel")
        s.edit_line("c.txt", "C1", "C1 port")
        self.p_c = s.commit("Port c")
        s.git("checkout", "-q", "main")
        self.rows = [["port", "x64", self.p_c, "Port c", "-"],
                     ["fix", "i0", self.f_a, "Fix a", "-"],
                     ["identity", "i0", self.id_b, "Identity b", "-"],
                     ["fix", "i0", self.f_dep, "Fix a again", "-", self.f_a[:9]]]

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def status(self, rec):
        return {r["commit"]: (r["status"], r["reason"]) for r in rec["rows"]}

    def test_release_trees_equal_and_push(self):
        s = self.s
        p, rec = s.run(s.series(self.rows), "rel", "t", "--compare-i0", "work", "--push")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertEqual(rec["counts"]["applied"], 4)
        self.assertTrue(rec["compare"]["i0"]["equal"])
        i0, x64 = s.ref("i0-release-t"), s.ref("x64-release-t")
        self.assertEqual(s.tree(i0), s.tree("work"))
        self.assertEqual(s.git("log", "-1", "--format=%P", x64).split(), [rec["rows"][0]["new_commit"], i0])
        self.assertEqual(s.git("show", x64 + ":a.txt").splitlines()[0], "A1 fix two")
        self.assertEqual(s.git("show", x64 + ":c.txt").splitlines()[0], "C1 port")
        msg = s.git("log", "-1", "--format=%B", i0)
        self.assertIn("(cherry picked from commit %s)" % self.f_dep, msg)
        self.assertEqual(s.git("log", "-1", "--format=%an", i0), "Test Author")
        heads = s.git("ls-remote", "--heads", str(s.origin))
        self.assertIn("refs/heads/i0-release-t", heads)
        self.assertIn("refs/heads/x64-release-t", heads)
        # a rerun without --replace stops; with --replace it moves the local branches and finds the remote ones
        p2, rec2 = s.run(s.series(self.rows), "rel", "t")
        self.assertEqual(p2.returncode, 10)
        self.assertIn(STOP_PREFIX + "branch i0-release-t exists already", p2.stdout)
        p3, rec3 = s.run(s.series(self.rows), "rel", "t", "--replace")
        self.assertEqual(p3.returncode, 0, p3.stdout + p3.stderr)

    def test_drop_rows_the_base_already_has(self):
        s = self.s
        # a later release: f_a's patch (same patch id), an upstream commit (#123), and a commit that makes id_b's
        # change together with another (different patch id: the pick is empty)
        s.branch_from("rel2", "rel")
        s.git("cherry-pick", self.f_a)
        s.write("d.txt", "D1\n")
        up = s.commit("Upstream change (#123)")
        s.edit_line("b.txt", "B1", "B1 identity")
        s.write("e.txt", "E1\n")
        s.commit("Bigger change that contains identity b")
        s.git("checkout", "-q", "main")
        # two more carried rows: one names the upstream commit, one the upstream pull request
        s.branch_from("work2", "rel")
        s.write("d.txt", "D1\n")
        g_hash = s.commit("Our copy of the upstream change")
        s.write("f.txt", "F1\n")
        h_pr = s.commit("Our copy of PR 123")
        s.git("checkout", "-q", "main")
        rows = self.rows + [["fix", "i0", g_hash, "Our copy of the upstream change", up],
                            ["fix", "i0", h_pr, "Our copy of PR 123", "#123"]]
        p, rec = s.run(s.series(rows), "rel2", "t")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        st = self.status(rec)
        self.assertEqual(st[self.f_a][0], "dropped")
        self.assertIn("patch id", st[self.f_a][1])
        self.assertEqual(st[self.id_b][0], "dropped")
        self.assertIn("changes nothing", st[self.id_b][1])
        self.assertEqual(st[g_hash][0], "dropped")
        self.assertIn("contains upstream", st[g_hash][1])
        self.assertEqual(st[h_pr][0], "dropped")
        self.assertIn("#123", st[h_pr][1])
        # a dropped row does not skip the rows that need it
        self.assertEqual(st[self.f_dep][0], "applied")
        self.assertEqual(st[self.p_c][0], "applied")
        self.assertEqual(rec["counts"], {"applied": 2, "dropped": 4, "skipped": 0, "conflict": 0, "pending": 0})
        self.assertEqual(s.git("show", "i0-release-t:a.txt").splitlines()[0], "A1 fix two")

    def test_conflicting_fix_is_skipped_with_the_rows_that_need_it(self):
        s = self.s
        s.branch_from("rel3", "rel")
        s.edit_line("a.txt", "A1", "A1 upstream changed it otherwise")
        s.commit("Upstream edits a")
        s.git("checkout", "-q", "main")
        p, rec = s.run(s.series(self.rows), "rel3", "t")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        st = self.status(rec)
        self.assertEqual(st[self.f_a][0], "skipped")
        self.assertIn("conflict in a.txt", st[self.f_a][1])
        self.assertEqual(st[self.f_dep][0], "skipped")
        self.assertIn("needs", st[self.f_dep][1])
        self.assertEqual(st[self.id_b][0], "applied")
        self.assertIn("skipped  fix", p.stdout)
        self.assertEqual(s.git("show", "i0-release-t:a.txt").splitlines()[0], "A1 upstream changed it otherwise")

    def test_port_conflict_stops_with_exit_10_and_writes_no_branch(self):
        s = self.s
        s.branch_from("rel4", "rel")
        s.edit_line("c.txt", "C1", "C1 upstream")
        s.commit("Upstream edits c")
        s.git("checkout", "-q", "main")
        p, rec = s.run(s.series(self.rows), "rel4", "t")
        self.assertEqual(p.returncode, 10, p.stdout + p.stderr)
        line = [x for x in p.stdout.splitlines() if x.startswith(STOP_PREFIX)]
        self.assertEqual(len(line), 1, p.stdout)
        self.assertIn("port row x64 %s" % self.p_c[:9], line[0])
        self.assertIn("conflict in c.txt", line[0])
        self.assertIn("; to unblock: ", line[0])
        self.assertEqual(rec["result"], "stop")
        self.assertEqual(rec["stop"]["cl_step"], 3)
        self.assertEqual(self.status(rec)[self.p_c][0], "conflict")
        self.assertIsNone(s.ref("i0-release-t"))
        self.assertIsNone(s.ref("x64-release-t"))

    def test_identity_conflict_stops(self):
        s = self.s
        s.branch_from("rel5", "rel")
        s.edit_line("b.txt", "B1", "B1 upstream")
        s.commit("Upstream edits b")
        s.git("checkout", "-q", "main")
        p, rec = s.run(s.series(self.rows), "rel5", "t")
        self.assertEqual(p.returncode, 10, p.stdout + p.stderr)
        self.assertIn(STOP_PREFIX + "identity row i0 %s" % self.id_b[:9], p.stdout)

    def test_merge_of_i0_into_x64_conflict_stops(self):
        s = self.s
        s.branch_from("x64bad", "rel")
        s.edit_line("a.txt", "A1", "A1 x64 edit")
        bad = s.commit("x64 edits a")
        s.git("checkout", "-q", "main")
        rows = [["port", "x64", bad, "x64 edits a", "-"]] + self.rows[1:]
        p, rec = s.run(s.series(rows), "rel", "t")
        self.assertEqual(p.returncode, 10, p.stdout + p.stderr)
        self.assertIn(STOP_PREFIX + "the merge of i0-release-t into x64-release-t conflicts: a.txt", p.stdout)
        self.assertIsNone(s.ref("i0-release-t"))

    # ticket 0639: a `merge` row replays a hand resolution of the i0 merge into x64, only on identical blobs
    def _conflict_and_resolution(self):
        """An x64 row and an i0 row that both edit A1 (their merge conflicts), and R: the x64 commit and the i0 commit
        merged by hand (first parent x64, second i0) with a.txt resolved."""
        s = self.s
        s.branch_from("x64bad", "rel")
        s.edit_line("a.txt", "A1", "A1 x64 edit")
        bad = s.commit("x64 edits a")
        s.branch_from("resolve", bad)
        s.edit_line("a.txt", "A1 x64 edit", "A1 x64 edit and fix")
        tmp = s.commit("resolution content")
        r = s.git("commit-tree", tmp + "^{tree}", "-p", bad, "-p", self.f_a, "-m", "Merge fix a into x64 edits a")
        s.git("checkout", "-q", "main")
        return bad, r

    def test_merge_row_resolves_the_conflict_on_equal_blobs(self):
        s = self.s
        bad, r = self._conflict_and_resolution()
        rows = [["port", "x64", bad, "x64 edits a", "-"], ["fix", "i0", self.f_a, "Fix a", "-"],
                ["port", "merge", r, "Merge fix a into x64 edits a", "-"]]
        p, rec = s.run(s.series(rows), "rel", "t", "--compare-x64", r)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertEqual(self.status(rec)[r], ("applied", "resolved a.txt in the i0 merge"))
        self.assertEqual(rec["merge_resolved"], ["a.txt"])
        self.assertTrue(rec["compare"]["x64"]["equal"], rec["compare"])
        head = s.ref("x64-release-t")
        self.assertEqual(s.git("show", head + ":a.txt").splitlines()[0], "A1 x64 edit and fix")
        self.assertEqual(len(s.git("log", "-1", "--format=%P", head).split()), 2)
        self.assertIn("Conflicts resolved as in %s" % r, s.git("log", "-1", "--format=%B", head))

    def test_merge_row_with_a_different_i0_blob_stops(self):
        # negative control: the i0 side now also has "Fix a again", so its a.txt is not R's second parent's
        s = self.s
        bad, r = self._conflict_and_resolution()
        rows = [["port", "x64", bad, "x64 edits a", "-"], ["fix", "i0", self.f_a, "Fix a", "-"],
                ["fix", "i0", self.f_dep, "Fix a again", "-"], ["port", "merge", r, "Merge fix a into x64 edits a", "-"]]
        p, rec = s.run(s.series(rows), "rel", "t")
        self.assertEqual(p.returncode, 10, p.stdout + p.stderr)
        line = [x for x in p.stdout.splitlines() if x.startswith(STOP_PREFIX)]
        self.assertEqual(len(line), 1, p.stdout)
        self.assertIn("conflicts: a.txt (%s: i0 side blob differs)" % r[:9], line[0])
        self.assertIn("`merge` row", line[0])
        self.assertEqual(self.status(rec)[r][0], "conflict")
        self.assertIsNone(s.ref("i0-release-t"))
        self.assertIsNone(s.ref("x64-release-t"))

    def test_merge_row_with_a_different_x64_blob_stops(self):
        s = self.s
        bad, r = self._conflict_and_resolution()
        s.branch_from("x64more", "rel")
        s.edit_line("a.txt", "A1", "A1 other x64 edit")
        other = s.commit("x64 edits a otherwise")
        s.git("checkout", "-q", "main")
        rows = [["port", "x64", other, "x64 edits a otherwise", "-"], ["fix", "i0", self.f_a, "Fix a", "-"],
                ["port", "merge", r, "Merge fix a into x64 edits a", "-"]]
        p, rec = s.run(s.series(rows), "rel", "t")
        self.assertEqual(p.returncode, 10, p.stdout + p.stderr)
        self.assertIn("a.txt (%s: x64 side blob differs)" % r[:9], p.stdout)
        self.assertIsNone(s.ref("x64-release-t"))

    def test_merge_row_unused_when_the_merge_is_clean(self):
        s = self.s
        _bad, r = self._conflict_and_resolution()
        p, rec = s.run(s.series(self.rows + [["port", "merge", r, "Merge fix a into x64 edits a", "-"]]), "rel", "t",
                       "--compare-i0", "work")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertEqual(self.status(rec)[r], ("dropped", "the i0 merge into x64 has no conflict"))
        self.assertEqual(rec["merge_resolved"], [])
        self.assertTrue(rec["compare"]["i0"]["equal"])

    def test_merge_row_must_be_a_merge(self):
        s = self.s
        bad, _r = self._conflict_and_resolution()
        rows = [["port", "x64", bad, "x64 edits a", "-"], ["fix", "i0", self.f_a, "Fix a", "-"],
                ["port", "merge", self.id_b, "Identity b", "-"]]
        p, rec = s.run(s.series(rows), "rel", "t")
        self.assertEqual(p.returncode, 10, p.stdout + p.stderr)
        self.assertIn(STOP_PREFIX + "merge row %s (series line 4) is not a merge of two parents" % self.id_b[:9],
                      p.stdout)
        self.assertIsNone(s.ref("x64-release-t"))

    def test_merge_row_with_upstream_is_exit_2(self):
        s = self.s
        p, rec = s.run(s.series([["port", "merge", self.f_a, "x", "#1"]]), "rel", "t")
        self.assertEqual(p.returncode, 2, p.stdout + p.stderr)
        self.assertIn("a merge row resolves the i0 merge and has no upstream", p.stderr)

    def test_missing_commit_stops(self):
        s = self.s
        rows = self.rows + [["fix", "i0", "0123456789abcdef0123456789abcdef01234567", "Gone", "-"]]
        p, rec = s.run(s.series(rows), "rel", "t")
        self.assertEqual(p.returncode, 10, p.stdout + p.stderr)
        self.assertIn(STOP_PREFIX + "1 series commit(s) not in the fork clone: 012345678 (line 6)", p.stdout)
        # issue 3509 (MC #3021): the unblock names the cause 3510 found (a release copy's commit never pushed)
        self.assertIn("never pushed before the clone that made it was deleted: name its pushed pick", p.stdout)
        self.assertEqual(self.status(rec)["0123456789abcdef0123456789abcdef01234567"][0], "missing")
        self.assertIsNone(s.ref("i0-release-t"))

    def test_missing_base_stops(self):
        s = self.s
        p, rec = s.run(s.series(self.rows), "no-such-tag", "t")
        self.assertEqual(p.returncode, 10)
        self.assertIn(STOP_PREFIX + "base no-such-tag is not in the fork clone", p.stdout)

    def test_bad_series_is_exit_2(self):
        s = self.s
        for rows, text in ((["patch", "i0", self.f_a, "x", "-"], "class 'patch'"),
                           (["fix", "x86", self.f_a, "x", "-"], "branch 'x86'"),
                           (["fix", "i0", self.f_a[:9], "x", "-"], "not a full lowercase hash"),
                           (["fix", "i0", self.f_a, "x", "-", "abcdef1"], "matches 0 earlier rows")):
            p, rec = s.run(s.series([rows]), "rel", "t")
            self.assertEqual(p.returncode, 2, p.stdout + p.stderr)
            self.assertIn(text, p.stderr)

    def test_subject_mismatch_is_a_warning(self):
        s = self.s
        rows = [list(r) for r in self.rows]
        rows[1][3] = "Another subject"
        p, rec = s.run(s.series(rows), "rel", "t")
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertEqual(rec["rows"][1]["warnings"], ["subject in the commit: 'Fix a'"])

    def test_the_committed_series_parses(self):
        rows = branches.parse_series(HERE.parent / "series" / "carry.tsv")
        # ticket 0574: the counts of the series as pin batches 3-5 (0463, 0517, 0551) left it; it was 45/6/28 before.
        # Ticket 0639: + the 0.1b pins (13 danger-package rows on x64, hidden embarks on i0, one merge row); was 57/7.
        self.assertEqual(len(rows), 72)
        self.assertEqual(sum(r.branch == "x64" for r in rows), 20)
        self.assertEqual(sum(r.branch == "merge" for r in rows), 1)
        self.assertEqual({c: sum(r.cls == c for r in rows) for c in branches.CLASSES},
                         {"port": 22, "identity": 11, "fix": 39})
        for copy in sorted((HERE.parent / "series").glob("carry-*.tsv")):   # release copies parse too
            self.assertGreater(len(branches.parse_series(copy)), 0, copy.name)   # each its own release's count


class PickMessageTest(unittest.TestCase):
    def test_trailer_block_gets_the_line_directly(self):
        m = branches.pick_message("Subject\n\nBody.\n\nCo-Authored-By: X <x@y>\n", "abc")
        self.assertEqual(m, "Subject\n\nBody.\n\nCo-Authored-By: X <x@y>\n(cherry picked from commit abc)\n")

    def test_plain_message_gets_a_blank_line(self):
        self.assertEqual(branches.pick_message("Subject\n", "abc"), "Subject\n\n(cherry picked from commit abc)\n")


if __name__ == "__main__":
    unittest.main()
