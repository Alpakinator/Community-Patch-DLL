"""Ticket 0339: the server's fog mask behind the white handle for the EXE's model draws (composite/fow_models.cpp) with its
producer (server/src/render/srv_fow_mask_pub.cpp, run alone as fowmask_producer.exe, C5_FOWMASK_PRODUCER), on the
stand-in (hidden desktop, rule 14). Model: test_overlay_tap.py.

test_host --fake-exe with fow_models_test=<mode>: a synthetic EXE in the DLL's memory (the white and base-layer globals,
a view table whose white entry holds a real D3D11 view, FOWAnim, OverlayProj panning 30 of every 60 frames) runs through
the real install, section, copy, swap, FOWAnim and restore code; the test mode stands in for the proxy's variables
(VP64SHELL_WHITE = 0x2, VP64SHELL_FOWBASE = 0x4). The producer writes the pair generation into texel (0, 0) of mip 0;
the composite reads it back after each copy (TEST VERIFY) against the slot record it copied. Checked: the hand-over and
the swap, the texels and records as one pair, FOWAnim and the lag measured, the consumer's counters in the section;
the white view put back when the server stops and swapped again when it resumes; put back at the process's end; every
install failure and every checked write that does not match (MC #2058) -> FAILED with its reason in the section (the
producer's consumerFailed(), which fails the server's fog install: the D125 route), one LOUD line, nothing swapped; no
white mask (WhiteFogMask = 0) and model_fow=0 -> OFF, nothing wanted; no mask in time -> FAILED TIMEOUT.

Needs C5_SHELL_BUILD (shell/build.cmd) and C5_FOWMASK_PRODUCER (server/tests/render/fow/test_fow_build.py builds
fowmask_producer.exe). A missing binary fails the test (no skip).
"""
import json
import os
import re
import struct
import subprocess
import time
from pathlib import Path

import pytest

import shellrun
import tapcheck

HOST = ("--vsync", "0", "--occluded-sleep", "0", "--load", "after", "--fake-exe")


def producer_exe():
    p = os.environ.get("C5_FOWMASK_PRODUCER", "")
    assert p and Path(p).exists(), ("C5_FOWMASK_PRODUCER must name fowmask_producer.exe "
                                    "(python -I server/tests/render/fow/test_fow_build.py --out DIR)")
    return p


class Producer:
    def __init__(self, pid, tmp_path, *args):
        self.json = tmp_path / ("producer_%d.json" % time.time_ns())
        self.err = tmp_path / ("producer_%d.err" % time.time_ns())
        self.ef = open(self.err, "w")
        self.p = subprocess.Popen([producer_exe(), "--exe-pid", str(pid), "--json", str(self.json), *args],
                                  stdout=subprocess.PIPE, stderr=self.ef, text=True, creationflags=0x08000000)

    def result(self, timeout=60):
        out, _ = self.p.communicate(timeout=timeout)
        self.ef.close()
        assert self.p.returncode == 0, (self.p.returncode, out, self.err.read_text())
        return json.loads(self.json.read_text())

    def log(self):
        return self.err.read_text()


def start_host(run, ini, seconds, deferred=False):
    run.main = tapcheck.Mapping(run.section, create=4096)
    run.main.write(0, struct.pack("<II", tapcheck.MAGIC, tapcheck.VERSION))
    cfg = dict(mode="composite", insert="clear", trace_frames="0", stats_ms="500", overlay_tap="0", water_tap="0",
               model_fow="1", fow_models_need_scene="0")
    cfg.update(ini)
    run.set_ini(**cfg)
    run.start_host(*HOST, *(("--deferred",) if deferred else ()), seconds=seconds)
    return run.host.pid


def flines(run):
    return [ln for ln in run.log_lines() if "fow_models:" in ln]


def finish(run, seconds=40):
    assert run.host.wait(seconds) == 0, shellrun.tail(run.host_log, 10)
    try:
        run.main.close()
    except Exception:
        pass


