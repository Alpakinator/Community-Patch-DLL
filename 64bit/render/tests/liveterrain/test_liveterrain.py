"""Tests of regioncmp.py and piccmp.py (ticket 4906), on synthetic fields and pictures; offline, nothing started.

    set PYTEST_DISABLE_PLUGIN_AUTOLOAD=1
    python -I -m pytest -q render/tests/liveterrain
    python -I render/tests/liveterrain/test_liveterrain.py

Every input check of the two tools has a negative test here (exit 2 and the REFUSED line).
"""
import io
import json
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

import numpy as np

sys.dont_write_bytecode = True
HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import piccmp  # noqa: E402
import regioncmp as rc  # noqa: E402

W, H = 8, 6          # map plots; the field has H + 4 plot rows
SX, SY, OX, OY = 1.0, 0.866, 0.0, 0.0


def centre(x, y):
    """A plot centre in world units on the synthetic grid (odd rows half a plot right, two margin rows below)."""
    return (x + 0.5 + 0.5 * (y & 1)) * 64 * SX - OX, (y + 2.5) * 64 * SY - OY


def write_centres(path, wrap=1, drop=None, dup=None, extra=None):
    lines = ["%d %d %d" % (W, H, wrap)]
    for y in range(H):
        for x in range(W):
            if drop == (x, y):
                continue
            wx, wy = centre(x, y)
            lines.append("%d %d %.4f %.4f" % (x, y, wx, wy))
            if dup == (x, y):
                lines.append("%d %d %.4f %.4f" % (x, y, wx, wy))
    if extra:
        lines.append(extra)
    Path(path).write_text("\n".join(lines), encoding="utf-8")


def field(seed=1, mat=True, rows=H + 4, cols=W):
    rng = np.random.default_rng(seed)
    h = rng.integers(1000, 60000, size=(rows * 64, cols * 64), dtype=np.uint16)
    m = rng.integers(0, 20, size=h.shape, dtype=np.uint8) if mat else None
    return h, m


def write_npz(path, h, m, sx=SX, sy=SY, ox=OX, oy=OY, wrap=1):
    info = {"grid_sx": sx, "grid_sy": sy, "grid_ox": ox, "grid_oy": oy, "wrap_x": wrap}
    kw = {"h16": h, "info": json.dumps(info)}
    if m is not None:
        kw["m8"] = m
    np.savez_compressed(path, **kw)


