# A save loaded with a different set of mods (ticket 0681, 2026-11-22)

Scripts and inputs: `server/evidence/0681/`. Nothing of the owner's save or logs is committed; the save is named by its
hash only. Section 1 is the diagnosis, 2 what a header can tell, 3 the stock game, 4 the server's check (the code),
5 the commands, 6 the end at the DLL's first deserialization error (ticket 0744, K31).

## 1. The crash of 2026-11-22 20:53 and its cause

- **Report**: `%USERPROFILE%/Documents/My Games/Sid Meier's Civilization 5/VP64/reports/20261122-205321` (test build
  0.1b3-UNTESTED-main-0d3114c03, EXE build 26577625): stage server-death, code engine-exception, the failed call
  `ICvPlayer1::Read(FDataStream &)`, the EXE's `StreamRead` (served call `rtE:StreamRead` 0xFFF10010) threw a C++
  exception 76 s after the start. [C: report.txt, vpproxy.log]
- **Which save** [C]: the server's line `save variant: save "Release-5.4.8-80-g1b87dd185 Copy" ... database at 22556`
  names the body's version string and the offset of its embedded database. `save_body_id.py` over the owner's save
  folders (read-only) finds exactly one save with that pair: a `ModdedSaves/single` turn-58 save written 2026-11-22
  15:26:47, SHA-256 `f1f153f4c8baa2055ae6855b6116e587e5d2702cc055248b0b64a4ed9265c29d`, body 19,332,646 B (its
  database size field at 22552, the magic at 22556). The version string is the x64 DLL pin 1b87dd185 of 0.1b2 and 0.1b3:
  **the save was written with the patch ON**.
