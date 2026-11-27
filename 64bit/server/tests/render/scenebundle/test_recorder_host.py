"""Epic 0301, task 3904: the scene recorder inside the render host (render_test_record*), against the fake composite.

The render host test program (render_host_test.exe, built by server/tests/render/build_render_tests.py) runs the render
objects server.exe links, from a file of server.cfg keys, without the DLL; the fake composite is renderfx (camera block,
height-field tap) as in server/tests/render/test_render_host.py. Every process on a hidden desktop (COMMON 14). The
bundle check is 3902's CheckBundle through recorder_test.exe --check (built by build_scenebundle_tests.py).

    set C5_RENDER_TEST_BUILD=<build_render_tests.py --out>/bin
    set C5_SCENEBUNDLE_TEST_BUILD=<build_scenebundle_tests.py --out>      (bin/recorder_test.exe; bin-asan/ with C5_SCENEBUNDLE_ASAN=1)
    python -m pytest -q server/tests/render/scenebundle/test_recorder_host.py

A missing build fails (never skips: COMMON 9). Each negative asserts its output and its exit code.
"""
import json
import os
import re
import subprocess
import time
from pathlib import Path

import pytest

import renderfx as fx

NOWIN = 0x08000000


@pytest.fixture
def rig(tmp_path, hidden_desktop, built, exe, request):
    r = fx.Rig(tmp_path, hidden_desktop, built, exe, request.node.name[:28].replace("[", "_").replace("]", ""))
    yield r
    r.close()


@pytest.fixture(scope="session")
def checker():
    """recorder_test.exe of build_scenebundle_tests.py --out DIR: C5_SCENEBUNDLE_TEST_BUILD = DIR (bin/, or bin-asan/
    with C5_SCENEBUNDLE_ASAN=1), the convention of test_sources.py."""
    d = os.environ.get("C5_SCENEBUNDLE_TEST_BUILD", "")
    sub = "bin-asan" if os.environ.get("C5_SCENEBUNDLE_ASAN") == "1" else "bin"
    p = Path(d) / sub / "recorder_test.exe"
    if not d or not p.exists():
        pytest.fail("C5_SCENEBUNDLE_TEST_BUILD must name a build folder with %s/recorder_test.exe" % sub)
    return p


def check(checker, bundle):
    cp = subprocess.run([str(checker), "--check", str(bundle)], capture_output=True, text=True, creationflags=NOWIN, timeout=120)
    return cp.returncode, (cp.stdout + cp.stderr).strip()


def consumer_opened(c, out, timeout=20.0):
    """As test_render_host.py's: wait until fake_consumer.exe has opened the shared texture before the cameras run."""
    t_end = time.time() + timeout
    while time.time() < t_end:
        try:
            if out.exists() and '"opened"' in out.read_text(errors="replace"):
                return
        except OSError:
            pass
        if not c.alive():
            break
        time.sleep(0.05)
    raise AssertionError("fake_consumer did not open the texture: " + (out.read_text(errors="replace")[-1000:] if out.exists() else "no output"))


def run_cameras(rig, n, hz=40.0):
    for f in range(1, n + 1):
        rig.sec.write_camera(fx.synth_camera(frame=f, pan=float(f % 7)))
        time.sleep(1.0 / hz)


def scene_json(bundle):
    return json.loads((Path(bundle) / "scene.json").read_text(encoding="utf-8"))


def test_keys_absent_no_recorder(rig, tmp_path):
    """Without render_test_record no recorder exists: no recorder line, no bundle, the frames as before."""
    rig.tap()
    p = rig.start(seconds=3, render_test_dump=str(rig.dump), render_test_dump_after=1)
    c, out = rig.consumer(frames=6)
    consumer_opened(c, out)
    run_cameras(rig, 30)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert not re.search(r"recorder: |recorder off", rig.text()), rig.text()[-2000:]
    assert not list(tmp_path.glob("**/scene.json"))
    assert rig.dump.exists()


