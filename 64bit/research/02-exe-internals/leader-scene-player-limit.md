# The 3D leader scene and player 21: where the EXE stops, and what drops the update hold above it

Ticket 0355 (epic 0334's F4, with 0335), 2026-11-15, implementer impl-B-0355 (account B), branch
`task-0355-leader-scene-player-21`. Static part: a read-only pass over a copy of the DX11 EXE (`%USERPROFILE%/tools/
ghidra-projects/bin/CivilizationV_DX11.exe`, sha256 b2ce96ae...c88e, TimeDateStamp 0x546CD0A8) with cdb 10.0.26100
(`cdb -z <copy>`, `u`/`uf`/`dds`) and small Python scanners for call targets and displacements. Live part: section 4.
Marks: `[C]` read in the EXE file's code, `[L]` seen in a running game, `[I]` inferred. Addresses are VAs at ImageBase
0x400000. Behaviour only; no disassembly or decompiler output is committed (D83).

**Ticket 0381 (2026-11-16, impl-B-0381, branch `task-0381-leader-scene-majors`) added section 7: the route that loads
the 3D scenes for majors 22-42 on the product path (the proxy calls the EXE's own slot 181), its live check on the
43-civ build, the 22-civ build unchanged, upstream's state, and the VP Lua test that still hides the scene.**

## 0. In short

1. **The 22 is one loop bound in the EXE** `[C]`. When a game is loaded or started, `LoadGameState::Update`
   (0x403170, the stage at 0x4035CB) calls the EXE's own `ICvEngineUtility1::NotifyAILeadersInGame` (engine utility
   vftable 0x9EA718 slot 180 -> 0x694160). That function, if the game is final-initialised, calls
   `NotifySpecificAILeaderInGame(i)` (slot 181 -> 0x68B370) for **i = 0..21 only** (the loop compares with 22). The
   DLL never calls `NotifyAILeadersInGame`; it calls `NotifySpecificAILeaderInGame` in one place only, when a
   disconnected human's slot passes to the AI (`CvPlayer.cpp` near line 49235 at `04b1211b8`, "Load leaderhead for
   this new AI player") `[C source]`. So **majors 22-42 never get their leader scene loaded** in a 43 game.
2. **Nothing else in the leader path is bounded by player id** `[C]`. `NotifySpecificAILeaderInGame(i)` skips the
   human players, players never alive and the active player, then posts one game-to-UI message with the player id and
   the leader's `ArtDefineTag` (128 bytes). Its handler (0x5985B0, on the leader-head object 0xDA2780) loads the scene
   by that name and stores it in a **`std::map` keyed by player id** (the object's +0x24, so 0xDA27A4; size at
   0xDA27AC), once per player. When a leader message is shown, 0x6EC820 looks the speaking player up in that map: if
   found, it makes it the current scene (0x6A2E00 writes 0xDA2780 + 0x680C = **0xDA8F8C**) and plays the message's
   animation; **if not found, it does nothing and the scene stays 0** (the case the EXE's debug texts "THERE IS NO
   SCENE LOADED FOR THIS LEADER!!" and "NO LEADER SCENE LOADED FOR PLAYER ID: %u. Somebody forgot to send the
   NotifyAILeaderInGame event." describe).
3. **The update hold does not depend on the scene** `[C]`. `GameplayDiplomacyAILeaderMessage` (0x51B1E0) raises the
   hold 0x2DE044C for any player id (no bound, no scene check); the only decrement (0x597590) is slot 2 of the
   leader view-state class (vftable 0xA1DAE0; slot 1 0x465240 is its enter, slot 3 0x6A36A0 its per-frame method),
   i.e. its exit: it drops the hold if set, clears the scene (`0x6A2E00(0)`) and returns, whatever the scene was. So
   for majors 22-42 **the hold is dropped the same way as for 0-21: by leaving the leader view** (`UI.RequestLeaveLeader`
   from the screen's close, which VP's `LeaderHeadRoot.lua` calls on both its 3D and its static-picture path).
4. **Live: no stall** `[L]`. 4255 saw it for player 30 on both arms of the 43 build (`research/03-dll-engine-boundary/
   player-limits.md` 8.4.9, 8.4.11); this ticket's run on the stock arm (section 4.1) read the EXE's state during the
   screens of players 22 and 21: the scene cache holds players 1-21 only, the scene stays 0 for player 22, and the
   hold rises to 1 and falls to 0 on leaving for both. Side effect: after a screen above 21 the EXE's debug text "THERE
   IS NO SCENE LOADED FOR THIS LEADER!!" stays at the top-left of later 3D leader screens (cosmetic).

## 1. Where the 22 is: `NotifyAILeadersInGame` [C]

| VA | What |
|---|---|
| 0x4035CB | in `LoadGameState::Update` (0x403170): after a readiness check (0x642EB0), `[[0xCC8E74]+0x1460]->NotifyAILeadersInGame()`, then sets `GameViewState+0x1C1` = 1 |
| 0x694160 | `NotifyAILeadersInGame` (engine utility slot 180): `ICvGame1::IsFinalInitialized()` (slot 45), then `this->NotifySpecificAILeaderInGame(i)` for i = 0..21 (loop end 0x16 at 0x6941AE) |
| 0x68B370 | `NotifySpecificAILeaderInGame(i)` (slot 181): `IsFinalInitialized`; player i: skip if `IsHuman` (slot 6), skip unless `IsEverAlive` (slot 21), skip if i = `GetActivePlayer` (game slot 3); then `GetLeaderHeadInfo(GetLeaderType())->GetArtDefineTag()` copied (128 B) into a message with the player id, posted through 0x4648F0 on the double-buffered game-to-UI table (0x2A10E08 + 0x4C x [0x2A10200]) |

Interface slots from `proxy/model/policy-table.tsv` (`slot_x86`). The only `call` through slot 180 in `.text` is
0x4035DF; the only one through slot 181 is the loop at 0x6941AB (a scan for `mov reg,[reg+0x2D0/0x2D4]; call reg`;
`[I]` that no other form of call exists, as for every such scan).

## 2. The scene cache and the current scene [C]

| VA | What |
|---|---|
| 0xDA2780 | the leader-head scene object (also `camera-and-picking.md`: its two FCameras at +0x6810 / +0x68E8) |
| 0xDA27A4 (+0x24) | `std::map<player id, scene>`: head node at +0x28 (0xDA27A8), size at +0x2C (0xDA27AC); MSVC 2008 node: key +0xC, scene +0x10 (layout confirmed live, section 4) |
| 0x5985B0 | handler of the "AI leader in game" message: unless a UI query (object from 0x4455D0, its slot at +0xD4) says so, if the player is not in the map, load the scene by its art-define name (0x41C960, or 0x473DB0 when the byte 0xD90F10 is set) and insert it |
| 0x6EC820 | on a leader message (the message's player at +4, animation at +0x40C): the same UI query; find the player in the map; if found, set it as the current scene and play the animation (0x672400); if not found, return |
| 0x6A2E00 | set current scene: writes +0x680C (= **0xDA8F8C**; its only absolute reference is the per-frame check 0x6A36D4) |
| 0x6047A4 | the object's constructor zeroes +0x680C |
| 0x634200 | a synchronous load-by-name (wait cursor, 0x41C960 / 0x56CE80) used by a family of methods with fields at +0xB66/+0xB67/+0xB94/+0xB98 (0x43CFE0, 0x4C1990, 0x4CB050, ...): by its shape a scene viewer, not the per-player path `[I]` |

## 3. The hold for majors 22-42 [C]

- **Raised**: 0x51B1E0, `GameplayDiplomacyAILeaderMessage(player, state, text, animation, data)`, returns at once
  unless `IsFinalInitialized`, and also when `GetVictory() != -1` and `GetGameState() == 1`; copies the text (1 KB);
  if no leader is current (`[manager+0x1E4] == -1`) it makes `player` current and, when the UI query above returns
  false, raises the hold (`0x769FB0(1)`), else marks a different UI table and does not hold; then posts the message.
  A message for another player while one is current is queued at `[manager+0x1E8]`. **No comparison with a player
  bound and no look at the scene cache** (corrects nothing in `game-core-thread.md` 2; adds the two early checks it
  left open: `IsFinalInitialized`, and the game-over pair `GetVictory`/`GetGameState`).
- **Dropped**: 0x597590, slot 2 of vftable 0xA1DAE0 (a view-state class: slot 1 0x465240 enter, slot 3 0x6A36A0
  per-frame): if the hold is set, `0x769FB0(0)`; then always `0x6A2E00(0)` (clear the scene) and a call on the member
  at +0x5A4. It reads neither the player nor the scene.
- **So**: for a major above 21 the screen comes up with no 3D scene (scene pointer 0; VP's Lua covers the view with
  its static picture, `player-limits.md` 3.2), the update is held while it is up, and leaving the leader view drops the
  hold exactly as for a 3D leader. Nothing in the EXE ties the hold to the scene having loaded.
- **Correction to `research/05-graphics-composition/unit-state-ii.md` (0335's [I])**: "the EXE raises
  `AILeaderMessage` only once the scene is ready" is not what the code shows for this path: a player with no cached
  scene still gets the screen (4255 saw `AILeaderMessage` reach `LeaderHeadRoot` for player 30, `player-limits.md`
  8.4.9). Why the screen did not come up in 0317's Unit-key arms is still open there.

## 4. Live [L]

**4.0 The cache layout, one read [L, 2026-11-15 21:26 EST].** `leader_probe.py PID` (one sample, read-only) on
four running games of other tickets: three had an empty cache (scene 0, hold 0, current -1; autoplay or D33 runs, not
looked into); the game on c5h-e (pid 49076) had `cache_n` 7 and the tree walk found the keys `1 2 3 4 5 6 7`, the size
and the walk agreeing: the AI majors of an 8-player game without the human in slot 0, as section 1 predicts. That
confirms the map's place and node layout.

**4.1 The 43 run: players 22 and 21 [L, 2026-11-15 21:47-21:52 EST].** Instance c5h-h (pool owner impl-B-0355),
the stock 32-bit EXE with the 32-bit 43 DLL `i0ship-26234371-m43-ia` (4252; sha256 204fbfcc..., installed by
`provision.py set-dll`, the pin `i0ship-26234371-ia` 3c2f47da... put back afterwards; a comparison arm in the sense of
COMMON 18, the 43 build has no pinned layout), mods cp, vp, eui. Save: a copy (`0355-SMALL43-T000`, deleted
afterwards) of 4255's turn-0 save `4255-SMALL43-PP-T000` (sha256 9c2d9b7c...; Huge Continents, 30 majors in slots
0-29, the human Carthage in slot 0, ids 21 Celts / Boudicca and 22 England / Elizabeth). Command:
`python -I research/03-dll-engine-boundary/evidence/0355/run_live.py c5h-h --save 0355-SMALL43-T000 --out
F:/c5-64bit-work/0355/live --mods cp,vp,eui --players 22,21 --hold 30`, exit 0 (4255's driver unchanged, its shots
filed under 0355; `leader_probe.py` every 0.5 s, 372 samples, 0 failed reads; EXE pid 67536).

| Moment | hold 0x2DE044C | scene 0xDA8F8C | current leader | scene cache keys |
|---|---|---|---|---|
| game loaded (21:49:03) | 0 | 0 | -1 | 1-21 (21 entries) |
| player 22: first-contact greeting (21:49:06-21:49:36) | 1 | **0 throughout** | 22 | 1-21 |
| player 22: denouncement inside its AI turn (21:49:45-21:50:15) | 1 | **0 throughout** | 22 | 1-21 |
| player 21: greeting (21:50:30-21:51:00) | 1 | 0x8E6D2770 | 21 | 1-21 |
| player 21: denouncement in its AI turn (21:51:08-21:51:39) | 1 (scene 0 for one sample, then set) | 0x8E6D2770 | 21 | 1-21 |
| after each screen | 0 | 0 | -1 | 1-21 |

- **The cache holds exactly the AI majors 1-21**: the human (slot 0) is skipped and players 22-29 are never loaded,
  as section 1 reads. It did not change during the run.
- **Player 22 (above the bound)**: the hold rose to 1 for each message, the scene pointer stayed 0 (no 3D scene: the
  map lookup of section 2 failed), the diplomacy manager's current leader was 22, and **the hold fell to 0 when the
  harness's diplomacy handler closed the screen** after 30 s; the AI turn completed (turn 0 -> 1, the human active
  again) 50.6 s after the end of turn. The channel was silent 28.1 s (greeting) and 24.9 s (denouncement). VP's three
  screens logged `LeaderMessageHandler: Player ID > 21` for both messages (6 lines in `Lua.log`), and the screen showed
  VP's static full-screen picture of Elizabeth with the message and its two answers (Visual Checks feed, issue 0355).
- **Player 21 (control, below the bound)**: the same hold and release, the scene pointer set to the cached scene while
  the screen was up and cleared on leaving (section 3: the exit clears it); the turn completed (1 -> 2) 44.9 s after the
  end of turn; silent 28.0 s and 27.8 s. Then the harness's safe stop, rc 0.
- **So the outcome on the 43 build is: no stall** `[L]`, with the mechanism seen live: for a major above 21 the hold is
  raised and dropped exactly as for one below, and only the scene is missing. This agrees with 4255's two arms
  (`player-limits.md` 8.4.9, 8.4.11: player 30, stock and product path).
- **A visible side effect** `[L]`: after a screen of a major above 21, every later 3D leader screen in the session
  carries the EXE's debug text "THERE IS NO SCENE LOADED FOR THIS LEADER!!" in small white letters at the top-left
  corner (this run's Boudicca shots after Elizabeth; 4255's Elizabeth-of-player-5 shot after Attila-of-player-30 in
  `F:/c5-64bit-work/0334/4255/stall43-stock-p5/leader-p5-ai.png`; absent in its 22-build shots). The per-frame method
  0x6A36A0 writes it while the scene is 0; that the text then stays on screen is `[I]` (a debug-text buffer that is not
  cleared). On the static-picture screens themselves VP's texture covers it. Cosmetic, stock 43 games too.
- Restore: front end restored by hash, the save copy deleted, the pinned DLL back (3c2f47da...), `provision.py verify
  c5h-h --quick` ok with no warnings, `provision.py snapshot` before/after: no differences; c5h-h released 21:53.
  Evidence (not committed): `F:/c5-64bit-work/0355/live/` (`summary.json`, `probe.csv`, `probe.log`, per player
  `stall.json`, `timeline.csv`, shots; `Lua.log`), `verify-*.json`, `snap-*.txt`.

## 5. What a 3D scene above 21 would need [I]

*Done on the product path by ticket 0381 (2026-11-16), from the proxy instead of the DLL: section 7. The text below is
0355's reasoning, kept; its "would load if something called NotifySpecificAILeaderInGame(i)" is now [L] (7.3).*

Not a stall fix (there is no stall): a feature. The EXE side is player-agnostic past the loop (a map by player id), so
the scenes for 22-42 would load if something called `NotifySpecificAILeaderInGame(i)` for them once the game is
final-initialised and the players exist. The DLL can do that through `gDLL` (it already does for one case), for
example at game start and after a load, for i = 22..MAX_MAJOR_CIVS-1; VP's Lua fallback `iPlayer > 21`
(`player-limits.md` 3.2) would then have to test for a loaded scene instead of the id. Cost not measured: every
cached scene stays in the 32-bit EXE's address space for the whole game (the stock EXE caches up to 21), so 21 more
scenes are a memory question first (`allocations-and-memory-map.md`; the 43-civ stock arm had 646 MB free at turn
180, `player-limits.md` 8.4.8). Only on the owner's word; no ticket is proposed for it here.

## 6. Not verified

- What the UI query (the object from 0x4455D0, slot at +0xD4) is; when it is true no scene is loaded or set and no
  hold is raised.
- What consumes 0x418C30's 0-21 summary event (`player-limits.md` 3.3): not traced in this pass.
- ~~That the scene loads would work for ids above 21 if they were sent (section 5): read, not run.~~ Run by 0381
  (2026-11-16): they load and show, section 7.3 [L].
- The probe run was on the stock arm only; the product path (proxy + x64 server) was not rerun here (4255 8.4.11 ran
  it for player 30 with the same outcome; the hold, the cache and the scene are inside the EXE either way `[I]`).
- Why the debug text stays on screen after the scene-less screen (4.1).

## 7. The route on the product path: the proxy asks the EXE for the scenes above 21 (ticket 0381)

Written 2026-11-16 13:30 EST by impl-B-0381 (account B), branch `task-0381-leader-scene-majors`. Workstation runs on
the Goldberg instance c5h-b; nothing here is a timing claim (COMMON 23).

**What is current (corrected 2026-11-16 14:15, after the MC returned the first version for its memory cost, 7.8).**
The job is **off by default**. It has three modes (7.10):
- `vpproxy.ini` `leader_scenes = 0`, the default: the stock behaviour.
- `1`, on demand: a scene is asked for only when that leader's screen opens.
- `2`, all at load: 7.1-7.5 below, the first version, kept for comparison.

`VPPROXY_LEADER_SCENES=0|1|2` overrides the ini. Sections 7.1-7.8 describe the first version, which was always on,
and its runs; read "the job" there as mode 2. (Replaced: "the job" always on; its `VPPROXY_LEADER_SCENES=0` was the
only switch.)

**7.1 The route [C: this repository's code; the EXE's behaviour from sections 1-2].** No EXE code byte is read,
compared or written (D4, D83); two things only:
- **A call of the EXE's own exported behaviour.** `ICvEngineUtility1::NotifySpecificAILeaderInGame(i)` (engine utility
  slot 181, 0x68B370) is the call the EXE's own loop makes for 0-21 and the DLL makes for a hot-joined slot (section 1).
  The proxy holds the engine utility the EXE passed to `SetDLLIFace` and calls slot 181 for the players the EXE skipped.
  The call's own checks stay the EXE's (final-initialised, human, ever alive, active player).
- **A data read** of the scene cache (the `std::map` at 0xDA27A4, section 2): which players have a scene, and when a new
  game session empties it. Nothing of the EXE is written.

Where and when (`proxy/src/proxy_leaderscene.cpp`, called by one line in `ProxyRtInt::BeforeUpdate`,
`proxy/src/proxy_update.cpp`): on the EXE's game-core thread at the top of each `ICvGame1::Update`, the thread the DLL
itself calls slot 181 from.
- The cache is read at most once a second, rebased on the EXE's load address. The walk is bounded (64 nodes) and
  validated (the head node is nil, keys 0..63 without duplicates, the count equals the size) and runs under SEH: a
  fault is counted, and after 5 the job turns itself off.
- The slots are evaluated when the cache's keys change, else every 10 s, and only when the cache is not empty (the EXE
  loads scenes in this session) and the game is final-initialised.
