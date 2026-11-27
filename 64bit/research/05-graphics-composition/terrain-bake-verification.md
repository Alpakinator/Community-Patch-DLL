# Independent verification of bake stage 1 in the server (epic 3400), one-off 9047

Written 2026-11-13 by one-off 9047, an agent that did not build epic 3400. Subject: `render/`, `shell/` and the research
of epic 3400 at `main` 3f24adb (merge of D92); `main` a8fb811 (COMMON 16 change, D93) merged into the verifier's branch
at 11:58 before the live C/D work, with no change to `render/` or `shell/` code in between. Every number below was
measured in this session unless it says "their tool" (the team's own program rerun) or "their data" (the team's raw
files reanalysed). Scripts and raw data: `D:/c5-64bit-work/9047/` (not committed: game-derived, D83).

Marks: [C] measured here offline (own code or the product on game files); [L] measured here in the live game; [I]
inferred.

**Builds.** Fresh clone of 3f24adb at `D:/c5-64bit-work/9047/clone` (0 local changes); `render/build.py` into
`D:/c5-64bit-work/render-build/2d4dddc6cf/`; `shell/build.cmd` into `D:/c5-64bit-work/9047/shell-build`
(`shell_server.exe` SHA-256 8016A68F..., `shell_composite.dll` ACCAD524...); every build with
`VSCMD_SKIP_SENDTELEMETRY=1`, started through `quietlock.py run`.

**Instance.** c5h-e (Goldberg, D root), claimed under 9047, standard set backed up by SHA-256 at
`D:/c5-64bit-work/9047/backup-standard` (+ `backup-extra` for `viewmask.enabled`, `shell.helper`, `vpproxy.ini`),
CP/VP/EUI, RTX 4080, game window 2554 x 1411. Other teams' builds, suites and games ran during every run (no hold
taken: no timing claim here).

## 1. Verdict per claim