def test_bundle_from_the_host(rig, tmp_path, checker):
    """render_test_record on the v3 path: the field, the feed's drains, a span of 4 frames, every frame dumped; the bundle
    passes 3902's checker; the dumps have the sink's size."""
    bundle = tmp_path / "bundles" / "SYNTH-3904_20261115-000000_test"
    rig.tap()
    p = rig.start(seconds=5, feed_events=20, render_test_record=str(bundle), render_test_record_frames=4,
                  render_test_record_dump="all")
    c, out = rig.consumer(frames=30)
    consumer_opened(c, out)
    run_cameras(rig, 60)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    text = rig.text()
    assert re.search(r"recorder: bundle '.*' finished: 4 frames", text), text[-3000:]
    assert "heightfield/load0001.c5hf written" in text, text[-3000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["frames"]["count"] == 4 and len(m["frames"]["dumped"]) == 4, m["frames"]
    assert m["save"]["name"] == "SYNTH-3904" and m["asset_set"]["machine"] == "test", m["save"]
    assert m["render_config"]["render_handover"] == "v3" and m["recorder"]["keys"]["render_test_record_frames"] == "4"
    # ticket 4737 (rule A): the fights' approach rule and the tier gate as the live side ran them (tier gate default 1;
    # charge_any default 1 again since ticket 4743 W81, MC #11142, 0 from W46 to W81; the "0" arm:
    # test_bundle_carries_charge_any_off)
    assert m["render_config"]["render_test_units_charge_any"] == "1", m["render_config"]
    assert m["render_config"]["render_test_units_tier_gate"] == "1", m["render_config"]
    # ticket 4735 (rule A): the turn before a fight whose figures stand, as the live side ran it (default 1)
    assert m["render_config"]["render_test_units_turn_first"] == "1", m["render_config"]
    # ticket 4744 (rule A): a fight's standing figures are the drawn ones, as the live side ran it (default 1)
    assert m["render_config"]["render_units_fight_drawn_figures"] == "1", m["render_config"]
    # tickets 4745 + 4747 (rule A): the reform before a fight, as the live side ran it (default 1)
    assert m["render_config"]["render_test_units_reform_first"] == "1", m["render_config"]
    # ticket 4742 (rule A): the walk home inside the fight, as the live side ran it (default 1)
    assert m["render_config"]["render_test_units_walk_home_in_fight"] == "1", m["render_config"]
    # ticket 0796 (rule A; TEST ONLY): the Quick Combat rate as the live side ran it (default 0: every fight at real time)
    assert m["render_config"]["render_test_units_quick_combat"] == "0", m["render_config"]
    # ticket 4748 (rule A): a melee attack on a city strikes in place, as the live side ran it (default 1)
    assert m["render_config"]["render_test_units_city_in_place"] == "1", m["render_config"]
    # ticket 4746 (rule A): which waiting figures enter combat ready, as the live side ran it (default 1)
    assert m["render_config"]["render_test_units_ready_after_move"] == "1", m["render_config"]
    assert m["render_config"]["render_test_units_defend_from_stance"] == "1", m["render_config"]   # ticket 4750
    # ticket 5050 (rule A): stock's phases 1 + 2, as the live side ran them (default 1 since 5050 W74, MC #10667 / #10726;
    # the "0" arm: test_bundle_carries_the_5050_keys_off)
    assert m["render_config"]["render_test_units_reform_ends_at_arrival"] == "1", m["render_config"]
    assert m["render_config"]["render_test_units_attack_from_stance"] == "1", m["render_config"]
    # ticket 5057 (rule A): the rush ride-through and the type 2 defender wait as the live side ran them (default 1, MC
    # #11162; the "0" arm: test_bundle_carries_the_5057_keys_off)
    assert m["render_config"]["render_units_rush_ride_through"] == "1", m["render_config"]
    assert m["render_config"]["render_units_defender_waits_first_landing"] == "1", m["render_config"]
    # ticket 5053 (rule A): the death by variant and the end after the dying as the live side ran them (default 1 since
    # the MC's word of 2026-11-25; the "0" arm: test_bundle_carries_the_5053_keys_off)
    assert m["render_config"]["render_test_units_death_by_variant"] == "1", m["render_config"]
    assert m["render_config"]["render_test_units_end_after_dying"] == "1", m["render_config"]
    # ticket 5059 (rule A): the victory rule as the live side ran it (default 1, lead-0042 GO #11696; the "0" arm:
    # test_bundle_carries_the_5059_key_off)
    assert m["render_config"]["render_units_victory_word"] == "1", m["render_config"]
    # ticket 5066 (rule A): the fighter's walks around its fight as the live side ran them (default 1, lead-0042 #12574)
    assert m["render_config"]["render_units_fight_after_walk"] == "1", m["render_config"]
    # ticket 5067 S1 (rule A): the type 2 phase 2 as the live side ran it (default 1, MC #12907; the "0" arm:
    # test_bundle_carries_the_5067_key_off)
    assert m["render_config"]["render_units_type2_phase2"] == "1", m["render_config"]
    # ticket 0743 (rule A): stock's melee reach as the live side ran it (default 1; the "0" arm:
    # test_bundle_carries_the_0743_key_off)
    assert m["render_config"]["render_units_melee_reach"] == "1", m["render_config"]
    # ticket 5061 (rule A): stock's type 2 defender step as the live side ran it (default 1; the "0" arm:
    # test_bundle_carries_the_5061_key_off)
    assert m["render_config"]["render_units_defender_step"] == "1", m["render_config"]
    # ticket 5065 (rule A): a walking unit's work and fortify after its walk, as the live side ran it (default 1)
    assert m["render_config"]["render_units_orders_after_walk"] == "1", m["render_config"]
    # ticket 5052 (rule A): the trigger names as the live side ran them (striker weapon default 1 since W76, MC #10847;
    # terrain default 1 since W89, MC #11356; the "0" arm: test_bundle_carries_trigger_terrain_off)
    assert m["render_config"]["render_test_units_trigger_striker_weapon"] == "1", m["render_config"]
    assert m["render_config"]["render_test_units_trigger_terrain"] == "1", m["render_config"]
    # ticket 5064 (rule A): a held projectile's pose and member scale, as the live side ran it (default 1)
    assert m["render_config"]["render_units_projectile_pose"] == "1", m["render_config"]
    # ticket 5055 (rule A): both sides of a melee named by the striker, the key absent from server.cfg gives its default 1
    # (MC #11607; the "0" arm: test_bundle_carries_trigger_striker_melee_off; sceneshot reads a bundle without it as 0)
    assert m["render_config"]["render_units_trigger_striker_melee"] == "1", m["render_config"]
    # ticket 5058 (rule A): a city striker's sound names by its owner's era, as the live side ran them (default 1)
    assert m["render_config"]["render_units_trigger_city_era"] == "1", m["render_config"]
    # ticket 5062 (rule A): a city's own strike flies its owner era's projectile, as the live side ran it (default 1)
    assert m["render_config"]["render_units_trigger_city_shot"] == "1", m["render_config"]
    assert len(m["recorder"]["server_exe_sha256"]) == 64 and len(m["pins"]["exe_sha256"]) == 64
    for fid in m["frames"]["dumped"]:
        assert (bundle / "dumps" / ("%d.bmp" % fid)).stat().st_size == 54 + fx.W * fx.H * 4
        assert (bundle / "dumps" / ("%d.depth.f32" % fid)).stat().st_size == fx.W * fx.H * 4


def test_bundle_carries_the_5050_keys_off(rig, tmp_path, checker):
    """Ticket 5050 (W74): with render_test_units_reform_ends_at_arrival=0 and render_test_units_attack_from_stance=0 (the
    rules before 5050; both 1 by default since W74) the bundle's render_config carries "0" for each, so sceneshot replays
    the fights as they ran."""
    bundle = tmp_path / "bundles" / "SYNTH-5050_20261122-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_test_units_reform_ends_at_arrival=0, render_test_units_attack_from_stance=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_test_units_reform_ends_at_arrival"] == "0", m["render_config"]
    assert m["render_config"]["render_test_units_attack_from_stance"] == "0", m["render_config"]


