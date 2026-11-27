# The compositor on the proxy route (issue 9040, decision D81)

Written 2026-11-13 by one-off 9040. The first run that joins increment 1 (the x64 DLL in the server, reached from the
EXE through the x86 proxy) and increment 2 (D33: the EXE's world drawing off, the server's terrain drawn by
`shell_server.exe --scene terrain` and composited into the EXE's frame). Every increment-2 result before this used the
32-bit test DLL `i2-shell-test` in process (terrain-surface.md 5, shared-texture-and-compositor.md 14-15). Marks: [C]
source, decompile or dump; [L] live game; [I] inferred. Addresses are DX11 VAs at ImageBase 0x400000.

## 1. Why the combined configuration could not run before (2026-11-13 00:20 EST) [C]

The D33 + terrain setup needs two things done from inside the EXE's process, and both lived only in the 32-bit test
DLL `i2-shell-test` b92373aeb (deps/i2-shell-test.lock):

1. **Loading the composite DLL.** `ShellHelperLoader::LoadOnce` (fork file `CvGameCoreDLL_Expansion2/ShellHelperLoader.cpp`,
   commit 0a7795fdf) reads `crashlogs\shell.helper` and `LoadLibraryW`s the path in it, from `DllGetGameContext`
   (shared-texture-and-compositor.md 3). Without it there is no camera block, no height-field tap, no bake tap and no
   insertion: no compositor at all.
2. **The view-mask write of D33.** `ViewMaskSync` (`ViewMaskSync.cpp`, b92373aeb) ANDs `[GVS+0x1B4]` with the config
   mask and the marker's `clear=` bits, from `DllGetGameContext` and from the top of every `CvGame::update`
   (disable-systems-and-masks.md 4). Without it the D33 set crashes the EXE at load (River/Arrows view bits set while
   their systems are off: VA 0x8CD492, disable-systems-and-masks.md 5 probes A2/B2; terrain-surface.md 5 "A cleaner
   reference is not available": the same crash with the marker absent).

On the proxy route the EXE loads the x86 proxy (`proxy/`) in place of the DLL, and the x64 DLL runs in `server.exe`:
- the proxy had neither: `proxy/src` at main 0cb4dea has no `shell.helper`, no view-mask code; its only
  `LoadLibrary` is the `mode=off` fallback (`proxy_rt.cpp` BootOff);
- the x64 pin 76992d235 (deps/dll.lock) has neither file (grep of the git archive `D:/c5-64bit-work/9036/src-76992d23`:
  no `ViewMaskSync`, no `ShellHelperLoader`; they are on the instrument branches, not on `x64`);
- and the x64 DLL could not do either from the server in any case: a `LoadLibrary` there loads into `server.exe`, and
  the EXE's memory (`g_Civ5App` 0xCC8E74, the GameViewState) is another process's.
So "D33 + server terrain on the proxy route" was not a configuration that existed. What it needed: both pieces in the
one module of ours that the EXE does load on that route, the proxy. (Alternatives put to the MC and not taken: the
harness's preload allowlist of D57 plus an outside view-mask poller such as `shell/tools/view_mask.py` in a loop.)

## 2. The proxy module `proxy/src/proxy_shell.cpp` (MC decision 2026-11-13 00:22, option a) [C]

Ported from the two fork files (GPL, origin named in the file, D22); the same markers, logs and checks, nothing done
without the markers:
- `ProxyShell::OnBoot()` from `DllGetGameContext` after a successful `mode=on` boot (one line in `proxy_rt.cpp`;
  `mode=off` never calls it, the fallback DLL does its own): the helper loader (`crashlogs\shell.helper`, log
  `crashlogs\shell.log`) and `ViewMaskSync::Install` (`crashlogs\viewmask.enabled`, log `crashlogs\viewmask.log`).
- `ProxyShell::OnUpdate()` from `ProxyRtInt::BeforeUpdate` (`proxy_update.cpp`, the top of each `ICvGame1::Update` on
  the game-core thread, where the DLL's `CvGame::update` hook sat): the marker re-read every 2 s and the re-clear.
- Differences from the origin: the proxy has no game turn without a crossing, so the per-turn read-back line becomes a
  read-back line once a minute (with the Update count); each part also writes one line into `vpproxy.log`.
- Writes only EXE heap data (`[GVS+0x1B4]`, compare-and-swap), after the origin's checks: TimeDateStamp 0x546CD0A8, six
  code patterns, the `g_Civ5App` address inside two of them, the GameViewState vtable 0xA20908, VirtualQuery of every
  pointer. No code byte (D4).

## 3. The combined run (2026-11-13 02:49-03:10 EST) [L]

**Set-up.** Instance c5h-a (Goldberg), DX11 EXE (TimeDateStamp 0x546CD0A8, loaded at base 0x00310000), CP/VP/EUI,
statistics logging as provisioned on c5h-a, window 2554 x 1411, RTX 4080. Save `C5H-9040-HUGE_0350` (copy of
`C5H-VP8P-HUGE_0350`, SHA-256 c499d3c6..., huge, turn 350; the save of terrain-surface.md 5 and of 9036's proxy runs).
- Proxy route (`provision.py set-proxy`, D28 layout; vpproxy.ini `mode=on local_answers=1 local_verify=0 lua_mirror=0`
  for the run, restored by hash): **proxy built off-pin** from this repository at fbbe6a1 (main 4f1f646 + this issue's
  proxy_shell.cpp; it carries 9036's merged proxy fixes and 9042's options file) `8F10D815FD6F9695...`, `server.exe` of
  the same build `77877FB1F9C10E02...`, x64 DLL `12B7F4FB2B55E400...` = 9036's release build of the pinned source
  76992d235 (`D:/c5-64bit-work/9036/b1/deps`, fingerprint matches deps/dll.lock; not the bytes of the pin folder,
  FA75F483...). The pinned server.exe was not used: this proxy writes the 9042 `options_file=` line into server.cfg.
- D33: `[DisableSystems]` of `shell/tools/darkred.py` (18 keys) and `crashlogs\viewmask.enabled` = `clear=0x1`
  (`shell/tests/tap_live.py config --arm d33`); `crashlogs\shell.helper` naming the composite DLL.
- Shell from the same commit (`shell\build.cmd`, `D:/c5-64bit-work/9040/shell-build`): `shell_composite.dll`
  `7D84898351CEDC66...` with `mode=composite tap=1 bake_tap=1 insert=tagged tag_rule=after tag_match=clear_bb camera=1
  section=Local\VPShell9040`; `shell_server.exe` `D57AC80D902BD123...` `--scene terrain --fallback constant
  --constant-rgba 0xFFFF00FF` on a hidden desktop.
- Driver `shell/evidence/9040/run_9040.py run ... --max-turns 30 --no-lock`; tables `shell/evidence/9040/summarize_9040.py`
  -> `shell/evidence/9040/summary.json`; logs `shell/evidence/9040/viewmask.log`, `shell.log`, `vpproxy_shell_lines.txt`.
  Raw: `D:/c5-64bit-work/9040/run2/` (run.json, run.log, memory.csv every 5 s, composite and server logs, proxy run
  folder copies, end.png at turn 380). Guard check rc 0, start-up watch ok, harness stop rc 0 (stop_readiness).
- **No quiet-machine hold** (the take was refused twice: other owners' heavy jobs, a game of 9043 and suites and a DLL
  build of 4000, ran the whole time). So **no frame-time figure is claimed** here; the frame windows of the driver
  (in-process d33pt against d33t, 6 rounds, as shared-texture-and-compositor.md 15.2) were not run.

**What worked: everything in the path, unchanged from the in-process route [L].**
1. The proxy loaded the compositor at its boot: `shell: helper (crashlogs\shell.helper): loaded
   D:\c5-64bit-work\9040\live\shell_composite.dll at 0x7add0000` (vpproxy.log, 0.886 s after the proxy's boot).
2. The view-mask write on the proxy route, exactly as the DLL's on the stock route (disable-systems-and-masks.md 5,
   2026-11-11): at boot (front end) view `0xFFFFFFFF -> 0xFF000662` (config 0xFF000663, cleared 0x00FFF99D); the load
   set back `0x801` (Terrain + Combat) and the first Update after it (02:52:30.8, the in-game moment) cleared both
   (`0xFF000E63 -> 0xFF000662`); then `0xFF000662` at every once-a-minute read-back through turn 380 (1,946 Updates).
   **No crash at load** (River/Arrows clear before the load: the 3206 crash at VA 0x8CD492 needs them set).
3. The composite DLL hooked the device (18 targets), adopted the swap chain at 02:51:22, read the camera (11 patterns),
   bake tap enabled (7 patterns): `load 1 tapped: 135 slots (110 both, 25 t0 only), 245 copies` 68.7 s after the load
   began; height-field tap `READY load 3 copy 1: 132120704 bytes ... copy 462.4 ms` and `RELEASED ... ack from pid
   49872, result 0, 520 ms after READY; section closed`.
4. The server: `tap load seq 3 read: 128x84 plots, 132120704 bytes, hash ok, copied and acknowledged in 490.0 ms`,
   field built in 2,503 ms (128 x 84 tiles of 1 plot; 1,837 ms in 3206), `bake manifest seq 271 applied: 135 slots, 135
   opened, 0 missing`, state OK at 02:52:36; 97,262 camera blocks, 97,181 draws, 0 draw errors, 0 stale; at the load
   view 104 tiles drawn, 46 without a cache (the same counts as 3207's view (100, 30)); GPU per draw mean of the
   per-second means 1.18 ms (not a measurement: no hold, AI turns running).
5. 30 AI turns, 350 -> 380, through the proxy with the terrain composited: no crash, no dialog, no LOUD line in
   vpproxy.log, 0 borrowed-argument warnings; the picture at turn 380 shows the server's terrain under the EXE's units,
   cities and UI, magenta on tiles without a cache (end.png, as `w5_huge_*_pt_terrain` of 3206).
6. Missed insertions: 111,596 of 240,640 frames, **all `miss_nomatch`** (frames without the scene-clear list). 105,657 of
   them fall in turn 350, whose wait took 236.7 s: the harness's diplomacy answerer was not armed on that turn (below,
   bug 0122) and three leader screens stayed open 78 / 76 / 69 s until the stall fallback closed them; the other 5,898
   in 12 of the 29 later turns (153-965 frames each, about 1-3 s of a leader screen at the frame rates seen) [I: matched
   to leader screens by kind, not per event; 3206 matched every such frame to a leader screen]. 0 stale camera seconds
   after the load, `cam_bad` 10 (load only).

**Memory [L] (tools/harness/memory.sample for the EXE; private bytes by pid for the two servers; shell_server's own
`mem periodic` line for video memory).**

| Moment | EXE private | EXE committed | EXE largest free | x64 server.exe private | shell_server private | shell_server video (local) |
|---|---|---|---|---|---|---|
| in game, 30 s after LookAt (t350) | 1,444.3 MB | 1,749.4 MB | 1,855.3 MB | 219.9 MB | 1,226.9 MB | 873.1 MB |
| turn 353 | 1,675.4 | 1,980.1 | 1,654.5 | 260.2 | 1,226.9 | - |
| turn 380 | 2,353.6 | 2,656.8 | 892.3 | 283.7 | 1,226.9 | 69.3 (the graphics driver demoted unused tile textures [I]; 3206 saw 856 -> 174) |
| **slope, turns 353-380 (28 points, MB per turn, 95 % interval)** | **+25.09 [24.03, 26.14]** | +25.03 [23.97, 26.09] | **-27.97 [-29.02, -26.92]** | +0.70 [0.50, 0.90] | 0.00 | |

- **The EXE's growth is the proxy route's bug 0102, not the compositor:** 9036 measured 24.3 MB per turn on the same
  turns through the proxy in the stock configuration (stock route 2.41); here 25.1. The composite DLL, the taps and the
  shell server add nothing per turn (shell_server flat at 1,226.9 MB; the tap's section was released after the ack).
- **At load D33 saves what it saved in process:** EXE private 1,444 MB here against 1,839 MB through the proxy without
  D33 (9036, 2026-11-11, load sample) and 1,622.5 MB in D33 + terrain on the in-process route (3207, 15.3, the 32-bit
  DLL in the EXE) [L, single samples of different sets: context, not a measured difference]. The largest free block
  starts at 1,855 MB (3207 in process: 1,595 MB).
- **Consequence [I, projection]:** the largest free block falls 28 MB per AI turn; at 892 MB at turn 380 it would be gone
  about 30 turns later. 9036's abort (bug 0102, `std::bad_alloc` at turn 420, largest free block 12 MB) would come at
  about the same turn with D33 as without it: the ~400 MB D33 frees are used up by the proxy's growth in about 16 turns.
  The combined configuration needs 9041's fix before any run longer than about 50 turns.
- The x64 server stays small (220-287 MB; +0.7 MB per turn).

**What broke [L].** Nothing that the combination itself caused. Found on the way:
- bug 0122 (harness, proxy route): on the first AI turn after the load `turns.py wait` logged `diplo: core not armed:
  timeout (lua: no answer within 5 s ... state LeaderHeadRoot ...)`, so three leader screens were closed by the stall
  fallback at 78, 154 and 223 s (236.7 s for one AI turn); from the next turn on the answerer was armed. The same
  timeout is in 9036's proxy runs of 2026-11-12/13 (`D:/c5-64bit-work/9036/runs/*/run.log`, twice), so it is a proxy-route
  harness issue, not the compositor's.
- Cosmetic: the proxy's viewmask.log line reads `update update N` (the reason word and the counter label are the same
  word); the product form drops this log (section 4).

**Not verified / not done.** Frame time and frame rate on this route (no hold; the driver's windows are ready:
`run_9040.py run` without `--no-lock`, needs a free machine for about 20 minutes); pan lag; any colour or geometry
comparison (the draw is the same code and the same inputs as 3206's, which measured those); a reload in the same
process on the proxy route (the tap's requestSeq path); turns past 380; the Tiny save.

## 4. `proxy_shell.cpp` in product form (issue 3305, epic 3300 wave 2, 2026-11-13)

### 4.1 The split 9040 proposed [I, design judgement, 2026-11-13 by 9040; built as 4.2 says]

Keep for the product (the proxy is the only module of ours inside the EXE on the shipped route, so both jobs belong
there):
- **The composite load from the proxy's boot**, on the EXE's thread after a successful `mode=on` boot. In the product
  the path is not read from a marker in `crashlogs\`: the composite DLL ships in the patch's own folder (beside `VP64\`)
  and is switched by the options file of D89 (`VP64Options.ini`, which the proxy already reads at the same boot), and
  its hash or version is checked against the proxy's before `LoadLibrary`.
- **The view-mask write**: `view &= config | 0xFF000000` and the Terrain bit of D7 c1 cleared, with every check the port
  keeps (timestamp, six code patterns, `g_Civ5App` inside two of them, GameViewState vtable, VirtualQuery, the
  compare-and-swap) and the re-check from the Update hook (GVS::StartupGame ORs Terrain back at every load). The
  `clear=` mask becomes a fixed product rule (Terrain c1) rather than a marker value.
- **Logging into `vpproxy.log`** (one line per change).
Test-only (drop or put behind a test switch): the two marker files and their 2-s re-read, `keep=` / `clear=` as live
values, the marker-removal restore path, the separate `crashlogs\shell.log` and `crashlogs\viewmask.log` with their
600-line cap, the once-a-minute read-back line.
Open for the product: the frames between GVS::StartupGame (load stage 4) and the first Update after it still have the
Terrain bit set (disable-systems-and-masks.md 4, "Not covered"); a product write may need an earlier hook than
`ICvGame1::Update` [I]. *(Answered by 4.3, 2026-11-13: a watcher thread, no hook.)*

### 4.2 As built (issue 3305, W5 of team 3300) [C code; offline tests 4.4]

Files: `proxy/src/proxy_shell.cpp`, `proxy_shell.h`; three calls in `proxy/src/proxy_rt.cpp` (`ProxyShell::Configure`
in `Boot` after `OptionsBoot` and before server.cfg is written, the existing `OnBoot` after a successful mode=on boot,
`OnUnload` at the top of `DetachOnUnload`); the option `[Renderer] ServerRenderer` in `options/` and `docs/OPTIONS.md`;
the manifest tool `proxy/tools/shell_manifest.py`.

- **Switch.** `VP64Options.ini` `[Renderer] ServerRenderer` = 0 (default) or 1. With 0 the proxy does nothing of this
  file (one log line), exactly as before 9040, unless a test switch is present.
- **Where the composite is.** `<proxy folder>\shell_composite.dll` (the folder that holds `VP64\`; on the D28 layout
  `MODS\(1) Community Patch\`). Its ini stays `shell_composite.ini` beside it (the composite reads `<its folder>\
  shell_composite.ini`).
- **The check (decided at Configure, before the server starts).** `<proxy folder>\shell_composite.sha256` must name
  the proxy's own SHA-256 (`proxy <hex>`, the build it was packaged with) and the composite's (`composite <hex>`);
  comments `#`, any other line is an error; at most 4096 bytes. The proxy hashes its own file and the composite (bcrypt
  loaded by name, no import added). The composite is opened `GENERIC_READ, FILE_SHARE_READ` only and **held open until
  `LoadLibraryW`**, which opens the file again for execute (allowed by that sharing): nobody can write, delete or
  rename it between the hash and the load, so the bytes checked are the bytes loaded [C; tested: a write open and a
  rename fail while held, 4.4]. A refusal is one line `shell: [Renderer] ServerRenderer = 1: composite <path> REFUSED:
  <why> (<detail>); the game draws its own terrain ...`; reasons: the DLL missing, the manifest missing / not openable
  (error) / invalid (which rule), another proxy build (both hashes), the composite's hash differs (both hashes), the
  composite cannot be opened without write sharing (someone holds it for writing). Why a manifest and not an embedded
  hash: the proxy's own hash cannot be known when it is built, and the composite is built by `shell/build.cmd`, not by
  `build_proxy.py`; the manifest is written when the x86 files are packaged or provisioned
  (`python proxy/tools/shell_manifest.py write --proxy <proxy dll> --composite <shell_composite.dll>`; `check
  <folder>` verifies a layout the way the proxy will).
- **Names (contract of design section 5).** `<base>` = `Local\VP64Shell_<EXE pid>` (decimal). When the composite
  passed (or the test switch names one) server.cfg gets `render=1` and `render_section=<base>`; with vpproxy.ini
  `server_config=` nothing is added (logged). Before `LoadLibraryW` the proxy sets `VPSHELL_SECTION=<base>` in the
  EXE's environment; the composite reads every ini key overridden by `VPSHELL_<KEY>` (`shell_composite.cpp`
  ReadConfig), so the composite needs no change [C; the stand-in composite saw the value, 4.4]. The variable is
  inherited by processes the EXE starts later (none of ours: the server is started before).
  *(2026-11-13, issue 3308: the lines are now `render=1`, `render_section=<base>`, `render_handover=ring`; from the
  proxy's second boot in one EXE process `<base>` = `Local\VP64Shell_<pid>_<boot>` plus `render_texture=
  Local\VP64ShellWorld_<pid>_<boot>`, and the composite follows `VPSHELL_SECTION`: renderer-in-server-design.md 4.8.)*
- **The view-mask write.** With ServerRenderer = 1: `allowed = (config | 0xFF000000) & ~Terrain`, Terrain (0x1, D7 c1)
  only while the composite is loaded (a refused composite keeps the config rule alone, so the EXE draws its own
  terrain and River / Arrows stay safe under D33). Every check of the origin unchanged (timestamp, six code patterns,
  `g_Civ5App` in two of them, vtable, VirtualQuery of every pointer, compare-and-swap); a check that fails logs once
  per pointer, not once per poll. Re-checked at the top of each `ICvGame1::Update` and by the watcher (4.3).
- **Logs.** Every write is one `shell: view mask: ...` line in `vpproxy.log`, named by its reason and the Update count:
  `boot (Updates 0)`, `re-check (Updates N)` (the Update hook), `watch (Updates N)` (9040's doubled `update update N`
  is gone). A check that fails is logged once per pointer.
- **Test switches (unchanged markers).** `crashlogs\shell.helper` names a DLL loaded instead of the product composite,
  without a hash check (`shell.log` as before); with ServerRenderer = 1 the names are still written and set.
  `crashlogs\viewmask.enabled` (`keep=` / `clear=`, re-read every 2 s) adds its bits to the rule, writes
  `crashlogs\viewmask.log` and the once-a-minute read-back; removing it sets back what it cleared except River,
  Arrows and, with ServerRenderer = 1, what the product rule still clears. With ServerRenderer = 0 both markers behave
  as in 9040 (W6's D33 arms keep working).

### 4.3 The Terrain bit between GVS::StartupGame and the first Update [C code; live 4.5: 34.6 s closed to 10.4 ms]

GVS::StartupGame (LoadGameState state 4, load-sequence.md 1) ORs Terrain (and Combat) back at every load
(`or [esi+1B4h],1`, 0x468BCC). Then come state 5 (the queue-B wait with the Landmark layout, ~18 s on the huge save),
state 8 (StartGPUInitWork, which calls UpdateViews) and only then the game's first `ICvGame1::Update`, the first point
the Update hook could clear it. 9040's run: the load set back 0x801 and the first Update cleared it at the in-game
moment. So with the Update hook alone the bit stays set for the whole rest of the load (34.6 s on the huge save, 4.5).
The load screen is translucent and the world is drawn under it at least at the end of the load (4.5).

No earlier synchronous hook exists without EXE code bytes (D4): the next EXE->DLL call after StartupGame comes from
StartInitStuffOnMap through the generated proxy stubs, which this issue does not own, and hooking every crossing would
put a check on the hot path. So the window is closed by time instead: a **watcher thread** (64 KiB stack) runs the
same re-check every 10 ms from the boot on, timed by a high-resolution waitable timer
(`CreateWaitableTimerExW(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)`, Windows 10 1803+, looked up by name; it does not
change the system's timer resolution); without one it falls back to `Sleep(10)`, which waits one or two ticks of
15.6 ms. It and the Update hook share one critical section. Offline (shell_unit, workstation, 2026-11-13 09:40 EST):
5 set-backs at random moments cleared after mean 5.1 ms, worst 6.7 ms with the timer; 18.6 / 30.6 ms with `Sleep(10)`
(the run before the timer was added) [C]. Each watcher write carries the gap to the previous poll (`(watcher: the previous poll
N ms before saw them clear)`), an upper bound of how long the bit was set; the first Update after a watcher write logs
`first Update N ms after the watcher cleared the Terrain bit` = how long it would have stayed set without the watcher.
The watcher's reads of the EXE's objects are inside SEH (an access violation, if the EXE frees the GameViewState under
it, ends the poll and is counted; the next poll checks the pointers again). It is stopped at the unload under the
loader lock: a stop flag, a bounded wait (500 ms) until it is outside its work, then `TerminateThread` (it holds no
lock while outside its work; it never returns by itself because a thread exit would need the loader lock).

### 4.4 Offline tests (2026-11-13) [C]

`python proxy/tools/test_proxy_rt.py --build <B> --scratch <dir> --only-shell --asan` (section 5 of that script;
workstation, builds `D:/c5-64bit-work/3300/w5/b2`, b3 of this branch):
- `rt_tests/shell_unit.cpp` linked with `proxy_shell.cpp` and the options loader, clang-cl -m32 with the VC9 headers:
  **122/122** (09:40-09:58 EST); the same under MSVC `/fsanitize=address` x86: **122/122**, canary caught, no report.
  It covers: the manifest parser (3 valid forms, 13 invalid ones, each reason named); the EXE checks against a fake
  9 MB image in memory (no image, DOS and PE signatures, `e_lfanew` out of range, timestamp, image size, each of the
  six patterns, `g_Civ5App` inside the two patterns, an unreadable page); the write (9040's values: config 0xFF000663,
  view 0xFFFFFFFF -> 0xFF000662, cleared 0x00FFF99D; the load's 0x801 set back and cleared by the Update re-check;
  River / Arrows; the rule without Terrain; keep / clear; NULL app or GameViewState = waiting, logged once; an
  unreadable app pointer, a wrong vtable, a read-only GameViewState = skipped and logged once, nothing written; a
  re-created GameViewState); the watcher (5 set-backs cleared after mean 5.1 ms, worst 6.7 ms; a thread doing the EXE's
  unlocked `or` 1 against 2,000 Update re-checks: no deadlock, clear at the end; the GameViewState decommitted under the
  watcher: exactly 1 access violation caught and counted, then skipped by the pointer check; the new object found and
  written by the watcher alone; after the stop no poll and no write); Configure / OnBoot with the stand-in composite
  (option 0: nothing; option 1: missing DLL, missing / locked / invalid manifest, another proxy build, wrong composite
  hash, the composite locked or open for writing by someone else, `server_config=`; the good case: the render lines,
  a write open and a rename refused while held, the load with `VPSHELL_SECTION` seen by the stand-in, the file
  writable again after the unload; the test switch `shell.helper` with option 0 (no names: the stand-in saw
  `(unset)`) and 1 (names), an empty marker).
- Loader cases (the built proxy as the EXE loads it, the real server, `VP64Options.ini` per case): **17/17** in 10 s
  (10:02 EST, build b2): shell-off, shell-on (`render=1`, `render_section=Local\VP64Shell_60552` in server.cfg, the
  stand-in saw `VPSHELL_SECTION=Local\VP64Shell_60552`, the server logged `render:` lines, the view mask refused in
  loader.exe by the timestamp check), shell-badhash, shell-otherproxy, shell-missing, shell-nomanifest.
- The options tests (`options/test_asan.py`, x64 MSVC ASan): **143/143** (126 before + 17 for ServerRenderer).
- Found on the way (not this issue's code): since bug 0123's default `loc_db` (the game's own `Documents\My Games\...\
  cache\Localization-Merged.db` when it exists), a loader-test layout on the workstation picks up the owner's real cache
  (the loader's Documents are not redirected); the server then fails `Localization::Initialize` (no ICU data under the
  layout's game_dir) and dies with an access violation at ntdll+0x1a12d before it attaches (`boot FAILED: the server did
  not attach within connect_timeout_ms 60000`) [L 2026-11-13 09:58, build b2]. The shell cases write `loc_db=none`;
  the older d28 / guard cases of `test_proxy_rt.py` write only `mode=on`: the full run of all five sections on build
  b3 (10:41-10:49 EST, 486 s) gave **66/79**: every shell check passed (unit 122/122, ASan 122/122, loader 17/17), and
  the 13 failures are exactly the older cases that boot the real server (d28 x7, strict x3, guard-ok x2, guard-off x1),
  each with that localization failure in its server.log (`loc_db=...\Documents\My Games\...\cache\
  Localization-Merged.db`, `EXCEPTION 0xC0000005 ... ntdll.dll+0x1a12d`). Two defects for the bugfix team: the test
  layouts need `loc_db=none` (or a redirected Documents), and the server should not crash after a failed localization
  init.

### 4.5 Live load on the proxy route (2026-11-13 10:52-10:56 EST) [L]

Instance c5h-c (Goldberg), DX11 EXE (TimeDateStamp 0x546CD0A8, base 0x00B60000), CP/VP/EUI, save `C5H-3305-HUGE_0350`
(copy of `C5H-VP8P-HUGE_0350`, SHA-256 c499d3c6..., huge, turn 350; deleted after). Proxy and server.exe of this
branch, build b3 (`D:/c5-64bit-work/3300/w5/b3`; repository 3a7604a, the build id says dirty=1: only a research file
was uncommitted), proxy SHA-256 1afb5904...; the x64 DLL of the pin layout `x64-76992d23-p1607d42` (FA75F483...).
`provision.py set-proxy`; D33 `[DisableSystems]` by `shell/tests/tap_live.py config --arm d33` with its
`crashlogs\viewmask.enabled` marker **removed** (the product path, no test switch); `VP64Options.ini` = `[Renderer]
ServerRenderer = 1`; `shell_composite.dll` of W6's build of the team branch (ce86e00e...) beside the proxy, its ini with
`mode=composite tap=1 bake_tap=1 insert=tagged tag_rule=after tag_match=clear_bb camera=1` and **no `section=` line**;
manifest by `shell_manifest.py write`. Guard check ok (3 vetoes, 0 failed), start-up watch ok, harness stop clean,
every backed-up file restored and equal by hash, the layout removed (`set-proxy --original`). Logs:
`D:/c5-64bit-work/3300/w5/live/` (vpproxy.log, server.cfg, shell_composite.log, ingame-t350.png).

- `vpproxy.log` at 0.008 s: `composite ... sha256 ce86e00e... matches shell_composite.sha256 and this proxy's build`,
  `server.cfg gets render=1 render_section=Local\VP64Shell_41944`; at 1.234 s `VPSHELL_SECTION=Local\VP64Shell_41944
  set`, `composite ... loaded at 0x7c590000 (2.9 ms)`; `boot (Updates 0): config 0xff040663 ... view 0xffffffff ->
  0xff040662 (cleared 0x00fbf99d)`; `watcher thread ... (high-resolution timer ...)`.
- server.cfg `render=1`, `render_section=Local\VP64Shell_41944`; server.log `render host: handover v3, section
  'Local\VP64Shell_41944'`, state READY at 1.5 s, the tap read (128x84 plots, 132,120,704 bytes, hash ok) at 116 s;
  render.log at 10:55:22 `terrain 1s: state=OK drawn=207 ... tiles=102 ... bake=ACTIVE slots=121`.
- The composite (ini without a section) `opened section 'Local\VP64Shell_41944'`: the name came from the environment.
- **The Terrain window, measured:** the load set back Combat 0x800 at 66.7 s (cleared by the watcher, `the previous
  poll 10.5 ms before saw them clear`) and Terrain 0x1 at 79.1 s (cleared by the watcher, previous poll 10.4 ms
  before); the first `ICvGame1::Update` came at 113.7 s: `first Update 34586 ms after the watcher cleared the Terrain
  bit`. So with the Update hook alone the Terrain bit would have stayed set for **34.6 s** after GVS::StartupGame on
  this huge-save load; with the watcher it was set for **at most 10.4 ms** (one poll). (9040 saw 0x801 set back
  together; here the two bits came 12.4 s apart: Combat from GameView::Startup's `mask |= 0x1800`, Terrain from
  GVS::StartupGame [I: by the bits and the order of the load stages].)
- Whether those frames mattered: the screenshot after the first Update, with the load screen still up ("Continue Your
  Journey"), shows the world (cities, units, the server's terrain) **through the translucent load screen** [L,
  ingame-t350.png]. So frames under the load screen do draw the world at least at its end; the EXE's own terrain in
  the 34.6 s window would have been drawn under it [I for the frames before the first Update]. Not measured: the
  frame count in the window (no per-frame record ran), any picture of those frames.
- 0 LOUD lines in vpproxy.log. No AI turn was played (a load, not a game; W6's acceptance runs the long game).