| # | Claim (D92, the 3400 issue, the research) | Verdict | Section |
|---|---|---|---|
| E1 | `render/build.py --test` 17/17 from a fresh clone | **confirmed by own measurement** (17/17, 0 skipped, 73 s) | 2 |
| E2 | render ASan variant | **confirmed by own measurement** (17/17, canary fired, 0 ASan reports) | 2 |
| E3 | shell `test_residency` + `test_terrain_scene` + `test_bake_live`: 139 | **confirmed by own measurement** (139 passed, 0 skipped; again 139 under the shell ASan build, no report, peak 1,164 MB) | 2 |
| A | layer lists equal to the EXE's on every tile | **confirmed by own measurement on two own saves**: a Tiny map of another game 2,240 / 2,240, the huge map at turn 428 10,752 / 10,752 (current pin, stock), with an own reader of the EXE's lists beside the team's probe | 3 |
| B1 | comparator figures (fair set bit-equal Tiny 1,798 / 1,882, huge 3,146 / 3,960; decoded 0.9234 / 0.8591; ceilings 0.9429 / 0.9226) | **confirmed only by rerunning their tool** (fresh-clone build): every figure and every per-texture row (8,686 rows) identical, so the server's bake is deterministic between builds and sessions | 4.1 |
| B2 | "every remaining difference is a decal (stage 2) except one t8" | **supported by own measurement on Tiny, not proven** (still [I], as the team says): every one of the 84 differing fair-set Tiny textures lies on a tile whose own or neighbouring plots carry a river, a feature, an improvement or a route; rivers on 40 of 48 differing t0 against 16 of 804 equal t0; the differences are local (a few connected regions), not a global error; pictures show river beds, vegetation patches and border slivers. Huge checked by shape and pictures only | 4.2 |
| C | 0 gray frames after the bake-ahead (Tiny, exact baker, live) | **confirmed only by rerunning their tool** (their driver and the residency's own counter: 0 of 124,813 frames after a 14.4 s bake-ahead; radius 0 control 33). My own shot count saw no gray tile at radius -1, but it also missed the control's short gray bursts, so it is not an independent confirmation | 5 |
| D | a road re-baked within about 0.5 s (live) | **confirmed only by rerunning their tool** (block line 0.345 s after the road, 7 tiles re-baked, latency max 22.4 ms: within 0.37 s), timestamps read by me from the server log | 5 |
| D94 | asset set of the colour comparisons | B's server side ran on the install copy `pure_vp`, its EXE reference on c5h-e's install (**crossed installs**); both on this workstation hold the same VT folder (hash 45B3B433...) and the same Shared TerrainDef.csv: **the conclusion holds**; the server's lookup resolves to the VT copies (bit-equal continent tiles) | 4.3 |
| M | residency memory: huge BC1 arrays 352 MB | **confirmed by own measurement** (offline drive on the huge field with the exact baker: 33 arrays, 351.9 MiB, 350.4 MiB of BC1 in 8,411 slices; arithmetic checks: 33 x 256 x 43,680 B = 351.9 MiB). "MB" in D92 and the research is MiB | 6 |
| M2 | Gibraltar tiles fixed (3404 section 9) | **confirmed by own measurement**: 4,922 tiles baked, 0 failed (was 4,918 + 4 failed) | 6 |
| G | gamefs mounts package folders as the EXE does (TerrainDef.csv = the Shared DLC file) | **confirmed by own measurement** for TerrainDef.csv: on both own saves the product resolved `Assets/DLC/Shared/Terrain/TerrainDef.csv` and its tables equal the live EXE's tables; the general rule (67 files change resolution, no `.dds`) not re-checked | 7 |
| R | research files (terrain-bake-records, -blend, -passes, -comparison, -residency) | **consistent with the measurements here**, with the notes of section 8 | 8 |

## 2. Builds and tests [C]

2026-11-13 11:30-11:47 EST, chain `D:/c5-64bit-work/9047/chain_build.sh` (logs beside it):

| Step | Result |
|---|---|
| harness natives (`tools/harness/native/build.bat`) | exit 0 |
| `python render/build.py --test` | 17 / 17 passed, 0 skipped, 72.8 s of tests |
| `python render/build.py --asan --test` | canary reported its overflow; 17 / 17 passed, 0 skipped, 0 ASan reports |
| `shell/build.cmd` | exit 0 |
| `pytest shell/tests/test_residency.py test_terrain_scene.py test_bake_live.py` | **139 passed**, 0 skipped (227 s) |
| `shell/build.cmd asan` + the same three files under `memcap.py --mb 6144` with `ASAN_OPTIONS log_path` | 139 passed, 0 ASan logs, peak commit 1,164 MB |

## 3. Acceptance A on saves of the verifier's choice [L]

Current pin `i0ship-bbfacca1-ia` (E589306E...), stock configuration, c5h-e. Per save: launch, in game + 40 s, then
(1) the team's `shell/tools/layer_probe.py --lists-txt`, (2) **an own reader** (`D:/c5-64bit-work/9047/mylists.py`:
ReadProcessMemory with only the offsets of terrain-bake-records.md 2.1, the EXE base from the module list: it was
relocated to 0xEB0000 on both launches), (3) heights and material bytes by PID (`tap_live.py rawdump` + `hfbake
decode`), then the product's check `bakelayers_check` (fresh-clone build) against each list file.

| Save (own copy) | Map | Probe at | Team's probe lists | Own reader's lists | Tables |
|---|---|---|---|---|---|
| `C5H-9047-TINYB_0018` = `C5H-3000-TINY-T0018` (E337244C...), turn 18 | Tiny 56 x 36, **another game**: 1,081 of 2,240 tile lists differ from 3401's Tiny | 11:39 | **2,240 / 2,240 equal** | **2,240 / 2,240 equal**, identical to the probe's | equal |
| `C5H-9047-HUGE_0428` = `9043-HUGE-T428` (EF3271DD...), turn 428 | huge 128 x 80, the team's map 78 turns later: 44 tile lists differ from turn 350 | 11:43 | **10,752 / 10,752 equal** (6 slope tiles, 3 sorted > 8) | 10,751 equal, 1 differs: tile (91, 21) read as count 2, ids [0, 0] 4 s after the probe, which read [3, 43] | equal |