def test_bundle_carries_the_5057_keys_off(rig, tmp_path, checker):
    """Ticket 5057 (W85): with render_units_rush_ride_through=0 and render_units_defender_waits_first_landing=0 (the
    rules before 5057; both 1 by default) the bundle's render_config carries "0" for each, so sceneshot replays the
    fights as they ran."""
    bundle = tmp_path / "bundles" / "SYNTH-5057_20261123-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_units_rush_ride_through=0, render_units_defender_waits_first_landing=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_units_rush_ride_through"] == "0", m["render_config"]
    assert m["render_config"]["render_units_defender_waits_first_landing"] == "0", m["render_config"]


def test_bundle_carries_the_5053_keys_off(rig, tmp_path, checker):
    """Ticket 5053 (W112; the off arm since W153): with render_test_units_death_by_variant=0 and
    render_test_units_end_after_dying=0 (the rules before 5053; both 1 by default since the MC's word of 2026-11-25) the
    bundle's render_config carries "0" for each, so sceneshot replays the fights as they ran."""
    bundle = tmp_path / "bundles" / "SYNTH-5053_20261123-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_test_units_death_by_variant=0, render_test_units_end_after_dying=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_test_units_death_by_variant"] == "0", m["render_config"]
    assert m["render_config"]["render_test_units_end_after_dying"] == "0", m["render_config"]