def lag_lines(run):
    """The source lag (0339's measure: the window the server built the mask in) and the effective one (ticket 0394: the
    window the models read after the move) of the first 10-s report."""
    src = run.wait_log(r"fow_models: 10 s: \d+ map frames, \d+ with a window lag", timeout_s=2)
    eff = run.wait_log(r"fow_models: 10 s shifted: \d+ map frames", timeout_s=2)
    m = re.search(r"totals (\d+) frames, (\d+) exact, (\d+) lag", src)
    e = re.search(r"totals (\d+) effective exact \(residual max ([\d.]+) texels\), (\d+) effective lag .* made: (\d+) shifted, (\d+) resampled \(another "
                  r"size\), (\d+) as built for another size, (\d+) too far, (\d+) views; (\d+) frames whose window moved after the frame start; (\d+) local "
                  r"masks made \((\d+) at a frame start\), fill (\w+), shift (\w+)", eff)
    assert m and e, (src, eff)
    return (dict(frames=int(m.group(1)), exact=int(m.group(2)), lag=int(m.group(3))),
            dict(exact=int(e.group(1)), residual=float(e.group(2)), lag=int(e.group(3)), shifted=int(e.group(4)),
                 resampled=int(e.group(5)), scale=int(e.group(6)), far=int(e.group(7)), views=int(e.group(8)), moved=int(e.group(9)),
                 made=int(e.group(10)), at_start=int(e.group(11)), fill=e.group(12), shift=e.group(13)), eff)


def test_handover_swap_verify_lag_and_restore(run, tmp_path):
    """The producer hands masks over for 13 s in a window that steps by the clock, as the host's synthetic EXE does: the
    composite copies them, swaps the white view after its first copy, moves each mask to the frame's window (a whole-
    texel copy, or the resample pass when the window is 896 units wide on one side and 832 on the other) at the
    frame's first ExecuteCommandList (--deferred: the host runs its frame as a command list), and every read-back finds
    the producer's block at the world origin of the window the mask was moved to (and the fill at the strip's edge).
    The source lag (the server's window) exists on the stepping frames; after the move no frame is off (ticket 0394).
    FOWAnim follows; the verify diagnostic reports; when the producer ends (STOPPED) the white view is put back; no LOUD
    failure."""
    pid = start_host(run, dict(fow_models_test="ok", fow_models_verify="1", fow_models_verify_ms="0", fow_models_fill="zero"),
                     seconds=20,
                     deferred=True)
    prod = Producer(pid, tmp_path, "--seconds", "13", "--hz", "120")
    run.wait_log(r"fow_models: INSTALLED: white handle 0x2 \(view 0x[0-9A-F]+\), cloud base 0x4", timeout_s=10)
    run.wait_log(r"fow_models: SWAPPED: the white handle 0x2 now reads the server's mask", timeout_s=10)
    run.wait_log(r"fow_models: first mask copied \(slot \d, generation \d+, 1024x1024, 11 mips", timeout_s=2)
    run.wait_log(r"fow_models: local mask 1024x1024, 11 mips made .* the fill 'zero'\); window shift on", timeout_s=2)
    tot = run.wait_log(r"fow_models: TEST totals: verify \d+ ok 0 bad; swapped 1", timeout_s=14)
    ok = int(re.search(r"verify (\d+) ok", tot).group(1))
    assert ok >= 50, tot
    src, eff, line = lag_lines(run)
    assert src["frames"] > 0 and src["lag"] > 0, src            # the window steps: the server's masks lag
    assert eff["shifted"] > 0 and eff["at_start"] > 0, line      # moved, also at a frame start
    assert eff["resampled"] > 0 and eff["scale"] == 0, line      # the 896-unit steps: resampled, none left as built
    assert eff["lag"] == 0 and eff["moved"] == 0, line           # no frame reads a window off its own
    assert eff["residual"] <= 0.5 + 1e-3 and eff["fill"] == "zero" and eff["shift"] == "on", line
    # ticket 0407: the programs are built in; nothing loads d3dcompiler_47 (the stand-in host has no other user of it)
    rdy = run.wait_log(r"fow_models: the resample pass is ready \(built-in vs_4_0 / ps_4_0, \d+ \+ \d+ bytes; set-up ([\d.]+) ms; "
                       r"d3dcompiler_47\.dll not loaded\)", timeout_s=2)
    assert float(re.search(r"set-up ([\d.]+) ms", rdy).group(1)) < 1000.0, rdy
    run.wait_log(r"fow_models: first mask copied .* render-thread time [\d.]+ ms: slot open [\d.]+, local textures [\d.]+ "
                         r"\(resample set-up [\d.]+\), copy, move and swap [\d.]+", timeout_s=2)
    run.wait_log(r"fow_models: VERIFY \d+: resampled, shift", timeout_s=4)
    run.wait_log(r"fow_models: VERIFY \d+: shift, shift -?\d+,-?\d+ texels \(exact -?[\d.]+,-?[\d.]+\) against the mask built for that window", timeout_s=2)
    r = prod.result()
    assert r["failed"] is False and r["published"] > 100 and r["faults"] == 0, r
    assert r["consumerCopies"] >= 50 and r["ackGeneration"] > 0 and r["lagFrames"] == 0, r
    run.wait_log(r"fow_models: LOUD: the server's mask is STOPPED \(episode 1\)", timeout_s=5)
    run.wait_log(r"fow_models: RESTORED the white view 0x[0-9A-F]+ into the entry of handle 0x2 \(the server's mask stopped", timeout_s=2)
    run.wait_log(r"fow_models: RESTORED FOWAnim \(fow_anim\) to its start value \(0, 0, 0, 0\)", timeout_s=2)
    finish(run)
    assert not [ln for ln in flines(run) if "FAILED" in ln or "VERIFY BAD" in ln], flines(run)
    assert "fog models: section" in prod.log() and "first mask handed over" in prod.log()


