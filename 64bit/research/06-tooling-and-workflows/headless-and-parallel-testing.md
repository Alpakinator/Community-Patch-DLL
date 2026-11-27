# Headless and parallel testing

Two routes around "one GUI game at a time": the headless host for DLL logic, and a second, Steam-free install for
EXE-side tests. The first is proven; the second is only a candidate.

## 1. Headless DLL A/B tests with `civ5-headless` (`c5h`)

Worked 2026-10-30 (instant-yield trigger logging); faster and safer than the GUI tooling for anything that does
not need the EXE's renderer or UI.

| Item | Value |
|---|---|
| Checkout | `~/Documents/civ5-headless`; run from that folder as `./c5h.cmd run SPEC --turns N ...` |
| Game folder | the toolkit bridge's copy, `verns_civ5_vp_toolkit/headless_data/game/VP_MODPACK_5_4_8_NEUTRALS/Sid Meier's Civilization V`, passed as `--game-dir` |
| DLL | always pass `--dll` (the Steam DLC folder has no VP modpack, so `civ5headless.toml`'s `dll` path is dead) |
| DB | the toml's pinned `db` / `loc_db` (5.4.8 cdrift, `SQLITE_LOGGING=1`, events off) apply by default |
| Spec | map script must be the modpack path, e.g. `Assets/DLC/VP_MODPACK_5_4_8_NEUTRALS/Mods/(2) Vox Populi/Mapscripts/Continental_Drift.lua`; add `--no-mods`; stock example `examples/specs/continents-standard-fixed-seed.json` |
| Output | each run gets `<run-dir>/cache/stats.db`; Lua probe prints in `<run-dir>/Logs/Lua.log` |
| Speed | standard map, 8 majors: 200 turns in ~10 min, ~370 MB, even alongside the owner's 16-worker batch |
| Forcing rare events | `--lua-probe` (`Player:SetHasPolicy`, `Player:InitUnit` + `Unit:PushMission`) |

**Determinism makes it an A/B harness:** games are deterministic for fixed `--seeds`. Build the baseline from
`git worktree add --detach <scratch> master` plus `build_vp_clang.py --config release` (LTO ~5 min; both builds can
run at once), play both DLLs on the same seeds, and diff the two `stats.db` with `ATTACH` (use `C:/...` paths in
`ATTACH`, not `/c/...`).

**DB compatibility:**
- Master before 5.4.7 worked with the pinned 5.4.8 DB (only define values differ).
- **2026-11-07: upstream master 59c50bc00 (post-5.4.8) is not compatible** with the pinned DB: a debug DLL traps in
  the cache phase on `no such table: UnitPromotions_TerrainHeal`. Build a matching DB from the checkout's own mod
  files (~5 s), copy it, flip `CustomModOptions` (e.g. `SQLITE_LOGGING=1`), then pass `--db`:

```
./c5h.cmd dbcache --snapshot cache/db/snapshot/4DF730138BA1F0EE/snapshot.db --mods-dir <repo root> --mod d1b6328c-ff44-4b0d-aad7-c657f83610cd --mod 8411a7a8-dad3-4622-a18e-fcc18324c799 --dll <dll>
```

The explicit `--snapshot` is required: the default Steam-folder snapshot build reports benign WB_Script errors and
`dbcache` refuses it, and snapshot `04E7DFA2784E8518` is polluted with VP tables ("table Community already
exists"). Run with the same `--mods-dir`/`--mod` pair.

Parallelism: headless runs already run in parallel (each has its own run dir), so DLL-only questions need no GUI
instances at all. More on the host and why the 64-bit server is modelled on it: `../03-dll-engine-boundary/headless-host.md`.

### Setup note: antivirus exclusions for the harness (2026-11-10)

[L] On 2026-11-10, between 02:26 and 02:28, Windows Defender quarantined the harness stand-in
(`tools/harness/native/bin/stand_in.exe` in the 1103 worker worktree `agent-a4a7fb1fb3aae8561`) and a
copy of it under a test instance's `install\` folder. The detection was
**`Behavior:Win32/DefenseEvasion.A!ml`** (ThreatID 2147738096), a behaviour-ML detection of the focus
guard's in-process hooking: a suspended start, a remote `LoadLibraryW`, and inline hooks in the process'
own copy of user32. nofocus-README residual case 9 predicted this; it had not happened on the stand-in
on 2026-11-09. Exact sequence (`Get-MpThreatDetection`, 1103 worker):

| Time (EST) | Resources | What |
|---|---|---|
| 02:26:52 | `...\scratchpad\inst\s1\install\stand_in.exe` + `process:_pid:58296` | the behaviour detection, on the running stand-in started by `tools/harness/cli.py launch` at 02:26:47-51 (shell-gates binaries of 2026-11-09, stand-in SHA-256 `7da2bb9a...`), launched from `%TEMP%\claude\...\scratchpad` |
| 02:26:58 | the same file | quarantined |
| 02:27:46, 02:27:59 | `...\agent-a4a7fb1fb3aae8561\tools\harness\native\bin\stand_in.exe` (same hash, never run) | blocked when pytest copied it: Python's `open()` failed with errno 22, `cmd type` said "the file contains a virus or potentially unwanted software", CreateProcess would return 225 (ERROR_VIRUS_INFECTED) |

The copy with the same hash in the shell-gates `nofocus\bin\` was still readable at 02:29. The
process had run for about 12 s, received posted keys and a click and was then terminated by the
harness's `stop`; whether Defender's verdict depended on the `%TEMP%` launch path is not known [I].
After the exclusions (below), stand-ins rebuilt from source (`build.bat`, SHA-256 `B7A6F13A...`) ran
at 02:32-02:36 in the scratchpad with **no detection** (two runs of the 7 live tests in
`tools/harness/tests/test_core_standin.py`, 8 stand-in launches each). (Corrected
2026-11-10 by the 1103 worker; replaced "in a 1105 worker worktree" and "user32 and shell32": the
binaries that ran were the shell-gates build, whose guard hooks user32 only.)

- *2026-11-10, late (issue 9014):* the owner added a further exclusion themself for `D:\c5-64bit-work` (the second instances
  root `D:\c5-64bit-work\instances` and the large scratch of rule D37), confirmed through the MC. c5h-d and c5h-e were
  created there afterwards; no file went missing and every hash matched (section 6).
- *2026-11-14 (ticket 0180, decision D133: D: is storage only):* the second instances root is now `F:\c5-instances`
  (the USB SSD), which has **no** Defender exclusion and none is asked for (standing rule). c5h-d..c5h-i were rebuilt
  there from the Goldberg install on C: (each `verify --full` ok); the D: copies are retired (manifest state `retired`,
  not deleted) and `D:\c5-64bit-work\instances` is no longer searched. Times, the live start from F: and the census of
  what else still points at D: are in `d-drive-load.md` section 10. *Replaced for where instances live: "c5h-d, c5h-e
  (and later c5h-f..c5h-i) under `D:\c5-64bit-work\instances`".*
- *2026-11-14 (issue 9067):* the owner saw constant Windows Security toasts "Security scan required ... needs to perform a
  cloud security scan. The scan could take up to 20 seconds" (genuine Defender, block-at-first-sight). The source was
  Defender's cloud hold and sample upload (event 2050) of every freshly built `.exe` under the new work roots, which had
  no exclusion: 409 uploads on 2026-11-14 alone, 400 of 418 in three days under `C:\c5-64bit-work`, the rest under
  `F:\c5-64bit-work` and the pins folder [L]. Each new binary's first start may wait up to 20 s, so timings that
  span the exclusion change are not like for like. The fix is folder exclusions that the owner adds for the work roots of
  D111/D133. The owner added exclusions for `C:\c5-64bit-work`, `F:\c5-64bit-work`, `F:\c5-instances` and
  the pins folder at 20:42:00 (event 5007), after which a fresh build was no longer held [L]. Details are in
  `tracker/issues/9067-defender-security-scan-popups.md`. Event 2050
  under a project folder means that folder lacks an exclusion. *This replaces, for the work roots, "F: has no
  exclusion and none is asked for".*
- On any machine, the harness's native shim and the per-instance game copies need **Defender path
  exclusions**. The owner added these on 2026-11-10; they were in by 02:30 EST (MC, corrected 2026-11-10 from "about 02:40"):
  `%USERPROFILE%\Documents\GitHub\c5-64bit-patch` (with its `.claude\worktrees`),
  `%USERPROFILE%\Documents\GitHub\Community-Patch-DLL*` (the fork and its sibling worktrees),
  `%USERPROFILE%\c5-instances`, `%USERPROFILE%\c5-ops`, `%USERPROFILE%\AppData\Local\Temp\claude` (the
  Claude scratchpad root). Keep harness binaries, instance copies and test folders inside these paths.
- If a behaviour detection still fires on a running binary inside an excluded path, stop that run and
  record the file, the detection name and the time. The next step, a process exclusion, is the owner's to add. [I] The game EXE copies under `c5-instances` may be flagged the same way once the shim runs
  in them.
- Exclusions are a system setting. Only the user grants them, never an agent and never a script
  (AGENTS.md section 4). The shim is not changed to avoid detection.
- Quarantined binaries are rebuilt from source after the exclusions are in, never restored from
  quarantine.

## 2. Why GUI runs are one at a time today

- vp-dll-dev SKILL: "Games can only be run sequentially ... a hard limitation of the Steam client, which permits a
  single running instance of a title". `vp_game.py start|load` aborts if a `CivilizationV_DX11.exe` is running;
  `vp_build.py` refuses to install over a DLL the running game holds open; `civ_bg.py launch` refuses if the game is
  running.
- The EXE has its own **single-instance check**: the named mutex **`Local\Civ5App`** (`CreateMutexA` at
  0x006023CC in main `FUN_00602380`). If the mutex already exists, the second process exits silently before it
  creates a window [C, 1102, section 4.5]. *Corrected 2026-11-10; replaces "`FUN_00842920` does
  `FindWindowA("WinClass_FXS", title)` ... a second launch raises the first instance" (nofocus README).*
  `FUN_00842920` exists, but DX11 startup never reaches it.
- Every driver addresses "the running Civ 5" by process name, not a specific instance (vp-dll-dev SKILL
  troubleshooting), and civ_bg keeps one state file for the real game (`%LOCALAPPDATA%\civ_bg\state_game.json`).
- A VM was rejected for no-focus work partly because one Steam account can play only one game at a time
  (nofocus README).

## 3. The `pure_vp` install with the Goldberg Steam emulator

**Policy (the owner, 2026-11-10, tracker 0072):** teams may copy saves and configs into the Goldberg install freely.
Standing rule: copies only, under their own names, in per-instance folders; nothing is ever written into the owner's
`My Games` tree or into the Steam install; no junctions or symlinks (AGENTS.md section 4).

Location: `%USERPROFILE%\Desktop\pure_vp\Sid Meier's Civilization V`. Intended basis for running several game
instances in parallel for testing (coordinator's brief). **Rewritten 2026-11-10 (issue 1101) from a read-only
inventory of the install itself**; the earlier version was built from the truncated `pure_vp-listing.txt` capture.
Nothing was launched and nothing was written into the install, the owner's `My Games` tree or the Steam install. Scripts
used (scratch, not in the repo): a PE section/byte diff, a PE export/import/version reader, a minidump module lister.

Policy: issue 0072 answered 2026-11-10 (the owner, via the MC): copies of saves and configs may go into the Goldberg
install freely; the standing rule is unchanged (copies only, under their own names; never the owner's `My Games` tree,
never the Steam install, no links).

### 3.1 Identity of the install [C]

| Item | Value (measured 2026-11-10) |
|---|---|
| Size | 14,406,383,919 B in all; of that 5,734,092,959 B are 55 `STEAM_LOG_<n>.log` files (3.2), `Assets` 5,349,281,862 B, `Resource` 2,401,584,228 B, five opening movies 5 x 96,781,893 B. Without logs and movies ~8.2 GB. *(Corrects "total 6202520 (~6 GB)", which was `ls`'s block count for root files only.)* |
| Origin | A copy of the Steam install made 2026-03-25 13:30 (every top-level folder has that mtime; stock files keep the Steam install's 2025-12-24 dates). Goldberg files added 2026-03-25/26; `d3d9.dll` and the replaced `lua51_Win32.dll` 2026-07-13; VP 5.4.7 modpack 2026-10-14. No `MODS` folder, **no `crashlogs` folder** in the install root. |
| `CivilizationV_DX11.exe` | 16,982,584 B, SHA-256 `04cfdc7105cd0ea099460ff14fa76aace9d071d7eb17916b74fca42c1c8814c3`, TimeDateStamp 0x546CD0A8. `CivilizationV_DX11.exe.bak` is **byte-identical** (same SHA-256): a backup, not a different build. Steam install: `b2ce96aefcf27d1ed6d5d021c5ad1e4fdb0fd01628e115da880596f00670c88e`, same stamp and size. Both carry a **valid Valve Corp. Authenticode signature** (`Get-AuthenticodeSignature`: Valid). |
| `CivilizationV.exe` (DX9) | 10,602,040 B, `15b0c917f03c6e630661f4badaec288fa8ca13dc13f31c1ce939e67cbe4b8a72`, 0x546CCB59; Steam: `e1b4870f462a70ea049480b80eb810f55484b19ce23f6a33a1f8d73c147788d9` |
| `CivilizationV_Tablet.exe` | 16,990,776 B, `390424f161ba8875b461bcb352fe996861d2ab4b82e828045e8970d47c7d74aa`, 0x546CD5F8; Steam: `d0bd797d92781536db56a193f05d9c01cabb2f83c63dc1a50587ae8f92b6e9a7` |
| Identical to the Steam install | `Launcher.exe` (`d1fdb389...`), `CvGameCoreDLLFinal Release.dll` (`03f1b7f0...`), `generate_interfaces_x32.exe` (`034151b3...`, a Goldberg tool that is also in the Steam install), all of `Assets\DLC` except the modpack and one sounds XML (3.5) |
| `steam_appid.txt` | `8930` (4 B); `steam_interfaces.txt` as listed before (14 interface versions) |

### 3.2 Why the EXE hashes differ: per-user Steam CEG, not a patch [C]

Byte diff of pure_vp `04cfdc71...` against Steam `b2ce96ae...` (same section table, same data directories):

| Region | Differs? | Detail |
|---|---|---|
| `.text` (VA 0x401000-0x9D5D30) | **no, byte-identical** | all EXE code |
| `.data`, `.shader`, `.version`, `BINKCONS`, `.reloc` | no | |
| Import directory (RVA 0x78BA18), IAT (RVA 0x5D6000-0x5D6EE0), export directory, load config, debug directory | no | all lie outside the differing ranges |
| `.rdata` | yes, 574,277 B | one **580,221 B high-entropy blob** at file 0x6B3F60-0x7419DD = **VA 0xAB4D60-0xB4285D** (entropy 7.92 / 7.93 bits/byte in the two files), plus 16 small values of 4 or 16 B between VA 0x9EBF40 and 0xAB4654 (0x9EBF40, 0x9EBF7C, 0xA0C160, 0xA0C584, 0xAA41B4, 0xAA6400, 0xAA73B0, 0xAA9238, 0xAA9F30, 0xAACB20, 0xAAD1D0, 0xAAF890, 0xAB09CC, 0xAB1EF0, 0xAB33A4, 0xAB4654) |
| `.rsrc` | yes, 16 B | VA 0x6882D40 |
| Headers | yes | DOS-stub area file 0x40-0xD0: a Valve block `"VLV\0"`, 1, signed length 0x01030C00, then a Unix time (**0x69A8A754 = 2026-03-04 21:42:44 UTC** in pure_vp, 0x69CDAEAE = 2026-04-01 23:47:58 UTC in Steam - each matches its file's mtime) and 128 B of per-copy data; PE CheckSum (0x01039EC9 vs 0x0103A925) |
| Authenticode certificate table (file 0x1030C00, 0x1638 B) | yes, 446 B | the digests and signature over the new content; both valid |

The DX9 and Tablet EXEs show the same pattern ( `.text` identical, one 580,221 B `.rdata` blob, a few small
`.rdata` values, 16 B `.rsrc`, the `VLV` block with 0x69A8A755 = 2026-03-04 21:42:45, the certificate).

Classification: **per-user/per-download Steam CEG** (Custom Executable Generation). Steam regenerated the CEG data
when it delivered each copy (pure_vp's on 2026-03-04, the Steam install's current copy on 2026-04-01) and re-signed
the file. It is not a stripped-CEG build (the CEG code in `.text` is unchanged and the blob is still present) and
not a patch (no `.text`, import, relocation or `.data` byte differs). Put plainly: only the CEG data, the Valve
header block, the checksum and the signature differ; **the import table does not differ**. The pure_vp EXEs
are the same build as the Steam install's, generated four weeks earlier.

Consequences:
- **The DLL instruments recognise it [C, static].** Every check they make lands in identical bytes:
  EngineQueueGuard's stamp 0x546CD0A8, its four `.text` patterns (RVA 0x68951 writer, 0x1CA5E0 reserve, 0x90DF0
  swap, 0x2A5970 dispatch), its address checks and the IAT slot 0x5D647C; EngineAllocTrace's three allocator
  patterns (RVA 0x42C090, 0x413A80, 0x413AB0, and the DX9/Tablet equivalents) and its MSVCR90 import slots;
  MemoryImports' import-slot walk. None overlaps a differing range (sources read in
  `Community-Patch-DLL/.claude/worktrees/shell-gates-d8e422/CvGameCoreDLL_Expansion2/EngineQueueGuard.cpp` and
  `EngineAllocTrace.cpp`). Not yet seen live in a pure_vp process [I].
- Every VA in `research/02-exe-internals/addresses.md` holds for the pure_vp EXE except data inside the CEG blob and
  the 16 small `.rdata` values listed above.
- The SHA-256 of a Goldberg instance's EXE will never equal TOOLING.md's `b2ce96ae...`; tools that check the EXE
  should check the stamp plus `.text` (or the patterns), not the file hash.
- CEG still runs inside the EXE under Goldberg. It evidently passes: the 2026-04-26 minidumps (3.6) show a VP game
  running in this EXE with Goldberg's `steam_api.dll`, and the newest log (2026-10-14) runs 309 s to a clean
  `SteamAPI_Shutdown()` [C, artifacts; not re-run].

### 3.3 The Goldberg emulator (gbe_fork) [C]

| Item | Value |
|---|---|
| `steam_api.dll` | 24,675,752 B, `bd897b3c4a5bbf5bd73f6adb6f77dd6001066cef8c7a8b47f468aafa13c444c5`, TimeDateStamp 0x69B6A4EB (2026-03-15), version resource "GSE Client API" 08.33.09.23, signed by a self-made "CN=GSE" certificate (status UnknownError). PDB path `D:\a\gbe_fork\gbe_fork\build\win\vs2022\debug\experimental\x32\steam_api.pdb`: **the gbe_fork *debug*, *experimental* x32 build** from the project's GitHub Actions. 1,265 exports; `.detourc`/`.detourd` sections (Detours; the experimental build hooks OS networking calls, log line "Hooking lan only functions"). Steam's own is 120,616 B, `b0fd893e...`. |
| `steamclient.dll` | 519,080 B, `ad6838d4...`, 0x69B6A424, "GSE Steamclient.dll" 08.56.38.63, same debug/experimental build; exports only `CreateInterface` and loads `steam_api.dll`. It is not loaded by the EXE (not in any minidump's module list); it serves gbe's ColdClientLoader setup, unused here. |
| `steam_settings\` | only **`configs.main.ini`** is real; everything else is gbe's `*.EXAMPLE*` set (configs.app/overlay/user, achievements, mods, controller, fonts, sounds, ...). Set values: `[main::connectivity] offline=1`, `disable_networking=1`, `listen_port=47584`, `disable_lan_only=0`. Two example placeholders were left **active**: `crash_printer_location=./path/relative/to/dll/crashes.txt` (gbe resolves it to `<install>\path\relative\to\dll\crashes.txt` and writes there on an unhandled crash) and `steam_game_stats_reports_dir=./path/relative/to/dll/` (only for Source-engine games). |
| User settings | No `steam_settings\configs.user.ini`, so gbe uses the global one: `%APPDATA%\GSE Saves\settings\configs.user.ini` (created 2025-12-26 14:51, `account_name=gse orca`, `account_steamid=76561199869884579` = account id **1909618851**, `language=english`). |
| Saves (gbe's own) | Base path `%USERPROFILE%\AppData\Roaming\GSE Saves\` [path scrubbed 2026-11-17] (log line `create_localstorage_settings() ... base path for saves`). Per-app folder `GSE Saves\8930\` holds only `playtime.txt` (1 B, last written 2026-10-14 20:15). No `achievements.json`, no `remote\`: the game wrote no achievements or Steam Cloud files through gbe [C, files]. Also present: `%APPDATA%\Goldberg SteamEmu Saves\settings\` (2026-03-25: account_name `Noob`, user_steam_id 76561197965572245), the legacy Goldberg's folder, not read by gbe_fork [I]. |
| `userdata\1909618851\8930` | empty folders (copied from the Steam install, where gbe ran first on 2025-12-26; the id is gbe's generated account id) |
| `STEAM_LOG_<n>.log` | 55 files, 5,734,092,959 B, 2026-04-24 to 2026-10-14 20:15; largest 2,995,866,947 B (2026-04-26), 1,655,746,889 B (2026-07-13). `<n>` is a random number per process, not a PID. Every `SteamAPI_RunCallbacks` frame logs ~5 lines (RunCallbacks begin/done, `SteamUserStats() old`, `SteamUser() old`, `BLoggedOn()`): the 2026-10-14 run wrote 27.5 MB in 309 s (~89 KB/s), so a long autoplay run writes GB. |

**gbe logging: it cannot be turned off by configuration; it can be moved.** [C, gbe_fork source, `dev` branch read
2026-11-10, plus strings in the installed DLL]
- `dll/dll/common_includes.h`: `PRINT_DEBUG` writes to `dbg_logger` only `#ifndef EMU_RELEASE_BUILD`; in release
  builds it is an empty macro. No ini key or runtime switch exists. The only ways to stop the logs are a release
  build of the same version (a binary download: not done, needs the MC/the owner) or not logging into the instance root.
- `dll/base.cpp`: `dbg_log dbg_logger(get_full_program_path() + "STEAM_LOG_" + to_string(rand_number(UINT32_MAX)) + ".log")`.
  `get_full_program_path()` returns the env var **`GseAppPath`** if set, else the folder of the emulator DLL. The
  installed DLL contains the string `GseAppPath` [C]. Caution: the same program path is where gbe looks for
  `steam_settings\` (`Local_Storage::get_game_settings_path()` = program path + `steam_settings`), so a
  `GseAppPath` folder must also hold a copy of `steam_settings\` [C source; I that the installed build behaves the
  same - not run].
- Saves: env **`GseSavePath`** overrides, else `[user::saves] local_save_path` in `steam_settings\configs.user.ini`
  (relative to the program path; "full portable": the global `%APPDATA%\GSE Saves\settings` is then ignored), else
  `%APPDATA%\GSE Saves\` via `SHGetFolderPathW(CSIDL_APPDATA)` (`dll/settings_parser.cpp`, `dll/local_storage.cpp`,
  `post_build/README.release.md`). The installed DLL contains `GseSavePath` and `local_save_path` [C]. Note: gbe uses
  **CSIDL_APPDATA**, not Documents, so the 1105 Documents redirection alone does not separate gbe's saves.

### 3.4 The replaced `d3d9.dll` and `lua51_Win32.dll` [C]

**`d3d9.dll`** (28,160 B, `76312069b44ae97098985adf63eeefd10cee97822b54ae14cb2f9236978328ae`, TimeDateStamp 0x69EE70F0 =
2026-04-26 20:09 UTC, file mtime 2026-07-13, unsigned, no version resource). PDB path
`%USERPROFILE%\d3d9-blank-proxy\Release\d3d9.pdb`; source at `%USERPROFILE%\d3d9-blank-proxy` (git remote
`https://github.com/CIVITAS-John/d3d9-blank-proxy.git`, a fork of ME3Tweaks' blank proxy; last commits "Added minhook
and got kiero hooking on D3D11", "Added an auto re-start mechanism", "Force Civ to play sound in production mode").
It forwards the 15 `d3d9` exports to `%SystemRoot%\SysWOW64\d3d9.dll` and, from a thread started in `DllMain`, uses
kiero + MinHook to **hook D3D11 `IDXGISwapChain::Present`** (vtable index 8). Behaviour from `d3d9_blank_proxy/d3d9.cpp`
(the binary's imports - COMCTL32 #410/#412/#413, Thread32First, GetAsyncKeyState - match it):
- Unless env `VOX_DEORUM_PRODUCTION=1`: **rendering is off by default** - the hook skips the real Present and sleeps
  33 ms (30 fps cap, nothing reaches the screen); Backspace toggles it, read with `GetAsyncKeyState` only while the
  game owns the foreground window. Screenshots of a background instance would show stale frames.
- **Watchdog: `ExitProcess(0)` when Present has not been called for 300 s**, in both modes. A stalled or very long
  load, or anything that stops Presents, kills the game without regard to autosaves.
- With `VOX_DEORUM_PRODUCTION=1`: renders normally, and on the first Present MinHook-hooks user32
  `GetForegroundWindow`, `GetActiveWindow`, `GetFocus` and subclasses the game window to swallow `WM_ACTIVATEAPP`,
  `WM_ACTIVATE`, `WM_NCACTIVATE`, `WM_KILLFOCUS` (`audio_keepalive.cpp`) - the same calls and messages civ_bg's focus
  guard handles, so the two would stack.

Does a DX11 launch load it? **Yes, at process start: the DX11 EXE imports `d3d9.dll` statically**
(`D3DPERF_BeginEvent`, `D3DPERF_EndEvent`; checked in both EXE copies) [C, 1102, section 4.7]. *Corrected
2026-11-10; replaces "does not import `d3d9.dll` statically, loaded by name at run time".* Every 2026-04-26 minidump
of the pure_vp DX11 EXE lists `C:\Windows\System32\d3d9.dll` (WOW64 path) next to `d3d11.dll` and `dxgi.dll` [C,
dumps]; those dumps predate the proxy. With the proxy in the application folder, the standard DLL search order
(application folder first; `d3d9.dll` is not a KnownDLL) makes the DX11 EXE load **the proxy** at start [I, strong;
not observed live]. Instance copies leave it out (3.7).

**`lua51_Win32.dll`** (171,008 B, `f276ef5564053bc1febbb73b8f50bcb84d8c40e33d2dfd27c5e5d885b01dce6f`, TimeDateStamp
0x68D0B3A0 = 2025-09-22, mtime 2026-07-13, unsigned, no version resource): a rebuild of **PUC Lua 5.1.5** (`$Lua: Lua
5.1.5 Copyright (C) 1994-2012`), PDB `F:\Minor Solutions\lua51-win32\src\lua51_Win32.pdb`, same 123 exports as the
stock DLL, MSVCR90 like the stock one, plus four kernel32 critical-section imports the stock DLL lacks (a build with
`lua_lock` - inferred [I]). `lua51_Win32.dll.bak` is the stock Firaxis Lua 5.1.4 (151,040 B, `f58ceffe...`, 0x514886F2,
PDB `d:\Firaxis\Civ5Expansion2\SDKs\Lua\5.1\...`), byte-identical to the Steam install's `lua51_Win32.dll` and
`.stock`. The DX11 EXE imports `lua51_Win32.dll` statically (79 functions) [C], so **every launch loads the
replacement**; the 2026-04-26 dumps show the stock one (0x514886F2) loaded from the install folder, i.e. before the
swap. `lua51_Win32_jit.dll` (428,544 B, `550aaf10...`, "LuaJIT 2.1.0-alpha", export name `luajit.dll`) is not
loaded by anything under that name [I]. Both replacements and the proxy come from the "Vox Deorum" (CIVITAS)
autoplay setup, as does the modpack's `autoplay` mod [I, from names and the remote].

### 3.5 `Assets\DLC` and the VP install type [C]

- Same folders and byte totals as the Steam install (`DLC_01`..`07`, `DLC_Deluxe`, `DLC_SP_Maps(_2,_3)`,
  `Expansion`, `Expansion2`, `Shared`, `Tablet`, `UI_bc1` 190 files, `VPUI` 10 files, `VT` 55 files, `.idea`), except
  `Expansion2\Sounds\XML\MinorCivSounds_VoxPopuli.xml` (Steam 5,903 B dated 2026-07-17, pure_vp 5,787 B) and the
  modpack below, which only pure_vp has. `UI_bc1`, `VPUI` and `VT` are in the Steam install too (not pure_vp
  additions).
- **`MP_AUTOPLAY_VP_5_4_7`**: VP 5.4.7 installed as a **DLC modpack** (2,734 files, 288,247,205 B, written
  2026-10-14). `MPModsPack.Civ5Pkg`: GUID `{b5932ae4-0f4f-498f-9333-e2d31b20e095}`, SteamApp 235580, Priority 300,
  name "VP Modpack". `Mods\`: `(1) Community Patch`, `(2) Vox Populi`, `(3a) VP - EUI Compatibility Files`,
  `autoplay`, `Quick Turns`; plus `Override\` and `UI\` (`InGame.lua`). Its DLL `Mods\(1) Community Patch\
  CvGameCore_Expansion2.dll`: 16,837,120 B, `FF1903555CE1B2D1B707E1FB205809AF03F248EA7DBFEECABE04230C66053B32`,
  build string `Release-5.4.7-0-g7336b3f77 Clean` (the official VP release build), **no instruments** (no
  `VPLuaExec`, `VPMemSnapshot`, queue guard or alloc trace strings).
- The April dumps loaded `Assets\DLC\MP_AUTOPLAY_VP_5_2_3\...\CvGameCore_Expansion2.dll` (0x69E337BF): the modpack has
  been swapped between versions in place.
- So pure_vp's VP is **5.4.7 in modpack form**, not the 5.4.8 mods form of the owner's reference save `VP8P-HUGE_0350`. A
  DLC modpack is always active, so running the mods form there means removing it from the instance's copy (item 6
  of the old unknowns, resolved).

### 3.6 Links into the owner's tree, registry, logs [C]

- `Config.ini.lnk` -> `%USERPROFILE%\Documents\My Games\Sid Meier's Civilization 5\config.ini`; `Logs.lnk` -> `...\Logs`;
  `Saves.lnk` -> `...\Saves` (all three 2026-10-14 20:10, no arguments). They point into **the owner's** user folder: the
  install has been run against the owner's `My Games` tree (no redirection exists in it).
- `reg query HKCU\Software\Firaxis\Civilization5` (2026-11-10): `LastKnownPath` = the **Steam** install,
  `LastKnownExe` = `DX11`; nothing else. Sibling key `HKCU\Software\Firaxis\ModBuddy`. `LauncherLog.txt` (2025-06-07,
  copied from Steam) shows the Launcher writing that key.
- `CvAssert.log` (5,298 B): asserts from 2025-12-19 to 2026-07-02, from Justus's release DLLs and the owner's own builds
  (`%USERPROFILE%\documents\github\community-patch-dll\...` [path scrubbed 2026-11-17]); the last one is in `SqliteLogger.cpp:595`.
  `Callstack.log` 2024-05-23 (a Squads mod Lua stack, copied from Steam). `RendererStats.csv` 2026-02-04.
- Minidumps: eight `CvMiniDump_20260426_<hhmmss>_5.2.3-0-ge9bb9f486_Release.dmp` (17:49 to 20:05): DX11 EXE from
  pure_vp at base 0x420000, Goldberg `steam_api.dll` (0x69B6A4EB), stock `lua51_Win32.dll`, System32 `d3d9.dll`, the
  5.2.3 modpack DLL.

### 3.7 What a per-instance copy must exclude or adjust (for 1106; a checklist, not yet tried)

Exclude:
- `STEAM_LOG_*.log` (5.7 GB) and gbe's `crashes.txt` placeholder folder if one ever appears.
- `CvMiniDump_*.dmp`, `CvAssert.log`, `Callstack.log`, `RendererStats.csv`, `LauncherLog.txt`, `*.bak` (the EXE `.bak`
  is a duplicate; `lua51_Win32.dll.bak` is the stock Lua and should be **kept** under the live name if the stock Lua
  is chosen).
- The three `.lnk` files (they point into the owner's tree).
- `Assets\DLC\MP_AUTOPLAY_VP_5_4_7` for any run that uses the mods form (5.4.8, the owner's saves); keep it only for
  deliberate 5.4.7 modpack runs.
- Optionally the five opening movies (484 MB) - effect of their absence not verified.

Decide (coordinator):
- **`d3d9.dll`** (3.4): leave it out (the DX11 EXE then loads the system `d3d9.dll`, as in April) unless the
  Vox Deorum render-off mode is wanted. Kept without `VOX_DEORUM_PRODUCTION=1` it blanks rendering; kept with it, it
  hooks the focus calls the guard hooks; in both cases its 300 s watchdog can `ExitProcess` the game. Recommended:
  exclude.
- **`lua51_Win32.dll`**: Lua 5.1.5 rebuild vs the stock 5.1.4 (`.bak`). The Steam install, where all instruments and
  the Lua channel were proven, runs the stock one; for comparable results use the stock DLL.

Adjust:
- `steam_settings\configs.main.ini`: clear `crash_printer_location` and `steam_game_stats_reports_dir` (or point
  them into the instance's `run\`). Keep `offline=1`, `disable_networking=1`.
- Add `steam_settings\configs.user.ini` with `[user::saves] local_save_path=<instance>\gse` (or set env `GseSavePath`
  per instance) so gbe never writes `%APPDATA%\GSE Saves` or reads its global settings; add `account_name` /
  `account_steamid` there if instances must differ (they need not with networking off).
- To keep logs out of the install, set env `GseAppPath=<instance>\run\gbe\` **and** copy `steam_settings\` into that
  folder (gbe reads its settings from the program path). The logs still grow ~89 KB/s; rotate or delete per run.
- Create `crashlogs\` in the instance's install copy: the instruments' marker files and logs (`queueguard.log`, ...)
  are relative to the working directory, and with no folder they write nothing.
- `listen_port=47584`: whether gbe still binds it with `disable_networking=1`, and so collides between two
  instances, is not verified - check in 1109.
- Free space: C: had 92 GB free (coordinator, 2026-11-10); at ~8 GB per copy, about ten copies fit.

### What it should enable

- Starting the EXE without the Steam client: the emulator answers Steam API calls; `SteamAPI_RestartAppIfNecessary`
  returns to the game (logged in the 2026-10-14 run, 13.3 s after `DllMain` - the EXE's start-up before it calls
  Steam), so the Steam single-running-instance rule and the Steam relaunch (civ_bg's residual case 1) should not
  apply [C for one past run; not run by us].
- No Steam overlay (it is injected only into Steam-launched games, nofocus README); gbe's own overlay should be off [I] (no
  `configs.overlay.ini`, experimental overlay default off), which removes the overlay recursion hazard from
  compositor tests.
- Possibly several instances at once, which would turn the sequential GUI A/B runs into
  parallel ones (the n=5 memory-myths batch of 2026-09-29 ran 19 GUI runs one after another in 8.5 h,
  `night_batch.py`).

### What is unknown (must be validated before use)

*Answered 2026-11-10 by epic 1100: see section 5 (validated workflow, Q8 closed) and section 4.*

1. **Shared user folder.** Both installs default to `%USERPROFILE%\Documents\My Games\Sid Meier's Civilization 5`
   (config.ini, GraphicsSettingsDX11.ini, `cache\` with stats.db, `luaexec.enabled` and Lua request files,
   `Logs\` truncated at every start, `Saves\`, `ModdedSaves\`, `MODS\`); the `.lnk` files (3.6) confirm pure_vp has
   used the owner's tree. Two instances would share config, saves, mods database and logs, and an experiment instance
   could write into the owner's saves. A per-instance user folder is the job of 1102/1105; gbe's own saves need the
   separate `GseSavePath`/`local_save_path` setting (3.3).
2. **Shared registry.** `HKCU\Software\Firaxis\Civilization5` holds `LastKnownPath` = the Steam install and
   `LastKnownExe` = DX11 (3.6); launching through `Launcher.exe` may cross the two installs. Launch the EXE directly.
3. **The EXE's own single-instance check** (`FindWindowA("WinClass_FXS", title)`): whether a second instance exits,
   or only raises the first, is unknown; the focus guard blocks the `SetForegroundWindow` part.
4. **Fixed kernel-object names** collide across instances in one session: the Lua channel's `Local\VPLuaExec`, the
   memory snapshot event `Local\VPMemSnapshot`, the `TurnByTurn` freeze mutex, the compositor's
   `Local\VPShellSection` (the texture name carries the server pid; the section does not). `Local\CivBgGuard_<pid>`
   is per-pid and fine. (The installed 5.4.7 DLL has only `TurnByTurn`; the others need an instrumented DLL.)
5. **Driver scripts are single-instance**: `vp_common.py` paths (`VP_INSTALL_DIR`, `VP_USER_DIR`, ... are
   overridable), civ_bg's one state file, `wait_turn.py`'s liveness check by image name, `addrspace.py` taking the
   first PID by name.
6. ~~Whether VP's install type in `pure_vp` matches the 5.4.8 DLL; what is in its `Assets\DLC`.~~ Resolved
   2026-11-10: VP 5.4.7 as a DLC modpack (3.5); remove it for mods-form runs.
7. GPU and memory cost of N rendering instances (each EXE ~1.6-2.2 GB private on the huge save; the machine has
   32 GB RAM, nofocus README).
8. (new) The `d3d9.dll` proxy and the replaced Lua (3.4) change behaviour against the Steam install; see 3.7.

### Proposed workflow for parallel EXE testing (proposal of 2026-11-10; validated the same day, see section 5)

1. ~~Inventory `pure_vp` read-only.~~ Done 2026-11-10 (issue 1101; this section).
2. One instance first: launch `pure_vp`'s `CivilizationV_DX11.exe` directly with Steam closed, through civ_bg's
   `--exe PATH --proc NAME` (exists in civ_bg.py; it treats the target as a non-game and does not run vp_modgame's
   setup, so mods/save selection would need another route). Confirm: no Steam relaunch, PID stable, which user
   folder it writes (watch `Logs\Lua.log` mtime or Process Monitor, which needs elevation), whether
   `HKCU\Software\Firaxis\Civilization5` changes.
3. Decide the per-instance user-folder strategy from what step 2 shows; never let a test instance write into the owner's
   `My Games` tree.
4. Make the DLL's channel and markers instance-aware before running two at once (for example, suffix kernel-object
   names with the PID - a code change, not yet made), and give civ_bg and the waiters a PID argument.
5. Only then start a second instance and check the single-instance check, focus, and that each Lua channel answers
   for its own game.
6. Keep the safety rules: restore by hash, no junctions into either install, never kill during an autosave.

Sources added 2026-11-10 (issue 1101): the pure_vp install itself (files, PE headers, Authenticode, minidump module
lists, `STEAM_LOG_4285538848.log`, `steam_settings\configs.main.ini`, `.lnk` targets), `%APPDATA%\GSE Saves`,
`reg query HKCU\Software\Firaxis\Civilization5`, `%USERPROFILE%\d3d9-blank-proxy` (source of the proxy),
`Community-Patch-DLL/.claude/worktrees/shell-gates-d8e422/CvGameCoreDLL_Expansion2/EngineQueueGuard.cpp` and
`EngineAllocTrace.cpp` (instrument checks), gbe_fork `dev` branch on GitHub (Detanup01/gbe_fork:
`dll/dll/common_includes.h`, `dll/base.cpp`, `dll/settings_parser.cpp`, `dll/local_storage.cpp`,
`post_build/README.release.md`), read 2026-11-10. Short fact list for 1102 and 1106: `1101-notes.md` in the epic 1100 shared notes folder.

Sources: `research/sources/memory-notes/headless-dll-ab-test.md`, `research/sources/pure_vp-listing.txt`,
`research/sources/skills/vp-dll-dev-SKILL.md` ("One game at a time", Troubleshooting),
`research/sources/shell-gates/nofocus-README.md` (EXE single-instance check, rejected VM, residual cases),
`tools/civ_bg.py`, `TOOLING.md` (EXE hash), `compositor-README.md` (section name), `civ5-game-ui-SKILL.md` (memory
snapshot event), `research/sources/memory-notes/n5-run-plan.md`.

### 3.8 Provisioned instances (1106, 2026-11-10)

Tool: `tools/instances/provision.py` (usage in `tools/instances/README.md`). It applies the 3.7 checklist; nothing
was launched, no EXE from a copy was run. Evidence: `tools/harness/evidence/1106/`.

What a copy is (profile `mods548`) [C, files]:
- `install\` = pure_vp minus `STEAM_LOG_*.log`, `CvMiniDump_*.dmp`, the three `.lnk`, `CvAssert.log`, `Callstack.log`,
  `RendererStats.csv`, `LauncherLog.txt`, `CivilizationV_DX11.exe.bak`, `d3d9.dll`, four opening movies (the
  `en_US` one is kept, matching `Language = en_US`; `UserSettings.ini` gets `SkipIntroVideo = 1`, already the owner's
  value) and `Assets\DLC\MP_AUTOPLAY_VP_5_4_7\`. `lua51_Win32.dll` = pure_vp's `.bak`, SHA-256
  `f58ceffeb317e42b7691d823b0db4da63da0b988d2b381ccec9c3c18a58c0e35`, the same hash as the Steam install's live
  `lua51_Win32.dll` (both hashed 2026-11-10). `install\crashlogs\` created empty.
- gbe: `configs.main.ini` `crash_printer_location=<inst>\run\gbe\crashes.txt`,
  `steam_game_stats_reports_dir=<inst>\run\gbe\stats\`, `listen_port` unique per instance (47585, 47586, ...;
  pure_vp keeps 47584); `offline=1`, `disable_networking=1` unchanged. New `configs.user.ini`: `account_name=gse orca`,
  `account_steamid=76561199869884579` (the global account of 3.3), `language=english`, `local_save_path=<inst>\gse\`.
  `GseAppPath` is not set, so gbe's `STEAM_LOG_*.log` will land in `<inst>\install\` (inside the instance; delete
  per run). `create --gbe-dir PATH` puts another gbe build's `steam_api.dll` (and `steamclient.dll`) into the copy
  instead, with both hashes in `instance.json` `gbe.dlls` (for 1112's logging-off release build, D16; c5h-a and
  c5h-b were made without it and carry pure_vp's debug build `bd897b3c...`). `set-gbe NAME --gbe-dir PATH`
  (2026-11-10 03:45) swaps them in an existing instance, behind 1113's wrapper too (it then replaces
  `steam_api_gbe.dll` and needs an identical export table). 1112's release build in
  `%USERPROFILE%\c5-instances\_tools\gbe-release-x32` exports the same names and ordinals as pure_vp's debug build
  (`steam_api.dll` 1,265 exports, `steamclient.dll` 1) [C, export tables compared 2026-11-10].
  Later additions (2026-11-10 03:45-04:00, usage in `tools/instances/README.md`): `set-dll` (game DLL swap with the
  first DLL kept in `<inst>\run\dll-original\`, PE x86 check, build string), `collect` (cache, Logs, crashlogs,
  saves out with a SHA-256 manifest), `clean-logs`; `verify` treats the EXE's `Config.ini.lnk` / `Logs.lnk` /
  `Saves.lnk` in `install\` as run output and fails if their target leaves the instance. pure_vp's three `.lnk`
  have LinkInfo local base `C:\Users\` + suffix `<user>\Documents\My Games\Sid Meier's Civilization 5\<config.ini|Logs|Saves>`,
  link flags 0x8f [C, parsed 2026-11-10]. Build strings read by `pe_info`: the owner's CP DLL `262aee88...` =
  `Release-5.4.8-0-g3dc5bea6e Clean`, TimeDateStamp 0x6AD20081 (2026-10-16); the 5.4.7 modpack DLL =
  `Release-5.4.7-0-g7336b3f77 Clean`, 0x6AC91B2A [C].
  **Lua channel in these DLLs** [C, byte scan 2026-11-10 04:00]: the owner's CP DLL `262aee88...` (the DLL c5h-a and
  c5h-b were created with) contains no `VPLuaExec` or `VPMemSnapshot` string, ASCII or UTF-16, only `TurnByTurn`:
  **it has no Lua channel**, so a harness run on it cannot drive turns until an instrumented DLL is set. The 5.4.7
  modpack DLL has none either. 1107's `Release-instance-aware-440878983` build has `Local\VPLuaExec%s%s` and
  `Local\VPLuaExecDone%s%s`. `provision.py` records this as `dll.names` = `none` / `legacy` / `instance-aware`. Absolute paths in `local_save_path` are allowed by gbe's example ini; whether this build accepts the
  absolute `crash_printer_location` and the trailing `\` of `GseSavePath` is not verified [I] - check in 1108
  (`<inst>\gse\` should fill, `%APPDATA%\GSE Saves\8930\playtime.txt` should keep its 2026-10-14 mtime).
- User folder `Documents\My Games\Sid Meier's Civilization 5\`: copies of `MODS\(1) Community Patch`,
  `(2) Vox Populi`, `(3a) VP - EUI Compatibility Files`; `config.ini` (one absolute path into the owner's tree,
  `LastMapScript`, rewritten to the instance), `GraphicsSettingsDX11.ini` (`FullScreen = 0`; the owner's already is),
  `UserSettings.ini`; `cache\luaexec.enabled`; saves under new names. Not seeded: `cache\` databases (the game
  builds `Civ5ModsDatabase.db` on first start, so the mods are not yet enabled there - the save load or the mods
  menu must enable them, 1104/1108), `Text\`, `Maps\`, `ModUserData\`.
- `LocalAppData\` (for `C5_LOCALAPPDATA`, 4.2), `gse\`, `run\gbe\` created empty.
- `instance.json` records the launcher's env (`C5_DOCUMENTS`, `C5_LOCALAPPDATA`, `C5_ISOLATE_SINGLE_INSTANCE`,
  `GseSavePath`, `VP_INSTANCE`, `VP_LUAEXEC=1`), the DLL (`original_sha256`, `sha256`), saves, key file hashes, and
  the SHA-256 of `install.manifest.tsv` / `user.manifest.tsv`, which list sha256 and size of every copied file.
- The owner's reference save `VP8P-HUGE_0350.Civ5Save` (2,227,934 B, SHA-256 `c499d3c6cf83b8ba...`) names only two mods in
  its header: `(1) Community Patch` `d1b6328c-ff44-4b0d-aad7-c657f83610cd` and `(2) Vox Populi`
  `8411a7a8-dad3-4622-a18e-fcc18324c799` [C, header bytes]; `(3a)` is copied for the EUI UI only.

Measurements, 2026-11-10 02:43-02:51 EST, C: NVMe, root `%USERPROFILE%\c5-instances` (Defender exclusion), 8 copy
threads, Python 3.11.5 [L]:

| | c5h-a | c5h-b |
|---|---|---|
| Files / bytes in `install\` | 13,310 / 7,964,536,935 | same |
| Files / bytes in `Documents\` | 2,136 / 259,277,735 (mods 2,131 files) | same |
| Instance total | 15,449 files, 8,226,047,821 B (8.23 GB) | 8,226,047,823 B |
| Create time | 98.4 s (install 83.7 s, user folder 13.5 s) | 120.4 s (103.1 s, 13.7 s) |
| Free on C: before / after | 83.8 / 75.9 GB | 75.9 / 67.3 GB |
| `verify` (every file re-hashed) | ok, 71.6 s | ok, 67.9 s |
| `verify --quick` (install by size) | ok, 17.0 s | - |

DLL in both: the mod's own `CvGameCore_Expansion2.dll` from the owner's tree, 16,837,632 B,
`262aee88c5312c4bd4e899da1d8b413c000bc996fa57abfe8d04b87fd37df16e` (the 2026-11-09 reference value in this section's `README.md`, safety rule 4;
`--dll PATH` replaces it, for 1107's build). Save `C5H-VP8P-HUGE_0350.Civ5Save` in each `ModdedSaves\single\`.

The owner's tree and the installs unchanged [C]: a listing (kind, path, size, mtime) of the owner's top level, `MODS\(1)`,
`(2)`, `(3a)`, `ModdedSaves\single`, all of pure_vp and the Steam install's top level, plus SHA-256 of
`config.ini` (`7b53b17c...`), `GraphicsSettingsDX11.ini` (`f1d9cf01...`) and the CP DLL (`262aee88...`), taken at
02:42:46 and 02:50:16: no difference (`art-tree-diff.txt`). The instances stay in place for 1108.

### 3.8a The Steam update guard and the golden pre-update copy (ticket 0583, D200, 2026-11-19)

Firaxis shipped a Civ V update on 2026-11-19 (Steam build 26577625). The patch, the EXE notes and the tools are keyed
to build 4390913. Once Steam updates an install, every tool that copies from it or launches through it would get the
new build without a word, so those tools now check the install's DX11 EXE first and stop unless told explicitly.

*Changed 2026-11-19 22:40 EST by epic 0590 (child C3, D202): both builds are known now, keyed by TimeDateStamp and
SizeOfImage from `tools/exebuild/builds.py`; the file size is information only; see 3.8b. The rest of this section
describes the 0583 guard as it was (one known build, keyed by stamp and size).*

**The check** (`tools/harness/exe_build.py`, the one list every tool imports): `CivilizationV_DX11.exe` must have PE
TimeDateStamp `0x546CD0A8` and one of the known per-copy sizes. Steam makes every installed EXE unique (section 3.2;
`quiet-testbed.md` 15), and the size differs too [C, files read 2026-11-19 / quiet-testbed 15]:

| Copy | Size (B) | Stamp |
|---|---|---|
| Workstation Steam install, `pure_vp`, the golden copy | 16,982,584 | 0x546CD0A8 |
| Laptop Steam install | 16,982,584 | 0x546CD0A8 |
| oldpc Steam install (signature 256 B larger) | 16,982,840 | 0x546CD0A8 |

A missing EXE, a non-PE file, another stamp or the right stamp with another size is refused; the message names the
stamp and size found, the golden copy and the option. A new machine's copy of 4390913 with yet another size is
refused too: add its size to `KNOWN_SIZES` (with where it came from) rather than passing the option. The option is
`--allow-unknown-exe-build` everywhere; use it only once the migration to the new build is approved (D200).

Where the check runs [C, code; hermetic tests, 2026-11-19]:

| Tool | Before | Refusal | Option |
|---|---|---|---|
| `tools/instances/provision.py create` | any write, on `--source-install` | exit 2, nothing written | `--allow-unknown-exe-build`; the record goes to `instance.json` `source_exe` |
| `provision.py set-exe` (and `commission.py set-exe`, which calls it on the test hosts) | the copy of `--exe` (DX11 EXE only) | exit 2, the instance unchanged | `--allow-unknown-exe-build` |
| `tools/testbed/jobs/steam_launch.py --phase install` | the backup and the copy into the Steam install | a precondition, exit 2 | `--allow-unknown-exe-build` |
| `steam_launch.py` session op `launch` | `steam.exe -applaunch` / `steam://` | `"rc": "refused"` **always** unless the session job has `--allow-steam-update` (a launch through Steam installs a pending update first; oldpc and the laptop are set to update Civ V only on launch), then still for another build | `--allow-steam-update`, `--allow-unknown-exe-build` |
| `tools/x64-dll/civ5map/run_ab.py` | the build, when the default maps are read from the Steam install | exit 2 | `--steam-install DIR` (e.g. the golden copy), `--allow-unknown-exe-build`, or `--no-default-maps` |

Also new in `create`: a source without `steam_settings\configs.main.ini` (not a Goldberg base) is refused before any
copy; it used to fail after the whole install had been copied.

Looked at and left unchanged: `tools/harness/instance.py` `Instance.legacy_steam()` only describes the owner's Steam game
for reading (status, logs, shots of a game the owner started); the harness never launches it (`launch()` and `env()`
refuse) and copies nothing from it. `tools/harness/backup.py` names the Steam install only as a write-protected root.
`tools/harness/native/wrapper.py` and `tools/hi/phase/jobs/install_job.py` only refuse Steam paths.
`tools/testbed/jobs/capabilities.py` reads the host's `local_game_exe` hash for a report. Not covered:
`tools/installer/patch64.iss` (the shipped installer installs into the player's Steam install and has no EXE build
check; a product change, its own ticket).

**The golden copy** [C, files read 2026-11-19 ~21:00 EST]: `F:\c5-64bit-work\golden\civ5-steam-build4390913-ws\` is a
copy of the workstation's Steam install taken by the MC before the update (2026-11-19, 20:4x): 13,379 files,
16,808,807,558 B. Its manifest is beside it, `F:\c5-64bit-work\golden\civ5-steam-build4390913-ws.sha256.json`: one
JSON object, relative path with `/` -> SHA-256, 13,379 entries (the file's own SHA-256 `0717bb05...cfc34` when read).
`CivilizationV_DX11.exe` there: 16,982,584 B, 0x546CD0A8, SHA-256 `b2ce96ae...0670c88e` (the workstation Steam
copy of section 3.2 and the Ghidra project). `tools/harness/exe_build.py` `GOLDEN_INSTALL` names it; environment
variable `C5_GOLDEN_INSTALL` overrides. Check it against its manifest before trusting it (a script that hashes every
file and compares with the JSON; `provision.py` does not read this manifest).

**Provisioning and the golden copy.** `provision.py create` copies from `pure_vp` (`--source-install`, default
`%USERPROFILE%\Desktop\pure_vp\Sid Meier's Civilization V`), never from the Steam install, and Steam does not update
`pure_vp`; the test hosts copy from their `base/pure_vp`. So instances keep build 4390913 after the workstation's
update without any change. The golden copy is a plain Steam install, not a Goldberg base: it has Steam's own
`steam_api.dll` (`b0fd893e...`), no `steam_settings\`, no `steam_appid.txt`, and its live `lua51_Win32.dll` is already
the stock `f58ceffe...` (plus `lua51_Win32.dll.stock`, no `.bak`). `create --source-install <golden>` is therefore
refused by the Goldberg check above, before any write. Its uses: the reference for the old build (0582), maps for
`run_ab.py --steam-install`, and the source to rebuild a Goldberg base the way `pure_vp` was made (section 3) should
`pure_vp` ever be lost; the build check accepts it. [I: making `create` take a plain Steam install directly would
need gbe's root files and `steam_settings\` from another source and the Lua fallback; not done.] Instances of the new
build are made from `pure_vp` with the new build's changed files laid over the copy (`create --exe-build`, 3.8b), not
from the golden copy directly.

### 3.8b Instances of build 26577625 (epic 0590 child C3, D202, 2026-11-19)

**Both builds known** [C, code and hermetic tests, 2026-11-19]. `tools/harness/exe_build.py` reads the list of known
builds from `tools/exebuild/builds.py` (the Python twin of `exebuild/include/vp_exe_build.h`): a DX11 EXE is build
4390913 (stamp `0x546CD0A8`, SizeOfImage `0x06519000`) or build 26577625 (stamp `0x6AF3837D`, SizeOfImage
`0x063A2000`); the key is stamp **and** SizeOfImage. The file size is kept as information (`KNOWN_SIZES`: 16,982,584
and 16,982,840 for 4390913, 15,466,264 for 26577625): a known build with a size not seen yet passes with
`size_known: false` and a note to add it to `builds.py`. Only a file whose optional header cannot be read (a truncated
test header) falls back to the 0583 rule (stamp plus a known size). `check_install` returns `build` ("4390913",
"26577625", or "unknown", the last only with `--allow-unknown-exe-build`) and takes `want=` to insist on one build;
`golden_install(build)` / `golden_manifest(build)` name each build's golden copy (`C5_GOLDEN_INSTALL_<build>` overrides;
`C5_GOLDEN_INSTALL` still for 4390913), and `GOLDEN_MANIFEST_SHA256` pins both manifests (4390913 `0717bb05...cfc34`,
26577625 `dcfc481c...9d3f`, hashed 2026-11-19 22:10 EST). `tools/harness/instance.py` `GAME_PE_TIMESTAMPS` (a renamed
EXE is still the game) has the DX11, DX9 and Tablet stamps of both builds (B.1 of
`02-exe-internals/new-exe-build-26577625.md`: new 0x6AF3837D, 0x6AF3818F, 0x6AF385BA; old 0x546CD0A8, 0x546CCB59,
0x546CD5F8). The testbed's `steam_launch.py` and `run_ab.py` use the same check unchanged, so a test machine's Steam
install of either build now passes it (a launch through Steam stays refused without `--allow-steam-update`).

**What differs from the old build's instances** [C, the two golden manifests, read 2026-11-19 22:10 EST]. The two
golden copies (13,379 files each) differ in exactly 11 files, none added or removed: the three EXEs, `lua51_Win32.dll`,
`Launcher.exe`, the database and localization libraries, the three stock game-core DLLs and
`Resource\DX9\ConquestDeluxeTextures.fpk` (B.1's list). `pure_vp`'s copies of 7 of them are byte-identical to the old
golden copy's; the 3 EXEs differ only per copy (CEG; same section table and `.text`); `pure_vp`'s live
`lua51_Win32.dll` is the 5.1.5 rebuild, but the instance gets the `.bak` (the stock `f58ceffe...`), which equals the old
golden copy's. `steam_api.dll` (Steam's own `b0fd893e...`) is the same in both builds, so the gbe `steam_api.dll` and
`steam_settings\` of `pure_vp` serve the new EXE unchanged. Without CEG every copy of the new EXE is the same file
(`a7b4f520...` in the golden copy and in both instances).

**How they are made** [C, code; L, run 2026-11-19]: `provision.py create NAME --exe-build 26577625` copies `pure_vp` as
always and takes every file whose SHA-256 differs between the two golden manifests from the new build's golden copy
instead (`plan_build_overlay`), all in one copy pass. Checked before any write: both manifests against their pinned
hashes; the golden copy's EXE is build 26577625 (`want=`); every overlay source file against the target manifest; and
the base side: each `pure_vp` file the overlay replaces must be the old golden copy's file (same SHA-256) or, for a PE
that Steam made unique per copy, the same build (`same_build`: section table and `.text`); otherwise the create is
refused ("would mix builds"). After the copy the overlay files are checked again by hash and the EXE must be build
26577625. `instance.json` gets `exe_build` (build, stamp, SizeOfImage, size, label; every new instance has it, of
either build) and `build_overlay` (both manifests with their hashes, each replaced file with its new hash, the replaced
source and hash, and how the base side was checked). The stock Lua check accepts the build's own Lua
(`STOCK_LUA_SHA256_BY_BUILD`: 26577625 `b310ee97...a212`, the same body with 2K's signature); `verify` checks the Lua of
the instance's build and that the EXE is still the build `exe_build` records. `set-exe` records `exe_build` again.
`--build-source DIR` names another copy of the build (manifest `<DIR>.sha256.json` beside it). Without `--exe-build`
(or with the source's own build) a create is a plain copy as before.

**The instances** [L, 2026-11-19 22:35-22:53 EST, workstation, root `F:\c5-instances`]:

```
python -I perf/quietlock/quietlock.py run --who w3-0590 --what "provision ..." -- \
  python -I -X utf8 tools/instances/provision.py --root F:\c5-instances create c5h-new-a --exe-build 26577625 \
    --gbe-dir %USERPROFILE%\c5-instances\_tools\gbe-release-x32 --like c5h-d \
    --dll %USERPROFILE%\c5-i0\dlls\i0ship-e96457fd-ia\CvGameCore_Expansion2.dll
```

| | c5h-new-a | c5h-new-b |
|---|---|---|
| Use | short runs (0591, 0592 smokes) | long runs (B.3's 50-turn arms, soaks) |
| Files / bytes | 13,310 install + 2,148 user, 8.21 GB | same |
| Create time | 131.2 s (install 116.1 s; 22:48-22:51) | 138.2 s (install 125.2 s; 23:22-23:25, made again) |
| gbe port | 47595 | 47596 |
| DLL | `i0ship-e96457fd-ia` `3eaa3290...` (pin batch 4) | same |
| gbe | `_tools\gbe-release-x32` (1112) with 1113's import wrapper, as c5h-d | same |
| Saves | `C5H-VP8P-HUGE_0350` (from `--like c5h-d`) | same |
| EXE | `a7b4f520...`, 15,466,264 B (the golden copy's) | same |
| `verify --full` | ok, no error, no warning (22:53) | ok, no error, no warning (23:26) |

Both are profile `mods548` and `exe_build.build` 26577625; `pool.py` gives them out only to a claim that asks for that
build (below).

**The installed patch in the owner's tree** [C, files listed 2026-11-19 22:45 EST]: since 2026-11-15 the owner's own
`MODS\(1) Community Patch\` holds the 64-bit patch as the installer lays it (`CvGameCore_Expansion2.dll` is the 1,097,216
B proxy, `CvGameCore_Expansion2_x86.dll`, `VP64\`, `vp64helper.exe`, `vpproxy.ini`; `VP64Options.ini` and `VP64\` at the
user folder's top). `create` copied the mod folder whole, so an instance made from the owner's tree since then carried the
layout files (`--dll` replaced only the DLL itself), and `verify` then fails with "proxy layout file present but the
instance is not proxied". `create` now leaves the top-level `PATCH_LAYOUT_IN_MOD` entries of a source mod folder out
(10 files for the owner's tree), records them in `instance.json` `excluded_user`, and refuses a create without `--dll` from
such a tree (its mod DLL is the proxy). The user folder's top-level `VP64\` and `VP64Options.ini` were never copied
(only `USER_CONFIGS` are). Instances made before 2026-11-15 are not affected. The first c5h-new-b (22:41) was made
before the fix and was claimed by another session for ticket 0592 at 22:42, before it could be made again (its three
layout files are inert while its DLL is the i0ship build: the proxy reads `vpproxy.ini`, nothing else does); once that
session released it, it was made again (23:22-23:26) without them. Both instances now hold the same files by hash
apart from their names, ports and per-instance settings. The DLL is batch 4's instance-aware build as the brief named (COMMON rule 18 then); `deps/i0.lock` names
batch 5 (`5d18d3d6c`) since 2026-11-19, whose `-ia` build is in `%USERPROFILE%\c5-i0\dlls\i0ship-5d18d3d6-ia`: a
`set-dll` moves an instance to it.

**Claiming by build** (`tools/harness/pool.py`, 0590 C3) [C, hermetic tests]: `pool.py claim --exe-build 26577625`
takes a free instance whose `instance.json` records that build; a claim without `--exe-build` and without `--name`
takes only build 4390913 (`DEFAULT_EXE_BUILD`; an instance with no `exe_build` recorded, every instance made before
0590, counts as 4390913), so a run that asks for nothing never gets a new-build instance by chance. `--name` takes any
build unless `--exe-build` is given too (then a different build is refused). The claim record and `pool.py list` show
the build (in brackets when assumed). Example: `C5_POOL_OWNER=w3-0590 python -I tools/harness/pool.py claim --owner
w3-0590 --exe-build 26577625 --ticket 0590`.

**Live smoke on c5h-new-a** [L, 2026-11-19 22:57-23:04 EST, workstation; DLL `i0ship-e96457fd-ia`, no 64-bit patch;
Steam client running (pid 23152), as always on this machine]. A new Duel game (Continents, 1 AI, no city-states,
`maxturns=8`) with mods CP, VP, the EUI compatibility files and a copy of the owner's `autoplay (v 1)` mod (its one Lua file
calls `Events.LoadScreenClose()`, `Game.SetPausePlayer(-1)`, `Game.SetAIAutoPlay(1,-1)` at `SequenceGameInitComplete`,
so the AI plays with no Lua channel; the copy was put into the claimed instance's `MODS\` for the run and taken out by
hash afterwards). Steps and results:

| Step | Result on build 26577625 |
|---|---|
| `frontend.prepare(..., new_game=...)` (patched MainMenu.lua, mod activation, new game) | works: 4 mods activated, game set up and started without a click |
| `cli.py -i c5h-new-a launch` (cloak, offscreen; shim, guard, dialog watcher) | exit 0; the EXE ran from the instance as one process (pid 45800 from launch to stop: no Steam relaunch, no exit, no DRM wait), gbe's `steam_api.dll` behind 1113's wrapper |
| AI turns | 8 turns, turn 0 to 8, one `AutoSave_Post_*` per turn in `ModdedSaves\single\auto` 23:00:03-23:00:20 (about 2 s a turn), then the time-limit game-over screen ("Napoleon has Won!") |
| `cli.py shot --what ... --issue 0590` | exit 0 (Visual Checks `vc-20261119-230027-workstation-461a`, diagnostic) |
| `lua.py --instance c5h-new-a "return Game.GetGameTurn()"` | **exit 1**: `VP_LUAEXEC runner failed: =VP_LUAEXEC:127: attempt to call upvalue 'loadstring' (a nil value)` |
| `frontend.py ingame` | **exit 1** (`[error] in_game probe failed:` the same runner error), not 2 (silent) |
| `turns.py ready` | **exit 1** "Lua channel silent" |
| `turns.py end` / `wait`, `frontend.dismiss_load_screen`, `ensure_in_game`, `diplo.py`, every channel call | fail the same way (not run one by one; they all go through `lua.run_lua`) [I, code] |
| `cli.py -i c5h-new-a stop` | **exit 2** "stop needs a human-turn check": the DLL carries the channel names, so 0176's automatic switch to `stop_nolua` does not happen |
| `stop_nolua.py -i c5h-new-a --settle 60` | exit 0: save folders still 61 s, then terminate after the gates (no in-game exit without the channel) |
| front end after `stop_nolua` | **left patched**: `stop_nolua.py` does not call the 0575 restore; `frontend.py restore` put `MainMenu.lua` back by the manifest's hash |
| `provision.py verify c5h-new-a` | ok; one warning, the `stats.db-journal` left by the terminate (cleaned with `sqlite_journal.py clean`, backup in `F:\c5-64bit-work\0590\c3\journal-backup`) |

So with DLL `i0ship-e96457fd-ia` on build 26577625 every harness step that asks the game through the Lua channel fails
with the runner's `loadstring` error, and everything that works from files, windows and the front end works: launch,
the front end's mod activation and new game or save load, shots, the dialog watcher, autoplay by mod, `stop_nolua`. *Corrected 2026-11-20 (ticket 4968, below): with the
`-ia` DLL `+ia4968` (`ff0f6e86...`) the channel works on build 26577625; the autoplay-mod and `stop_nolua.py` way
below is needed only with an older `-ia` DLL.* (Replaced: "A run until 0591 lands: an autoplay mod or a turn-limited
new game for turns, `stop_nolua.py` to stop, `frontend.py restore` after it.") `Lua.log` showed no error from EUI's `loadstring` include (`02-exe-internals/new-exe-build-26577625.md` B.1):
in the mods form CP's own `InfoTooltipInclude.lua` replaces EUI's ("This is the modded InfoTooltipInclude from Community
Patch" in every UI context), so that include does not run; only VP's usual `VPUI_loader.lua` error appeared. c5h-new-b
was not smoked by C3: 0592's session ran its D205 live checks on its first copy (22:42-23:22), and the copy made again
afterwards was handed straight back for that work.

**The channel on both builds with the `-ia` DLL `+ia4968` (ticket 4968)** [C, code and release tests; L, 2026-11-20,
workstation]. Build 26577625 sets `_G.loadstring` to nil, so the instruments' Lua runner may not call it. Fork branch
`ia-luaexec-no-loadstring` (`d45303627`, ticket 0591) passes a C compiler (`LuaExecCompile` over `luaL_loadbuffer`)
to the runner as its third argument (`local _, _, compileC = ...`). It is taken without any new fork branch, as the
release stage's `IA_PATCHES` row `4968` in `tools/release/stages/build.py`: base `i0-ship-batch5` `5d18d3d6c` (the
`deps/i0.lock` pin), patch `tools/d10/evidence/4968/ia-5d18d3d6c-4968.patch` (SHA-256 `0882f9a5...3304`, 41 files),
which is 0463's instruments patch plus the `440878983..d45303627` delta as one diff against the pin (made by
`s01_ia_patch.sh`; both apply cleanly, and the result applies to a `git archive` of the pin). `pick_ia_patch` gives an
i0 commit that contains the pin this row, and an older i0 0463's row. The release tests check this (`-k ia_patch`, Ran
10, OK, 2026-11-20 01:58 EST). The DLL was built the way the release stage builds it (`s02_build_ia.py`: build.py's
own `pick_ia_patch` and `write_version_stub`, then `build_vp_clang.py --config release` under quietlock, tree in
`C:/c5-64bit-work/4968/build`, 01:41-01:49 EST): version stub `Release-5.4.8-50-g5d18d3d6c+ia4968 Copy`, 17,482,752
B, SHA-256 `ff0f6e86a56ca1d2927414a3a79eb4ecd02af8fd16f6474c74fc27503bbcf7ec`, kept in
`C:\c5-64bit-work\4968\dll-ia4968\`.

Live, a new Tiny game each time (`tools/d10/evidence/4968/s03_prepare.py`: cp, vp, eui, Continents, 3 AIs), the DLL
put in with `provision.py set-dll --dll` and taken out with `set-dll --previous`, checked by hash:

| Step | c5h-new-a, build 26577625 (02:00-02:06 EST) | c5h-a, build 4390913 (02:11-02:16 EST) |
|---|---|---|
| `cli.py -i <inst> launch` | exit 0 (pid 48416, start-up watch ok) | exit 0 (pid 4932, start-up watch ok) |
| `lua.py --instance <inst> "return 6*7, _VERSION, type(loadstring), Game.GetGameTurn()"` | exit 0: `42`, `"Lua 5.1"`, `"nil"`, `0` | exit 0: `42`, `"Lua 5.1"`, `"function"`, `0` |
| `frontend.py -i <inst> dismiss`, then `ingame` | exit 0, exit 0 (`IN GAME [in_game]`) | exit 0, exit 0 |
| `turns.py ready`, then 4 x `turns.py end` + `turns.py wait <turn>` | all exit 0, turns 2 to 6, 3-4 s a turn | all exit 0, turns 0 to 4, 3 s a turn |
| `cli.py -i <inst> stop --human-turn-hook tools.harness.turns:stop_readiness` | exit 0 (human turn, newest autosave settled 11.5 s; front end restored by hash, 0575) | exit 0 (the same) |
| DLL back, `provision.py verify` | `3eaa3290...` again, verify ok, no warning | `3c2f47da...` again, verify ok (one warning, a `stats.db-journal` of 2026-11-14, older than this run) |

The stop ends with a terminate after the human-turn and save gates ("no exit hook registered"): the CLI has no default
in-game exit hook (`--exit-hook MODULE:FUNCTION`), on either build. Each game step took a few seconds. Bare
`turns.py end` calls in a row, without `wait`, while turn 0 was still being processed, ended turns 0 and 1 together
("0 forced 3"). Use `end` and then `wait <turn>`.

**The new-build instances' pinned test DLL is `+ia4968` since 2026-11-20 02:28 EST** (MC's decision under COMMON rule
18, on ticket 4968's question; set by lead-0590) [L, `provision.py set-dll`, then `verify --quick`: ok, no error, no
warning, on both]. c5h-new-a and c5h-new-b carry `CvGameCore_Expansion2.dll` `ff0f6e86...f7ec` (version stub
`Release-5.4.8-50-g5d18d3d6c+ia4968 Copy`, the `-ia` build of the i0 pin `5d18d3d6c` with the runner that needs no
`loadstring`); the durable copy is `%USERPROFILE%\c5-i0\dlls\i0ship-5d18d3d6-ia4968\` (the build's own folder
`C:\c5-64bit-work\4968\dll-ia4968\` is scratch). It replaced `i0ship-e96457fd-ia` (`3eaa3290...`, the row "DLL" of
the table under "The instances" above describes the instances as they were made; `set-dll --previous` gives that DLL
back). The old-build instances keep their DLL, and `deps/*.lock` are unchanged. A run that puts another DLL into one
of the two restores `ff0f6e86...` by hash at its end.

**An extra variable for the EXE (`cli.py launch --env`)** [C, code; hermetic test]. The EXE's environment is the
caller's (`winapi.build_env` starts from `os.environ`), with the instance's variables (`Instance.env`), the preload
and frame-cap variables laid over it. So `VP64_DEV_READY_BUILD=26577625 python -I tools/harness/cli.py -i NAME launch`
already reaches the EXE, but nothing records it. Since 4968:

```
python -I tools/harness/cli.py -i c5h-new-a launch --env VP64_DEV_READY_BUILD=26577625
```

`--env NAME=VALUE` can be repeated. It is passed as `launch()`'s `extra_env`, so it is recorded in the state file's
`"env"` and in harness.log's `launch_start` line. It is refused (exit 2) for the instance's own variables (`C5_*` of the
contract, `CIVBG_*`, `VP_INSTANCE`, `VP_LUAEXEC`, the frame cap and preload variables), and a malformed pair is a usage
error. Test: `tools/harness/tests/test_core_launch.py::test_cli_launch_env_reaches_the_exe_and_the_state_file`, which
checks the variable in the stand-in's process environment and in the state file. A worktree whose native harness files
are not built needs `C5_HARNESS_BIN` pointed at a built `bin` folder: the launch fails with "bg_helper32.exe is
missing" (it did here, 01:58; the failed launch restored the prepared front end as 0575 says).

Not done: the new DLL is not the instances' pinned test DLL. c5h-new-a/b still hold `3eaa3290...` (batch 4), so a run
on them puts `ff0f6e86...` in with `set-dll` and takes it out afterwards. Making it the pin is COMMON rule 18, the MC's
decision.

### 3.9 gbe built from source (1112)

Issue 1112, 2026-11-10 03:06-03:41 EST, decision D16. Build record, exact steps and tool list:
`tools/instances/gbe-patches/README.md`. No game was launched.

Source and result [C]:
- Upstream: gbe_fork, https://github.com/Detanup01/gbe_fork, tag `release-2026_03_15`, commit
  `e5df46e1d691a4e2538aade464b45e35a14810ac` (2026-03-15 12:18:37 UTC). pure_vp's debug DLLs (3.3) carry
  TimeDateStamps 12:20:52 (`steamclient.dll`) and 12:24:11 UTC (`steam_api.dll`) the same day [I: built by CI from
  this commit; the version resource 08.33.09.23 is the same in the source's `resources/win/api/32/resources.rc`].
- Built with VS 2022 (MSVC 14.43.34808), VS's bundled CMake 3.30.5, official premake 5.0.0-beta2 (byte-identical to the
  copy gbe vendors), Windows `tar.exe`; protobuf v31.1 fetches abseil-cpp 20250127.0 from its official repo at
  configure time. gbe's vendored 7za/cmake/openssl/signtool/dos-stub binaries were not run. 32-bit dependencies 14 min,
  emulator 46 s.
- Output: `%USERPROFILE%\c5-instances\_tools\gbe-release-x32\` `steam_api.dll` (release `api_experimental`, Win32,
  16,379,904 B, SHA-256 `665bb50bd3f5b6d466abfe1c2e2064634e85c30c18abb7376f40e6ae56c4b707`) and `steamclient.dll`
  (`steamclient_experimental_stub`, 89,088 B, `5ff5222d5d81b1deb936dc2611a0c90cddf37a42fe2c92df8eacc9b231ce988b`).
  **No patch**: the release configuration defines `EMU_RELEASE_BUILD`, which removes `PRINT_DEBUG` and the `dbg_logger`
  object, so the DLL contains no `STEAM_LOG_` string and writes no log. 3.3's "it cannot be turned off by
  configuration" stays true for the debug build; with this build the 5.7 GB of logs (3.1) do not occur.
- Same interface as pure_vp's: x86 (machine 0x14C), the same 1,265 exports, the same 14 imported DLLs, all 25
  `steam_api` functions the DX11/DX9/Tablet EXEs and the database DLL import. The CP DLL imports nothing from
  `steam_api.dll`. The stub `steamclient.dll` exports only `CreateInterface` and imports only KERNEL32, as installed.

Load probe [L, x86 test EXE, no game; throwaway instance `c5h-gbetest` from `provision.py create --gbe-dir`,
destroyed afterwards]: `SteamAPI_Init` = 1; steam id 76561199869884579, persona `gse orca`, language `english`, app id
8930, `BLoggedOn` = 0 (`offline=1`). Editing `install\steam_settings\configs.user.ini` changed persona and language
(the instance's `steam_settings\` is read). No TCP or UDP endpoint owned by the process. Saves went to `<inst>\gse\`
both with `GseSavePath` and with `local_save_path` alone (`gse\8930\playtime.txt`); `%APPDATA%\GSE Saves\8930\playtime.txt`
kept its 2026-10-14 20:15 mtime; no `STEAM_LOG_*.log`. The owner's tree and both installs unchanged (`provision.py diff`).
This resolves 3.8's open points for this build: an absolute `crash_printer_location` is used as is
(`common_helpers::to_absolute` returns absolute paths unchanged) and `GseSavePath` with a trailing `\` is fine
(`parse_local_save` appends a separator only when missing) [C, source].

What else in gbe could hold up two concurrent instances [C, source at e5df46e, `dll/`, `helpers/`, `crash_printer/`,
`overlay_experimental/`]:
- **Listen port: no conflict.** With `disable_networking=1` the `Networking` constructor returns before creating any
  socket (`dll/network.cpp` ~line 747), so `listen_port` is never bound [C; L: no endpoint in the probe]. With
  networking on, gbe tries `listen_port + i` for i = 0..999 for both UDP and TCP until a bind succeeds, so a fixed
  port would not collide either. 1106's unique ports are harmless but not needed.
- **Named kernel objects and pipes: none.** gbe's own source has no `CreateMutex`/`CreateEvent`/`CreateFileMapping`
  with a name, no `Local\`/`Global\` names and no named pipes (the only `CreateFileMappingW` is Detours' unnamed image
  mapping). Its locks are process-local `std::recursive_mutex`. (Matches 4.7's string scan of the debug DLL.)
- **Global settings folder.** Only read when no local save is configured: with `GseSavePath` or `local_save_path`
  set, gbe skips `%APPDATA%\GSE Saves\settings` entirely ("global settings will be ignored since local save is being
  used", `dll/settings_parser.cpp`). Defaults it must invent (`account_name`, `account_steamid`, `language`,
  `ip_country`) are written into `<save path>\settings\configs.user.ini`, i.e. inside the instance. Observed:
  `gse\settings\configs.user.ini` (148 B) with `ip_country=US`, because 1106's ini does not set `ip_country`
  (harmless; add `ip_country=US` to `[user::general]` to stop the write). `get_user_appdata_path()` calls
  `SHGetFolderPathW(CSIDL_APPDATA)` without the create flag, so it only computes a path.
- **Files outside the app path and save path:** `crash_printer_location` and `steam_game_stats_reports_dir` (1106
  points both into `<inst>\run\gbe\`); nothing else. Everything gbe reads (`steam_settings\`, `steam_appid.txt`,
  `steam_interfaces.txt`, `load_dlls\`, a `crack` DLL) is under the program path (`GseAppPath` or the DLL's folder),
  which is per instance. No registry writes in the DLL (the HKCU/HKLM `Valve\Steam` writes are in the separate
  ColdClientLoader tool, unused here).
- **In-process hooks of the experimental build** (pure_vp's flavour too): with `disable_lan_only=0` it Detours-hooks
  `sendto`, `connect`, `WSAConnect` and `WinHttpConnect` in the game process (LAN-only filter); the overlay hooks are
  not installed unless `enable_experimental_overlay=1` (default off, `dll/dll/settings.h` `disable_overlay = true`).
  None of this touches the CEG `STEAM_DRM_IPC` objects (4.6); no change was made to the emulator (CONTRACT section 7).
  `crash_printer_location` installs a `SetUnhandledExceptionFilter` that chains to the previous filter.

Tooling note: `provision.py` refuses a `--gbe-dir` under the instances root, which includes `_tools\`; until 1106
exempts `_tools\`, copy the two DLLs to a folder outside `%USERPROFILE%\c5-instances` before `create --gbe-dir`.

## 4. Per-instance isolation: what the processes read and write (2026-11-10)

Issue 1102, static analysis only (no game launched). Method: read-only headless Ghidra 12.1.4 (`analyzeHeadless
-readOnly -noanalysis`, scripts `PathsDump.java` and `DecompList.java`) over a scratch **copy** of project `civ5`
(the Steam DX11 EXE, SHA-256 `b2ce96ae...`, build 4390913); PE import and string scans in Python of the `pure_vp`
modules; DLL source by `git show`/`git grep` at tag `Release-5.4.8` and branch `claude/shell-gates-d8e422`
(c60138b82). VAs at ImageBase 0x400000. `[C]` decompile/dump/source, `[I]` inferred, `[L]` seen on disk.

### 4.1 Which EXE: the code is the same in both installs

Analysed: the Steam copy. The `pure_vp` copy differs only in per-user CEG data, headers and signature; `.text` and
the import table are byte-identical (3.2, confirmed by an independent byte diff in this task) [C], so every code VA
and IAT slot in this section holds for both. Three of the per-copy `.rdata` values (the 16-byte GUIDs at 0x9EBF7C,
0xA0C584, 0xAA41B4) are the CLSIDs the EXE looks up in HKCR (4.4).

### 4.2 The user folder (`My Games\Sid Meier's Civilization 5`)

[C] The EXE derives it with **SHELL32 `SHGetSpecialFolderPathW` only** (IAT slot 0x009D6910). It does not import
`SHGetFolderPathA/W` or `SHGetKnownFolderPath`, reads no environment variable for it and no registry value.

| Call site | Function | Call | Cached in |
|---|---|---|---|
| 0x0081B1D9 | FUN_0081B1A0 (Documents user dir) | `SHGetSpecialFolderPathW(NULL, buf, CSIDL_PERSONAL = 5, fCreate = TRUE)` | FString ptr at 0x00CB7898 |
| 0x0081B2F3 | FUN_0081B2C0 (local user dir) | `SHGetSpecialFolderPathW(NULL, buf, CSIDL_LOCAL_APPDATA = 0x1C, TRUE)` | 0x00CB789C |

- Each getter appends `\` if missing, then `L"My Games\\"` (0xB68288 / 0xB6829C), then the app folder name from
  0x00CB7890, then creates the path (`FUN_00842400`, mkdir -p). The app folder name is set once by the Civ5App
  constructor FUN_0051F050 at 0x0051F286: `FUN_0081B020(L"Sid Meier's Civilization 5")`.
- Computed once: only while the cached global is empty. The only writers of 0xCB7890/0xCB7898/0xCB789C besides
  the getters are clear-on-exit routines (FUN_006B2440, FUN_009D33B0, FUN_009D3440, FUN_009D3480). **No
  command-line switch or ini key moves the user folder** [C].
- Order at startup: `FUN_0081B3D0` (app init) -> `FUN_0082A5E0`: local-appdata getter, then the Documents getter,
  each tested for writability with a temp file (`GetTempFileNameW(dir, L"fxs")`, FUN_00843990), then `Config.ini`
  (name from 0xCB78B0), `Logs`, `Saves` are registered under the user folder.
- `%LOCALAPPDATA%\My Games\Sid Meier's Civilization 5` exists on this machine and is empty (mtime 2026-11-10 02:03)
  [L]; all of the owner's files are under Documents. The local one is still created by every start.
- The DLL gets the cache folder **from the EXE**: `gDLL->GetCacheFolderPath()` (ICvDLLUtility) in
  `CvLuaSupport.cpp:752` (Lua channel request files), `MemoryDiagnostics.cpp:2615` (snapshot request),
  `SqliteLogger.cpp:101`, `CvGame.cpp:1287/11124/11156` (stats.db) [C, source]. DLL logs (`LOGFILEMGR`) go through
  the EXE's logger into `Logs\`. So redirecting the EXE's Documents folder moves cache, Lua channel, stats.db,
  logs, config, saves and MODS together.
- One DLL exception: `CvGame::CopyModDataToMPMP` (Modpack Maker only, `CvGame.cpp` ~14650 at 5.4.8) reads
  `HKCU\SOFTWARE\Microsoft\Windows\CurrentVersion\Explorer\Shell Folders` value `Personal` with
  `RegQueryValueExA` and builds `...\My Games\Sid Meier's Civilization 5\MODS` itself [C, source]. Not on the
  normal game path.
- Other SHELL32 folder calls: `SHGetFolderLocation` + `SHGetPathFromIDListA` (0x0067A4BF-0x0067A508,
  FUN_00679EA0) only print folder names into a system-information text (gamver/winver/DXDIAG); `ShellExecuteA`
  (FUN_0081B600) opens `%SystemRoot%\ehome\ehshell.exe` (Media Center return). Neither affects paths.
- Environment variables the EXE reads: `INTEL_LIBITTNOTIFY32`, `INTEL_ITTNOTIFY_GROUPS`, `TBB_VERSION`,
  `OPENSSL_ALLOW_PROXY_CERTS`, OpenSSL config names. None affects paths [C].

### 4.3 Paths relative to the install folder (current directory)

[C] The EXE sets the current directory to its own folder before the DLL loads: Civ5App ctor FUN_0051F050 ->
FUN_005DDCA0(1) -> FUN_004D84B0 -> FUN_0081B6D0: `GetModuleFileNameA(NULL)` (0x0081B780), strip the file name,
`SetCurrentDirectoryA` (0x0081B7A3). Everything below is opened by a relative name, so it lands in the install
folder of the EXE that runs, i.e. per instance when each instance has its own install copy:

| File | Writer | How |
|---|---|---|
| `crashlogs\crashes.log`, `crashlogs\CvMiniDump_*.dmp`, `CreateDirectory("crashlogs")` | DLL `CvGlobals.cpp` | relative [C] |
| markers `crashlogs\nodumps.please`, `nopopups.please` | DLL | relative [C] |
| `crashlogs\allocTrace.enabled`/`.log`, `queueguard.capture`/`.log`/`queueguard-dropped-*.txt`, `shell.helper`, `shell.log` | DLL lineage instruments | relative [C] |
| `CvAssert.log` | DLL `CvGameCoreUtils.cpp:103`, `fopen("CvAssert.log","a")` | relative [C] |
| `RendererStats.csv` | EXE FUN_006D61A0 -> FUN_008CCA80("RendererStats.csv") | relative [C]; present in the `pure_vp` root [L] |
| Steam crash minidump `civilizationv_dx11_*_crash_*.mdmp` | EXE filter 0x0042DEA0 -> `SteamAPI_WriteMiniDump` | location chosen by steam_api (gbe: not checked) |
| `STEAM_LOG_<random>.log`, gbe `crash_printer_location` | gbe_fork debug `steam_api.dll` | program path (`GseAppPath` or the DLL's folder), see 3.3 |

Temp: `%TEMP%\fxs*.tmp` writability probes (unique names) and `%TEMP%\Civ5ModInstaller\` (Steam Workshop mod
unpacking, FUN_007CFD40) [C]; shared between instances but only the Workshop path writes a fixed folder.

### 4.4 Registry

[C] The EXE imports no `RegSetValue*`, `RegCreateKey*` or `RegDeleteKey*`: **it never writes the registry**, and the
string `Firaxis\Civilization5` does not occur in it (ASCII or UTF-16). `HKCU\Software\Firaxis\Civilization5`
(the working directory) belongs to `Launcher.exe` only (LauncherLog, section 3). Reads, all RegOpenKeyEx call sites:

| Function | Key | Purpose |
|---|---|---|
| FUN_00923B00 | HKLM `Software\Valve\Steam` `InstallPath` (also with KEY_WOW64_64KEY) | locate Steam, fallback system dir |
| FUN_009262A2 | HKLM `SOFTWARE\Classes\CLSID\{FE750200-B72E-11d9-829B-0050DA1A72D3}\ServerBinary` | loads that DLL, `GetInterface(5)` [I: an optional COM helper] |
| FUN_005D5730, FUN_004BAB60, FUN_0050D920 | HKCR `CLSID\{GUID}` existence, GUIDs from the per-copy CEG data (0x9EBF7C, 0xA0C584, 0xAA41B4) | [I] CEG checks; on failure they call a fail handler |

### 4.5 The single-instance check (corrects section 2)

[C] **The gate is a named mutex, not the window search.** FUN_00602380 (the EXE's main, called from
`___tmainCRTStartup` at 0x00926EB0):

1. `SteamAPI_RestartAppIfNecessary(8930)` (0x00602386); if true, return 0 (Steam relaunch).
2. FUN_0045A860: the CEG startup handshake (4.6); if it fails, return 0.
3. Unless the command line contains `-Nexus`: `CreateMutexA(NULL, FALSE, "Local\\Civ5App")` (0x006023CC, IAT slot
   0x009D6450, string 0xA11524); if `GetLastError() == ERROR_ALREADY_EXISTS` (0xB7), **return 0: the second process
   exits silently**, before the Civ5App object or any window exists. It does not raise the first instance.
4. Otherwise construct Civ5App (`g_Civ5App` 0xCC8E74) and run.

FUN_00842920 (`FindWindowA("WinClass_FXS", title)`, restore if iconic, `SetForegroundWindow`) is reached only from
FUN_0081B3D0, and only when its mutex-name argument (EAX) is non-empty. Its one caller chain FUN_0071E400 ->
FUN_0081DA60 passes **NULL** (`PUSH 0x0` at 0x0071E4E6 becomes EAX at 0x0081DA60), so in the DX11 startup path the
window search **never runs** [C, disassembly]. The window title it would use is `"Sid Meier's Civilization V
(DX11)"` (0xA0E8F4, FUN_0071E400). `-Nexus` switches the EXE into the Nexus tool mode (terrain editor, unit
viewer etc.; strings at 0xA779xx) and is not a usable bypass.

Consequence for two instances: rename `Local\Civ5App` per instance (hook `CreateMutexA` for that exact name, or
patch nothing and let a shim answer it); no `FindWindowA` isolation is needed for the DX11 EXE. The statement in
section 2 ("a second launch raises the first instance") and the same line in `run-and-drive-the-game.md` describe
code that the DX11 start-up does not reach.

### 4.6 Fixed kernel-object names

| Name | Type | Where | Per instance? |
|---|---|---|---|
| `Local\Civ5App` | mutex | EXE 0x006023CC | **No - blocks the second instance** (4.5) |
| `STEAM_DRM_IPC` | section 4 KB, NULL DACL | EXE CEG FUN_00517AD0 (also copies FUN_005167F0, FUN_0078F0C0) | No, session-wide by design |
| `STEAM_DIPC_CONSUME`, `SREAM_DIPC_PRODUCE` (sic) | semaphores | same | No |
| `STEAM_START_ACK_EVENT_<...>`, `STEAM_TERM_EVENT_<...>` | events | EXE FUN_0061F110 -> FUN_006936D0 | names include the PID [C] |
| `Local\SteamStart_SharedMemLock` / `Local\SteamStart_SharedMemFile` | opened event / section | EXE FUN_00925D30 (no direct caller found) | client side of Steam start |
| `Local\VPLuaExec`, `Local\VPLuaExecDone` | events | DLL lineage `CvLuaSupport.cpp:469-470,765-766` | No (1107) |
| `Local\VPMemSnapshot`, `Local\VPMemSnapshotDone` | events | DLL lineage `MemoryDiagnostics.cpp:2159-2160,2596-2597` | No (1107) |
| `TurnByTurn` | mutex, `OpenMutex` each tick | DLL `CvGame.cpp:1598` (lineage), also in Release-5.4.8 | No (1107) |
| `Local\VPShellSection`, `Local\VPShellWorld*` | section, shared textures | compositor `shell_shared.h:23-24` | section no; textures carry the server PID |
| `Local\VPTap_*`, `Local\VPTapB_*`, `Local\VPTapMatrix_%u` | shared handles | gate2 tap | sequence-numbered |
| `Local\CivBgGuard_<pid>` | section | focus guard | yes |

All other `CreateEventA`/`CreateSemaphoreA`/`CreateMutexA` calls in the EXE pass NULL names [C]. No named pipes and
no `Global\` names in the EXE, the DLL or the instruments. Other modules: 4.7.

**CEG handshake** [C logic, I consequences]: FUN_0045A860 creates the three `STEAM_*` objects, takes
`SREAM_DIPC_PRODUCE` (waits up to 30,001 ms), writes a request with its PID into `STEAM_DRM_IPC`, and waits up to
29,999 ms for the ack event (FUN_004D7BC0). Without an answer it fails and main returns 0. So something outside the
EXE answers at start even when `steam_api.dll` is gbe. **[L] 1108 (2026-11-10): the answering process is the real Steam
client**: `steam.exe` (pid 4972, running since 2026-11-08) held `STEAM_DRM_IPC`, `STEAM_DIPC_CONSUME` and
`SREAM_DIPC_PRODUCE` before the instance started and also held the game's `STEAM_TERM_EVENT_<pid>_*` during start-up;
the handshake took <= 0.2 s (run-and-drive-the-game.md section 12). Goldberg instances therefore need Steam running.
*Corrected 2026-11-10; replaced "[I: presumably the Steam client; 3.2 shows the check passed in `pure_vp` on
2026-04-26 and 2026-10-14, whether Steam was running then is not recorded]".* Two instances starting
together serialise on the PRODUCE semaphore (each may wait up to ~30 s). Do not rename these objects; stagger
instance starts by a few seconds. Whether the `pure_vp` EXE starts with the Steam client closed was not tried (Steam is never closed by this project).

### 4.7 gbe_fork and the other modules in the install

gbe's log and save paths, their overrides (`GseAppPath`, `GseSavePath`, `local_save_path`) and the d3d9 proxy are
in 3.3 and 3.4. Additions from this task:
- **Correction to 3.4** [C, PE import table of both DX11 copies, 2026-11-10]: the DX11 EXE **does import
  `d3d9.dll` statically** (`D3DPERF_BeginEvent`, `D3DPERF_EndEvent`; also listed in
  `research/sources/exe-world-subsystems/imports.txt`), so the proxy in the install folder is loaded at process start,
  before any game code (the loader resolves static imports from the application folder first). 3.4's conclusion
  (the DX11 EXE loads the proxy) holds; its reason ("loaded by name at run time") is the weaker one.
- gbe's `steam_api.dll` imports `SHGetFolderPathW` and `SHGetKnownFolderPath` (SHELL32), `RegOpenKeyExA` /
  `RegQueryValueEx*`, `CreateMutexW`, `CreateFileMappingW`, `CreateEventA/W`, `SetEnvironmentVariableW` [C, imports].
  No fixed kernel-object name strings with `Local\` or `Global\` were found in it [C, strings]; its mutexes and
  events are not checked further.
- Other modules' imports for path and naming APIs [C]: `Mss32.dll` (CreateMutexA/CreateEventA, no name strings),
  `CvGameDatabaseWin32Final Release.dll` (SQLite; `CreateMutexW` belongs to SQLite's WinCE lock path, temp files are
  `GetTempPath` + random names [I]), `lua51_Win32.dll` (`getenv`, `GetModuleFileNameA`: Lua's own `package.path`
  defaults), `CvLocalizationWin32Final Release.dll` (none). The VP 5.4.8 release DLL in the owner's MODS and the 5.4.7
  modpack DLL import `RegOpenKeyExA`/`RegQueryValueExA` (the Modpack Maker read in 4.2), `OpenMutexA`
  (`TurnByTurn`), `GetCurrentDirectoryA`, `CreateDirectoryA` and nothing folder-related from SHELL32.

### 4.8 Redirect list for the native shim (1105)

| # | API (module) | Match | Answer | Why |
|---|---|---|---|---|
| 1 | `SHGetSpecialFolderPathW` (SHELL32; EXE IAT 0x009D6910) | `CSIDL_PERSONAL` (5) | `C5_DOCUMENTS` | the EXE's user folder (4.2) |
| 2 | same | `CSIDL_LOCAL_APPDATA` (0x1C) | `<instance>\LocalAppData` | the EXE creates and tests `My Games\...` there |
| 3 | `CreateMutexA` (KERNEL32; EXE IAT 0x009D6450) | name `Local\Civ5App` | `Local\Civ5App_<id>` | single-instance gate (4.5) |
| 4 | `SHGetFolderPathW` (SHELL32, called by gbe) | `CSIDL_APPDATA` | `<instance>\AppData` | gbe `GSE Saves` (3.3); simpler: set env `GseSavePath` per instance and skip this hook |
| 5 | `RegQueryValueExA` (ADVAPI32, DLL) | HKCU `...\Explorer\Shell Folders` `Personal` | `C5_DOCUMENTS` | only Modpack Maker; optional |

Not needed: `FindWindowA` (unreached, 4.5), HKCU Firaxis (the EXE never touches it; launch the EXE directly, never
`Launcher.exe`), `crashlogs`/`CvAssert.log`/`RendererStats.csv` (follow the install copy). The four instrument
names go per instance through the DLL fork (1107, `VP_INSTANCE`), not the shim. Hook order: the shim must be in place
before Civ5App construction (the user-folder globals are filled once), which an injection at
`CREATE_SUSPENDED` (the focus guard's way) satisfies.

Not verified (static only): that the shim's answers are used live (1108), the CEG handshake without Steam, gbe's
exact log/save paths at run time, and the Steam minidump location.

## 5. Parallel Goldberg instances: the validated workflow (epic 1100, 2026-11-10)

Closes open question Q8. Everything below is [L] on the owner's machine on 2026-11-10 unless marked, with the 1107 DLL
(`instance-aware-instruments` @ 440878983, SHA-256 `5fdf3432...`), the import-time route with the 1118 guard, the
source-built release gbe (`665bb50b...`) and the save copy `C5H-VP8P-HUGE_0350`. Runs: 1108 (one instance, 03:36),
1114 (import route, 04:16-04:30), 1116 (diplomacy A/B, 06:21-06:37), 1109 (two at once, 06:55-07:00), 1110 (checklist
alone and two at once, 07:0x-07:20); epic 1300 ran three DLLs on `c5h-b` from the recipe (04:59-06:16). Details:
`run-and-drive-the-game.md` sections 10-12.5; tools: `tools/harness/README.md`, `tools/instances/README.md`,
`tools/harness/RECIPE-team-dll.md`.

**How to run N instances** (N = 2 shown; *2026-11-11, issue 9014: five instances exist under two roots, and a new
one is one command, `provision.py --root D:\c5-64bit-work\instances create NAME --like c5h-c`; see section 6. The old
line read "disk allows about one more".*):

1. `tools/instances/provision.py create NAME --profile mods548 --dll <instance-aware DLL> --gbe-dir
   %USERPROFILE%\c5-instances\_tools\gbe-release-x32` (8.2 GB copy of pure_vp without its gbe logs, dumps, `.lnk`
   files, the d3d9 proxy and the VP 5.4.7 DLC; stock Lua 5.1.4; per-instance gbe settings; the user folder seeded
   with copies of CP/VP/EUI from the owner's tree and the save copy), then `tools/harness/native/wrapper.py install
   <inst>\install` and `provision.py verify NAME`. *(2026-11-11, issue 9014: in this order `verify` used to fail with
   "gbe steam_api.dll is not the recorded build", because it hashed the wrapper. Fixed in `provision.py`; `create
   --like` now does all three steps.)*
2. Per launch: `tools/harness/frontend.py -i NAME prepare --mods cp,vp,eui --load <save>`, then
   `tools/harness/cli.py -i NAME launch` (import route by default; the guard and shim from the process' first DllMain;
   fail closed), then `frontend.py -i NAME ingame` / `dismiss`, then `diplo.py --instance NAME arm`.
3. Drive with `turns.py` / `checklist.py` / `lua.py --instance NAME`; stop with `cli.py -i NAME stop
   --human-turn-hook tools.harness.turns:stop_readiness`; then `frontend.py restore`, `provision.py clean-logs`,
   `verify`.

**What isolates the instances:**
- an install copy and a user-folder copy per instance (`<inst>\install` 7,980,486,847 B / 13,311 files; user copy
  259,892,647 B / 2,136 files);
- the shim redirects Documents (CSIDL 5 / FOLDERID_Documents) and LocalAppData (CSIDL 0x1C) per instance, so
  `My Games\...`, `cache\` (stats.db, the Lua request files), `Logs\` and the saves are the instance's own;
- the EXE's single-instance gate `Local\Civ5App` (4.5) is renamed per instance: `Local\Civ5App_c5h-a` and
  `Local\Civ5App_c5h-b` existed at once, no legacy name;
- the DLL's kernel objects get the suffix from `VP_INSTANCE` (`Local\VPLuaExec_<name>` etc.); a Lua value set in one
  game was invisible in the other, and `os.getenv("VP_INSTANCE")` answered each instance's own name;
- gbe's save path per instance (`GseSavePath` / `local_save_path`); the harness's state, logs and shim log per
  instance; every top-level window of each process cloaked at creation (1118).

**What they still share:**
- **the Steam client as the CEG responder**: `steam.exe` holds `STEAM_DRM_IPC`, `STEAM_DIPC_CONSUME` and
  `SREAM_DIPC_PRODUCE` and answers each instance's start-up handshake (<= 0.44 s); Steam must be running (it is never
  closed by this project; a start with Steam closed was not tried);
- **start serialisation**: the harness launch gate (`Local\C5HarnessLaunchGate`) holds a start until the previous
  instance's main window exists (waits of 22.46 s and 13.76 s measured); the handshakes never overlapped;
- the GPU (about 1.5-1.6 GB VRAM per instance in game; whole GPU 32-46 % with two in game), CPU, RAM and the system
  commit limit; the read-only `HKCU\Software\Firaxis\Civilization5` key (never written by the EXE, 4.4); gbe's
  `SHGetFolderPathW(CSIDL_APPDATA)` read of the owner's Roaming folder (nothing written there).

**Measured costs per instance:**
- start: window 18-38 s after the gate (import route 18-37 s, `--early` 38 s); launch to in game 2.6-2.9 min (mod
  activation 91-115 s, load 60-66 s); two loads in parallel took 60-66 s each, as alone (63 s);
- memory in game: private 2.14-2.29 GB, working set 1.79-2.01 GB, committed 2.43-2.62 GB, largest free block
  0.89-1.15 GB, unaffected by the other instance; system commit +~5 GB for two; commit headroom (limit 42.7-43.1 GB
  with other sessions' builds running) was the tightest resource, not physical RAM;
- AI turn 350->351 with the 1116 handler: 42.4-43.9 s with two running vs 37.2-37.9 s alone (+14-18 %, one sample
  each); without the handler a turn with AI leader screens took 336 s (90 s stall per screen);
- frame rate of each cloaked off-screen window (both idle at once): 113-155 fps;
- disk: 8.2 GB per instance; a few MB per run (the release gbe writes no logs).

**Safety record:** across all runs no window of an instance was ever visible to the owner or took the foreground after
1118 (guard counters `win_cloaked_watchdog` 0); before 1118 a DLL message box was visible (section 7 safety note of
`run-and-drive-the-game.md`); one forced stop happened in a driver's cleanup path before 1115 (same section). Every
run ended with an empty snapshot diff of the owner's tree, pure_vp and the Steam install, and identical registry exports.

**Not verified:** an instance running next to the owner's own Steam game (it was not running during the runs); three or
more instances; the CEG handshake under real contention (the gate never let two overlap); per-process GPU memory
(nvidia-smi pmon reports 0 under WDDM). *(2026-11-11, issue 9014: per-process GPU memory is read from the Windows
counter `GPU Process Memory(pid_N)\Dedicated Usage`, section 6.3; two instances were launched one after the other
through the gate again; three or more at once are still not run.)*

## 6. More instances, two roots, the pool, and what the machine carries (issue 9014, 2026-11-10/11)

Decision D38 (the owner, 2026-11-10): this machine may run at least four headed instances in the background; more machines
may join (see `remote-test-machines.md`). Branch `worktree-agent-a3f5eb8a186875a45`. Tools: `tools/instances/README.md`
("Several roots"), `tools/harness/pool.py`, `tools/harness/README.md` (last section).

### 6.1 The instances [L]

| Instance | Root (disk) | gbe port | Made | Create time | Proven (harness; huge save unless noted) | `verify` after the run |
|---|---|---|---|---|---|---|
| c5h-a, c5h-b | `%USERPROFILE%/c5-instances` (NVMe) | 47585, 47586 | 1106 | 98-120 s | sections 3.8, 5 | (not touched by 9014) |
| c5h-c | same | 47587 | 2026-11-10 23:31 | 55.4 s | 23:35-23:40, turns 350-352 | ok, 30.6 s |
| c5h-d | `D:/c5-64bit-work/instances` (SATA HDD, Seagate ST8000DM004) | 47588 | 23:43 | 151.6 s | 23:52-00:06, turns 350-353 | ok, 170.1 s |
| c5h-e | same | 47589 | 23:47 | 177.0 s | 23:58-00:06, a new Tiny game, turns 0-3 | ok, 168.5 s |

All three new ones follow c5h-b's recipe: profile `mods548`, the 1107 DLL `5fdf3432...`
(`Release-instance-aware-440878983`), the source-built release gbe `665bb50b...` behind 1113's import wrapper, the
save copy `C5H-VP8P-HUGE_0350`, no proxy. c5h-c was made with `create --dll ... --gbe-dir ... --save VP8P-HUGE_0350
--min-free-gb 25` (C had 38.6 GB free, 29.4 GB after), then `wrapper.py install`. c5h-d and c5h-e took one command
each: `provision.py --root D:/c5-64bit-work/instances create c5h-d --like c5h-c`. Every launch: guard self-test ok
(user32 hooks 17/17, win32u 0x10f, veto err 0), `startup_watch` ok (569 / 880 / 641 polls), Documents and LocalAppData
redirected into the instance, the window cloaked and never seen. Every stop went through `cli.py stop` with the
`turns:stop_readiness` hook after the autosave settled, with no orphans and `leftovers` clean, and the front end was
restored. No Defender quarantine on D: every file was present and every hash matched after creation and after the runs.

**Fixed along the way:** `verify` of an instance made with `create --gbe-dir` and wrapped afterwards (the documented
order of section 5) failed with "gbe steam_api.dll is not the recorded build", because it hashed the wrapper. It now
hashes the real DLL beside it [C, test `test_verify_gbe_build_behind_a_wrapper_installed_after_create`].

**The D drive is a hard disk** [L]: copying is 2.7-3.2x slower than on C (151-177 s against 55 s), a full `verify` is
5.5x slower (170 s against 31 s cold; 68.5 s right after creation, with the files cached), and the huge save's launch to
Lua answer took 4:48 against 2:43 on C (but see 6.3: the D figure overlaps a disturbance). Instances on D are for soaks
and long runs; load-time measurements belong on C.

**A new game through `frontend.prepare(new_game=...)`** (Python only; the CLI has no option): `map=Continents;
size=WORLDSIZE_TINY;ais=3;minors=4` generated the game and stopped at the "Begin Your Journey" screen. `frontend.py
dismiss` closed it and the game was in game at turn 0; `turns.py end` then forced turns as usual [L, c5h-e].

### 6.1a The DLL of the instances follows the pin (2026-11-11, common rule 18)

*Correction to 6.1:* c5h-c, c5h-d and c5h-e were created with the 1107 build `5fdf3432...`
(`Release-instance-aware-440878983`, Release-5.4.8 plus instruments), which lacks the ship branch's fixes and the D35
statistics pragmas. Rule 18 (the owner, 2026-11-11): every test runs on builds of the pins. `provision.py create` now takes
the instance-aware build of `deps/i0.lock`'s commit by default (`--dll pin`, see `tools/instances/README.md`);
`create --like` no longer inherits the other instance's DLL. The swap of the three instances and the provenance check
of the build are in 6.1b.

### 6.1b Provenance of `i0ship-ccfd6dd4-ia` and the swap (2026-11-11 00:30-00:50)

The build `<pins_dir>/dlls/i0ship-ccfd6dd4-ia/CvGameCore_Expansion2.dll` (17,465,344 B, SHA-256
`a395cad2206aa09e674855a9256db02a4ebd483e6908fdde93fc8ac6a9f0d712`, TimeDateStamp 0x6AF3F19C) was made by team 2800's
worker W3 (issue 2803) from `ccfd6dd4d` (deps/i0.lock) plus the instance-aware patch, as its `README.txt` says. Checked
by 9014:
- **The patch** [C]: `git diff --binary 7307aefdf 440878983 -- CvGameCoreDLL_Expansion2 build_vp_clang.py` in the
  fork clone gives SHA-256 `97488fbc866304313b5f0a3d129de8c2d5385c6030e8018e38d1501d56061cd9`, byte for byte the
  folder's `ia-7307aefdf-440878983.patch`. Both commits exist in the fork clone (`440878983ee8477d...`, "Per-instance
  kernel-object names for the instruments (VP_INSTANCE)").
- **An independent rebuild** [C]: `git archive ccfd6dd4d`, `git apply` of that patch (clean), the same version stub
  (`Release-5.4.8-13-gccfd6dd4d+ia440878983 Copy`), `build_vp_clang.py --config release` with clang-cl from
  `C:\Program Files\LLVM\bin`, in a folder whose path has the same length as W3's (`...\Temp\claude\e9014\w3\src`
  against `...\e2800\w3\src`; the DLL embeds the source path in 106 `__FILE__` strings). Result: the same size,
  17,465,344 B, every section the same virtual size. After mapping `e9014` to `e2800`, the only differing strings are
  `__DATE__`/`__TIME__` (`Nov 10 2026` `23:39:03` `23:39:16` against `Nov 11 2026` `00:38:26` `00:38:37`) and the PDB
  signature. All other byte differences follow from those strings: 87 bytes in `.rdata`, where the string pool moves
  by 9 bytes, 68 single bytes in `.text`, each +9 (pointers to those strings), and the header TimeDateStamp. A first
  rebuild under a shorter path (`D:/c5-64bit-work/9014`) differed in section sizes only by the path strings
  (`.rdata` -2,752 B = 106 x 26). Rebuilt files: SHA-256 `8dc1013a...3704` (same-length path) and `19c30c54...eb9f`;
  both build trees were deleted afterwards.
- **Contents** [C]: `PRAGMA synchronous = OFF` and `PRAGMA journal_mode = PERSIST` (D35) present, `RunId` (the
  instruments' stats column) present, Lua channel names instance-aware, the PE version `Release-5.4.8-13-gccfd6dd4d`.

So the build is `ccfd6dd4d` plus only the instance-aware change. Not checked here (as its README says): whether the
instruments leave game results equal to the plain pin build `i0ship-ccfd6dd4` (`3E1A7FC7...`).

**The swap** [C]: `provision.py set-dll <name> --dll pin` on c5h-c, c5h-d and c5h-e (2026-11-11 00:48). Each now has
`dll.sha256` `a395cad2...d712`, `names` instance-aware, and the earlier `5fdf3432...` kept in `run\dll-original\`
(`set-dll --original` restores it). c5h-a and c5h-b were not touched (both were running at the time). Full
`verify` afterwards: c5h-c ok (48.5 s), c5h-d ok (298.7 s, with c5h-c booting beside it), c5h-e ok (188.3 s).

**Boot proof on the pin DLL** [L, c5h-c, 2026-11-11 00:58-01:02, under the MC's pool claim `MC-verify-2800` with the
MC's permission]: guard self-test ok, `startup_watch` ok (745 polls), Lua answered 2:47 after launch (5 s polls), in
game after `dismiss`, `os.getenv("VP_INSTANCE")` = `c5h-c`, AI turn 350->351 in 36 s (harness wait; 42.6 s wall),
stopped through `cli.py stop` after the autosave settled, no orphans, front end restored, `verify` ok at 01:06.
c5h-d and c5h-e were not booted on the new DLL (hash-verified only).

### 6.1c The pin of upstream fix batch 1: `i0ship-bbfacca1-ia` (one-off 9029, 2026-11-11)

Since 2026-11-11 14:07 EST the pin's instance-aware build is `<pins_dir>/dlls/i0ship-bbfacca1-ia/
CvGameCore_Expansion2.dll` (17,476,608 B, SHA-256 `e589306e...5c54`, version `Release-5.4.8-30-gbbfacca1a+ia440878983
Copy`): one-off 9020's `i0next-bbfacca1-ia`, made by the route of 6.1b (git archive of bbfacca1a, `git apply` of the
same `ia-7307aefdf-440878983.patch`, clean, version stub), copied unchanged into the folder name `provision.pinned_dll`
derives from `deps/i0.lock` (`i0ship-<commit[:8]>-ia`) [C, hash]. `provision.pinned_dll()` resolves to it.
Re-provisioned with `provision.py set-dll <name> --dll <that file>` [C, full `verify` ok]: **c5h-a** (14:23; before:
the 1107 build `5fdf3432...`, not the old pin) and **c5h-c** (14:24, unclaimed, no process; claimed by 9029 for the
change and released). **Not re-provisioned:** c5h-b (claimed by one-off 9034, running on the old pin), c5h-d and c5h-e
(epic 3200's test DLLs of `i2-shell-test`/`i2-white-mask`, both based on ccfd6dd4d; they move when that epic rebases
onto bbfacca1a). Quiet testbed: see `quiet-testbed.md`.

### 6.1d The instruments patch on upstream master: `ia-8be73b6f5-440878983.patch` (queue ticket 3515, 2026-11-14)

The instruments patch for the next release's i0 branch, made because 9045's `ia-ee893bc18-440878983.patch` stops the
release driver's `-ia` step on master (`release-driver.md` W7.2 row 11). Evidence and scripts:
`tools/d10/evidence/3515/` (the patch, `scripts/`, `acceptance.txt`).

- **Base** [C]: the dry run's `i0-release-5.4.9-ce4e6aae0` 6038f0746 lived only in a deleted scratch clone. The fork's
  pushed `i0-release-master-ba8aafdc-r2` **8be73b6f5** has the same tree **eb8ef8e73** (describe
  `Release-5.4.8-169-g8be73b6f5`), so the patch applies to either commit. Clone: `git clone --shared` of the owner's fork clone
  into `C:/c5-64bit-work/3515/fork` (`scripts/s03_clone.sh`).
- **Line endings** [C]: the committed copy of 9045's patch is CRLF in a Windows checkout (index LF, `core.autocrlf=true`,
  no `.gitattributes` rule for `*.patch`); `git apply -3` then falls back to direct application in every file. Normalised
  to LF, the three-way apply gives exactly the dry run's **6 conflicts**: `CvGame.cpp`, `CvGameCoreDLL.cpp`,
  `CvGlobals.cpp`, `Lua/CvLuaSupport.cpp`, `Lua/CvLuaSupport.h`, `SqliteLoggerRegistrations.h`; the other 35 files apply.
- **Resolution** (`scripts/s08_ia_resolve.py`, both sides kept every time) [C]:
  - `CvGame::update()`: master's `g_ulTContainerCacheThread` line, then the instruments' pollers, then 0116's
    `m_iCacheStamp++` after them (9045's order).
  - `CvGameCoreDLL.cpp`: master's `Lua/CvLuaSupport.h` include plus the instruments' five includes.
  - `CvGlobals.cpp` `CustomFilter`: master's crash-log-folder line plus the queue-guard line (format string and
    arguments merged without conflict).
  - `SqliteLoggerRegistrations.h`: master's file (new `DiplomacyLog` and others) with the instruments' 1,003 lines of
    tables appended, as at 5.4.8.
  - The Lua port (#13489 rewrote `CvLuaSupport`): master has no `LuaSupport::InitLuaFramework`; its successor, also
    called from `CvDllDatabaseUtility::CacheGameDatabaseData`, is `LuaSupport::NotifyDatabaseCached`.
    `MemoryDiagnostics::InstallLuaAllocHook()` now runs there **after** the support state `!VPDLL` exists and after
    `gc_reset`: the hook creates the probe thread `VP_MEMPROBE`, whose `CreateLuaThread` runs `RegisterScriptData` ->
    `DoRegisterScriptData` -> `executeSupportEvent("gc_post")` on `supportState`, which is NULL before the first
    `NotifyDatabaseCached`. `CallHook` keeps `MEMHOOK_SCOPE(MEMTAG_LUA)`; the header gets `PollExternalLuaRequest()` inside
    master's namespace; the external Lua channel (425 lines) is appended unchanged. Master's `CvLuaSupport.cpp` reaches
    the Lua C API through `!CvLuaApiExec.h` -> `!CvLuaApi.h` -> `lua.hpp`, and `MemoryDiagnostics.cpp` includes `<lua.h>`
    itself, so no include was needed for 3518's reason.
  - Committed on a local branch of the scratch clone (`ia-3515-i0-release-5.4.9`, 247c682a3, not pushed); the patch is
    `git diff --binary 8be73b6f5 247c682a3 -- CvGameCoreDLL_Expansion2 build_vp_clang.py`: 41 files, +9,182 -41,
    SHA-256 (LF) `1b783ea8...59d0`.
- **Apply as the driver does** [L, `scripts/s10_src.py`]: `git archive` of 8be73b6f5, `git apply --whitespace=nowarn`:
  `--check` passes in LF and in CRLF form; applied (CRLF), version stub `Release-5.4.8-169-g8be73b6f5+ia440878983 Copy`
  (the driver's `prepare_ia` takes `440878983` from the file name).
- **Build** [L, 2026-11-14 22:49-22:58 EST, `scripts/s11_build.py` under `quietlock.py run`, holding a whole-program
  link place (D107)]: `build_vp_clang.py --config release`, clang-cl 19.1.7: pch 26 s, cpps 82 s, LTO link 415 s, 527 s in
  all, exit 0. `CvGameCore_Expansion2.dll` **13,749,248 B, SHA-256 `388A2D2B48A7AA444038937C82DD45B3BC2AD81FAC1A525842ABA5ECB1CA14E2`**
  (TimeDateStamp 0x6AF92C39). Not byte-repeatable (6.1b: `__FILE__`, `__DATE__`, link time). Archived with its PDB and
  the patch in `D:/c5-64bit-work/3515/i0ship-8be73b6f-ia/`; not a pin.
- **Static checks of 6.1b** [C, `scripts/s14_contents.py`]: D35 pragmas `synchronous = OFF` and `journal_mode =
  PERSIST` present, `RunId` present, Lua channel names `instance-aware` (`provision.lua_channel_names`), the request file
  and event names, `VP_INSTANCE`, the memory tables, master's `DiplomacyLog`; full version string present 3 times.
- **Boot on a Goldberg instance** [L, c5h-b, 2026-11-14 23:14-23:19 EST]: master's DLL traps in
  `CacheGameDatabaseData` on 5.4.8's mod database (3513), so the instance got master's `(1)`, `(2)`, `(3a)` mod folders
  from the archive, laid out as the VP installer lays out 5.4.8 (the 70 repository files that 5.4.8's installed folders
  lack, `.civ5proj`/`.xcf`/EUI-replaced Lua, left out; the CP DLL slot given to `provision.py set-dll`), with the CP
  `.modinfo` given four VFS imports (next point), `cache/Civ5ModsDatabase.db` removed and `crashlogs/nopopups.please`
  (`scripts/s13_mods.py`, `s18_fixmodinfo.py`; all backed up by SHA-256 first). New Tiny Continents game (3 AIs, 4
  city-states) through `proxy/tools/live/prepare.py`. Guard self-test ok, start-up watch ok (772 polls); the Lua channel
  answered 46 s after the front end went into the game; `os.getenv("VP_INSTANCE")` = `"c5h-b"`; `Game.GetGameTurn()` 0,
  63 player slots, 2,016 plots, in the `Main` state resolved to the probe thread (`env=thread`, as at 5.4.8) and in
  `InGame`; AI turns 0->1 and 1->2 through `turns.py`; stopped by `cli.py stop` with the `stop_readiness` hook, no
  orphans; no `CvAssert.log`; one Lua error (VP `CultureOverview.lua` failed to load, not followed up). No `stats.db` was
  written in this game (statistics logging off in the setup), so the `RunId` column was checked statically only.
- **The first boot** [L, c5h-c, 22:59-23:13] used master's committed CP `.modinfo` unchanged: `Error loading
  GamecoreInit.lua`, then `Game`, `Map`, `Players`, `DB` nil in every Lua state; the map script failed (`Continents.lua:36`
  `Map` nil), and the DLL's assert `CvTeam.cpp:888` "Zero possible players?" (`DoBarbarianTech`) stopped the game behind
  a dialog (dismissed with the MC's D60 override, #1187). It is a consequence of the missing Lua API, not of master's code
  or the instruments: the c5h-b boot with the imports did not fire it. The channel itself worked there too
  (`VP_INSTANCE` = `"c5h-c"`, header turn 1, `env=_G`); the game was ended in game with `UI.ExitGame()` through the channel
  (MC #1190), clean, no dump. Cause: master's committed CP `.modinfo` lacks four VFS imports that `Community Patch.civ5proj`
  has (`Gamecore/GamecoreInit.lua`, `Gamecore/GamecorePostProcess.lua`, `Assets/Yields/YieldAtlas_128_Culture.dds`,
  `..._Faith.dds`; `scripts/s16_modinfo.py`); ModBuddy rebuilds the modinfo at a release (`NEW RELEASE INSTRUCTIONS.txt`
  line 167). See `new-vp-release-checklist.md` step 2.
- **Restore** [C]: both instances: front end restored, mod folders and the standard set restored by hash
  (`backup.restore`, 231 files each, the 17 added files removed), the mods database restored, the DLL set back to
  `i0ship-bbfacca1-ia` `e589306e...` (what both carried), full `provision.py verify` ok (c5h-b lists `UserSettings.ini` as
  changed-mutable, written by the game).
- **For the release driver** (not changed here): its `-ia` step for a release cut from master (tree eb8ef8e73) runs with
  `--ia-patch tools/d10/evidence/3515/ia-8be73b6f5-440878983.patch`; 9045's patch stays the default for 5.4.8-based pins.

### 6.2 Several roots and the pool (code, tested offline)

- `instance.KNOWN_INSTANCES_ROOTS` = `provision.KNOWN_INSTANCES_ROOTS` = (`<home>/c5-instances`,
  `D:/c5-64bit-work/instances`); `C5_INSTANCES_ROOTS` replaces it (empty means none). `Instance.load(NAME)` and every
  `provision.py` command on an existing instance search all roots. `C5_INSTANCES_ROOT` / `--root` still force one
  root (the old behaviour, so commands for c5h-a and c5h-b are unchanged), and a name under two roots is refused.
  `create` refuses a name or `vp_instance` taken under any root and picks the lowest gbe port free across all roots,
  reserved in the provisional manifest. `provision.py collisions` reports shared names, ports and `vp_instance`
  values (none on 2026-11-11 00:14).
- Machine places come from the environment, for another test machine (issue 9016): `C5_PURE_VP_ROOT`,
  `C5_GAME_USER_DIR`, `C5_STEAM_COMMON` and `C5_DEFAULT_INSTANCES_ROOT`. The defaults are derived from the user
  profile and equal the old literals on the owner's machine. The owner's literal places stay forbidden for writes and launches
  whatever the environment says.
- `pool.py`: one claim file per instance under `<default root>/_pool/claims/` (`C5_POOL_DIR`), created with
  O_CREAT|O_EXCL (with 8 concurrent claimers of one instance exactly one wins, test). No claim is made on an instance
  with a live process (the state file's pid with its create time, or any process whose image lies in the instance
  folder). A claim is stale when its lease has expired and nothing runs; `reap` frees it by an atomic rename into
  `claims/released/`, and an expired claim whose game is alive is kept. `--hours 0` means no expiry. Tests:
  `tools/harness/tests/test_pool.py` (17 tests), `tools/instances/tests/test_provision.py` class `MultiRootTest`. The
  full harness suite passed (438 passed, 18 skipped: the live, opt-in tests) and provision ran 64 OK. No test starts
  a game.
- Not done: `launch` does not require a claim, so the pool protects only teams that use it. At 00:14 the pool showed
  c5h-b as "free" while epic 3000 holds it between runs. Until teams use the pool, the MC should take permanent claims
  for the instances teams hold (`pool.py claim --owner 3000 --name c5h-b --hours 0`).
- **Profile rule (ticket 0467, 2026-11-17) [C, code and offline tests].** Before 0467, `pool.py claim` without
  `--name` took the first free ready instance in root and disk order whatever its provision profile, so a run asking
  for "any instance" could get a special one (2604's `modpack548` instance, VP as a DLC modpack instead of mods) and
  test the wrong install form (found by epic 2600, note #3315). Now:
  - A claim without `--name` takes only instances whose `instance.json` `profile` equals the profile it asks for
    with `--profile`, by default `pool.DEFAULT_PROFILE` = `mods548`. That is provision's own default:
    `tools/instances/provision.py` `create(profile_name="mods548")` and `create --profile` default `mods548` (VP 5.4.8
    as mods); `tools/harness/tests/test_pool_profile.py` fails if the two ever differ.
  - An instance whose manifest records no profile counts as `mods548`. On 2026-11-17 none such exists: all nine
    instances under the searched roots (`%USERPROFILE%\c5-instances` c5h-a..c, `F:\c5-instances` c5h-d..i) record
    `mods548` (read-only count of their `instance.json`; c5h-mpk was not under the searched roots at that time). The
    testbeds' (oldpc, laptop) instances were not counted.
  - A claim by `--name` is unchanged: it takes the named instance whatever its profile. With `--profile` as well, a
    named instance of another profile is refused (exit 2) and nothing is claimed.
  - A refusal names every skipped instance with its profile and how to take it (`--profile P` or `--name N`). The
    claim record stores `"profile"`, `claim` prints it, `list` shows a profile column (in brackets when none is
    recorded and the default is assumed) and `list --json` gives `profile` (as recorded) and `profile_effective`.
  - So: a run that needs a modpack instance claims `pool.py claim --owner X --ticket T --profile modpack548`; every
    other run claims as before and never gets one by chance.
  - Tests: `tools/harness/tests/test_pool_profile.py` (8 tests); with `test_pool.py`, `test_pool_tickets.py`,
    `test_pool_host_alias.py` and `test_pool_verify_cache.py`, 83 passed on 2026-11-17 (`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1`:
    the workstation's `dash` pytest plugin fails at import otherwise).

### 6.3 What one instance costs, and how many the machine carries

Machine: 32 GB RAM (31.6 GB visible), 32 logical processors (i9-13900KS), RTX 4080 16 GB, page file on C and system
managed (11,264 MB allocated at 00:14; the commit limit moved from 42.6 to 43.7 GB during the series). Other teams'
builds ran the whole time: before any game, CPU 21-47 %, commit 31.4-31.6 GB, GPU 2.1 GB used at 10-13 %. c5h-a and
c5h-b were **not** running during any sample. Process figures come from `tools/harness/memory.py`, GPU memory per
process from the `GPU Process Memory(pid_N)` counter (dedicated; nvidia-smi cannot split it per process under WDDM,
section 5), and CPU as cores busy over 5 s (`TotalProcessorTime` difference). Quiet-machine lock held 23:35-23:40 and
23:52-00:07.

**Disturbed windows (MC, 2026-11-11):** during the second hold a team-3000 worker ran GPU-heavy stand-in tests at
about 23:56-23:57 and 23:59:30-00:01:30. These numbers are affected and **not valid**: c5h-d's launch to Lua time
(4:48), the GPU figures of c5h-d's first idle sample (23:57:58: 3D 15 %, whole GPU 50 %), c5h-d's turn 351->352
(31.7 s) and c5h-e's load time. The memory figures are unaffected. Everything else below was taken outside those
windows.

| Measurement (2026-11-10/11) | Huge save (VP8P-HUGE_0350, t350-353) | Tiny (new Continents game, 3 AI + 4 city-states, t0-3) |
|---|---|---|
| Private / working set / committed, idle in game | 2,094-2,160 / 1,802-1,875 / 2,423-2,488 MB | 1,748 / 1,369 / 2,077 MB |
| The same after AI turns | 2,183 / 1,898-1,905 (peak 2,005) / 2,511 MB | 1,757 / 1,381 / 2,086 MB |
| Largest free address-space block (32-bit) | 964-1,120 MB | 1,497 MB |
| GPU dedicated memory | 1,280 MB (c5h-c alone), 1,563 MB (c5h-d with c5h-e) | 966 MB |
| GPU 3D engine use, idle in game | 23 % alone (c5h-c), 8-10 % next to c5h-e | 17-19 % |
| CPU, idle in game (render loop not capped) | 5.7 cores alone, 4.2 next to c5h-e | 6.5-6.9 cores |
| System commit added | +4.0 GB (c5h-c: 31.6 -> 35.6), +3.3 GB (c5h-e added to c5h-d: 34.2 -> 37.5) | (in the +3.3) |
| Launch to window | 18.3 s (C), 29.3 s (D) | 21.6 s (D) |
| Launch to Lua answer / in game | 2:43 / 2:51 on C (10 s polls) | not valid (disturbed; the logs show the game generated about 2:17 after launch) |
| AI turn | 30-34 s in the harness wait (41.3 / 30.8 s wall, c5h-c alone; 31.8 s for c5h-d with c5h-e idle) | 4-5 s |

Free physical memory: 15.6 GB before, 14.5 GB with one huge instance, 12.8 GB with a huge and a Tiny one [L].

**Capacity** [I: extrapolated from one and two instances; three or more were not run at once]:
- **The commit limit runs out first**, not physical RAM. Each instance adds 3.3-4.0 GB of system commit, and the
  page file that would raise the limit lives on C, which has 29 GB free after c5h-c. With today's background of about
  31.5 GB committed by other sessions and a 42.6-43.7 GB limit, about **three** more instances fit, huge or Tiny alike
  (a Tiny game saves only about 0.4 GB). Four need either less background (other teams' builds finished) or more room
  for the page file on C.
- **Then the processor:** an idle instance in game keeps 4-7 of the 32 logical processors busy (the render loop is not
  capped; 113-155 fps per cloaked window, section 5). Four instances take about 20-26 of them, which leaves nothing for
  other teams' builds, and AI turns add more. A frame cap changes this: since issue 0157 (2026-11-14) every harness launch runs at 30 fps unless declared a timing launch, which took an idle huge-save game from 4.3-5.4 to 1.06-1.19 processors on the stock route (`run-and-drive-the-game.md` section 15). *Replaced: "A frame cap would change this; not measured."*
- **The GPU is not the limit:** 1.0-1.6 GB of video memory each (about ten instances by memory) and 8-23 % of the 3D
  engine each.
- **Answer:** with other teams working, **3 at once** (huge or Tiny) without risk; **4** (D38's "at least four") when
  the background drops below about 28 GB committed, or for Tiny games while other work is light. Ten, as the owner ran with
  Goldberg before, would need about 35-40 GB of commit headroom and 40-70 processors: not on this machine with VP
  loaded in game. Disk: 8.2 GB per instance; C takes no more instances (rule D37), and D has 2.79 TB free.
  *Superseded 2026-11-21 (noted by one-off 9080): this is the 2026-11-10 measurement on 32 GB. Since 2026-11-14 the
  workstation has 64 GB, the owner set the places at game-instance 6, headless 9 (D226), and instances live on C: and F:
  (`capacity-series.md` section 10); D: is storage only (D133).*

**Not verified:** three or more instances at once; the effect of a frame cap on CPU (measured since: section 15 of `run-and-drive-the-game.md`); whether D's slower load comes
from the disk or the disturbance (repeat c5h-d's launch to Lua outside any disturbance); the Tiny load time.

### 6.4 The instances' mod set against a real VP 5.4.8 install (ticket 0423, 2026-11-16)

Question (from 0392's finding 4510): do the pool instances run EUI without VP's EUI compatibility mod, a set-up no VP
player has? **Answer: no.** The instances carry the compatibility mod byte for byte, and every harness default activates
it. 4510's set-up came from a hand-picked `--mods cp,vp`. The default mod set does not change. On the MC's ruling of
2026-11-16 (thread 0423), `provision.py` closes the remaining gaps to a 5.4.8 install:
- `(4a) Squads for VP` is copied, as an optional mod. It is never activated by default; frontend's alias `squads`
  activates it.
- The 5.4.8 `MinorCivSounds_VoxPopuli.xml` and `VPUI_tips_en_us.xml` are taken from a 5.4.8 release source tree. The
  table and the end of this section give the details.

**What a VP 5.4.8 "with EUI" player has** [C: `VPSetupData.iss` at `Release-5.4.8` of Community-Patch-DLL,
component `FullEUI`; installer-notes.md 1]:
- In `My Games/.../MODS`: `(1) Community Patch` and `(2) Vox Populi`, both without their `LUA` folders (those go only
  to the no-EUI components). Also `(3a) VP - EUI Compatibility Files`, `(4a) Squads for VP` and
  `(5) Modpack Maker for VP`.
- In `My Games/.../Text`: `VPUI_tips_en_us.xml`.
- In the game folder: `Assets/DLC/UI_bc1` (EUI), `Assets/DLC/VPUI`, VP's `Expansion2.Civ5Pkg` and
  `Expansion2/Sounds/XML/MinorCivSounds_VoxPopuli.xml`.
- The player then enables the mods in the Mods menu.
- The owner's own install (read only, D26) matches this. The owner's mods database (`cache/Civ5ModsDatabase.db`, read from a copy)
  has these enabled: CP v151, VP v17, `(3a)` v1, `(4a) Squads` v1, and four extras of his own (Quick Turns v10,
  VernsImprovedCommanderInfluenceBorders, VernsTweaks, Wake All and Upgrade All Units) [L, 2026-11-16 09:12 EST].

**The comparison** [L, 2026-11-16 09:11-09:14 EST]:
- Script `C:/c5-64bit-work/0423/compare.py` (scratch, not committed). It ran through `quietlock.py run` and read for
  about 2 min.
- It hashed every file of the components above in the owner's install. It then hashed the same paths in all nine pool
  instances: c5h-a..c on C:, c5h-d..i on `F:/c5-instances`, all profile `mods548`.
- The owner's tree was snapshotted with `provision.py snapshot` before and after the run, and the two snapshots are equal (see
  the line below the table).

| Component | The owner's install | Every pool instance |
|---|---|---|
| `(2) Vox Populi` (1,350 files) | 5.4.8 | **equal** |
| `(3a) VP - EUI Compatibility Files` (44 files) | 5.4.8 | **equal** |
| `(1) Community Patch` | 5.4.8 plus the patch's files | mod files **equal**. The only differences are the harness's and the patch's own files: the game DLL, `CvGameCore_Expansion2_x86.dll`, `vpproxy.ini`, `vp64helper.exe`, `VP64/*`, the composite, PDBs |
| `DLC/UI_bc1` (190 files), `DLC/VPUI` (10), `Expansion2.Civ5Pkg` | 5.4.8 | **equal** |
| `Expansion2/Sounds/XML/MinorCivSounds_VoxPopuli.xml` | 5,903 B, dated 2026-07-17 | **different**: 5,787 B, dated 2025-10-13. This is pure_vp's 5.4.7 copy (3.5). It holds sound definitions only; the 5.4.8 file is presumably the owner's `[I]` |
| `(4a) Squads for VP` (11 files) | present, enabled by the owner | **missing** |
| `(5) Modpack Maker for VP` (2 files) | present, not enabled | **missing** |
| `Text/VPUI_tips_en_us.xml` | present | **missing** (load-screen tips) |

**The mod set each tool activates** [C, main fe132c23]:
- `cp,vp,eui` (CP, VP and `(3a)`) is the default of `tools/harness/frontend.py prepare` (`--mods`).
- The same default is in `tools/scenario/runner.py`, `tools/stressmap/ptwe.py`, `perf/capacity/games.py`,
  `perf/profilekit/acceptance.py`, `perf/ledger/owners/m3_drive.py` and every testbed job that prepares a front end
  (`release_fallback`, `release_d33_runtime`, `release_d125`, `product_path`, `minimap_step_a`, `live_session`,
  `headed_probe`, `fps_idle`).
- `steam_launch.py` installs and activates `(1) (2) (3a)` too.
- The vp-dll-dev skill's examples use `cp,vp,eui`.
- The one set without `(3a)` found is 0392's HI work. 4504's set-up (7.3 of `human-input-testing.md` on the 0392
  branch), the stock replays and 4510's live totals (12.2) all chose `frontend.py prepare --mods cp,vp` by hand
  (4510: `F:/c5-64bit-work/0392/4510/scripts/relaunch.sh`).
- `modsExclusive` then turns `(3a)` off.

**Saves do not name `(3a)`.** Its modinfo has `AffectsSavedGames = 0`, and so does Squads'. A save header lists only
CP v151 and VP v17, whichever set made it [C: `server/tools/civ5save.py info` on `C5H-3600-TINY_0008`, SHA-256
`b2ceb663...`; release-driver ruling in `tools/release/stages/testdata.py` `s_mods`]. So a save's header cannot tell
whether `(3a)` was active. The mods that were activated before the load decide it: `frontend.py prepare --mods`.

**Why 4510's hover stops the DLL only without `(3a)`** [C]:
- EUI's `UI_bc1/PlotHelp/PlotHelpManager.lua:624` calls
  `plot:IsResourceConnectedByImprovement( revealedImprovementID )` on any plot with a resource. On an unimproved plot
  that ID is -1, and -1 meets the DLL's `PRECONDITION(eImprovementNum > -1)` (0421).
- `(3a)` ships its own `LUA/PlotHelpManager.lua` (modinfo `import="1"`, so it replaces EUI's in the VFS). At line 695
  it reads `revealedImprovementID ~= -1 and plot:IsResourceConnectedByImprovement(...)`. So a player with the
  compatibility mod never makes that call with -1.
- The DLL guard of 0421 is still right: other Lua can pass -1.

**Live check, one Tiny load per mod set** [L, 2026-11-16 09:22-09:32 EST]:
- Set-up: instance c5h-d (F:, claimed by `impl-0423` for 0423), stock route with the pin's 32-bit DLL
  `i0ship-26234371-ia` (3C2F47DA...), client 2554x1411, frame cap 30 fps.
- Save: `HI0423-TINY_0008`, a copy of `C5H-3600-TINY_0008` (`b2ceb663...`, the same bytes as 4510's
  `HI4510-TINY_0008`), loaded by `frontend.py prepare --load`.
- Plot 51,13 is coast with `RESOURCE_CRAB` (ID 32), unimproved: revealed and actual improvement both -1.
- The hover:
  - `UI.LookAt(Map.GetPlot(51,13), 0)` over the Lua channel, then posted mouse moves (`cli.py move`) to client
    (1532,706).
  - `UI.GetMouseOverHex()` read 51,13 there.
  - The mouse was held 3.5 s, with a move every 0.25 s, after starting from 48,13.
  - Driver: `C:/c5-64bit-work/0423/hover.py` and `hold.py` (scratch).

| Mods (`frontend.py prepare --mods`) | Activated mods (`Modding.GetActivatedMods`) | Result |
|---|---|---|
| `cp,vp,eui` (the harness default) | CP, VP, `(3a)` | **No stop.** The game ran on and the Lua channel answered 10 s after the hover. The plot tooltip is drawn ("Resource: Crab", Coast, Output 3 food 1 gold; Visual Checks `vc-20261116-092642-workstation-d99d`). Lua.log shows the `(3a)` version loaded ("This is the modded ResourceTooltipGenerator from CBP"). It also logs `PlotHelpManager: GameInfo.Improvements[-1] is undefined` once, a harmless lookup. There is no `TopPanel.lua:976` error; 4510's `cp,vp` log has 27,514 of them |
| `cp,vp` (4510's set, positive control) | CP, VP | **DLL fatal box** 2 s into the hover (09:31:11): `CvGlobals.cpp:4244 eImprovementNum > -1`, the box of 4510's stock run. It was closed by the claim owner (`dismiss --fatal`, `stop --after-fatal`; queue note #2512). Evidence: `F:/c5-64bit-work/0423/20261116_093111_pid68504` and `..._093237_pid68504`, with minidump and `crashes.log` |

**So 4510's hover crash is an artefact of a hand-picked `cp,vp` set.** It does not reproduce with the set a VP player
has (`cp,vp,eui`). The DLL's missing guard (0421) is real but unreachable from this tooltip with `(3a)`. The D118
point of 0422 (the product route stops with no message) is not affected by this.

**Which arm changes for stock comparisons.** No arm of the standard harness changes: stock and product arms prepared
with the defaults already run `cp,vp,eui`.
- The arms that change are 0392's HI runs prepared with `cp,vp`, stock and product alike (4504, 4505, 4510). The pairs
  stay comparable with each other, because both arms of each pair used `cp,vp`. They are not comparable with any
  `cp,vp,eui` run, nor with what a player sees. Mods active and the Lua file that serves the hover tooltip both differ.
- A run that must match a VP player prepares `--mods cp,vp,eui`. Squads is optional, and not in any default (MC,
  thread 0423, 2026-11-16). The owner plays with it enabled; `--mods cp,vp,eui,squads` gives their set without their four extras.
- Without `(3a)`, EUI's top panel also logs `TopPanel.lua:976 ... 'GetYieldPerTurnFromVassals' (a nil value)` on
  every tooltip frame (4510, 12.5). That is one more reason a `cp,vp` set is not a VP player's.

**The instances brought to 5.4.8** (MC ruling of 2026-11-16 on thread 0423; code in `tools/instances/provision.py`,
described in `tools/instances/README.md`, "VP 5.4.8 as the installer leaves it"):
- `create` and the new `align548 NAME` copy `(4a) Squads for VP` as an optional mod. Like the other mods, it comes
  from the instance's source user folder (the owner's MODS, read only).
- They also write the two 5.4.8 release files from a 5.4.8 release source tree, not from the owner's install:
  `Community-Patch-DLL-5.4.8-pragmas`, a checkout built as `Release-5.4.8-0-g7307aefdf`.
- Each release file is checked against a pinned SHA-256: `MinorCivSounds_VoxPopuli.xml` `d91e5c3d...` (5,903 B) and
  `VPUI Text/VPUI_tips_en_us.xml` `28a3061c...` (9,174 B). These equal the files the 5.4.8 installer wrote into the owner's
  install, and Community-Patch-DLL's own working tree. pure_vp's 5.4.7 sound file is `45b1a16d...` (5,787 B).
- `verify` now warns, naming `align548`, when an instance lags either of them.
- Applied [L, 2026-11-16 09:44-09:53 EST]. Each instance was claimed by `impl-0423` with no process running, then
  `align548` ran, then a plain `verify` through `quietlock.py run`:

| Instance | `align548` | `verify` |
|---|---|---|
| c5h-a, c5h-b, c5h-c, c5h-d, c5h-f | sounds replaced (5.4.7 copy in `run/align548/<stamp>/`), tips added, Squads 11 files copied | ok, no errors; install 13,310-13,311 files, user 2,148. c5h-a and c5h-c warn of an older stale `stats.db-journal` (2026-11-14), not from this change |
| c5h-e, c5h-g, c5h-h, c5h-i | **not yet**: claimed by other runs (0417, 0392/4506, 9073, 9074) until 12:19-15:15 EST | their next `verify` warns: "... is not the VP 5.4.8 release's ...: `provision.py align548 NAME` (ticket 0423)" |

- A new instance on a test machine whose base has no 5.4.8 release tree keeps pure_vp's sound file and no tips file.
  `create` says so, records it in `instance.json` `release_files.note`, and `verify` warns about it.

### 6.5 Instances on Vox Populi 5.5 (ticket 0766, D259, 2026-11-25)

Upstream tagged `Release-5.5` on 2026-11-24 (commit b0bce8c2f; release page with the installer `Vox.Populi.5.5.0.exe`,
109,826,403 B, sha256 `ef13c556...0e6e`, and a `Debug.zip`). The instances get VP 5.5 from that tag, never from the
owner's MODS (5.4.8 until the owner updates them).

**The release tree** [C code; L 2026-11-25 14:45-14:56 EST]. `tools/instances/vp_release_tree.py build --tag T --out DIR`
lays a release as its installer lays the component FullEUI (`VPSetupData.iss` [Files]; unchanged from 5.4.8 to 5.5 but
`MyAppVersion`): `user/MODS/(1) (2) (3a) (4a) (5)`, `user/Text/VPUI_tips_en_us.xml`, `install/Assets/DLC/UI_bc1/**`,
`VPUI/**`, `Expansion2/Expansion2.Civ5Pkg` (from `Expansion2_VoxPopuli.Civ5Pkg`), `Expansion2/Sounds/XML/
MinorCivSounds_VoxPopuli.xml`, and the Civ43EUI extras under `extras/43civ/`. Three facts the byte compare needed:
- The installer packs ModBuddy's `Build\<mod>` folders: a mod's file is in the release only when its `.civ5proj` lists
  it. That leaves out 4 files of `(1)` (Credits.txt, Kit/readme.md, SampleContracts.xml, SampleEvents.xml) and 10 of
  `(2)` (nine `.xcf` atlases and `promoVP_04_256_linear.dds`), at 5.4.8 and at 5.5 alike.
- Line ends: the installed files are a Windows checkout's (`core.autocrlf=true`: CRLF in every text file, the tag's
  `.gitattributes` applied), except the `.modinfo` files, which are the tag's blobs byte for byte (LF, BOM).
  **Correction to installer-notes.md 14.1** ("the three `.modinfo` differ in line ends only, git LF, VP's installer
  CRLF"): the installed `.modinfo` are LF; 2604's compare read the tag through a clone with `core.autocrlf=true`, which
  gives CRLF.
- `(5) Modpack Maker` is laid without subfolders (its line has no `recursesubdirs`).

The proof on 5.4.8 [L]: the tree of `Release-5.4.8` against c5h-a (MODS copied from the owner's installer-laid tree, DLC files
pure_vp's, sounds and tips by `align548`): **every file equal byte for byte** in `(1)` (737 files), `(2)` (1,350),
`(3a)` (44), `(4a)` (11), Text, UI_bc1 (190), VPUI (10), Expansion2.Civ5Pkg and the sounds file; the only differences
are the DLL (c5h-a carries the pin's `3c2f47da...`; the tree the tag's `262aee88...`) and `(5)`, which no instance
copies. So a tree made this way is what the installer writes, and `Release-5.5`'s tree is VP 5.5 as installed
`[I]` until compared with the owner's own install after the owner has updated it (read only).

`Release-5.5`'s tree: 2,370 files, 267.2 MB, manifest SHA-256 `bc4c091a769ee248bc112d5e40c3d2a18905234f0bfe4cc26d4b9f22559d9918`
(the builder is deterministic: two builds gave it); `(1) Community Patch (v 152).modinfo` (5.4.8: v151, the same mod id
`d1b6328c-...`), VP v17, `(3a)` v1, `(4a)` v1; the 32-bit DLL `fd2fc01f337df5f0f3b0ad839e6cf270d229cb1a3062a7bd22370216052715e7`
(11,353,088 B), the 43-civ DLL `b65231d5f205c1dd3bdfd0ec80f35d93d145fc2d93fff1be4a6b351e082d0271`. Game-folder files that
change from 5.4.8: `UI_bc1/CityView/CityView.lua` and `VPUI/Core/Highlights.xml`. Kept at
`C:/c5-64bit-work/keep/vp-release/vp-Release-5.5` (README there; never committed, D83).

**The profile `mods55`** (`tools/instances/provision.py`, README "VP 5.5 as the installer leaves it"): every VP file from
the tree (manifest SHA-256 pinned in `VP_TREES`, each file checked before and after the copy); the user source gives
only the three ini files; pure_vp's `UI_bc1` and `VPUI` are left out and the tree's laid (the installer's
`InstallDelete`); `instance.json` `vp_release`. Only a DLL built on `Release-5.5` (build string) or the tag's own is
taken, by `create --dll` and `set-dll`: the 5.4.8 pin is refused (`--dll mod` until the 5.5 pin exists). `pool.py claim`
without `--name` never hands a `mods55` instance to a run that asks for nothing (default profile `mods548`), so the
5.4.8 instances and runs stay as they are until beta 5 ships.

**No 5.4.8 save loads on a 5.5 build** [C: `git show 0848d52cc` (upstream #13643, 2026-11-24), an ancestor of
Release-5.5; first found by impl-0764, #15153]. `CvPlayer::Serialize` writes a new field `m_iNextObjectID` (each
player's own city/unit/army ID counter) right after `m_armyAIs`, with no save-version guard; the commit message says
"Saves made before this change do not load". A 5.4.8 save's player block is read one int off, on the x86 and the x64
DLL alike. Also every 5.4.8 save header names `(1) Community Patch` v151 (5.5: v152, same id); the save header check
alone was not the gate at master before (release-driver.md P10). How the EXE and VP 5.5's own DLL fail on such a load
(refusal or crash box) is the live row below.

**The tiers' save and bundle inputs on 5.5** (from `tools/release/tiers/stages/*.py`, `tools/release/stages/*.py`,
`tools/hi/journeys/journeys.json`, `tools/hi/kit/kit.json` at main 3f2534191; read 2026-11-25):

| Input | Read by | 5.4.8 file (SHA-256) | On 5.5 |
|---|---|---|---|
| `REL548-TINY_0008` (Tiny T8) | instance stage (CL 17), hotfix and intermediate | `D:/c5-64bit-work/3500/inputs/` (`b2ceb663...`) | does not load: make new (`C5_RELEASE_TINY_SAVE`, `TINY_SHA256`) |
| huge reference save | refsave stage (W5) | per take | the stage makes a new one per release already |
| `HI-4506-BASIC-T0`, `HI-4506-LARGE-SCENE` | hi_journey (D162 std) | `F:/c5-64bit-work/0392/4506/saves/` (`68ed61e6...`, `c01bb2bf...`) | do not load: new saves, and the traces `release-basic.jsonl`, `release-large.jsonl` re-recorded on them (clicks on the old map's plots and units) |
| `HI-0580-BASIC-M43-T0`, `HI-0580-LARGE-M43-SCENE` | hi_journey (D162 43-civ) | same folder (`513f5ba9...`, `96b54368...`) | as above, made with the 5.5 43-civ build |
| `HI-KIT-MEDIEVAL-LARGE-T200` | sweep (intermediate) | `F:/c5-64bit-work/0398/kit/saves/` (`e3da1d17...`) | does not load: new kit save (`kit.json` path and hash) |
| modpack T19 autosave | modpack stage (intermediate) | on c5h-new-mpk | does not load; the form also needs a 5.5 modpack. **Made 2026-11-26 (ticket 0779)**: `MPK55-STD-T0019` (`fefe04b7...`) on the modpack55 instance c5h-55-mpk; the stage picks it and the profile for a 5.5 take (below, "The 5.5 modpack-form T19 save") |
| scene library bundles, asset root `mods548-vt` | scenes stage (intermediate) | `C:/c5-64bit-work/scene-library` | **no change needed** [C, scenes.py]: an offline regression of the candidate's pictures against the last release's on the same bundles and the same frozen asset root; no game and no installed VP is read. Bundles of 5.5 games and a 5.5 asset root are a later step (fidelity on 5.5 art), not a gate input |
| new games (wholegames, savetest, fixchecks, oldsave) | headless stages | the descriptor's test data | the descriptor and test database of Release-5.5 (0765/0759, not instance files); oldsave's 5.4.8 save fails by design (3517) |

Making the new saves needs a 5.5 DLL with the Lua channel (an instance-aware build on Release-5.5: 0764's i0-vp55 with
the instruments, or the tag with them); VP 5.5's own DLL has none (runbook 3.7 TRAP 0679), so the harness can launch
and load with it but not drive turns, save, or set a scene.

**The -ia DLLs of Release-5.5** [C build; L 2026-11-25]. 4968's instruments patch (`ia-5d18d3d6c-4968.patch`, with
the no-loadstring runner that build 26577625 needs) applied with `git apply --3way` to a detached fork worktree at
Release-5.5: 31 of 39 files clean, 8 conflicted, resolved by hand: `CvGame.cpp`, `CvGameCoreDLL.cpp`, `CvGlobals.cpp`,
`Lua/CvLuaSupport.h` both sides kept; `Lua/CvLuaSupport.cpp` upstream's side (5.5 removed `LuaSupport::InitLuaFramework`,
which the patch touched; `MemoryDiagnostics` installs its Lua hook itself as a fallback); the i0-only cache stamp of bug
0116 (`m_iCacheStamp`) dropped; `SqliteLogger.cpp/.h` upstream's; `SqliteLoggerRegistrations.h` upstream's plus the
instruments' 39 `RegisterMem*Table` functions (the first build failed in `MemoryDiagnostics.cpp` without them). The
result is `tools/d10/evidence/0766/ia-b0bce8c2f-0766.patch` (9,714 lines, applies with plain `git apply` to a git
archive of the tag) and the `IA_PATCHES` row `0766` (base Release-5.5) in `tools/release/stages/build.py`. Built with
runbook 1.9 (`build_vp_clang.py --config release`, 404 s and 415 s on a loaded workstation), version stubs
`Release-5.5-0-gb0bce8c2f+ia4968-0766 Copy` and `...+m43 Copy` (take_release's 43-civ define injection):
22-civ `63e997c1281765e12f5120da075e58471a79006be82638312745cc661812764f` (13,785,600 B), 43-civ
`4ef94f1d7825ff878d9a18d18bf2cac1f06ad0feadafc73d813825728416da0a` (13,756,416 B), kept in
`C:/c5-64bit-work/keep/vp-release/dlls/vp55-ia[-m43]/`. Lua channel instance-aware; in game the 43 build reads
`GameDefines.MAX_MAJOR_CIVS` 43, the 22 build 22 [L]. VP's asserts stay on: a forced turn end (`turns.py end`) while a
city has no production raises the continuable box `CvPlayer.cpp:34626 GetEndTurnBlockingType() ==
NO_ENDTURN_BLOCKING_TYPE` (got 3) [L, 17:25, closed with OK under MC-B's override #15533]; set production and research
before each forced end (`choose` chunk below).

**Games on F: did not start on 2026-11-25** [L 16:23-17:08]. c5h-55-a on `F:/c5-instances`: three launches, none
reached a game. The first sat 17 min at "Configuring Game Data... Please Wait." after the mods' activation; the second
hung with the main thread waiting on a critical section inside the EXE; the third had no window after 4 min, its main
thread in `ReadFile` (cdb `~0 kb`, non-invasive). typeperf at 17:08: F: 0 % idle, 0.76 s per read, queue 4-5 (impl-0771's
autoplays, render tests and scene-bundle builds of epic 0042 reading or writing F:). The same steps on c5h-55-c on C:
reached the load screen of a new game in about 3 min. So a game instance on F: is not usable while F: is saturated
(compare d-drive-load.md 11-12); the loads say nothing about VP 5.5 then.

**Instances copied the patch's session config** [L, 2026-11-25 17:5x; found by impl-0771 #15565]. c5h-55-a, -c and -d
copied the owner's current `config.ini` (source SHA-256 `ff38781c...`), which carries the 64-bit patch's session
switches `[DisableSystems] Decal, Forest, FOW, FluidFOW, YieldIcons, DistanceFog = 1` (installer-notes.md 11.4); c5h-new-a
(made 2026-11-21) has them 0. On the stock route such an instance draws a wrong (red) world. They are render switches
only, so saves made there are unaffected `[I]`. Since this change `create` writes those six keys as 0 when the source
has 1 (`PATCH_SESSION_SWITCHES`, recorded in `files[...config.ini].edits`); the three instances made before it are
fixed by hand (back up by SHA-256, the six keys to 0).

**A 5.4.8 save on VP 5.5, live** [L]: the one granted live load (a copy of REL548-TINY_0008 on c5h-55-a with the
-ia DLL) ran into the F: saturation above, so it shows nothing about the save; the [C] reading of 0848d52cc stands.

**The short 5.5 saves made** [L 2026-11-25 17:19-17:47, c5h-55-c on C:, profile mods55, build 26577625; kept in
`C:/c5-64bit-work/keep/vp55-saves/` with a README and SHA256.txt; never committed, D83]. Each is a new game made by the
vp-dll-dev front end's `newGameSetup` with 0580's `human=` addition (a copy of the skill's MainMenu.lua through
`C5_FRONTEND_ASSETS`), the load screen closed by `frontend.py dismiss`, `UI.SaveGame` on the main thread (a one-shot
`Events.LocalMachineAppUpdate` handler). New games, so the maps are new (other seeds): anything keyed to a plot or unit
of the old save must be picked again (list below).

| New save | SHA-256 | Size | Old save it replaces | Recipe (newGameSetup; then) | Read in game before the save |
|---|---|---|---|---|---|
| `REL55-TINY_0008` | `6d1f3a1016624dc97bdd48f8f2a54e180ebb2237599e3119b653b19e968c0f57` | 256,585 | `REL548-TINY_0008` (instance stage) | `map=Continents;size=WORLDSIZE_TINY;ais=3;minors=-1;speed=GAMESPEED_STANDARD;handicap=HANDICAP_PRINCE;human=CIVILIZATION_POLYNESIA`; the settler founds the capital on turn 0; each turn: a worker (else warrior) into an empty city queue and a research if none, `turns.py end`, to turn 8 | turn 8, Polynesia, Prince, 4 majors, map 56 x 36 (as the old), Honolulu 47,11, warrior 46,11, pathfinder 47,12 (unmoved), no autoplay |
| `HI-0766-BASIC55-T0` | `70c9bb1561309dc313e0811b24518cded82181856795ded6ac8d761034e67e5c` | 203,959 | `HI-4506-BASIC-T0` (release-basic) | `map=Continental_Drift;size=WORLDSIZE_TINY;ais=3;minors=-1;handicap=HANDICAP_EMPEROR;human=CIVILIZATION_RUSSIA`; saved at turn 0 | turn 0, Russia, Emperor, 4 majors, MAX_MAJOR_CIVS 22, 60 x 40, settler 20,17, warrior 21,18, pathfinder 21,16 |
| `HI-0766-BASIC55-M43-T0` | `19bbfd719960362f26159504efa1e5da53b07770c50e33a5da92c755528ac4bf` | 205,654 | `HI-0580-BASIC-M43-T0` (release-basic-m43) | the same, with the 43-civ -ia DLL | turn 0, Russia, Emperor, 4 majors, MAX_MAJOR_CIVS 43, 60 x 40, settler 47,8, warrior 46,8, pathfinder 46,7 |

Every header names CP v152 and VP v17. The long saves (huge T350, the Large T200 bases of HI-KIT medieval, HI-4506 large
scene and the m43 large scene) are ticket 0771's (posted on thread 0766); the modpack-form T19 autosave needs a 5.5
modpack (Modpack Maker in a running 5.5 game, installer-notes 14.1) and a `modpack55` profile: not made on 2026-11-25
(*made 2026-11-26 by ticket 0779: the next paragraph*).
The Lua chunks are `tools/instances/evidence/0766/` (`found.lua`, `choose.lua`, `save.lua` with `@NAME@`, `info.lua`);
the commands are runbook 3.5b.

**The 5.5 modpack-form T19 save** (ticket 0779, D259) [L 2026-11-26 07:11-07:5x EST, workstation, C:, build 26577625]:
- **The modpack**: made by VP's own Modpack Maker in a 5.5 game on c5h-55-c (`installer-notes.md` 14.1a): kept at
  `C:/c5-64bit-work/keep/vp-release/modpack55/VP_MODPACK`, 2,743 files, manifest SHA-256 `26dbf95e...1372`.
- **The instance** c5h-55-mpk (profile `modpack55`, `%USERPROFILE%/c5-instances` on C:, port 47600), made in the place of
  c5h-55-d (MC #16985: C: had 45 GB, F: 0 % idle; c5h-55-d removed 07:20 by `provision.py destroy` under a claim, after
  impl-0788's "done" and a SHA-256 check that its two Large -55 saves are on `F:/c5-64bit-work/0771/saves`; C: 45 -> 52
  GB): `provision.py create c5h-55-mpk --like c5h-new-mpk --profile modpack55 --exe-build 26577625 --dll
  <vp55-ia 63e997c1...> --user-src C:/c5-64bit-work/0779/user --min-free-gb 40` through `quietlock.py run`: 16,055 + 5
  files, 8.24 GB, 92.6 s (install 70.9 s), 47.3 GB free after; `verify --full` ok, no error, no warning. The three ini
  files came from a copy of c5h-55-c's (a `--user-src` inside the instances root is refused; `--like c5h-55-c` would
  also have re-read its recorded save source in the owner's ModdedSaves, so `--like c5h-new-mpk`, which lists none).
- **The game, the old T19's recipe** (0620 run P, `research/02-exe-internals/new-exe-build-26577625.md` C.7.3: new
  Standard game, no mods): `tools/instances/evidence/0779/drive_t19.py prepare` (newGameSetup
  `map=Continents;size=WORLDSIZE_STANDARD;ais=7;minors=-1;speed=GAMESPEED_STANDARD;handicap=HANDICAP_PRINCE`), `cli.py
  launch` (hidden, 30 fps cap), first launch to the load screen about 1.5 min, in game 07:37:13 (one dismiss): turn 0,
  Aztecs (the random civ), Prince, 8 majors, map 80 x 52, autoplay 0. `play`: per turn `choose.lua` (one step of our own:
  VP 5.5's -ia DLL asserts on a forced end with a blocker, 6.5 above), `turns.py end`, `turns.py wait`, the box marker;
  turns 0 -> 20 in 7.0-11.8 s each, no box. The settler is not moved and no city founded (the 0620 driver, c_drive.py,
  ends turns only); at turn 20 the human has a settler 55,38, an Otomi 56,38 and a pathfinder 55,37, no city.
- **Naming** [L]: Civ V writes `AutoSave_00NN` when turn NN+1 begins (after `AutoSave_Post_00NN`), with turn NN's year:
  `AutoSave_0018 BC-3280` came at 07:43:31, when turn 19 began; `AutoSave_0019 BC-3240` (07:45:46) needed the end of turn
  19. So the old T19 also came from a game played to turn 20 (0620's game 2 played 10 -> 20), and the stage's earlier
  "AutoSave_0031, turn 32 -> 33" (validation-tiers-m2.md) is the same rule.
- **The save**: `AutoSave_0019 BC-3240.Civ5Save` copied (by `drive_t19.py keep`) as the instance's
  `Saves\single\MPK55-STD-T0019.Civ5Save` and to `C:/c5-64bit-work/keep/vp55-saves/` (README, SHA256.txt): SHA-256
  **`fefe04b7ea8d352a330b64ab9e3693e9d0207fbba5af6900fe683ae4d928bf3b`**, 388,239 B; header (`civ5save.py info`): turn
  19, CIVILIZATION_AZTEC, HANDICAP_PRINCE, GAMESPEED_STANDARD, WORLDSIZE_STANDARD, Continents.lua, **mods []** (the
  modpack form), DLC the base game's 14. Never committed (D83).
- **Load check** [L 07:47-07:51]: after the harness stop, `frontend.py prepare --mods "" --load MPK55-STD-T0019
  --restore-first` (the stage's prepare), launch: in game 07:49:48 at turn 20, the same civ, units and map as saved,
  `Modding.GetActivatedMods()` 0; one turn 20 -> 21 (choose, end, wait), no box; harness stop clean, `verify --quick`
  ok, leftovers clean; c5h-55-mpk released 07:52. Not run: the stage itself (it needs a take's TEST installer for 5.5)
  and `UI.ExitGame` (the stage's exit). Logs: `tools/instances/evidence/0779/` (`mm/`, `inst/`, `t19/`, `load/`).
- **Who reads it**: `tools/release/tiers/stages/modpack.py` `BY_VP['5.5']`: a take whose tag is `Release-5.5[.0][-...]`
  (`stages/instance.py` `vp_of_tag`) claims `--profile modpack55` and plays `Saves\single\MPK55-STD-T0019.Civ5Save` from
  the claimed instance's My Games; every other tag keeps `modpack548` and `Saves\single\auto\AutoSave_0019 BC-3240`;
  `C5_RELEASE_MODPACK_PROFILE` / `_SAVE` still override; a missing default save still falls back to the newest numbered
  autosave. The stage's `settings` gain `vp` only on a 5.5 take, so a 5.4.8 take's inputs hash is unchanged. Tests:
  `test_tier_modpack.py` 22/22 (5 new in `VP55`).

`tools/release/stages/instance.py` picks the Tiny save by the take's release: `TINY_SAVES_BY_VP['5.5']` (REL55-TINY_0008,
its hash, roots `C:/` then `F:/c5-64bit-work/keep/vp55-saves`) for a tag `Release-5.5[.0][-...]`; every other tag keeps
`TINY_SAVE` / `TINY_SHA256` (REL548, `b2ceb663...`); `C5_RELEASE_TINY_SAVE` still names the path. The tier stage uses
the same function.

**Plots and units keyed to the old saves** (to pick again on the new maps; from the repository at main 3f2534191 and
the 0766 branch, read 2026-11-25):
- 0456 and 0477's air-strike and fight scenes (`server/evidence/0456/scripts/setup.lua`, `ai_setup.lua`): placed
  relative to the human capital (`GetCapitalCity`, rings 2-7), so they carry over; the old capital was 51,14, the new
  is Honolulu 47,11. Their README coordinates are history.
- 4510's / 0423's hover check: plot 51,13 (coast, `RESOURCE_CRAB`, unimproved) of the old Tiny map: pick an
  unimproved resource plot on the new map.
- The scenario catalogue's layout `tools/scenario/layouts/tiny_cat_0009.json` (save `C5H-0301-CAT_0009`, from the old
  Tiny at turn 9): absolute studio rectangles and cells (land 36-47 x 7-14 camera 41,11; sea 25-33 x 6-12 camera 29,9;
  city 36-47 x 7-14 camera 42,10) and owners by player slot; a 5.5 catalogue needs a new layout read from the new map.
- The HI journey traces (`tools/hi/journeys/*.jsonl`): screen positions of plots and units of the old start saves; they
  are re-recorded on the new saves by ticket 0770.
- Tool defaults that name a 5.4.8 save (each needs its 5.5 twin before it runs on 5.5): `tools/testbed/jobs/live_session.py`,
  `release_d125.py` (TB-TINY_0008 / REL548-TINY_0008); `tools/testbed/bases.json` item `saves` (TB-HUGE_0350);
  `tools/hi/kit/kit.json` and `tools/hi/phase/phase.py`, `tools/release/tiers/stages/sweep.py` (HI-KIT-MEDIEVAL-LARGE-T200);
  `tools/d10/run_d10.py`, `tools/x64-dll/pgo/runs.py` (E2000-HUGE_0350); `perf/turnbench/turnbench.py`,
  `perf/capacity/games.py`, `perf/profilekit/*.py`, `tools/harness/frontend.py`, `tools/instances/provision.py`,
  `tools/i0b/run_1900.py` (VP8P-HUGE_0350); `tools/scenario/*` and `models/tools/formations/catalogue_tsv.py` (CAT_0009);
  `tools/release/tiers/stages/modpack.py` (the T19 autosave; its 5.5 twin is in, ticket 0779). The many `*/evidence/*` scripts that name them are history.

**The laptop and oldpc** (0766's open item 2, done by ticket 0802 on 2026-11-26) [L]: VP 5.5 copies `tb1-55` (both
hosts) and `tb2-55` (oldpc) of profile `mods55` beside the 5.4.8 `tb1`..`tb6`, from bases items `vp55` (this tree, its
manifest and the two -ia DLLs) and `saves55` (the 5.5 saves above and 0771's huge and large ones), by `commission.py
create-instances --profile mods55 --names ...`; a job names the copy with `--instance`. How, the timings and the load
checks: quiet-testbed.md section 34.