def test_bundle_carries_the_5059_key_off(rig, tmp_path, checker):
    """Ticket 5059 (W100): with render_units_victory_word=0 (the rule before 5059: a survivor whose machine takes 1800 may
    take victory; 1 by default) the bundle's render_config carries "0", so sceneshot replays the fights as they ran."""
    bundle = tmp_path / "bundles" / "SYNTH-5059_20261123-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_units_victory_word=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_units_victory_word"] == "0", m["render_config"]


def test_bundle_carries_the_5067_key_off(rig, tmp_path, checker):
    """Ticket 5067 S1 (W122): with render_units_type2_phase2=0 (no phase 2 in a charge fight; 1, stock's type 2 phase 2,
    by default since MC #12907) the bundle's render_config carries "0", so sceneshot replays the fights as they ran."""
    bundle = tmp_path / "bundles" / "SYNTH-5067_20261124-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_units_type2_phase2=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_units_type2_phase2"] == "0", m["render_config"]


def test_bundle_carries_the_0743_key_off(rig, tmp_path, checker):
    """Ticket 0743 (W137): with render_units_melee_reach=0 (every approach ends MoveRadius short of a doomed figure; 1,
    stock's melee reach, by default) the bundle's render_config carries "0", so sceneshot replays the fights as they ran."""
    bundle = tmp_path / "bundles" / "SYNTH-0743_20261125-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_units_melee_reach=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_units_melee_reach"] == "0", m["render_config"]


def test_bundle_carries_the_5061_key_off(rig, tmp_path, checker):
    """Ticket 5061 (W139): with render_units_defender_step=0 (the type 2 defender strikes from where it stands; 1, stock's
    step to the attackers from the first landing, by default) the bundle's render_config carries "0", so sceneshot replays
    the fights as they ran."""
    bundle = tmp_path / "bundles" / "SYNTH-5061_20261125-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_units_defender_step=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_units_defender_step"] == "0", m["render_config"]


def test_bundle_carries_charge_any_off(rig, tmp_path, checker):
    """Ticket 4743 (W81): with render_test_units_charge_any=0 (the 4775 rush-only rule; 1 by default again since W81) the
    bundle's render_config carries "0", so sceneshot replays the fights as they ran."""
    bundle = tmp_path / "bundles" / "SYNTH-4743_20261123-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_test_units_charge_any=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_test_units_charge_any"] == "0", m["render_config"]


def test_bundle_carries_trigger_terrain_off(rig, tmp_path, checker):
    """Ticket 5052 part D (W89): with render_test_units_trigger_terrain=0 (the rule before 5052, $(TERRAIN) DIRT; 1 by
    default since W89, MC #11356) the bundle's render_config carries "0", as the live side ran it."""
    bundle = tmp_path / "bundles" / "SYNTH-5052_20261123-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_test_units_trigger_terrain=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_test_units_trigger_terrain"] == "0", m["render_config"]