- Asked: slots 22..62 (`MAX_CIV_PLAYERS - 1`) missing from the cache whose player exists, is ever alive, not human, not
  the active player, and not a city state or the barbarians.
- The major/minor split is the DLL's answer at run time (`ICvPlayer1::IsMinorCiv`/`IsBarbarian`, one crossing per slot,
  cached by the slot's civilisation type), never the compiled `MAX_MAJOR_CIVS` (issue 4253). With the 22-major DLL every
  slot from 22 is a city state, so nothing is asked (7.4).
- Each player is asked at most twice per game session, 30 s apart; then one "still have no scene" line.
- A cache that loses entries (exit to the menu, a new load) starts a new generation: everything is asked again.
- Gates: the EXE's TimeDateStamp 0x546CD0A8, an image large enough, and the cache's RVA 0x9A27A4 inside a writable,
  non-executable section; otherwise one `off:` line. The 32-bit headless host fails the timestamp, so it is off there.
  The stock route (`mode=off`) never runs the proxy's Update hook.
- Test switch: `VPPROXY_LEADER_SCENES=0` in the EXE's environment (logged as `leaderscene: off:`).
- Every action is one `leaderscene:` line in vpproxy.log.

Tests [L, 2026-11-16 12:58]: `proxy/tools/rt_tests/leaderscene_unit.cpp`, run by `proxy/evidence/0381/run_unit.py`.
- 1534/1534 checks with the proxy's toolchain and again under MSVC `/fsanitize=address`; the canary's overflow is
  reported, so ASan is active.