@pytest.mark.parametrize("fill", ["seen", "edge"])
def test_present_only_host_moves_at_present_with_another_fill(run, tmp_path, fill):
    """A host without ExecuteCommandList (no frame-start hook): the mask is moved at the Present to the window of the frame
    just presented; the block is where that window puts the origin; with 'seen' the strip holds (1, 0, 0, 1) (checked at
    the strip's edge texel); with 'edge' every move goes through the resample pass (the strip takes the nearest known
    texel past the rim). Off frames remain only where the window stepped between the Present and the next frame: fewer
    than the source lag's."""
    pid = start_host(run, dict(fow_models_test="ok", fow_models_fill=fill), seconds=16)
    prod = Producer(pid, tmp_path, "--seconds", "12", "--hz", "120")
    run.wait_log(r"fow_models: local mask 1024x1024, 11 mips made .* the fill '%s'\); window shift on" % fill, timeout_s=10)
    tot = run.wait_log(r"fow_models: TEST totals: verify \d+ ok 0 bad; swapped 1", timeout_s=14)
    assert int(re.search(r"verify (\d+) ok", tot).group(1)) >= 50, tot
    src, eff, line = lag_lines(run)
    assert eff["at_start"] == 0 and eff["fill"] == fill, line
    if fill == "edge":
        assert eff["shifted"] == 0 and eff["resampled"] > 0, line
    else:
        assert eff["shifted"] > 0, line
    assert src["lag"] > 0 and eff["lag"] < src["lag"], (src, line)
    prod.result()
    finish(run)
    assert not [ln for ln in flines(run) if "FAILED" in ln or "VERIFY BAD" in ln], flines(run)


def test_shift_off_is_0339s_window(run, tmp_path):
    """fow_models_shift=0 (the comparison arm): the mask is used in the window it was built in; every read-back finds the
    block where that window puts the origin; the effective lag equals the source lag."""
    pid = start_host(run, dict(fow_models_test="ok", fow_models_shift="0"), seconds=16, deferred=True)
    prod = Producer(pid, tmp_path, "--seconds", "12", "--hz", "120")
    run.wait_log(r"fow_models: local mask .* window shift OFF \(fow_models_shift=0", timeout_s=10)
    tot = run.wait_log(r"fow_models: TEST totals: verify \d+ ok 0 bad; swapped 1", timeout_s=14)
    assert int(re.search(r"verify (\d+) ok", tot).group(1)) >= 50, tot
    src, eff, line = lag_lines(run)
    assert src["lag"] > 0 and eff["lag"] == src["lag"] and eff["shifted"] == 0 and eff["resampled"] == 0 and eff["at_start"] == 0,         (src, line)
    assert eff["shift"] == "off", line
    r = prod.result()
    assert r["lagFrames"] > 0, r
    finish(run)
    assert not [ln for ln in flines(run) if "FAILED" in ln or "VERIFY BAD" in ln], flines(run)


