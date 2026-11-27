"""Ticket 0804 (beta 4 player report: with MSAASamples = 8 the whole map is grey and the game crashes after about 10 s):
the insertion of shell_composite.dll on a multisampled back buffer and scene depth, on the stand-in. test_host.exe --msaa N
makes the swap chain and its D24S8 depth N-sampled, as the game does with GraphicsSettingsDX11.ini's MSAASamples = N (the
EXE draws its scene straight into the multisampled back buffer: research/05-graphics-composition/trees.md 11.5, and
shared-texture-and-compositor.md section 16 for this ticket). Every process runs on the session's hidden desktop (rule 14).

The server (lockstep_server.exe) draws x < 580 at depth 0.5 (1.0 elsewhere); the host's units list (depth LESS, after the
insertion) draws a yellow quad at z 0.8 and a cyan one at z 0.6 that reach into that region and an orange one at z 0.3
(test_lockstep.py's test_depth_written_into_the_scene_clear_dsv, the single-sample case). With the server's depth in the
EXE's depth buffer the yellow and the inner cyan quad lose inside the region; the composite's .shot dump resolves the
multisampled back buffer first. Negative cases check the refusal lines and counters of a depth view the insertion cannot
write. Skipped counts as failed (conftest.py)."""
import time

import pytest

import shellrun
from test_lockstep import HOST, INI, SERVER_S, LsRun, EXE_TAPS_OFF, lrun  # noqa: F401  (lrun: the fixture)

YELLOW_IN, CYAN_IN, CYAN_OUT, FREE_OUT, SERVER_IN = (500, 550), (550, 620), (590, 550), (1000, 300), (100, 100)
HOST_GREY = (38, 38, 38)   # test_host's clear colour


def is_server(px):
    """lockstep_server's colour inside --region (its frame id: R, G small, B any; seen 2026-11-26: (6, 8, 17..251)), not the
    host's grey (38, 38, 38), its UI panel (17, 17, 22) or its yellow and cyan quads."""
    return px not in (HOST_GREY, (17, 17, 22), (255, 255, 0), (0, 255, 255)) and px[0] <= 10 and px[1] <= 12


def _run_depth_case(lrun, samples, scene=False, **ini):
    """scene=False: the swap chain is N-sampled (EXE build 26577625's form); scene=True: the swap chain is single-sampled
    and the host draws its scene into an N-sampled target of the back buffer's size that it resolves into the back buffer
    before the UI list (test_host --scene-msaa N: build 4390913's form, the beta 4 report)."""
    keys = dict(INI, depth_write="1")
    keys.update(ini)
    lrun.set_ini(**keys)
    lrun.start_lockstep_server("--region", "0,0,580,720", "--depth-value", "0.5", seconds=SERVER_S)
    lrun.start_host(*HOST, "--scene-msaa" if scene else "--msaa", str(samples), seconds=7)
    time.sleep(4.0)
    img = shellrun.Image(lrun.shot())
    lines = lrun.finish()
    return img, lines, lrun.ls_lines()


@pytest.mark.parametrize("samples", [1, 2, 4, 8])
def test_depth_written_into_a_multisampled_scene_depth(lrun, samples):
    """The server's depth reaches the EXE's N-sampled depth buffer: the host's units behind it lose on screen, the ones in
    front and outside the region win; the depth counter runs and nothing is refused; the adoption logs the sample count."""
    img, lines, ls = _run_depth_case(lrun, samples)
    assert any("back buffer resource" in ln and ("samples %d" % samples) in ln for ln in lines), [ln for ln in lines if "back buffer resource" in ln][:2]
    assert any("depth target: the scene-clear DSV" in ln for ln in lines), [ln for ln in lines if "depth" in ln][:5]
    assert not any("depth NOT written" in ln for ln in lines), [ln for ln in lines if "depth NOT written" in ln][:2]
    assert not any("scene target:" in ln for ln in lines), [ln for ln in lines if "scene target:" in ln][:2]   # the back buffer is the scene
    assert sum(x["depth"] for x in ls) > 100, ls[-1]
    assert sum(x["depth_refused"] + x["depth_nodsv"] + x["depth_nosrc"] for x in ls[3:]) == 0, ls[-1]
    for p in (YELLOW_IN, CYAN_IN):   # lost to the server's depth: the server's colour (R = frame id >> 16 = 0)
        r, g, b = img.at(*p)
        assert r == 0 and (r, g, b) not in ((255, 255, 0), (0, 255, 255)), (samples, p, (r, g, b))
    assert img.at(*CYAN_OUT) == (0, 255, 255), (samples, img.at(*CYAN_OUT))
    assert is_server(img.at(*SERVER_IN)), (samples, img.at(*SERVER_IN))


