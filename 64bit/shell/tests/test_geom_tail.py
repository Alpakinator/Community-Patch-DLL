"""Issue 3206 (epic 3200, W7b): shell/tools/geom_tail.py, the geometry tail split by kind.

Every input check of the tool with its exit code and message, on small synthetic inputs (no game, no build, no files
outside the test's temporary folder), plus the cause order and the plot-kind rule. Runs under pytest or directly:
`python shell/tests/test_geom_tail.py` (prints passed / failed / skipped counts; exit 1 on any failure).
"""
import json
import math
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

TOOL = Path(__file__).resolve().parents[1] / "tools" / "geom_tail.py"
sys.path.insert(0, str(TOOL.parent))
import geom_tail as gt  # noqa: E402

W, H = 64, 48
POINT = (5, 5)


def run(*args):
    p = subprocess.run([sys.executable, str(TOOL), *map(str, args)], capture_output=True, text=True, timeout=120)
    return p.returncode, p.stdout, p.stderr


def make_inputs(d: Path, *, world_size=(W, H), json_size=None, hl_size=(W, H), camera=None, plots=None, magenta=False):
    """A minimal single-camera input set in d; returns the argument list of geom_tail.py."""
    gtd = d / "gt"
    gtd.mkdir(exist_ok=True)
    (gtd / "grid_constants.json").write_text(json.dumps({
        "M_h2w": [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]], "grid_sx": 1.0, "grid_sy": 1.0,
        "grid_ox": 0.0, "grid_oy": 0.0, "wrap_x": 0}))
    plain = np.zeros((H, W, 4), np.uint8)
    plain[..., 3] = 255
    hl = np.zeros((hl_size[1], hl_size[0], 4), np.uint8)
    hl[..., 3] = 255
    if magenta:
        hl[10:12, :, :3] = (255, 0, 255)
    Image.fromarray(hl).save(d / "hl.png")
    Image.fromarray(plain).save(d / "plain.png")
    ww, wh = world_size
    world = np.zeros((wh, ww, 4), np.float32)
    world[..., 3] = 1.0
    world.tofile(d / "world.f32")
    jw, jh = json_size or world_size
    (d / "world.f32.json").write_text(json.dumps({"width": jw, "height": jh}))
    cam = camera if camera is not None else {"views": [np.eye(4).tolist()], "proj": np.eye(4).tolist(),
                                              "viewport": {"X": 0, "Y": 0, "Width": W, "Height": H}}
    (d / "cam.json").write_text(json.dumps(cam))
    if plots is None:
        plots = ";".join("%d,%d,-,-,-,0,,rev,2" % (x, y) for x in range(0, 12) for y in range(0, 12))
    (d / "plots.json").write_text(json.dumps({"plots": plots}))
    return ["--gt-dir", gtd, "--hl-on", d / "hl.png", "--hl-off", d / "plain.png", "--world", d / "world.f32",
            "--camera", d / "cam.json", "--plots", d / "plots.json", "--points", "%d,%d" % POINT]


def expect(rc_out_err, code, needle):
    rc, out, err = rc_out_err
    assert rc == code, (rc, out, err)
    assert needle in err, err
    assert out == "", out   # nothing printed on stdout after a refusal


# --- usage and option checks (exit 2) ------------------------------------------------------------------------------

def test_both_modes_refused(tmp_path):
    expect(run(*make_inputs(tmp_path), "--w5-run", tmp_path), 2, "not both")


def test_edge_frac_out_of_range(tmp_path):
    expect(run(*make_inputs(tmp_path), "--edge-frac", "0.7"), 2, "--edge-frac must lie in (0, 0.5)")


def test_water_band_z_not_finite(tmp_path):
    expect(run(*make_inputs(tmp_path), "--water-band-z", "nan"), 2, "--water-band-z must be a finite number")


def test_lowland_z_negative(tmp_path):
    expect(run(*make_inputs(tmp_path), "--lowland-z", "-1"), 2, "--lowland-z must be a finite number")


def test_water_z_exe_too_large(tmp_path):
    expect(run(*make_inputs(tmp_path), "--water-z-exe", "1e9"), 2, "--water-z-exe must be a finite number")


def test_gt_dir_missing(tmp_path):
    a = make_inputs(tmp_path)
    a[1] = tmp_path / "nope"
    expect(run(*a), 2, "--gt-dir: file missing")


def test_single_camera_incomplete(tmp_path):
    a = make_inputs(tmp_path)
    i = a.index("--plots")
    del a[i:i + 2]
    expect(run(*a), 2, "are all required")


def test_hl_on_missing(tmp_path):
    a = make_inputs(tmp_path)
    a[a.index("--hl-on") + 1] = tmp_path / "missing.png"
    expect(run(*a), 2, "--hl-on: file missing")


def test_world_json_missing(tmp_path):
    a = make_inputs(tmp_path)
    (tmp_path / "world.f32.json").unlink()
    expect(run(*a), 2, "--world .json: file missing")


def test_w5_layout_incomplete(tmp_path):
    a = make_inputs(tmp_path)
    expect(run("--gt-dir", a[1], "--w5-run", tmp_path), 2, "needs --w5-run, --w5-stock and --w5-census")