- The one difference is a **list caught mid-rebuild by the own reader**, not a server error: count already 2, ids still
  0, in a game in play (the EXE clears and rebuilds lists when an area changes, 0x4387D0, comparison 7). A reader of the
  EXE's live lists can catch such a state; the server computes its own lists, so this concerns only checks like this one.
- Slope path on huge t428: the same six tiles as 3401 (94,47), (94,48), (111..112, 43..44), closest n.z 0.9000206: equal.
- Histogram of list lengths, own Tiny: 1: 1,576, 2: 174, 3: 284, 4: 158, 5: 44, 6: 4.

## 4. Acceptance B

### 4.1 Their comparator rerun [C]

`bakecompare` of the fresh clone, 3401's reference set (arm B), the same install copy (`%USERPROFILE%/Desktop/pure_vp`),
EXE copy and plot-art files as run 3 (`D:/c5-64bit-work/9047/chain_offline.sh`, 12:01-12:03): Tiny 1,307 tiles / 2,552
textures in 35.5 s, huge 3,083 / 6,134 in 70.2 s, 0 skipped. **Every aggregate figure of terrain-bake-comparison.md 5
is reproduced, and the per-texture CSV rows (columns tile .. ceiling, 8,686 rows) are byte-identical to run 3's**, class
by class. So the server's bake is deterministic across builds and process runs on this machine and driver [C].

### 4.2 The "decal" attribution, checked independently

*Corrected 2026-11-15 (issue 4107, from epic 0166's 4106, terrain-decals.md 13) [L]: the attribution below is now
measured, not only associated. With the decals of the same EXE session drawn by the server (one stock session per
save, `bakecompare --decals`), every texture of a decal-free tile is bit-equal and Tiny reaches 2,543 / 2,568 bit-equal
textures, huge 5,330 / 6,136 (13.3); the rest has named causes (13.4). The dry gravel river bed of c below is the
Riverbank decal (category 4, with Floodplains and their end caps, terrain-decals.md 2.2 and 4.4); of the 40 river t0
tiles of b, 39 are cached in 4106's session and **39 of 39 are bit-equal**. Replaced: "[I] from 10 pictures" and b's
"It does not prove the cause".*

The team's causes for the 84 Tiny and 814 huge fair-set textures that differ are [I] from 10 pictures (comparison 6).
Three own checks:

**a. Where the differences are (shape).** Sample: every differing fair-set Tiny tile (66 tiles) plus 10 equal
continent-style controls; 60 seeded-random (seed 9047, uniform, not the worst) differing huge tiles plus 15 controls;
pictures from their tool with `--tiles` (`server | EXE | |d| x 8`, mip 0), shape measured by own code
(`diffmask.py`, `dm_summary.py`):

| Save, class (fair set) | n | texels differing, median | > 16, median | of the differing, <= 4 | 16 x 16 cells with a texel > 16, median (min-max) | > 16 in the 3 largest regions |
|---|---|---|---|---|---|---|
| Tiny remaining t0 | 48 | 0.191 | 0.106 | 0.16 | 0.25 (0.00-0.81) | 0.95 |
| Tiny remaining t8 | 20 | 0.010 | 0.000 | 0.39 | 0.02 (0.00-0.49) | 0.50 |
| Tiny tile+0x300 t0 / t8 | 9 / 7 | 0.229 / 0.055 | 0.137 / 0.036 | 0.13 / 0.05 | 0.22 / 0.09 | 0.98 / 0.95 |
| huge remaining t0 | 32 | 0.112 | 0.068 | 0.11 | 0.14 (0.00-0.77) | 0.95 |
| huge remaining t8 | 33 | 0.027 | 0.007 | 0.30 | 0.06 (0.01-0.54) | 0.62 |
| huge tile+0x300 t0 / t8 | 19 / 7 | 0.297 / 0.075 | 0.223 / 0.033 | 0.09 / 0.06 | 0.38 / 0.16 | 0.98 / 0.82 |
| for comparison: not fair (EXE-vs-EXE differs), t0 | 15 / 20 | 0.45 / 0.47 | 0.34 / 0.34 | 0.08 / 0.05 | 0.59 / 0.57 | 0.98 / 0.99 |

The differences are large and local: most differing texels differ by more than 4, the large ones sit in one to three
connected regions, and most of each tile is bit-equal. A global cause of the pipeline (lighting, style LUT, rounding)
would spread small differences over the whole tile; none of the sampled pictures shows that.

**b. What the map holds there (Tiny).** Each plot's terrain, feature, improvement, route, resource and river edges
dumped through the Lua channel from the reference save itself (`C5H-9047-TINY_0008` = `c5h-9028-tiny-t0008`, turn 9,
12:05, `plotdump.py`), each tile mapped to its plots by 3406's live dump of the tile plot lists (tile+0x04, cube
coordinates, odd rows shifted) and their six hex neighbours (`featcorr.py`):