def test_resample_programs_refused_is_loud_and_left_as_built(run, tmp_path):
    """Ticket 0407, the negative case of the built-in programs: the device refuses the vertex program (test mode
    badprogram hands it 16 bytes). One LOUD line names it; whole-texel shifts still move the mask, size steps are made as
    built (0339's lag on those frames, counted); no FAILED (the model fog stays on), no d3dcompiler_47 loaded."""
    pid = start_host(run, dict(fow_models_test="badprogram", fow_models_fill="zero"), seconds=16, deferred=True)
    prod = Producer(pid, tmp_path, "--seconds", "12", "--hz", "120")
    run.wait_log(r"fow_models: TEST MODE fow_models_test=badprogram", timeout_s=10)
    run.wait_log(r"fow_models: LOUD: the resample pass is not available \(the built-in resample programs could not be made\): a "
                 r"window of another size reads the mask as built .*d3dcompiler_47\.dll not loaded", timeout_s=10)
    run.wait_log(r"fow_models: SWAPPED: the white handle 0x2 now reads the server's mask", timeout_s=4)
    run.wait_log(r"fow_models: TEST totals: verify \d+ ok \d+ bad; swapped 1", timeout_s=14)   # after the first 10-s report
    src, eff, line = lag_lines(run)
    assert eff["resampled"] == 0 and eff["scale"] > 0 and eff["shifted"] > 0, line
    r = prod.result()
    assert r["failed"] is False and r["consumerState"] != "FAILED", r
    finish(run)
    lines = flines(run)
    assert sum("resample pass is not available" in ln for ln in lines) == 1, lines
    assert not [ln for ln in lines if "the resample pass is ready" in ln or "FAILED" in ln], lines


def test_stop_and_resume_swaps_again(run, tmp_path):
    pid = start_host(run, dict(fow_models_test="ok"), seconds=16)
    prod = Producer(pid, tmp_path, "--seconds", "9", "--stop-at", "3")
    run.wait_log(r"fow_models: SWAPPED", timeout_s=10)
    run.wait_log(r"fow_models: LOUD: the server's mask is STOPPED \(episode 1\)", timeout_s=8)
    run.wait_log(r"fow_models: RESTORED the white view", timeout_s=2)
    r = prod.result()
    assert r["failed"] is False, r
    finish(run)
    swaps = [ln for ln in flines(run) if "SWAPPED" in ln]
    assert len(swaps) >= 2, flines(run)
    assert not [ln for ln in flines(run) if "FAILED" in ln], flines(run)


def _suspend(pid, on):
    """NtSuspendProcess / NtResumeProcess on the producer (its heartbeat stands still, as a held-up server render thread)."""
    import ctypes
    k32, nt = ctypes.WinDLL("kernel32", use_last_error=True), ctypes.WinDLL("ntdll")
    k32.OpenProcess.restype = ctypes.c_void_p
    h = k32.OpenProcess(0x0800, False, pid)   # PROCESS_SUSPEND_RESUME
    assert h, ctypes.get_last_error()
    try:
        st = (nt.NtSuspendProcess if on else nt.NtResumeProcess)(ctypes.c_void_p(h))
        assert st == 0, hex(st & 0xFFFFFFFF)
    finally:
        k32.CloseHandle(ctypes.c_void_p(h))