- The cache walk is tested against MSVC-2008-layout trees in memory, with its refusals: a size mismatch, a duplicate,
  an out-of-range key, cycles, a NULL link, nil flags, and a decommitted node, caught as a fault.
- The EXE check is tested against a fake image (timestamp, size, the section's flags, no section, no signature,
  unreadable).
- The driver is tested against a simulated EXE and DLL: the 22 build asks nothing, the 43 build asks 22..41 once; the
  human, the active player, never-alive and city-state slots are never asked; there are tests for the clock, an empty
  cache, a game not final-initialised, a scene that never loads (twice, then given up), a new session, a scene loaded by
  the DLL, a bad read and five faults.

**7.2 Build of the live runs.**
- server.exe and the proxy are built from this branch at ca632075, plus the driver commits (no source change):
  `server/build_server.py --src <archive of the fork at 04b1211b8> --config release --offline --deps
  F:/c5-64bit-work/0334/rebuild/m43/deps`.
- The proxy is sha256 67f3d77c..., server.exe b8c71335....
- The x64 DLL is release-43 cf981126... (`deps/dll.lock`).
- The composite is `shell/build.cmd` of this branch (4f638ca3...).
- Installed with `research/03-dll-engine-boundary/evidence/0334/4255/pp_4255.py prep`: the D33 product path, a
  **development layout** (`pack=0`), not the shipped pack. Every changed file was backed up by SHA-256 first.