| Tiny fair set, class | textures | with river / feature / improvement / route on own plots | on own plots or neighbours | river on own plots | flood plains on own plots |
|---|---|---|---|---|---|
| equal t0 | 804 | 200 (25 %) | 489 (61 %) | 16 (2 %) | 6 |
| equal t8 | 994 | 421 | 683 | 114 | 36 |
| remaining t0 | 48 | **44** | **48** | **40 (83 %)** | 26 |
| remaining t8 | 20 | 16 | **20** | 11 | 6 |
| tile+0x300 t0 / t8 | 9 / 7 | 9 / 7 | 9 / 7 | 4 / 2 | 3 / 2 |

**No differing fair-set Tiny texture lies on ground without such content** (0 of 84 within own plots + neighbours; the 8
without it on their own plots have a river, forest, jungle, flood plains or a route on a neighbouring plot: the
"pieces of neighbouring decals at the tile border" of comparison 6). Equal tiles are mostly bare ground; rivers sit on
83 % of the differing t0 against 2 % of the equal t0. This is a strong association with the content that bake stage 2
draws as decals. It does not prove the cause: that still needs a reference baked with the decal pass off or a decal
source (comparison 6, last paragraph). The huge save's plots were not dumped (no huge launch before the 13:00 window).

**c. Pictures** (12 seeded-random fair-set pictures looked at, 6 per save, `D:/c5-64bit-work/9047/png/m_*_rem.png`):
Tiny (40, 12) and (38, 10) t0: the server shows a dark blue-green water channel where the EXE shows a dry gravel
river bed over it (the largest single kind of difference); (47, 15) t0: a vegetation / dirt patch in one corner;
(3, 25), (6, 14) t0: slivers at the tile border (0.4 % / 0.02 % of texels); huge (24, 39), (19, 54) t0 and t8: small
edge pieces; huge (58, 49) t8: 36 % of texels in one large region of the specular cache. None shows a tile-wide shift.

**Consequence for D66 [I].** The most visible remaining difference on river tiles is the server's blue river-channel
material where the stock picture shows a river bed (the EXE's River system then draws water on top; under D33 the River
system is off). Until stage 2 draws the river-bed decal, every river shows as a dark channel in the server's ground.

### 4.3 The asset set of these comparisons (D94, the owner, 2026-11-13) [C]