def test_late_heartbeat_keeps_the_last_mask(run, tmp_path):
    """Ticket 0415: the server's heartbeat stands still (the producer suspended for 3.5 s, as the server's render thread held
    up by a decal apply or a field on a loaded board): a late episode, one line at its start and one at its end with its
    length, LOUD once past fow_models_late_loud_ms; the last mask stays bound (no RESTORED, no second SWAPPED) and every
    read-back stays good. Only the producer's STOPPED at its end puts the white view back. 0339's code put the white view
    back here (the models lost their tint for the whole stall)."""
    pid = start_host(run, dict(fow_models_test="ok", fow_models_stale_ms="800", fow_models_late_loud_ms="2000"), seconds=20)
    prod = Producer(pid, tmp_path, "--seconds", "12", "--hz", "120")
    run.wait_log(r"fow_models: SWAPPED", timeout_s=10)
    time.sleep(1.5)
    _suspend(prod.p.pid, True)
    try:
        run.wait_log(r"fow_models: the server's mask is late \(its heartbeat still for \d+ ms; late episode 1\): the last mask stays bound",
                     timeout_s=4)
        run.wait_log(r"fow_models: LOUD: the server's mask has been late for \d+ ms \(late episode 1, fow_models_late_loud_ms 2000\)",
                     timeout_s=4)
        time.sleep(0.5)
    finally:
        _suspend(prod.p.pid, False)
    end = run.wait_log(r"fow_models: late episode 1 ended after (\d+) ms \(the server's heartbeat moves again\); the last mask stayed bound",
                       timeout_s=4)
    ms = int(re.search(r"ended after (\d+) ms", end).group(1))
    assert 2000 <= ms <= 8000, end
    run.wait_log(r"fow_models: LOUD: the server's mask is STOPPED \(episode 1\)", timeout_s=14)
    tot = run.wait_log(r"fow_models: TEST totals: verify \d+ ok 0 bad; swapped \d", timeout_s=14)
    r = prod.result()
    assert r["failed"] is False, r
    finish(run)
    lines = flines(run)
    i_end = next(i for i, ln in enumerate(lines) if "late episode 1 ended" in ln)
    i_stop = next(i for i, ln in enumerate(lines) if "is STOPPED (episode 1)" in ln)
    assert not [ln for ln in lines[:i_stop] if "RESTORED the white view" in ln], lines   # bound through the late episode
    assert sum("SWAPPED" in ln for ln in lines[:i_stop]) == 1, lines
    assert sum("LOUD: the server's mask has been late" in ln for ln in lines) == 1, lines
    assert sum("the server's mask is late" in ln for ln in lines) == 1 and i_end < i_stop, lines
    assert not [ln for ln in lines if "FAILED" in ln or "VERIFY BAD" in ln], lines
    assert int(re.search(r"verify (\d+) ok", tot).group(1)) > 0, tot
    ex = [ln for ln in lines if "at the process's end:" in ln or "released (" in ln]
    assert not ex or re.search(r"late episodes 1 ", ex[-1]), ex


def test_new_map_load_puts_back_until_a_mask_built_after_it(run, tmp_path):
    """Ticket 0415: a new map load in the same process (test mode reload: the test world's load count steps 3 s after the
    swap, as the camera block's gridSeq does at a load): the white view is put back at once (not LOUD), masks the server
    built before the load was seen are passed over, and the first mask built after it is copied and swapped in again."""
    pid = start_host(run, dict(fow_models_test="reload"), seconds=16)
    prod = Producer(pid, tmp_path, "--seconds", "12", "--hz", "120")
    run.wait_log(r"fow_models: SWAPPED", timeout_s=10)
    run.wait_log(r"fow_models: a new map load \(load 1 -> 2; 1 in this process\): the white view is put back until a mask the server "
                 r"built after it is copied", timeout_s=8)
    run.wait_log(r"fow_models: RESTORED the white view 0x[0-9A-F]+ into the entry of handle 0x2 \(a new map load", timeout_s=2)
    first = run.wait_log(r"fow_models: the first mask built after the map load \(generation \d+, built ([\d.]+) ms after the load was "
                         r"seen; \d+ older masks passed over\) is copied", timeout_s=4)
    assert float(re.search(r"built ([\d.]+) ms after", first).group(1)) > 0, first
    tot = run.wait_log(r"fow_models: TEST totals: verify \d+ ok 0 bad; swapped 1", timeout_s=14)
    r = prod.result()
    assert r["failed"] is False, r
    finish(run)
    lines = flines(run)
    i_load = next(i for i, ln in enumerate(lines) if "a new map load" in ln)
    i_first = next(i for i, ln in enumerate(lines) if "the first mask built after the map load" in ln)
    swaps = [i for i, ln in enumerate(lines) if "SWAPPED" in ln]
    assert len(swaps) == 2 and swaps[0] < i_load < i_first < swaps[1], lines
    assert not [ln for ln in lines if "LOUD" in ln and "STOPPED" not in ln], lines
    assert not [ln for ln in lines if "FAILED" in ln or "VERIFY BAD" in ln], lines
    assert int(re.search(r"verify (\d+) ok", tot).group(1)) > 0, tot