def test_bad_option_is_usage(tmp_path):
    rc, out, err = run("--gt-dir", tmp_path, "--edge-frac", "abc")
    assert rc == 2 and "invalid float value" in err and out == "", (rc, out, err)


# --- inconsistent inputs (exit 3) ----------------------------------------------------------------------------------

def test_points_malformed(tmp_path):
    a = make_inputs(tmp_path)
    a[a.index("--points") + 1] = "5;5"
    expect(run(*a), 3, "--points: want X,Y;X,Y")


def test_camera_without_proj(tmp_path):
    expect(run(*make_inputs(tmp_path, camera={"views": [np.eye(4).tolist()], "viewport": {}})), 3, "has no 'proj'")


def test_world_json_size_differs(tmp_path):
    expect(run(*make_inputs(tmp_path, json_size=(W, H + 1))), 3, "its .json says")


def test_shot_sizes_differ(tmp_path):
    expect(run(*make_inputs(tmp_path, hl_size=(W + 2, H))), 3, "differ in size")


def test_plot_list_lacks_neighbour(tmp_path):
    plots = ";".join("%d,%d,-,-,-,0,,rev,2" % (x, y) for x in range(0, 12) for y in range(0, 12) if (x, y) != (6, 5))
    expect(run(*make_inputs(tmp_path, plots=plots)), 3, "is not listed")


def test_plot_list_malformed(tmp_path):
    expect(run(*make_inputs(tmp_path, plots="1,2,3")), 3, "want 9, 10 or 11")


def test_hl_not_an_image(tmp_path):
    a = make_inputs(tmp_path)
    (tmp_path / "hl.png").write_bytes(b"not a png")
    expect(run(*a), 3, "not an image")


def test_w5_without_highlight_views(tmp_path):
    a = make_inputs(tmp_path)
    (tmp_path / "terrain_live.json").write_text(json.dumps({"views": [{"name": "loadview"}]}))
    expect(run("--gt-dir", a[1], "--w5-run", tmp_path, "--w5-stock", tmp_path, "--w5-census", tmp_path), 3,
           "no view with highlight_points")


# --- nothing to measure (exit 4) -----------------------------------------------------------------------------------

def test_no_band_pixels(tmp_path):
    expect(run(*make_inputs(tmp_path)), 4, "too few band pixels on the drawn terrain: 0")


# --- logic ---------------------------------------------------------------------------------------------------------

def row(**kw):
    r = {"contaminated": False, "beside_model": False, "median_px": 4.0, "median_px_completed": 4.0, "lowland": False,
         "region": "centre", "shared": False, "kind": "flat"}
    r.update(kw)
    return r


def test_cause_order():
    assert gt.cause_of(row(contaminated=True, beside_model=True)) == "contaminated"
    assert gt.cause_of(row(beside_model=True, lowland=True)) == "beside_model"
    assert gt.cause_of(row(kind="coast_edge", median_px_completed=1.0)) == "water_truncated"
    assert gt.cause_of(row(kind="coast_edge", median_px_completed=4.1)) == "shore_other"
    assert gt.cause_of(row(lowland=True, region="edge")) == "river_bed"
    assert gt.cause_of(row(region="edge", shared=True)) == "screen_edge"
    assert gt.cause_of(row(shared=True)) == "shared_edge"
    assert gt.cause_of(row()) == "unexplained"


def test_plot_kind_rule():
    table = {(x, y): {"x": x, "y": y, "plot_type": 2, "city": "", "units": 0, "improvement": "-", "resource": "-"}
             for x in range(0, 12) for y in range(0, 12)}
    across = gt.neighbour(5, 5, 1, None)              # edge 0 faces AXIAL[1]
    table[across]["plot_type"] = 3
    assert gt.plot_kind(table, 5, 5, 0, None)[0] == "coast_edge"
    assert gt.plot_kind(table, 5, 5, 3, None)[0] == "coast"
    table[(5, 5)]["plot_type"] = 3
    assert gt.plot_kind(table, 5, 5, 3, None)[0] == "water"
    table[(5, 5)]["plot_type"] = 1
    table[across]["plot_type"] = 2
    assert gt.plot_kind(table, 5, 5, 3, None)[0] == "hills"


def test_stats_empty_and_signed():
    assert gt.stats([]) == {"n": 0}
    s = gt.stats([{"median_px": -4.0}, {"median_px": 1.0}])
    assert s["n"] == 2 and s["over3"] == 1 and math.isclose(s["signed_mean"], -1.5)


if __name__ == "__main__":
    passed = failed = 0
    for name, f in sorted(globals().items()):
        if not name.startswith("test_") or not callable(f):
            continue
        try:
            if f.__code__.co_argcount:
                with tempfile.TemporaryDirectory() as t:
                    f(Path(t))
            else:
                f()
            passed += 1
        except Exception as e:  # noqa: BLE001
            failed += 1
            print("FAIL %s: %r" % (name, e))
    print("passed %d, failed %d, skipped 0" % (passed, failed))
    sys.exit(1 if failed else 0)