Machine: the owner's workstation, RTX 4080. The reference caches of 3401 arm B were baked by the EXE of instance
c5h-e (install `D:/c5-64bit-work/instances/c5h-e/install`); the comparator (theirs and my rerun) baked the server side
from the install copy `%USERPROFILE%/Desktop/pure_vp/Sid Meier's Civilization V`. **So B crossed installs.** Both installs
hold the Vibrant Textures pack: `Assets/DLC/VT` present, 55 files, folder hash (SHA-256 over the sorted "path sha256"
lines, `D:/c5-64bit-work/9047/vthash.py`) **45B3B43359279105...** in both; `Assets/DLC/Shared/Terrain/TerrainDef.csv`
CE16F7E024672FA7... in both. VT overrides the continent textures (`afri_/amer_/asia_/euro_grassland_d`, `_plain_d`,
desert, tundra, forest, mountain folders) and `generic_snow`, `ice_d`, `generic_mesa`, `generic_reef`, `amer_crater`,
`generic_gibraltar_d`. **The server's lookup uses the VT copies**: hundreds of continent-style caches are bit-equal to
the EXE's (Tiny America t0 84, Asia 111; huge Europe 158 ...), which is impossible with the base pack's textures where
VT replaces them [C by implication; no per-file resolution trace]. The conclusion of B holds for this install pair; on an
install without VT (the laptop's Steam install, D94) the reference set would have to be baked again there.
The live runs of 5 used one install for both sides (c5h-e's). The offline drive of 6 compares no colours.

## 5. Acceptance C and D live (Tiny, exact baker)

Route as 3408 6.3 (D33 on 9040's proxy route): 9040's proxy b1 (`CvGameCore_Expansion2.dll` 8F10D815..., `VP64/server.exe`
77877FB1..., x64 DLL 12B7F4FB...; the same bytes as 3408), vpproxy `mode=on local_answers=1 local_verify=0 lua_mirror=0`,
D33 by `tap_live.py config --arm d33` **with the Overlay system kept on (D93, bug 0127: `Overlay = 0` in config.ini;
darkred.py's D33 list still names it on main a8fb811)**, `crashlogs/viewmask.enabled` `clear=0x1`, the composite DLL and
`shell_server.exe` of the fresh clone. Save `C5H-9047-TINY_0008` (copy of `c5h-9028-tiny-t0008`), tap file
`3400/ref/C5H-3401-TINY_0008/armA/heightfield.c5hf`, script `shell/evidence/3408/script_tiny.json`, their driver
`bake_live.py run --arm server --bake pipeline`. **Own check beside their counter**: the server draws a tile without
its bake in magenta (`--fallback constant --constant-rgba 0xFFFF00FF`), so `shotloop.py` takes PrintWindow shots of the
EXE's window as fast as it can from the server's `bake-ahead complete` line on and counts magenta pixels
(R > 235, G < 25, B > 235, every 4th pixel); radius 0 is the positive control of both counts.

Launch 12:14:41 (game pid 50708; guard self-test ok, start-up watch ok, guard check ok), in play 12:28:54 after the
front end's mod activation (14 min, section 10), stopped 12:38 with `cli.py stop` (human turn, autosave settled), c5h-e
restored and verified by hash, `provision.py verify` 0 (12:43). Overlay kept on: no crash at any pan or jump.

| Radius | Bake-ahead | Frames during the script | Gray frames after the bake-ahead (their counter) | Gray tile frames | Own shots with a magenta tile | Resident |
|---|---|---|---|---|---|---|
| -1 | **1,352 tiles in 14.4 s**, 0 failed, 822 ms of baker GPU time | 124,813 | **0** | 0 | 0 of 1,128 shots (12:30:03-12:32:33) | 1,352 = 102.2 MiB in 106.6 MiB of arrays |
| 0 (control) | 102 tiles in 1.2 s | 60,426 | **33** | 380 | 0 of 1,287 shots | 104 (6.9 MiB), 201 evicted |

- **C** holds by their counter: 0 gray frames over the 4 pans, 16 jumps and 8 revisits at radius -1, against 33 at
  radius 0. My own check counted magenta pixels (the fallback colour of an unbaked tile) in PrintWindow shots of the
  game's window at 7.5-8.6 shots per second: at most 2 pixels per shot (a UI element, in both runs), never a tile. The
  shots do show the server's terrain (`cd2/r-1/shots/shot_05.png`), but the control's 33 gray frames came in bursts of
  well under 0.1 s at about 400 frames per second, which the shots missed: **the own check only bounds gray periods to
  below about 0.25 s; it does not confirm the counter**. Their bake-ahead took 5.0 s on the same save (3408 6.4); 14.4 s
  here, with other teams' jobs running [I: load; no timing claim].
- **D**: the Lua road on plots (49, 12) and (49, 15) at 12:34:19.55 (both calls returned 0); the server's log line
  `tile generation block seq 4: 7 tiles changed, re-bake requested` at 12:34:19.895 (**0.345 s** after the road), 7
  re-bakes, latency from the feed's write to the cache set max 22.4 ms, mean 14.5 ms. **Re-baked within 0.37 s.** The
  EXE raised 7 tiles this time (3408: 6 and 10 on earlier loads). The feed reports the 7 tiles "never re-baked" by the
  EXE (its terrain is off under D33), as 3408 saw.
- A frame with a stale tile was counted once (`stale_frames=1`, between the road and the re-bake). `tilegen_err=261`
  counts the server's one-per-second retries before the feed created the block (12:29:48-12:34:10): not an error of
  the run [C, log].
- The live picture shows defect V1: the server's dark river channel (shot_05).
- An earlier attempt (12:02-12:12) is not used: two drivers of mine ran on one game (`cd_mixed/`; tracker 9047 log).

## 6. Residency memory and the bake-ahead on huge, offline [C]

`residency_drive.py --baker pipeline --radius -1` of the fresh clone on 3401's huge height field (armA), install
`pure_vp`, 2026-11-13 12:03:50-12:05:37 (other work on the machine; no timing claim): **bake-ahead 4,922 tiles in
16.6 s, 0 failed**, 987.4 ms of baker GPU time (0.20 ms per tile), mean 2.5 ms of CPU per tile (max 70 ms); resident
4,922 tiles, **350.4 MiB of BC1 in 33 arrays = 351.9 MiB** (8,411 of 8,448 slices: 4,922 t0 + 3,489 t8), 436 draw
copies; **0 gray frames after the bake-ahead** over 5,457 frames of the drive's pans, 24 jumps and 8 revisits (26 gray
frames before it). Server process: video local 806.8 -> 1,364.2 MiB, private 1,149 -> 1,743 MiB. Against 3407 4b
(4,918 + 4 failed, 50.4 s with the wave-2 inputs) and 3408 6.4 (19.4-20.6 s live): consistent; the 4 Gibraltar tiles now
bake (3404 section 9).

## 7. gamefs and TerrainDef.csv [L]

On both own saves `bakelayers_check` (which reads TerrainDef through `c5r::gamefs` since 3404) reported the origin
`Assets/DLC/Shared/Terrain/TerrainDef.csv` and **tables equal** to the TR8 tables read from the live EXE (layer count,
byte -> layer, slope flags, layer -> set, layer kind). The base pack's older copy would differ (comparison 2: 12 of 41
scales, slope flags), so the resolution matches the EXE's on these sessions. The rule's other consequences (2,832 loose
files, 67 changed resolutions, no `.dds`) were not re-measured.

## 8. The research files

Checked against the measurements above (not a full re-read of every derivation):
- terrain-bake-records.md 2.4 (A): reproduced on two other saves; add: a live read can catch a list mid-rebuild (3).
- terrain-bake-comparison.md 5-6, 9 (B, Gibraltar): reproduced exactly (4.1, 6); the decal causes stay [I], now with the
  association of 4.2 b for Tiny.
- terrain-bake-residency.md 4b: "4 tiles fail with the real baker" is history since 3404 section 9 (0 failed, 6).
  6.6's crash text was corrected on main by the MC (D93, bug 0127).
- "352 MB" (D92, residency 4b) is MiB (351.9 x 2^20 B).
- terrain-bake-blend.md and terrain-bake-passes.md: not re-derived; their end products (lists, blend, passes) are
  covered only through the comparator's reproduction.

## 9. Defects and notes

| Id | Severity | What | Owner (suggested) |
|---|---|---|---|
| V1 | moderate (D66) | River tiles: the server's bake shows the blue river-channel material where the stock caches show a river-bed decal; the largest visible remaining colour difference on land (4.2 c). **Closed at cache level 2026-11-15 (epic 0166, 4106, terrain-decals.md 13.3) [L]**: the Riverbank decal drawn, 39 of 39 river t0 cached in the session bit-equal; the frame level is terrain-decals.md 16 (4107) | bake stage 2 (epic 0166, bug 0130) |
| V2 | low | The decal attribution of B stays [I]: strongly associated with decal content on Tiny (4.2 b), not proven; a decal-off reference would settle it. **Closed at cache level 2026-11-15 (4106) [L]**: settled by drawing the same session's decals (4.2's note) | epic 3400 follow-up / stage 2 (epic 0166) |
| V4 | low (test method) | Acceptance C rests on the residency's own counter; a screenshot check at 8 shots per second cannot see its sub-0.1-s gray bursts (5). An independent check needs a per-frame record from the composite (for example the composite DLL counting fallback-coloured frames) | epic 3300 / test tooling |
| V3 | low | darkred.py's D33 list on main still disables `Overlay` (D93 says keep it on); a runner that forgets the override reproduces bug 0127 | team-3300 (applying D93) |

## 10. Front-end mod activation on the proxy route is slow (for the MC's bug)

2026-11-13 11:47-12:02, c5h-e, 9040's proxy b1 (as 5): the front end's "activating 3 mods - this rebuilds the game
database" took **about 15 minutes**: the proxy server's log shows one 512-write `DbOps` batch every 110-200 s (the
batch's round trip itself 0.009-0.045 s), 6 batches = 2,563 writes ending 727 s after the server started, i.e. about
0.28 s per write, the EXE "Not Responding" with its CPU time flat at 1:30 for minutes, and the
vpipc heartbeat reporting the EXE hung (no heartbeat for 5 s) and recovered. *Corrected 2026-11-13 (issue 0129): the 0.28 s is spent in the server, by the DLL's own write to the server's file copy of the game database (each write flushed to c5h-e's rotating disk D:), not on the EXE side; replaced: "0.28 s per write spent before the write reaches the server [I: on the EXE side, not traced]". Cause, fix and measurements: `research/03-dll-engine-boundary/proxy-design.md` section 19.* Log:
`D:/c5-64bit-work/instances/c5h-e/Documents/My Games/Sid Meier's Civilization 5/MODS/(1) Community Patch/VP64/run/20261113-114840-2760-1/server.log`
(and `vpproxy.log` beside it; kept in `D:/c5-64bit-work/9047/vp64-runs` after the proxy layout is removed); dumps of the
EXE and server.exe during it: `D:/c5-64bit-work/9047/dumps/`. The next launch of the same layout (12:15) activated the
mods again (no cache kept between launches) and took 14 min (12:14:41 -> 12:28:54) [L]. 3408's Tiny launch on c5h-b reached play in 30 s (no activation seen) [I:
that instance's mod state already matched].

## 11. Not verified

- The huge exact-baker live series (waits on bug 0127, as the brief says); the huge save's plot dump for 4.2 b.
- Radius 5 live; the stock baseline (6.2); the picture against stock (comparison 8).
- terrain-bake-blend.md / -passes.md derivations; the gamefs rule beyond TerrainDef.csv.