def test_restore_at_the_process_end(run, tmp_path):
    """The host ends while the mask is swapped in: DLL_PROCESS_DETACH puts the white view and FOWAnim back and logs it."""
    pid = start_host(run, dict(fow_models_test="ok"), seconds=6)
    prod = Producer(pid, tmp_path, "--seconds", "10")
    run.wait_log(r"fow_models: SWAPPED", timeout_s=6)
    finish(run)
    prod.result()
    lines = flines(run)
    assert any(re.search(r"RESTORED the white view 0x[0-9A-F]+ into the entry of handle 0x2 \(the process ends", ln) for ln in lines), lines
    assert any("RESTORED FOWAnim (fow_anim) to its start value" in ln and "the process ends" in ln for ln in lines), lines
    assert any("at the process's end:" in ln for ln in lines), lines
    assert not [ln for ln in lines if "LOUD" in ln], lines


def test_entry_cleared_by_the_games_teardown_is_quiet_at_the_process_end(run, tmp_path):
    """Ticket 0623: at UI.ExitGame the EXE's teardown clears its texture table before the DLL's detach (test mode
    exitclear: the test world sets the white entry to 0 just before the detach puts it back). The detach writes nothing
    there and logs a plain line, not LOUD; FOWAnim is still put back. The same 0 while the game runs stays LOUD
    (test_failure_is_loud_named_and_reaches_the_server, mode runclear)."""
    pid = start_host(run, dict(fow_models_test="exitclear"), seconds=6)
    prod = Producer(pid, tmp_path, "--seconds", "10")
    run.wait_log(r"fow_models: SWAPPED", timeout_s=6)
    finish(run)
    prod.result()
    lines = flines(run)
    assert any(re.search(r"fow_models: the entry of handle 0x2 is already 0 \(the process ends: the game's teardown cleared its "
                         r"texture table\): nothing to put back", ln) for ln in lines), lines
    assert not [ln for ln in lines if "RESTORED the white view" in ln], lines
    assert any("RESTORED FOWAnim (fow_anim) to its start value" in ln and "the process ends" in ln for ln in lines), lines
    assert any("at the process's end:" in ln for ln in lines), lines
    assert not [ln for ln in lines if "LOUD" in ln], lines


