"""Tests of render/build.py's defaults (bug 0091 items 1 and 2). Run by ctest as c5r_build_py_test, or directly:

    python -I render/tests/common/build_py_test.py

Before the fix every checkout on the machine built in D:/c5-64bit-work/2900/build/<config> and wrote test scratch to
D:/c5-64bit-work/2900/test-out, and a run with skipped tests passed. These tests fail on that version.
"""
import importlib.util
import pathlib
import sys
import tempfile
import unittest

sys.dont_write_bytecode = True  # no render/__pycache__ in the checkout

BUILD_PY = pathlib.Path(__file__).resolve().parents[2] / "build.py"


def load():
    spec = importlib.util.spec_from_file_location("render_build", str(BUILD_PY))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class DefaultsKeyedByCheckout(unittest.TestCase):
    def setUp(self):
        self.b = load()
        self.tmp = tempfile.TemporaryDirectory()
        self.work = pathlib.Path(self.tmp.name) / "work"
        self.work.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def test_two_checkouts_get_two_build_trees(self):
        a = self.b.default_build_dir(pathlib.Path(self.tmp.name) / "clone-a", "Release", self.work)
        c = self.b.default_build_dir(pathlib.Path(self.tmp.name) / "clone-b", "Release", self.work)
        self.assertNotEqual(a, c)
        self.assertEqual(a.parent.parent, self.work / "render-build")
        self.assertEqual(a.name, "Release")

    def test_same_checkout_is_stable_and_case_blind(self):
        p = pathlib.Path(self.tmp.name) / "Clone"
        self.assertEqual(self.b.default_build_dir(p, "Debug", self.work),
                         self.b.default_build_dir(pathlib.Path(str(p).lower()), "Debug", self.work))
        self.assertEqual(len(self.b.checkout_key(p)), 10)

    def test_never_the_2900_folder(self):
        d = self.b.default_build_dir(self.b.REPO, "Release")
        self.assertNotIn("2900", d.as_posix().split("/"))
        self.assertIn(self.b.checkout_key(self.b.REPO), d.as_posix())

    def test_work_dir_inside_build_tree(self):
        bdir = self.b.default_build_dir(pathlib.Path(self.tmp.name) / "clone-a", "Release", self.work)
        self.assertEqual(self.b.default_work_dir(bdir), bdir / "test-out")

    def test_no_work_root_falls_back_into_the_checkout(self):
        repo = pathlib.Path(self.tmp.name) / "clone-a"
        d = self.b.default_build_dir(repo, "Release", pathlib.Path(self.tmp.name) / "no-such-root")
        self.assertEqual(d, repo / "build" / "render-x64" / "Release")


class SkippedFailsTheRun(unittest.TestCase):
    def setUp(self):
        self.b = load()

    def test_counts_skipped_and_not_run(self):
        out = ("1/3 Test #1: c5r_common_test ..........   Passed    0.02 sec\n"
               "2/3 Test #2: c5r_shaders_test .........***Skipped   0.10 sec\n"
               "3/3 Test #3: c5r_quadtest_test ........***Not Run   0.00 sec\n")
        self.assertEqual(self.b.count_skipped(out), 2)

    def test_clean_run_has_none(self):
        out = "1/1 Test #1: c5r_common_test ..........   Passed    0.02 sec\n100% tests passed, 0 tests failed out of 1\n"
        self.assertEqual(self.b.count_skipped(out), 0)


class AsanVariant(unittest.TestCase):
    """build.py --asan (issue 3209): its own tree and toolchain, quoted log path, every report fails the run."""

    def setUp(self):
        self.b = load()
        self.tmp = tempfile.TemporaryDirectory()
        self.logs = pathlib.Path(self.tmp.name) / "asan-logs"
        self.logs.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def test_own_build_tree_and_msvc_toolchain(self):
        d = self.b.default_build_dir(self.b.REPO, "Release")
        a = self.b.asan_build_dir(d)
        self.assertNotEqual(a, d)
        self.assertEqual(a.parent, d.parent)
        self.assertEqual(a.name, "Release-asan")
        self.assertEqual(self.b.toolchain_file(True).name, "msvc-asan-x64.cmake")
        self.assertEqual(self.b.toolchain_file(False).name, "clang-cl-x64.cmake")
        self.assertTrue(self.b.toolchain_file(True).exists())

    def test_log_path_is_quoted_and_names_the_exe(self):
        o = self.b.asan_options(pathlib.Path("D:/x y/asan-logs"))
        self.assertTrue(o.startswith('log_path="D:/x y/asan-logs/asan":'), o)
        self.assertIn("log_exe_name=1", o.split(":"))
        self.assertIn("halt_on_error=1", o.split(":"))

    def test_report_is_found_with_its_summary(self):
        (self.logs / "asan.c5r_gamefs_test.exe.1234").write_text(
            "=================================================================\n"
            "==1234==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x12 at pc 0x34\n"
            "    #0 0x7ff c5r::BinReader::u32 binreader.h:40\n"
            "SUMMARY: AddressSanitizer: heap-buffer-overflow binreader.h:40 in c5r::BinReader::u32\n")
        r = self.b.asan_reports(self.logs)
        self.assertEqual(len(r), 1)
        self.assertEqual(r[0][0], "asan.c5r_gamefs_test.exe.1234")
        self.assertTrue(r[0][1].startswith("SUMMARY: AddressSanitizer: heap-buffer-overflow"), r[0][1])

    def test_report_without_summary_uses_the_error_line(self):
        (self.logs / "asan.t.exe.9").write_text("noise\n==9==ERROR: AddressSanitizer: stack-overflow\n")
        self.assertEqual(self.b.asan_reports(self.logs), [("asan.t.exe.9", "==9==ERROR: AddressSanitizer: stack-overflow")])

    def test_empty_files_other_files_and_no_folder_are_not_reports(self):
        (self.logs / "asan.t.exe.1").write_text("")
        (self.logs / "asan.t.exe.2").write_text("  \n")
        (self.logs / "notes.txt").write_text("ERROR: AddressSanitizer: not a log of ours")
        self.assertEqual(self.b.asan_reports(self.logs), [])
        self.assertEqual(self.b.asan_reports(self.logs / "missing"), [])

    def test_canary_needs_its_overflow_report(self):
        ok = [("asan.c5r_asan_canary.exe.7", "SUMMARY: AddressSanitizer: heap-buffer-overflow asan_canary.cpp:15 in main")]
        self.assertIsNone(self.b.canary_verdict(ok))
        self.assertIn("no report file", self.b.canary_verdict([]))
        self.assertIn("no heap-buffer-overflow", self.b.canary_verdict([("asan.x.1", "==1==ERROR: something else")]))

    def test_missing_canary_fails(self):
        self.assertIn("missing", self.b.asan_canary(pathlib.Path(self.tmp.name), {}))


if __name__ == "__main__":
    r = unittest.main(exit=False, verbosity=2).result
    sys.exit(0 if r.wasSuccessful() and not r.skipped else 1)