- **Mods** [C: the run folders' `server.log` line `decals: mods: N activated in ... Civ5ModsDatabase.db`]: the session
  that wrote it (run 20261122-152245) had 9 activated mods: (1) Community Patch v151, (2) Vox Populi v17, (3a) VP - EUI
  Compatibility Files v1, (4a) Squads for VP v1, InGame Editor+ v41, Quick Turns v10, **VernsImprovedCommanderInfluenceBorders
  v1, VernsTweaks v1, Wake All and Upgrade All Units In Military Overview v1**. The crash session (run
  20261122-205204) had the first 6. All three missing mods have `AffectsSavedGames` 0 in their `.modinfo`, so **the save
  header does not list them**: its mod list is CP v151 and VP v17 only, the same as the active set's save-affecting mods
  (`save_mods_scan.py`). The EXE therefore saw no mismatch and loaded the save with the mods that were active.
- **The database difference** [C: `db_diff.py` on the two sessions' `db/Civ5DebugDatabase.1.db`, the engine database
  the proxy snapshots at the load]: 45,334 rows (15:12 session, 9 mods) against 45,101 (crash session): UnitPromotions
  420 / 419 (VernsTweaks adds `PROMOTION_BT_MOVEMENT`), Unit_FreePromotions 950 / 735, Unit_Builds 83 / 66,
  CustomModOptions SQLITE_LOGGING 1/0, GLOBAL_PURCHASE_FAITH_BUILDINGS_IN_PUPPETS 1/0, BALANCE_RECON_ONLY_ANCIENT_RUINS
  0/1, Defines MINOR_MAX_XP_VALUE 999/70.
- **Offline reproduction** [C, 2026-11-22 21:30-21:40, `repro_offline.sh`]: the x64 server in direct mode
  (`server/run_server.py --load`) with the 0.1b3 binaries copied from the install and checked by hash (server.exe
  `b4098124...`, x64 DLL `4c5da20e...`), a copy of the save, and each engine database:
  - the 15:12 database: loads, body used up exactly (19,332,646 B, game section 27,676 B);
  - the crash session's database: `EXCEPTION 0xE06D7363 in load` (the server's memory stream raises at its end; the
    same read past the end as the EXE's `StreamRead` live).
  Bisected with `db_variants.py` (copies of the crash session's database with parts of the other): the three option
  values and the Defines value alone change nothing (all fail); **the one UnitPromotions row alone makes it load**
  (with or without the Unit_FreePromotions and FriendlyLandsPromotion rows); Unit_Builds alone fails.
- **The DLL's own check** [C: `CvGame.cpp:11004-11017` and `CvGlobals.cpp:4604` at 1b87dd185; the crash session's
  `Logs/custommods.log`]: `CvGame::Write` writes, after the u32 save version, a 128-bit **game data hash** (FNV-1a over
  the count and the `Type` names of about 75 info tables, promotions among them); `CvGame::Read` compares it with the
  running one and only logs `WARNING - Save data hash mismatch! Load will be attempted but corruption or crash is likely.
  Save Hash = [0x9b1c10f9-0xbd3db0a1-0x2727ecfe-0x06656a49] Game Hash = [0x4510836f-0x6b3aac37-0x0fa52c26-0x20ec3e4f]`,
  then reads on; a later `CheckSentinel - Deserialization Error` shows the misread, and a read past the end ends it.
  The offline arms log the same pair. The successful 15:22 load logs no mismatch.
- **Where the format depends on the promotion count**: not found by reading; the containers checked carry their own
  counts (`CvBitfield`, `std::vector`, `PromotionArrayHelpers`). Open [I]: a member sized by `GC.getNumPromotionInfos()`
  without a count, between the start of the player section and the first sentinel. What would confirm it: a debug
  build that logs the stream position at each sentinel.
- **Not a patch fault** [C]: the same binaries load the save once the database matches, and the stock game fails the
  same way (section 3, [L]). D239 (one VP version's builds interchangeable) is not touched: the save loads in a build
  of 5.4.8 with the mods it was made with.
- **The earlier report of 18:23** (0.1b2 33c3fcb44, same save, 9 mods but a different engine database: 835 tables,
  39,922 rows): the DLL logged a hash mismatch too (Game Hash `0xd35b28b0-...`), and the server ended on its database
  check (`table sqlite_stat1 differs`), code database. Same cause class, another signature. [C: that run's logs]

## 2. What a player's save header can and cannot tell

- The EXE writes the activated mods with `AffectsSavedGames` 1 into the header and re-activates them on a load. A mod
  with `AffectsSavedGames` 0 is never listed, whatever it does to the database (VernsTweaks adds a promotion and
  changes options). So the header detects version and presence differences of save-affecting mods only.
- The DLL's game data hash detects any change to the info tables it covers (count or Type names), whichever mod made
  it, but only inside `CvGame::Read` (body offset 4..19) and against the running database.

## 3. The stock game (patch off) on the same load [L, 2026-11-22 21:55-21:59]

c5h-c (EXE build 4390913, the instance's 32-bit DLL i0-ship `bbfacca1a`-ia, SHA-256 `e589306e...`, mods cp, vp, eui;
a copy of the save put in and taken out by hash with `saves_snap.py`):
- the DLL logs the same `WARNING - Save data hash mismatch!` with the **same Game Hash** `0x4510836f-...` as the owner's
  session on EXE build 26577625: the hash is the DLL's, made from the database, so the EXE build does not matter;
  then `Potential savefile format mismatch` (another DLL build string, harmless here) and `CheckSentinel -
  Deserialization Error`;
- 2.5 min after the launch VP's own **"Game Crash"** box: `0xe06d7363`, "Unhandled C++ exception - likely allocation
  failure (32-bit address space exhausted)", with advice on memory. The address space was not exhausted (largest free
  block 1.6 GB); VP's crash handler labels every unhandled C++ exception so. Nothing names the mods.
So the stock game crashes on this save as the patch did, and its box points the player the wrong way.

## 4. The server's check (ticket 0681, `server/src/remote/srv_remote_savemods.*`) [C offline, L live]

- Signal: the DLL's own mismatch warning, seen by the server's log service (`srv_ifaces.cpp`, every `CustomMods.log`
  line; that log is always written). No load is refused for the warning: a mismatch can load (a mod adding a row at the
  end of a table may not shift what the save layout reads), and stock allows it. (Corrected 2026-11-25, ticket 0744:
  was "No load is refused"; since 0744 remote mode ends the server at the DLL's first `Deserialization Error`, the
  point where the DLL itself has failed to read: section 6.)
- At each load (`ICvGame2_Read` in remote mode, `srv_save.cpp` in direct mode) the server logs `save mods: load of
  '<file>' (body version ..., game data hash ...)` with the save header's mods (read-only; the file name from the
  load path in direct mode; in remote mode the DLL's pregame `loadFileName` is EMPTY [L, arm 2 run 1, 2026-11-22
  22:26]: the EXE keeps the name, so on a mismatch the server finds the save among `ModdedSaves` and `Saves`, newest
  first, by its inflated body's first 4 KiB, inflating only each file's first zlib piece: 0.01 s on c5h-a; the 4 KiB are
  the bytes the 0354 save-variant scan reads anyway, never a byte more: a first version that read 4 KiB on its own ran
  past a short body's end and ended hutest's `streamread` scenario, `test_served_exception_text`, 2026-11-22 23:3x
  [C]), the active mods (`cache/Civ5ModsDatabase.db` through System32's `winsqlite3.dll`, with
  each mod's AffectsSavedGames), the difference in the mods a save lists, and the active mods saves never list.
- On the DLL's warning: one `save mods: LOUD:` line with both hashes and that list; on `Deserialization Error` one line.
  The mismatch stays armed for the session (no load-end mark exists in remote mode). A later deliberate end of the
  server (`WriteFatalRecord`, any code but a render module's) writes `code: save-mod-mismatch`, the player's summary
  ("This save was made with a different set of mods (or mod versions) than the mods active now, so the game could not
  load it; this is not a failure of the 64-bit patch, and the same save fails in Vox Populi without the patch too.
  Load the save with the mods it was made with.") and a reason with the detail and the original failure. The proxy
  already puts the server's summary into the popup and report.txt for a server death (ticket 0641).
- **Live, patch ON [L, 2026-11-22 22:35-22:40, c5h-a, EXE build 4390913** (no 26577625 instance in mods form was
  free), this branch's server.exe and proxy built from pin 1b87dd185 (x64 DLL `b84aa3e9...`), a pack made with
  `tools/release/pack.py` for the instance's fallback DLL (`3c2f47da...`), pack=1, mods cp, vp, eui, a copy of the save
  in and out by hash, `server/evidence/0681/arm2.sh`]: the server died as before (exit 6, `kRtE_StreamRead`), and
  report.txt says `code: save-mod-mismatch`, popup-summary.txt is the player's text above, and the reason names the
  save (`found by its body among the saves in 0.01 s`), both hashes, the header's mods (CP v151, VP v17), the active
  ones, "difference in the mods a save lists: none", the AffectsSavedGames = 0 note and "deserialization errors 1".
  The popup's own first line is still the generic "The game's 64-bit server stopped with this error: exe_exception
  ..." (ticket 0511's server reason).
- **The proxy's stage and the popup's first line** (after 0682, D240; 2026-11-22 23:00-23:15) [C hermetic]: the proxy's
  `ReportFailure` turns a server death whose `server-fatal.txt` code is `save-mod-mismatch` into the stage `save-mods`
  (its summary the server's words, the default ending "the game had to end."). vp64helper's popup (both the `popup`
  verb and D240's `failed` popup) opens with the plain line "This save was made with a different set of mods than the
  mods active now, so the game could not load it; this is not a failure of the 64-bit patch, which stays on." (the
  failed popup adds D240's Remove / Close sentence), then the server's summary, then the server's technical reason as
  "Details: " (for every other stage ticket 0511 puts that reason first; MC #10855: nothing technical before the mods
  line). Remove 64bit patch stays offered. `proxy/tools/test_vp64helper.py` 448/448 with its ASan arm (save-mods,
  look-alike stages save-modsx / xsave-mods refused, no Details line without a reason). Not yet seen live: the run on
  c5h-a predates this stage.
- Proposed, not built: a warning before the load (the front end would need the running game data hash, which only the
  DLL computes after the database is cached); a record "game data hash -> every activated mod" written at each save
  so the report can name even the AffectsSavedGames = 0 mods the save was made with; not switching the patch off
  (`failed=1`) after a mod-mismatch failure.
- Tests: `server/tests/tools/test_save_mod_mismatch.py` (the pure block compiled on its own, 14 checks; the save copy's
  body hash; the two offline load arms, the failing one checked for its exit code and lines).

## 5. Commands

```
python -I -X utf8 server/evidence/0681/save_mods_scan.py "<Saves or ModdedSaves folder>"     # header mod lists
python -I -X utf8 server/evidence/0681/save_body_id.py "<folder>"                             # body version + db offset
python -I -X utf8 server/evidence/0681/setup_repro.py C:/c5-64bit-work/0681                   # copies, by hash
python -I -X utf8 server/evidence/0681/db_diff.py A.db B.db
python -I -X utf8 server/evidence/0681/db_variants.py C:/c5-64bit-work/0681
bash server/evidence/0681/repro_all.sh with-extra-mods crash-session t-UnitPromotions ...      # 5-8 s per arm
C5_SERVER_BIN=<build_server out>/bin C5_0681_WORK=C:/c5-64bit-work/0681 \
  python -I -X utf8 server/tests/tools/test_save_mod_mismatch.py                               # about 35 s
```
`repro_offline.sh` names `C:/c5-64bit-work/0681/bin` (the copied 0.1b3 binaries); `setup_repro.py` adds the x64 DLL
there as `CvGameCore_Expansion2.dll`, the name `run_server.py`'s server.cfg uses (`FAILED: cannot load the DLL` without
it, 2026-11-22).

**Later effect, K31 (2026-11-25, ticket 0741):** a misread of this kind can also damage the server's memory before the save read fails: in the owner's beta 3 game of 2026-11-23 the server's render thread faulted in IceTapReader::poll 0.107 s after the DLL's Deserialization Error (research/05 renderer-failure-modes.md 6; research/06 known-crash-causes.md K31). Fix: ticket 0744 ends the server at the DLL's first Deserialization Error (section 6).

## 6. The end at the DLL's first deserialization error (ticket 0744, K31, the MC's ruling (c) of 2026-11-25)

**Where the line comes from** [C, the pin's source 1b87dd185, the kept plain copy `C:/c5-64bit-work/keep/pins-0.1b/
src-x64-1b87dd185`]: `CheckSentinel` (`CustomMods.cpp:613`) logs `Deserialization Error` through `CUSTOMLOG`
(`CustomMods.log`) when a read's sentinel is not `0xDEADBEEF`, and reads on. `MOD_SERIALIZE_INIT_READ`
(`CustomMods.h:1458`) is its only caller, in five save readers: `CvTeamTechs::Read` (`CvTechClasses.cpp:2226`) and the
`operator>>` of `CvEspionageSpy`, `CvPlayerEspionage`, `CvCityEspionage` and `CvEspionageAI` (`CvEspionageClasses.cpp`).
Nothing else reads with it, so the line only ever comes from a save's read, and by then the read is already out of
step (the sentinel is the word after a version: an earlier field took the wrong number of bytes).

**What the server does** [C, offline]:
- (c) `SrvSaveMods::OnDllLog` (`server/src/remote/srv_remote_savemods.cpp`): at the first such line after a load began
  (`BeginLoad`; the count resets there) it logs `save mods: LOUD: the server ends at the DLL's first deserialization
  error of the load (exit 6, code save-mod-mismatch; ticket 0744)` and calls the end hook with exit 6. Remote mode sets
  the hook to `SrvRemote::ReportAndExit` (`srv_remote_main.cpp` `RemoteMain`), so the server writes server-fatal.txt and
  ends at once, on the DLL's reading thread, inside the DLL's log call; nothing of the DLL runs after the line. The log
  service (`srv_ifaces.cpp` `SrvLogFile::Write`) now writes the line into `CustomMods.log` first and hands it on after
  (the end closes the file). A line before any load began ends nothing (one line says so). Direct mode
  (`run_server.py --load`, `srv_save.cpp`) sets no hook and reads on as before: it is a development tool, and an ASan or
  page-heap run of a misread (renderer-failure-modes.md 6, "What would confirm it") needs the read to go on.
- The record: exit 6 (FatalWords' engine-exception) is overridden to `code: save-mod-mismatch` with the player's words
  of section 4 (`PlayerSummary`, unchanged); the reason holds the mismatch detail (both hashes, the save, the mods) or,
  when the DLL logged no hash mismatch before the error, "a deserialization error ... without a game data hash
  mismatch before it ... most likely made with other mods or mod versions than the active ones" and the mods line; then
  `deserialization errors 1; the server ended at the DLL's first deserialization error (ticket 0744): ` and the end's
  own text. The proxy turns it into stage save-mods (section 4), so the popup opens with the mods line, not the generic
  server-death one.
- (a) `OverrideFatal`: after a deserialization error (the count > 0) a `render*` end is overridden too, its code kept as
  "what ended the server (code render-ice)"; with the hash mismatch alone (no deserialization error) a render end keeps
  its own code, as 0681 had it.
- (b) `WriteFatalRecord` (`srv_exe_crash.cpp`) writes a `pid: <server pid>` line after `exit:`. The proxy
  (`proxy_rt.cpp` `ReadServerFatal`) takes a record whose exit code differs from the server's when the record names this
  server's pid and the server ended by an exception code (`ProxyReport::UseFatalRecord`, `proxy/src/proxy_report.h`: the
  NTSTATUS error class but 0xC000013A, and 0x80000002..4), with the log line `server-fatal.txt names exit N and this
  server's pid P; the server then ended by exception code 0x... before its own exit: its record is used (ticket 0744)`.
  A record without a pid line (an older server) or of another pid is refused as before, with the reason in the line.
- Saves made with the same mods log neither line: nothing changes for them (D239). No load is refused before the DLL
  itself fails to read. No option key (D255): the end costs nothing on any other load and replaces a crash.

**Tests** [C, 2026-11-25, offline, no game]: `server/tests/remote/test_savemods.py` (25 checks: the end with and without
the hash line, direct mode, (a) for ice and units, the negatives render-nosentinel / noload / same, a returning hook
called once, the proxy's rule over 12 inputs); `test_exe_crash_report.py`'s fatal cases check the pid line;
`proxy/tools/test_proxy_session.py` loader cases `death-server-fatal-pid` (a record of this pid, exit 5, code
save-mod-mismatch, then 0xC0000005: used, stage save-mods, the server's words in the popup) and
`death-server-fatal-otherpid` (refused). Not seen live: no game was run for 0744; the first live sight is a mismatched
load on a build with it.

**Limit** [I]: the damage of K31 may begin before the first sentinel (the misread's earlier fields are read before the
sentinel that shows it); the end removes what the DLL would read after it, which in K31's game was 0.107 s of reading
before the render thread faulted. (a) and (b) cover a fault that wins that race.