- The save is a copy `0381-SMALL43-T000` of 4255's `4255-SMALL43-PP-T000` (sha256 9c2d9b7c...): Huge Continents, 30
  majors in slots 0-29, the human Carthage in slot 0. Mods cp, vp, eui.
- Driver: `proxy/evidence/0381/run_0381.py`. It runs 4255's `leader_stall.py --mode now` per player; each screen is
  closed by the harness's diplomacy handler after 20 s, and 0355's `leader_probe.py` samples every 0.5 s.
- A per-frame helper in the UI states `LeaderHeadRoot`, `DiscussionDialog` and `DiploTrade` can hide VP's
  `BackupTexture`, so that the 3D scene behind it can be seen. This is a diagnostic: no file is changed (MC #2670).

**7.3 Live on the 43-civ build: the scenes load and show [L, 2026-11-16 13:10-13:18 EST].**
- **The job asked at the first Update.** vpproxy.log: `[76.06 s] Update hook: first ICvGame1::Update`, `[76.08 s]
  leaderscene: on: the EXE's scene cache at 0x012c27a4`, then `[76.08 s] leaderscene: the EXE's scene cache holds 21
  scenes (players 1-21); asked the EXE to load players 22-29 (NotifySpecificAILeaderInGame); generation 1`. That was the
  only request.
- **The EXE was loaded at 0x920000, not at 0x400000, in this run** (cache VA 0x012C27A4 = 0x920000 + RVA 0x9A27A4) [L].
  Code that reads EXE data must rebase on the module's load address, as the proxy and `leader_probe.py` do.
- **The cache** held players 1-29 (29 entries) 10 s after the game came up, and kept them for the whole session: 481
  probe samples, all valid.
- **Players 22-25 opened diplomacy** (first-contact greeting, then a denouncement: 8 screens). Each held the game
  update 16-18 s and was closed by the handler.
- **The scene pointer 0xDA8F8C was set to that player's scene while the screen held the game:**
  - 22: 0x900692D0;
  - 23: 0x90067EC0;
  - 24: 0x900674B8;
  - 25: 0x90066AB0.
  - For 23 and 24 there were 2 and 1 samples of 0 at a screen's start: the hold is raised before the scene is set,
    as 0355 saw for 21.
- With the job off (7.5) the pointer stays 0 for player 22.
- **With the helper hiding VP's texture** (players 23 Sejong, 24 Harald, 25 Genghis Khan), the shots show the EXE's
  animated 3D leader scenes, and no debug text at the top left. These are diagnostic pictures in the Visual Checks feed,
  issue 0381.
- **Without the helper** (player 22 Elizabeth) the player sees VP's static picture, as before. VP's Lua logged
  `LeaderMessageHandler: Player ID > 21` 24 times: 4 players x 2 messages x 3 screens.
- **Player 21 (Boudicca)**, after the eight screens above 21, shows its 3D scene **without** "THERE IS NO SCENE
  LOADED FOR THIS LEADER!!" at the top left. With the job off, the text is there (7.5).
- The harness's safe stop ended the session, rc 0.
- **A second launch** (13:29-13:33, `live43-on2`, the same request line and cache 1-29) gave the same results:
  - 26 Darius showed scene 0x91C50C58 and 27 Washington 0x91C53478, each with one sample of 0 at a screen's start.
  - 5 Pedro's 3D scene was 0x883ADB28.
  - So 6 majors above 21 were seen with their scenes in all.

**7.4 The 22-civ build: unchanged [L, 2026-11-16 13:42-13:46 EST].**
- Same branch server and proxy, with the 22-major x64 DLL of the pin (29f58e17..., `x64-04b1211b-pfdee656`).
- Save: a copy `0381-DRY22-T000` of 4255's `4255-DRY22-STOCK-T000` (sha256 21c9a3fb...), mods cp, vp, eui and the
  Hormigas map pack.
  - Two first attempts without the map pack died loading the save, before any Update: the DLL's check "Failed to
    prepare database query" (`CvDatabaseUtility.cpp:80`, after `ShouldValidateGameDatabase: bad handle`).
  - The save needs the pack's tables. A harness/mod-list matter, not this job, which runs only from the first Update.
- vpproxy.log: `leaderscene: on: ...` and **no request**.
- The cache held players 1-20 for the whole session (235 probe samples, all valid).
- Players 5 (Elizabeth) and 7 (Bismarck) opened diplomacy with their 3D scenes (scene pointers 0x7E72B450 and
  0x7E72E678 while held, no zero sample). VP's Lua logged no `Player ID > 21`.
- By design (unit test 7.1) the job's only cost in this build is one `IsMinorCiv` crossing per ever-alive slot from 22,
  once per session, then a cache read a second.
- The stock 32-bit route never loads the proxy, so it is unchanged by construction.

**7.5 The negative control: the same build with the job off [L, 2026-11-16 13:21-13:25 EST].**
- `VPPROXY_LEADER_SCENES=0`, same layout, save and driver (`--expect-off`).
- vpproxy.log: `leaderscene: off: VPPROXY_LEADER_SCENES=0 (test switch)`, and no request.
- The cache held 1-21 only.
- Player 22's screens: the scene pointer was 0 in all 80 held samples, and VP's static picture showed.
- Player 21's 3D screens after them **carry the debug text at the top left**: 0355's symptom, reproduced on the product
  path.
- So the difference between 7.3 and 7.5 is the job.

**7.6 Upstream [C source, 2026-11-16].** `git grep` over upstream `LoneGazebo/Community-Patch-DLL` `master` at
1fb49429f (2026-11-16 02:35 EST, fetched into the fork clone) shows:
- the DLL still calls `NotifySpecificAILeaderInGame` in one place only (`CvPlayer.cpp:49670`, the hot-joined slot);
- the six `iPlayer > 21` tests listed in 7.7 are unchanged.

Upstream's 43-civ build is the stock EXE with this DLL and this Lua. So it shows what 7.5 and 0355's stock arm (4.1)
show: no 3D scene for majors 22-42, VP's static picture, and the debug text on later 3D screens.
- Not run with upstream's own release DLL. The EXE side is the same for any DLL (sections 1-2), and 0355's stock arm
  used `i0ship-26234371-m43-ia`, which carries upstream's code for these paths.

**7.7 What still hides the scene from the player: VP's Lua (out of 0381's scope; MC #2670).** The leader screens test
the id, not whether a scene exists. At upstream master 1fb49429f and the pin 04b1211b8:
- `(1) Community Patch/LUA/LeaderHeadRoot.lua:94` `if iPlayer > 21 then`
- `(1) Community Patch/Core Files/Overrides/DiscussionDialog.lua:111` (the same)
- `(1) Community Patch/Core Files/Overrides/Includes/TradeLogic.lua:224`
- `(2) Vox Populi/Core Files/Overrides/DiscussionDialog.lua:111`
- `(3a) VP - EUI Compatibility Files/LUA/LeaderHeadRoot.lua:121`
- `(3a) VP - EUI Compatibility Files/LUA/TradeLogic.lua:238`

Each prints `LeaderMessageHandler: Player ID > 21` and shows a full-screen `BackupTexture`. **Proposed test [I]:**
"no 3D scene for this player" instead of "id above 21". Lua has no binding for the EXE's scene cache. So the test
needs a value that whoever loads the scenes publishes, for example a `Defines` row (read through `GameDefines`) that the
64-bit path or the DLL sets when it asks for scenes above 21:

    if iPlayer > 21 and GameDefines.LEADER_SCENES_ALL_MAJORS ~= 1 then

Two other forms:
- An upstream DLL change that asks for 22..`MAX_MAJOR_CIVS`-1 itself at load (the call of 7.1 made from the DLL). The
  test then becomes `iPlayer >= GameDefines.MAX_MAJOR_CIVS`, which never holds, but it costs the 32-bit EXE the memory
  of 7.8.
- A patch-shipped override of those files.

The MC opens the follow-up (upstream draft after beta, D95/D130, or an override).

**7.8 Memory [L, workstation indication, 2026-11-16 13:21-13:33 EST: one launch per arm, not a measurement series].**
The EXE's memory (`tools/stressmap/calibrate.py mem`, one sample a second) is compared over the last 10 samples once
the cache had settled, at the human's turn 0, between two launches of the same save and layout: the job off (7.5,
cache 1-21) and the job on (a second launch, `F:/c5-64bit-work/0381/live43-on2`, cache 1-29).
- **Free address space: 1,798.5 MB off against 1,541.2 MB on (-257 MB for 8 scenes, about 32 MB each).** The largest
  free block went from 1,751 to 1,498 MB.