def test_bundle_carries_projectile_pose_off(rig, tmp_path, checker):
    """Ticket 5064 (W110): with render_units_projectile_pose=0 (the rule before 5064: a held projectile at scale 1 and the
    identity orientation; 1 by default) the bundle's render_config carries "0", as the live side ran it."""
    bundle = tmp_path / "bundles" / "SYNTH-5064_20261123-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_units_projectile_pose=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_units_projectile_pose"] == "0", m["render_config"]


def test_bundle_carries_trigger_striker_melee_off(rig, tmp_path, checker):
    """Ticket 5055 (W93): with render_units_trigger_striker_melee=0 (5052's rule: only a unit that does not strike back
    is named by its opponent; 1 by default, MC #11607) the bundle's render_config carries "0", as the live side ran it."""
    bundle = tmp_path / "bundles" / "SYNTH-5055_20261123-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_units_trigger_striker_melee=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_units_trigger_striker_melee"] == "0", m["render_config"]


def test_bundle_carries_trigger_city_era_off(rig, tmp_path, checker):
    """Ticket 5058 (W94): with render_units_trigger_city_era=0 (the rule before 5058: a unit struck by a city keeps its own
    weapon's names; 1 by default) the bundle's render_config carries "0", as the live side ran it."""
    bundle = tmp_path / "bundles" / "SYNTH-5058_20261123-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_units_trigger_city_era=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_units_trigger_city_era"] == "0", m["render_config"]


def test_bundle_carries_trigger_city_shot_off(rig, tmp_path, checker):
    """Ticket 5062 (W98): with render_units_trigger_city_shot=0 (the rule before 5062: a city's strike names no projectile;
    1 by default) the bundle's render_config carries "0", as the live side ran it."""
    bundle = tmp_path / "bundles" / "SYNTH-5062_20261123-000000_test"
    rig.tap()
    p = rig.start(seconds=4, feed_events=20, render_test_record=str(bundle), render_test_record_frames=2,
                  render_units_trigger_city_shot=0)
    c, out = rig.consumer(frames=20)
    consumer_opened(c, out)
    run_cameras(rig, 40)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    code, line = check(checker, bundle)
    assert code == 0 and line.endswith("scenebundle: OK"), line
    m = scene_json(bundle)
    assert m["render_config"]["render_units_trigger_city_shot"] == "0", m["render_config"]


def test_unwritable_folder_is_loud_and_the_renderer_goes_on(rig, tmp_path):
    """A folder the recorder cannot use (under a file): LOUD, no recorder, the renderer draws and publishes as before."""
    f = tmp_path / "a_file"
    f.write_bytes(b"x")
    rig.tap()
    p = rig.start(seconds=3, render_test_record=str(f / "bundle"))
    c, out = rig.consumer(frames=6)
    consumer_opened(c, out)
    run_cameras(rig, 30)
    assert rig.finish(p) == 0, rig.stdout()[-2000:]
    assert re.search(r"LOUD: recorder off: render_test_record=.*a_file.*bundle: .*; the game goes on", rig.text()), rig.text()[-2000:]
    s, line = fx.summary(rig.stdout())
    assert s["state"] == "STOPPED" and s["published"] > 0, line
    assert f.read_bytes() == b"x"


@pytest.mark.parametrize("keys,refused", [
    ({"render_test_record": "bundles/rel"}, "render_test_record=bundles/rel: an absolute folder"),
    ({"render_test_record": "C:/c5-64bit-work/x", "render_test_record_frames": "0"}, "render_test_record_frames=0: an integer"),
    ({"render_test_record": "C:/c5-64bit-work/x", "render_test_record_dump": "first"},
     "render_test_record_dump=first: first,last, all or every:N"),
    ({"render_test_record_frames": "4"}, "render_test_record_frames=4: needs render_test_record"),
])
def test_bad_key_refused_at_parse(rig, tmp_path, keys, refused):
    """A bad recorder key refuses the start like every render_* key (exit 2, the key and value named), and nothing is
    written: no bundle folder."""
    p = rig.start(seconds=1, **keys)
    assert rig.finish(p) == 2, rig.stdout()[-2000:]
    assert ("result: config refused: " + refused) in rig.stdout(), rig.stdout()
    assert rig.frame.server()["magic"] == 0
    assert not Path("C:/c5-64bit-work/x").exists()