@pytest.mark.parametrize("mode,reason,text", [
    ("foreignview", "WHITE", r"the view-table entry of the white handle 0x2 \(view 0x[0-9A-F]+, vtable 0x[0-9A-F]+\) is not a D3D11 shader resource view"),
    ("fowanim", "MISMATCH", r"FOWAnim \(fow_anim\) holds \(1, 2, 3, 4\), expected 0 or FluidFOW's start value 0\.002"),
    ("nobase", "BASE", r"the proxy's cloud base layer: test: the proxy refused the cloud base"),
    ("basemismatch", "BASE", r"the base layer \[pxw_fow_base\] = 0x7, the proxy's cloud base VP64SHELL_FOWBASE = 0x4"),
    ("fault", "EXCEPTION", r"the view-table entry of the white handle 0x2 .* \(fault\)"),
    ("stolen", "MISMATCH", r"the view-table entry of the white handle 0x2 changed under us to 0x[0-9A-F]+"),
    # ticket 0623: an entry cleared to 0 while the game still runs stays a LOUD mismatch (only the process's end is quiet)
    ("runclear", "MISMATCH", r"the view-table entry of the white handle 0x2 changed under us to 0x00000000"),
])
def test_failure_is_loud_named_and_reaches_the_server(run, tmp_path, mode, reason, text):
    """MC #2058: a failure or a write whose expected old value does not match is FAILED with its reason in the section,
    which the server's publisher reports (its fog install then takes the D125 route), one LOUD line; the view is never
    left swapped and FOWAnim never left written."""
    pid = start_host(run, dict(fow_models_test=mode), seconds=18)
    prod = Producer(pid, tmp_path, "--seconds", "12")
    run.wait_log(r"fow_models: LOUD: FAILED %s: %s" % (reason, text), timeout_s=14)
    run.wait_log(r"fow_models: FAILED reported in the section", timeout_s=8)
    r = prod.result()
    assert r["failed"] is True and r["failReason"] == reason and r["consumerState"] == "FAILED" and r["wanted"] == 0, r
    assert re.search(text, r["why"]), r
    finish(run)
    lines = flines(run)
    assert sum("LOUD: FAILED" in ln for ln in lines) == 1, lines
    if mode in ("stolen", "runclear"):
        assert any("SWAPPED" in ln for ln in lines)
        assert any("RESTORED FOWAnim" in ln for ln in lines), lines   # FOWAnim put back; the entry is not ours to write
    else:
        assert not any("SWAPPED" in ln for ln in lines), lines


@pytest.mark.parametrize("ini,text", [
    (dict(fow_models_test="nowhite"), r"fow_models: OFF: no white mask in this process \(VP64SHELL_WHITE not set"),
    (dict(fow_models_test="ok", model_fow="0"), r"fow_models: OFF \(model_fow=0\): the models keep the white mask"),
])
def test_off_wants_nothing(run, tmp_path, ini, text):
    """WhiteFogMask = 0 (no white handle) and model_fow=0: OFF, nothing wanted, no slot texture made, no failure."""
    pid = start_host(run, ini, seconds=14)
    prod = Producer(pid, tmp_path, "--seconds", "10")
    run.wait_log(text, timeout_s=12)
    run.wait_log(r"fow_models: OFF reported in the section", timeout_s=8)
    r = prod.result()
    assert r["consumerState"] == "OFF" and r["wanted"] == 0 and r["published"] == 0 and r["copies"] == 0 and not r["failed"], r
    finish(run)
    assert not [ln for ln in flines(run) if "SWAPPED" in ln or "FAILED" in ln], flines(run)
    assert "slots of" not in prod.log()


def test_no_mask_in_time_is_a_timeout(run, tmp_path):
    pid = start_host(run, dict(fow_models_test="ok", fow_models_wait_ms="1500"), seconds=10)
    prod = Producer(pid, tmp_path, "--seconds", "6", "--no-mask")
    line = run.wait_log(r"fow_models: LOUD: FAILED TIMEOUT: no mask from the server within 1500 ms of map frames", timeout_s=8)
    r = prod.result()
    assert r["failed"] is True and r["failReason"] == "TIMEOUT", r
    # ticket 0491: a host that presents at its full rate had its 60 map frames well before 1500 ms: no slow-load note
    m = re.search(r"\((\d+) map frames in (\d+) ms, slow-load bound 9000 ms;", line)
    assert m and int(m.group(1)) >= 60 and 1500 <= int(m.group(2)) < 3000, line
    assert not [ln for ln in flines(run) if "the EXE's load goes on" in ln], flines(run)
    finish(run)