def write_c5hf(path, h, m, wrap=1, flags=None, cut=0):
    syn, sxn = h.shape
    fl = (1 if wrap else 0) | (2 if m is not None else 0) if flags is None else flags
    hdr = bytearray(128)
    hdr[0:4] = b"C5HF"
    struct.pack_into("<6I", hdr, 4, 1, sxn // 64, syn // 64, sxn, syn, fl)
    struct.pack_into("<6f", hdr, 28, SX, SY, 0.2, OX, OY, 10.0)
    body = bytes(hdr) + h.astype("<u2").tobytes() + (m.tobytes() if m is not None else b"")
    Path(path).write_bytes(body[:len(body) - cut] if cut else body)


def labels():
    cen = {(x, y): ((centre(x, y)[0] + OX) / SX, (centre(x, y)[1] + OY) / SY) for y in range(H) for x in range(W)}
    return rc.label_samples(((H + 4) * 64, W * 64), cen, W, True)


def run(main, argv):
    buf = io.StringIO()
    with redirect_stdout(buf):
        code = main(argv)
    out = buf.getvalue()
    return code, (json.loads(out) if code == 0 else out)


class RegionCompare(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        self.cen = self.d / "centres.txt"
        write_centres(self.cen)
        self.h, self.m = field()
        self.a = self.d / "a.npz"
        write_npz(self.a, self.h, self.m)
        self.lab = labels()

    def tearDown(self):
        self.tmp.cleanup()

    def cmp(self, b, plots=("3,2",), extra=()):
        return run(rc.main, [str(self.a), str(b), "--centres", str(self.cen), "--plots", *plots, *extra])

    def changed(self, plot_xy, delta=100, mat=False, name="b.npz"):
        h, m = self.h.copy(), self.m.copy()
        sel = self.lab == plot_xy[1] * W + plot_xy[0]
        h[sel] = h[sel] + delta if delta >= 0 else h[sel] - (-delta)
        if mat:
            m[sel] = m[sel] + 1
        p = self.d / name
        write_npz(p, h, m)
        return p, int(sel.sum())

    # positives
    def test_identical(self):
        code, r = self.cmp(self.a)
        self.assertEqual(code, 0)
        self.assertEqual(r["diff_total"], 0)
        self.assertEqual(r["material_diff_total"], 0)
        self.assertEqual(set(r["regions"]), {"inside", "ring1", "ring2", "ring3", "ring4", "outside"})
        self.assertEqual(r["regions"]["inside"]["plots"], 1)
        self.assertEqual(r["regions"]["ring1"]["plots"], 6)
        self.assertEqual(sum(v["samples"] for v in r["regions"].values()), r["samples"])

    def test_inside(self):
        b, n = self.changed((3, 2))
        code, r = self.cmp(b)
        self.assertEqual(code, 0)
        self.assertGreater(n, 3000)
        self.assertEqual(r["regions"]["inside"]["diff"], n)
        self.assertEqual(r["regions"]["inside"]["max_abs"], 100)
        for k in ("ring1", "ring2", "ring3", "ring4", "outside"):
            self.assertEqual(r["regions"][k]["diff"], 0, k)

    def test_each_ring_apart(self):
        # (3,2) changed; (6,2) is at hex distance 3, (4,2) at 1
        b3, n3 = self.changed((6, 2), name="b3.npz")
        code, r = self.cmp(b3)
        self.assertEqual(r["regions"]["ring3"]["diff"], n3)
        self.assertEqual(r["regions"]["ring1"]["diff"] + r["regions"]["ring2"]["diff"] + r["regions"]["ring4"]["diff"], 0)
        b1, n1 = self.changed((4, 2), name="b1.npz")
        code, r = self.cmp(b1)
        self.assertEqual(r["regions"]["ring1"]["diff"], n1)

    def test_outside_listed_with_distance(self):
        b, n = self.changed((7, 5), name="bo.npz")    # far corner; with the wrap distance from (0,0) is small
        code, r = self.cmp(b, plots=("2,0",))
        self.assertEqual(code, 0)
        self.assertEqual(rc.hexdist(2, 0, 7, 5, W, True), 5)
        self.assertEqual(r["regions"]["outside"]["diff"], n)
        self.assertEqual(r["outside_plots"][0], {"x": 7, "y": 5, "diff": n, "hexdist": 5})

    def test_rings_option(self):
        b, n = self.changed((6, 2), name="b3.npz")
        code, r = self.cmp(b, extra=("--rings", "2"))
        self.assertEqual(set(r["regions"]), {"inside", "ring1", "ring2", "outside"})
        self.assertEqual(r["regions"]["outside"]["diff"], n)

    def test_tolerance(self):
        b, n = self.changed((3, 2), delta=1, name="t.npz")
        code, r = self.cmp(b)
        self.assertEqual(r["diff_total"], 0)
        code, r = self.cmp(b, extra=("--tol", "0"))
        self.assertEqual(r["regions"]["inside"]["diff"], n)

    def test_material(self):
        b, n = self.changed((3, 2), delta=0, mat=True, name="m.npz")
        code, r = self.cmp(b)
        self.assertEqual(r["diff_total"], 0)
        self.assertEqual(r["regions"]["inside"]["material_diff"], n)

    def test_wrap_distance(self):
        self.assertEqual(rc.hexdist(0, 2, W - 1, 2, W, True), 1)
        self.assertEqual(rc.hexdist(0, 2, W - 1, 2, W, False), W - 1)
        b, n = self.changed((0, 2), name="w.npz")
        code, r = self.cmp(b, plots=("7,2",))
        self.assertEqual(r["regions"]["ring1"]["diff"], n)

    def test_margin_rows_are_outside(self):
        h = self.h.copy()
        h[:10, :] += 50          # the bottom margin rows: no plot
        p = self.d / "mg.npz"
        write_npz(p, h, self.m)
        code, r = self.cmp(p)
        self.assertEqual(r["regions"]["outside"]["diff"], 10 * W * 64)
        self.assertEqual(r["outside_plots"][0]["x"], -1)

    def test_c5hf_equals_npz(self):
        b, n = self.changed((3, 2), name="c.npz")
        with np.load(str(b)) as z:
            hb, mb = z["h16"], z["m8"]
        c = self.d / "b.c5hf"
        write_c5hf(c, hb, mb)
        a5 = self.d / "a.c5hf"
        write_c5hf(a5, self.h, self.m)
        _, r1 = self.cmp(b)
        _, r2 = run(rc.main, [str(a5), str(c), "--centres", str(self.cen), "--plots", "3,2"])
        self.assertEqual(r1["regions"], r2["regions"])

    def test_heights_only(self):
        c = self.d / "nomat.c5hf"
        write_c5hf(c, self.h, None)
        code, r = self.cmp(c, extra=("--heights-only",))
        self.assertEqual(code, 0)
        self.assertIsNone(r["material_diff_total"])

    # negatives: one per input check
    def refused(self, argv, what):
        code, out = run(rc.main, argv)
        self.assertEqual(code, 2, out)
        self.assertIn("regioncmp: REFUSED", out)
        self.assertIn(what, out)

    def base(self, b=None, cen=None, plots=("3,2",)):
        return [str(self.a), str(b or self.a), "--centres", str(cen or self.cen), "--plots", *plots]

    def test_neg_missing_field(self):
        self.refused(self.base(b=self.d / "nope.npz"), "field not found")

    def test_neg_unknown_form(self):
        p = self.d / "x.bin"
        p.write_bytes(b"XXXX" + bytes(200))
        self.refused(self.base(b=p), "unknown field form")

    def test_neg_bad_npz(self):
        p = self.d / "bad.npz"
        np.savez(p, other=np.zeros(3))
        self.refused(self.base(b=p), "not a drive.py field dump")

    def test_neg_npz_without_grid(self):
        p = self.d / "nogrid.npz"
        np.savez(p, h16=self.h, m8=self.m, info=json.dumps({"x": 1}))
        self.refused(self.base(b=p), "grid_sx")

    def test_neg_shape(self):
        h, m = field(rows=H + 5)
        p = self.d / "big.npz"
        write_npz(p, h, m)
        self.refused(self.base(b=p), "sample counts differ")

    def test_neg_geometry(self):
        p = self.d / "geo.npz"
        write_npz(p, self.h, self.m, ox=3.0)
        self.refused(self.base(b=p), "grid ox differs")

    def test_neg_material_one_sided(self):
        p = self.d / "nm.npz"
        write_npz(p, self.h, None)
        self.refused(self.base(b=p), "material layer")

    def test_neg_wrap_mismatch(self):
        cen = self.d / "nowrap.txt"
        write_centres(cen, wrap=0)
        self.refused(self.base(cen=cen), "wrap flag")

    def test_neg_centres_missing(self):
        self.refused(self.base(cen=self.d / "none.txt"), "centres not found")

    def test_neg_centres_header(self):
        p = self.d / "hdr.txt"
        p.write_text("eight six\n", encoding="utf-8")
        self.refused(self.base(cen=p), "first line")

    def test_neg_centres_line(self):
        p = self.d / "line.txt"
        write_centres(p, extra="1 2 x")
        self.refused(self.base(cen=p), "is not 'x y wx wy'")

    def test_neg_centres_missing_plot(self):
        p = self.d / "drop.txt"
        write_centres(p, drop=(1, 1))
        self.refused(self.base(cen=p), "expected")

    def test_neg_centres_duplicate(self):
        p = self.d / "dup.txt"
        write_centres(p, dup=(1, 1))
        self.refused(self.base(cen=p), "twice")

    def test_neg_centres_plot_outside(self):
        p = self.d / "out.txt"
        write_centres(p, extra="%d 0 1.0 1.0" % W)
        self.refused(self.base(cen=p), "outside")

    def test_neg_field_does_not_fit_map(self):
        h, m = field(rows=H + 3)
        a = self.d / "small_a.npz"
        b = self.d / "small_b.npz"
        write_npz(a, h, m)
        write_npz(b, h, m)
        code, out = run(rc.main, [str(a), str(b), "--centres", str(self.cen), "--plots", "3,2"])
        self.assertEqual(code, 2)
        self.assertIn("does not fit the map", out)

    def test_neg_centre_outside_field(self):
        p = self.d / "far.txt"
        lines = Path(self.cen).read_text(encoding="utf-8").splitlines()
        lines[1] = "0 0 10.0 -500.0"
        p.write_text("\n".join(lines), encoding="utf-8")
        self.refused(self.base(cen=p), "outside the field")

    def test_neg_no_plot(self):
        self.refused(self.base(plots=("",)), "")

    def test_neg_plot_text(self):
        self.refused(self.base(plots=("3;2",)), "is X,Y")

    def test_neg_plot_outside_map(self):
        self.refused(self.base(plots=("%d,0" % W,)), "outside the map")

    def test_neg_plot_twice(self):
        self.refused(self.base(plots=("3,2", "3,2")), "named twice")

    def test_neg_tol(self):
        self.refused(self.base() + ["--tol", "-1"], "--tol")

    def test_neg_rings(self):
        self.refused(self.base() + ["--rings", "0"], "--rings")

    def test_neg_c5hf_truncated(self):
        c = self.d / "t.c5hf"
        write_c5hf(c, self.h, self.m, cut=100)
        self.refused(self.base(b=c), "truncated")

    def test_neg_c5hf_flags(self):
        c = self.d / "f.c5hf"
        write_c5hf(c, self.h, self.m, flags=7)
        self.refused(self.base(b=c), "unknown flags")


class PictureCompare(unittest.TestCase):
    def setUp(self):
        from PIL import Image
        self.Image = Image
        self.tmp = tempfile.TemporaryDirectory()
        self.d = Path(self.tmp.name)
        rng = np.random.default_rng(3)
        self.A = rng.integers(0, 200, size=(60, 80, 3), dtype=np.uint8)
        self.a = self.save(self.A, "a.png")

    def tearDown(self):
        self.tmp.cleanup()

    def save(self, arr, name):
        p = self.d / name
        self.Image.fromarray(arr.astype(np.uint8)).save(p)
        return p

    def test_identical(self):
        code, r = run(piccmp.main, [str(self.a), str(self.a), "--rect", "0,0,80,60"])
        self.assertEqual(code, 0)
        self.assertEqual((r["median"], r["p95"], r["max"], r["over_thr"]), (0.0, 0.0, 0, 0))
        self.assertEqual(r["pixels"], 4800)

    def test_known_difference_in_region_only(self):
        B = self.A.astype(np.int16).copy()
        B[10:20, 10:20, 1] += 40            # 100 pixels by 40 inside the region
        B[50:55, 70:75, 0] += 50            # outside the region: not counted
        b = self.save(B, "b.png")
        diff = self.d / "diff.png"
        code, r = run(piccmp.main, [str(self.a), str(b), "--rect", "0,0,40,40", "--diff", str(diff)])
        self.assertEqual(code, 0)
        self.assertEqual(r["pixels"], 1600)
        self.assertEqual(r["max"], 40)
        self.assertEqual(r["over_thr"], 100)
        self.assertEqual(r["median"], 0.0)
        self.assertEqual(r["p95"], 40.0)    # 100 of 1600 = 6.25 % of the pixels differ by 40
        self.assertEqual(r["bbox"], [0, 0, 40, 40])
        with self.Image.open(diff) as im:
            self.assertEqual(im.size, (40 * 3 + 8, 40))

    def test_mask(self):
        B = self.A.astype(np.int16).copy()
        B[0:2, 0:2] += 30
        b = self.save(B, "b.png")
        M = np.zeros_like(self.A)
        M[0:4, 0:4] = 255
        mk = self.save(M, "mask.png")
        code, r = run(piccmp.main, [str(self.a), str(b), "--mask", str(mk)])
        self.assertEqual(code, 0)
        self.assertEqual(r["pixels"], 16)
        self.assertEqual(r["over_thr"], 4)

    def refused(self, argv, what):
        code, out = run(piccmp.main, argv)
        self.assertEqual(code, 2, out)
        self.assertIn("piccmp: REFUSED", out)
        self.assertIn(what, out)

    def test_neg_missing(self):
        self.refused([str(self.a), str(self.d / "no.png"), "--rect", "0,0,4,4"], "not found")

    def test_neg_unreadable(self):
        p = self.d / "x.png"
        p.write_bytes(b"not a png")
        self.refused([str(self.a), str(p), "--rect", "0,0,4,4"], "unreadable")

    def test_neg_size(self):
        b = self.save(self.A[:50], "small.png")
        self.refused([str(self.a), str(b), "--rect", "0,0,4,4"], "sizes differ")

    def test_neg_no_region(self):
        self.refused([str(self.a), str(self.a)], "no region")

    def test_neg_both(self):
        self.refused([str(self.a), str(self.a), "--rect", "0,0,4,4", "--mask", str(self.a)], "not both")

    def test_neg_rect_text(self):
        self.refused([str(self.a), str(self.a), "--rect", "0,0,4"], "X0,Y0,X1,Y1")

    def test_neg_rect_empty(self):
        self.refused([str(self.a), str(self.a), "--rect", "5,5,5,9"], "empty")

    def test_neg_rect_outside(self):
        self.refused([str(self.a), str(self.a), "--rect", "70,50,90,60"], "not inside")

    def test_neg_mask_size(self):
        mk = self.save(np.full((10, 10, 3), 255), "m.png")
        self.refused([str(self.a), str(self.a), "--mask", str(mk)], "mask")

    def test_neg_mask_empty(self):
        mk = self.save(np.zeros_like(self.A), "m0.png")
        self.refused([str(self.a), str(self.a), "--mask", str(mk)], "no pixel set")

    def test_neg_thr(self):
        self.refused([str(self.a), str(self.a), "--rect", "0,0,4,4", "--thr", "300"], "--thr")

    def test_neg_gain(self):
        self.refused([str(self.a), str(self.a), "--rect", "0,0,4,4", "--gain", "0"], "--gain")


if __name__ == "__main__":
    unittest.main()