def test_depth_write_off_on_msaa_draws_the_world_without_depth(lrun):
    """depth_write=0 on an 8-sampled target: the server's picture is drawn (its colour outside the units), nothing is
    written into the depth (the yellow quad wins inside the region), and no depth counter runs."""
    img, lines, ls = _run_depth_case(lrun, 8, depth_write="0")
    assert img.at(*YELLOW_IN) == (255, 255, 0), img.at(*YELLOW_IN)
    assert is_server(img.at(*SERVER_IN)), img.at(*SERVER_IN)   # the server's picture
    assert img.at(*FREE_OUT) == HOST_GREY, img.at(*FREE_OUT)   # outside --region the server's picture is transparent
    assert sum(x["depth"] for x in ls) == 0


@pytest.mark.parametrize("samples", [1, 2, 4, 8])
def test_scene_msaa_target_takes_the_world_and_its_depth(lrun, samples):
    """Ticket 0804 fix 2: a single-sampled swap chain with the scene in an N-sampled target (no list clears the back
    buffer): the composite takes that target's clear for the scene clear (logged once), inserts after it every frame,
    draws the server's colour and depth into the target and its N-sampled depth, so the host's units behind the server's
    depth lose and the resolved picture shows the server's world. Before the fix every frame was miss_nomatch and the
    picture was the host's grey. samples=1: a single-sampled offscreen scene copied into the back buffer (the Architect's
    second case): recognised by its clear together with a back-buffer-sized depth clear in the same list."""
    img, lines, ls = _run_depth_case(lrun, samples, scene=True)
    assert any("back buffer resource" in ln and "samples 1" in ln for ln in lines), [ln for ln in lines if "back buffer resource" in ln][:2]
    want = ("scene target: the EXE draws its scene into a %d-sample" % samples) if samples > 1 else "scene target: the EXE clears a single-sampled"
    assert sum(want in ln for ln in lines) == 1, [ln for ln in lines if "scene target" in ln][:3]
    assert any("depth target: the scene-clear DSV" in ln and "%d samples" % samples in ln for ln in lines), [ln for ln in lines if "depth" in ln][:5]
    st = shellrun.stats(lines)[2:]
    assert sum(r.get("ins_checked", 0) for r in st) > 100 and sum(r.get("missed_ins", 0) for r in st) <= 2, \
        [(r.get("ins_checked"), r.get("missed_ins"), r.get("miss_nomatch")) for r in st][:6]
    assert sum(x["exact"] for x in ls) > 100 and sum(x["noins"] for x in ls[3:]) == 0, ls[-1]
    assert sum(x["depth"] for x in ls) > 100 and sum(x["depth_refused"] for x in ls[3:]) == 0, ls[-1]
    for p in (YELLOW_IN, CYAN_IN):
        r, g, b = img.at(*p)
        assert (r, g, b) not in ((255, 255, 0), (0, 255, 255), HOST_GREY), (samples, p, (r, g, b))
    assert img.at(*CYAN_OUT) == (0, 255, 255), (samples, img.at(*CYAN_OUT))
    assert is_server(img.at(*SERVER_IN)), (samples, img.at(*SERVER_IN))


def test_scene_msaa_depth_write_off_still_draws_the_world(lrun):
    """depth_write=0 with the scene in an 8-sampled target: the insertion still finds the scene clear and the world is
    drawn into the target (the picture is not the host's grey), and no depth is written (the yellow quad wins)."""
    img, lines, ls = _run_depth_case(lrun, 8, scene=True, depth_write="0")
    assert is_server(img.at(*SERVER_IN)), img.at(*SERVER_IN)
    assert img.at(*YELLOW_IN) == (255, 255, 0), img.at(*YELLOW_IN)
    assert sum(x["depth"] for x in ls) == 0 and sum(x["exact"] for x in ls) > 100