def test_slow_load_waits_to_the_bound(run, tmp_path):
    """Ticket 0491: an EXE that presents only now and then after the map's first frame (test mode slowload: a map frame every
    400 ms, as on 9074's T1100) is a load going on: past fow_models_wait_ms with fewer than 60 map frames there is one note
    and no failure; the slow-load bound (6 x the wait) ends it with TIMEOUT and the frames named, never a hang."""
    pid = start_host(run, dict(fow_models_test="slowload", fow_models_wait_ms="1500"), seconds=20)
    t0 = time.monotonic()
    prod = Producer(pid, tmp_path, "--seconds", "15", "--no-mask")
    note = run.wait_log(r"fow_models: no mask yet after \d+ ms with only \d+ map frames presented \(fewer than 60: the EXE's load "
                        r"goes on\): waiting up to 9000 ms \(fow_models_wait_ms 1500 x 6; ticket 0491\)", timeout_s=8)
    assert not [ln for ln in flines(run) if "FAILED" in ln], flines(run)
    line = run.wait_log(r"fow_models: LOUD: FAILED TIMEOUT: no mask from the server within 1500 ms of map frames", timeout_s=14)
    assert time.monotonic() - t0 >= 8.0, (note, line)
    m = re.search(r"\((\d+) map frames in (\d+) ms, slow-load bound 9000 ms;", line)
    assert m and 10 <= int(m.group(1)) < 60 and int(m.group(2)) > 9000, line
    r = prod.result()
    assert r["failed"] is True and r["failReason"] == "TIMEOUT", r
    finish(run)
    assert sum("the EXE's load goes on" in ln for ln in flines(run)) == 1, flines(run)


BUILDS = ["4390913", "26577625"]


def pattern_symbols():
    """The symbol of every row of fow_models.cpp's pattern table, in order."""
    src = (Path(__file__).resolve().parents[1] / "composite" / "fow_models.cpp").read_text(encoding="utf-8")
    body = src[src.index("kFmSigs[] = {"):src.index("};", src.index("kFmSigs[] = {"))]
    return re.findall(r'X\(VPX_(\w+),', body)


@pytest.mark.parametrize("build", BUILDS)
def test_patterns_match_in_an_image_of_each_build(run, build):
    """Ticket 4965 (epic 0590): fow_models_test=patterns runs the real pattern check on a fake image of each known build
    (its header and the 7 patterns at that build's addresses from the build table), then OFF; nothing is read. The rows
    against the builds' EXEs themselves: test_exe_tables_shell_b.py (the local tables; the Steam install is not read)."""
    start_host(run, dict(fow_models_test="patterns", fow_models_test_build=build), seconds=5)
    run.wait_log(r"fow_models: test image of Steam build %s " % build, timeout_s=10)
    run.wait_log(r"fow_models: test: EXE patterns match \(7\) on Steam build %s .* - OFF, nothing read" % build, timeout_s=5)
    finish(run)
    assert not [ln for ln in flines(run) if "LOUD" in ln], flines(run)
    assert len(pattern_symbols()) == 7


@pytest.mark.parametrize("build", BUILDS)
@pytest.mark.parametrize("env,text", [
    (dict(fow_models_test_pattern="2"), r"pattern '.+?' at fow_pass1_anim does not match on Steam build %s "),
    (dict(fow_models_test_pattern="6"), r"pattern '.+?' at pxw_base_building_read does not match on Steam build %s "),
    (dict(fow_models_test_drop="fow_anim"), r"symbol fow_anim is not carried to Steam build %s "),
    (dict(fow_models_test_drop="fow_pass2_proj"), r"pattern '.+?': symbol fow_pass2_proj is not carried to Steam build %s "),
])
def test_patterns_refusals(run, build, env, text):
    start_host(run, dict(fow_models_test="patterns", fow_models_test_build=build, **env), seconds=5)
    run.wait_log(r"fow_models: LOUD: FAILED PATTERN: " + text % build, timeout_s=10)
    finish(run)
    assert not [ln for ln in flines(run) if "SWAPPED" in ln], flines(run)


def test_patterns_image_of_no_known_build_is_refused(run):
    start_host(run, dict(fow_models_test="patterns", fow_models_test_build="0"), seconds=5)
    run.wait_log(r"fow_models: LOUD: FAILED PATTERN: not an EXE build the patch knows \(PE timestamp 0x[0-9A-F]{8}, SizeOfImage "
                 r"0x06519000\)", timeout_s=10)
    finish(run)