- Committed: 1,905 against 1,973 MB (mean; the on arm's minimum was 1,923 MB). Reserved: 393 against 582 MB.
- So most of a scene is reserved address space, not commit.
- Private bytes in the on arm rose from about 1,224 MB to 1,829 MB in the 12 s after the request and fell back to
  1,594 MB once the loads ended (load-time buffers). The off arm ended at 1,557-1,585 MB.
- For a 42-major game, 21 more scenes would cost roughly 21 x 32 = 670 MB of address space [I: linear in the number of
  scenes, not run]. 4255's 42-major Hormigas game on the product path had 1,613 MB free at turn 180
  (`player-limits.md` 8.4.10), so about 940 MB would be left.
- On the stock 32-bit route (646 MB free at turn 180 there) the same scenes would not fit for long. This is why the
  route lives in the proxy, which only the 64-bit path loads, and why an upstream DLL version (7.7) needs care.
- Not measured: the cost over a long game, or with all 42 majors.

**7.9 Not verified.**
- Multiplayer and hot seat: the active player changes, and the EXE's call skips it. The job's evaluation follows
  `GetActivePlayer` at each evaluation [I].
- A save loaded from inside a running game (the generation change): the unit test covers it, the live runs did not.
- The tablet and DX9 EXEs: the job turns itself off (timestamp gate) [C], not run.
- The shipped pack (`pack=1`): the runs used the development layout (7.2). The job does not depend on the layout [I].
- Whether the EXE ever frees scenes during a game (for example when a player dies): not looked at.
  - If it does, the job reads the lost key as a new session and asks again for that player only: the others are still
    in the cache, and the player is still ever alive and not human.
  - That would load the scene back (once; the EXE's own 0-21 would stay freed).
  - Harmless but not stock-like [I]. The 0355 and 0381 runs saw no key leave the cache during a session.

**7.10 On demand (mode 1) and the decision [C code; L, 2026-11-16 14:05-14:10 EST].**

*Decision (MC, 2026-11-16, ticket 0381's return).* Off by default, so nothing changes for players now. The reasons:
- each scene costs the 32-bit EXE's address space (7.8), and that is what runs out first on large games;
- the player does not see the scenes until VP's Lua test changes (7.7).

`leader_scenes=1` loads a scene only for a leader whose screen opens.

*Which path each leader screen takes [C source, the fork at 04b1211b8].* Every AI leader screen is opened by the DLL's
`gDLL->GameplayDiplomacyAILeaderMessage(player, ...)`:
- **The AI's own messages**: greetings, denouncements, requests, trade offers (`CvDiplomacyAI`).
- **The player opening an AI himself**: Lua `Players[i]:DoBeginDiploWithHuman()` -> `CvDiplomacyAI::DoBeginDiploWithHuman`
  (CvDiplomacyAI.cpp:36907) -> `GameplayDiplomacyAILeaderMessage(..., DIPLO_UI_STATE_DEFAULT_ROOT, ...)`. Lua calls it
  from:
  - the diplomacy list (`DiploList.lua:160`, VP);
  - the city banners (`CityBannerManager.lua:1284`, VP; EUI `:873`);
  - the relationship screens (`DiploRelationships.lua:265`, `DiploGlobalRelationships.lua:463`);
  - the league screen (`LeagueOverview.lua:1054`);
  - EUI's notification panel (`NotificationPanel.lua:1296`).
- **Espionage and "discuss" variants**: `DoBeginDiploWithHumanEspionageResult` and `DoBeginDiploWithHumanInDiscuss`
  call the same function (from `CvNotifications.cpp:1003`, `:1022`).
- **The trade screen with an AI** opens inside that leader view (LeaderHeadRoot's Trade button, `DoTradeScreenOpened`).
- `Events.OpenPlayerDealScreenEvent` (DiploList's branch for a human player) is the human-to-human deal screen, which
  has no leader scene.

So **one hook covers every path**:
`EngHook_ICvEngineUtility4_GameplayDiplomacyAILeaderMessage` in `proxy_leaderscene.cpp`, on the DLL -> engine side of
the proxy. In mode 1, when the player is 22..62 and not in the cache, it calls slot 181 for that player first. It copies
the call's remaining arguments before that, because the call's nested calls into the DLL reuse the channel's payload.
Then it forwards the message unchanged; in modes 0 and 2 it only forwards.
- The EXE's own checks in slot 181 still apply: human, never alive, active player.
- City states never get a leader message.
- The EXE's own players 0-21 are never asked.
- Only the AI-initiated paths (first contact, a denouncement) ran live; the player-initiated ones share the call by
  source [C], not run.

*Live (43-civ build, the save copy of 7.2, `leader_scenes=1` set in vpproxy.ini with `set_ini.py` and unset
afterwards).*
- Proxy 24abb8f4..., built from 4665dae8; the driver's edit made its build id dirty=1, but no proxy source changed.
- Server 3f8809b9..., composite 7091499e....
- `run_0381.py --expect-demand --plan 22:1,23:1,24:1,21:0`, out `F:/c5-64bit-work/0381/demand43`, exit 0.
- vpproxy.log `[57.39] leaderscene: on: vpproxy.ini leader_scenes = 1 (on demand) ...`; no request at load.
- The cache held 1-21 until the first screen.
- Then exactly one request per player:
  - `[69.10] player 22 opens a leader screen without a scene (cache 21, read 0): asked the EXE for it first (on demand,
    request 1)`;
  - `[113.03]` for 23 (cache 22);
  - `[157.07]` for 24 (cache 23).
- **The scene was there on the first screen.** The scene pointer while held: 22 0x835ED658 and 23 0x835EC248, with no
  zero sample; 24 0x835ECC50, with one zero sample at the start, as for every player in 7.3.
  - So the load request and the message are handled in that order on the EXE's UI thread [L].
  - The greeting screens show the 3D leaders (diagnostic shots, VP's texture hidden by the helper).
- **The debug line:** no "THERE IS NO SCENE LOADED" text on any screen of 22-24, nor on player 21's screens after them
  (top-left corners checked).
- In mode 1 every leader that opens a screen has a scene, so the EXE never writes the text.
- **In mode 0 (the default) the text stays as in stock** (7.5). Clearing it without a scene would need a write into the
  EXE's debug-text state, which is not traced (where it is kept, and why it stays, are open, 6). **There is no
  debug-line fix without loading the opened leader's scene.**
- **The EXE does not free them during the session.** The cache held 1-24 from the third screen to the end, 400 probe
  samples, none failed. In 0355's and this ticket's runs no key ever left the cache in a session [L].
- **Address space for 3 opened leaders** [L, an indication from one launch]. Free address space went from 1,873.2 MB
  before the first screen to 1,805.8 MB after the third: -67.4 MB, about 22 MB a leader. The largest free block went
  from 1,816.6 to 1,747.8 MB.
  - This includes the diplomacy screens' own use (private and committed bytes both rose by 91 MB over the three
    screens, with reserved bytes -24 MB). It is an upper bound for the scenes, not a clean per-scene cost.
  - Mode 2's 8 scenes cost 257 MB (7.8). In mode 1 a game pays only for the leaders above 21 that the player actually
    meets on a screen, and once each.
- Not run: mode 1 on the 22-civ build (no leader above 21 exists there, so the hook only forwards [C]); the
  player-initiated paths live; a reload inside a session.

Tests: `leaderscene_unit.cpp` adds `DecideMode` and `DemandWants`, for 1554/1554 checks plain and under ASan
(`run_unit.py`, run_unit: OK, 2026-11-16 13:59). The cases:
- no ini, no key, the values 0/1/2, comments, the last key wins;
- bad values, which turn it off and are named;
- the environment over the ini, both ways;
- 0-21 never asked; 22-62 asked when not cached or when the read failed; 63 never.

`proxy_rt_logic.cpp` knows the key, so the ini parser no longer reports it as unknown.

## Sources

0381 (section 7): the code in `proxy/src/proxy_leaderscene.{h,cpp}`. Tests and drivers are in `proxy/evidence/0381/`:
`run_unit.py`, `run_0381.py`, `README.md` (the commands) and `acceptance.txt`. Run folders, not committed:
`F:/c5-64bit-work/0381/` (`live43b`, `live43-off`, `live43-on2`, `live22c`: `summary.json`, `probe.csv`,
`vpproxy.log`, `Lua.log`, per-player `stall.json` and shots).

The EXE copy above (read only). The scanners are in `research/03-dll-engine-boundary/evidence/0355/`: `xref.py` (direct
calls to a VA, any 32-bit field equal to a value, `mov`/`call` through a vtable offset) and `xref2.py` (`mov reg,
[reg+OFF]` followed by `call reg`: the virtual calls through one slot); they print addresses only. The cdb command
files (`.symopt+0x40`, `uf <VA>`, `dds <VA> L8`, run as `cdb -z <EXE copy> -cf <file>`) lived in
`C:/c5-64bit-work/0355/` (scratch, removed at the hand-back). The fork clone `%USERPROFILE%/Documents/GitHub/Community-Patch-DLL` at `04b1211b8` (`CvPlayer.cpp`,
`ICvDLLUtility.h`, `(1) Community Patch/LUA/LeaderHeadRoot.lua`). `proxy/model/policy-table.tsv` for slot numbers.
Probe: `research/03-dll-engine-boundary/evidence/0355/leader_probe.py`.
