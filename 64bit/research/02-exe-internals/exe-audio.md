# The EXE's audio: the Sound trigger's path, scripts, handles, loops, and the Miles import-slot log

Ticket 4797 (epic 0042, route S spike S-A; the Architect's note `../07-architecture-decisions/architect-2026-11-20-route-s-combat.md`
1.2 and 5, premises A1, A2, A3 / ASK-1). Written 2026-11-20 from 03:15 EST by w5-0042 (team-0042, account B). This is
the knowledge base's first description of the EXE's audio system (before it: `disable-systems-and-masks.md` row 10,
"kept on, never probed").

Sources and marks: `[C]` read by hand with llvm-objdump in the analysis copy of the **old build** (4390913, PE stamp
0x546CD0A8, checked before reading), strings and immediates only, written here as behaviour (D83: no decompiler
output); `[D]` read from the install's own files at run time on the Goldberg instance copy c5h-b (`install\` and its
`Documents\My Games` mods; counts and names only, no content committed); `[L]` measured live (section 9, with its run);
`[I]` inferred. Addresses are VAs of the old build at ImageBase 0x400000. No address of build 26577625 is written here
(D202); the entries epic 0590 must carry are named in section 8.

## 0. Answers in short

- **A1 holds [C].** A clip's Sound trigger does not play anything on the thread that fires it. Its listener (0x4C4430)
  puts a 0x80-byte record of type **0x15** on the 4 MB "A" queue (`message-queue-and-fixed-buffers.md` 1). On the main
  thread, when the queue is pumped, the type-0x15 listener (0x74C620) resolves the record's script and makes **one
  call that takes a script index and a world point: the game audio object's 3D play, 0x8DC7F0** (thiscall on the object
  held at 0x5F54F34; 11 stack arguments), which wraps the firesound library's `FAudioManager::Do3DSound` (0x90BD00,
  thiscall on the object held at 0x606BAF0). It returns a pointer to the started sound object (an `FSound`), or 0.
  **Confirmed live [L, section 9]:** the shell's prototype made that call with a script index and a plot's world point
  and the sound started (the same Miles chain as a trigger's sound), looped, and stopped on the prototype's stop.
- **How a script is named [C, D].** Scripts are rows of `Script3DSound` in the install's `*3DScripts.xml` files (236
  files, 17,962 rows `[D]`), each with a `ScriptID` name (`AS3D_...`) that points through `SoundID` at a sound define
  (`AudioDefines.xml`: `SoundID` -> file name, load type). The database copies them into `Audio_3DSounds` /
  `Audio_Sounds` (the EXE's query strings `[C]`). At run time a name becomes an index through
  `FAudioManager::GetAudioTagIndex` (0x908A80; kind 4 = `AUDIOTAG_3DSCRIPT` of `CvEnums.h`, the name lower-cased and
  hashed); the trigger records carry the hash instead and use the by-hash form (0x908C40). The DLL's
  `ICvEngineUtility1::GetAudioTagIndex` (slot 127) reaches the same table, so the shell can name scripts with no EXE
  address of its own.
- **A2 holds [C, L], with one caution.** The started sound is an `FSound` object from a fixed pool; the EXE keeps it and
  stops it through `FSound::Stop` (0x90EDE0, thiscall, no arguments) or fades it out through 0x90EB90 (thiscall: fade
  time in seconds, 0.0, 0). The caution: the pool reuses the object after the sound ends `[I]`, so a held pointer must be
  checked (section 4.3). **A loop is a property of the script** (`bLooping`: 3,871 of 3,943 rows that set it say True
  `[D]`; the trigger's call has no loop argument `[C]`). Looping unit sounds are kept alive by a **heartbeat**, not by a
  stop: a looping script started by a unit trigger enters a 64-slot table and is faded out when the same unit has not
  triggered it again for `fUnitLoopHeartbeatLength` (0.07 s) or, while the unit fights, `fUnitLoopCombatHeartbeatLength`
  (0.1 s), with a fade of `fUnitLoopHeartbeatFadeLength` (0.5 s) (`AudioParams.xml` values `[D]`; section 4.2).
- **ASK-1 / A3 [D]:** the install's 673 trigger files and the 18 in the instance's mods carry **no StopSound row** (no
  trigger of a stop type, and no file contains the class name). The EXE has the class (`FTimedTriggerStopSound`) and a
  listener for it (type 0x16, which stops every tracked sound of the unit), but no shipped file uses it. A3 holds in its
  first form: the controller has no StopSound to emit.
- **E1 vs E2 [C]:** the EXE registers no positional audio event for Lua: its only play event is `AudioPlay2DSound`
  (a 2D script by name), beside `AudioVolumeChanged`, `AudioDebugChangeMusic` and the two playlist events. **E2 is the
  entry**: the play of A1 and the stop of A2: the Architect's two entries, four rows of epic 0590's table with the play's object and the fade (section 8).
- **The instrument** for 0599: a 32-bit DLL that hooks the EXE's 68 mss32 import slots by name (15 of them logged) and
  writes one tab-separated line per Miles call (section 5). Built under `proxy/evidence/0042/4797/`, not product code.
- **Live, three stock melee fights [L, section 9]:** every unit sound that reached Miles was a looping script (Miles
  loop count 0) started through the 3D play and ended by the audio manager's own `AIL_end_sample` 0.12-0.25 s after
  `EndCombatSim` at the latest; nothing played on after a fight (0599's fault was not reproduced, as 4771's S2). Only four
  3D voices were ever used, and all four were busy at times. The one-shot unit scripts tried (attack and victory vox,
  sword swing) were refused by the play rules before any voice was taken. *Why, read 2026-11-21 (ticket 4734,
  section 12) [C, D]: the play rules refuse every script that is neither looping nor music while its sound type's
  effective volume is at most 0.01, and every test instance has `Effects.Volume = 0` (and the other three sliders 0) in
  its `UserSettings.ini`. Stock on the instances therefore starts only loops (all 318 sample lives of 0599 too); a
  player with the effects slider above 0 gets the one-shots. Replaced: "why is not read".*

## 1. The pieces [C]

| Layer | What | Where |
|---|---|---|
| Miles Sound System | `mss32.dll` in the game folder; 68 imports by name, slots 0x9D6D34-0x9D6E40 (4 bytes each, order of `pe.txt`) | every slot is referenced by exactly one thin wrapper in 0x920B30-0x923840 (16 for `AIL_stream_sample_handle`) |
| firesound `FAudioSystemMiles` | owns the Miles driver, allocates **all 2D and 3D sample handles once at start-up** (`AIL_allocate_sample_handle` has one call site, in the start-up path: "Initializing 3D samples..."), and plays, stops, pauses, loops, positions them by (type, index) through a vtable ("FAudioSystemMiles::StopSound: Illegal index", ...SetSoundLooping, ...Set3DSoundPosition) | strings 0xAA31A0-0xAA3DA8 |
| firesound `FAudioManager` | scripts, sound defines, contexts, the sound object pools, `DoSound` (2D, 0x90BB70), `Do3DSound` (0x90BD00), `GetAudioTagIndex` (0x908A80 by name, 0x908C40 by hash), the audio parameters (`fUnitLoop...` at +0x114/+0x118/+0x11C, read by 0x907620) | object pointer at **0x606BAF0**; strings 0xA9E268-0xAA0700 |
| firesound `FSound` | one playing sound `[offsets C, meanings I]`: +0x04 sample type, +0x14 sample index, +0x18 state bits, +0x20 the requested loop count (0 = no loop; set by `FSound::SetLooping` 0x90E850, read by 0x90E840 "is looping"; corrected 2026-11-20 from "non-zero while it holds a voice"), +0x28 "stopping", +0xA4 its script data (+4 of which is the script's index), position at +0xA8..+0xB0 | methods 0x90E640-0x90F0A9, 0x919A40-0x919DB5 |
| `FAudioGame` (FireEngine2) | the game-facing audio object: the 3D play 0x8DC7F0, the 2D play 0x8DB7F0, the soundscape play 0x8DBA40, the tag lookup 0x8DBA20 (by hash), the unit-loop table and heartbeat (section 4.2); constructed at 0x719433 (0x46C bytes, vtable 0xA99630) | object pointer at **0x5F54F34**; +0x34 "initialised" byte |
| the App (`cvAudioManager.cpp`, `cvAudioSystem`) | loads the database's audio tables and the XML scripts, mod and DLC scripts, options, the Lua debug calls (`BeginAudioLogging`, `GetAudioLog`, `GetSoundInstanceInfo`, ...) | strings 0xA72E8C-0xA75A9C |
| the Audio system key | `[DisableSystems]` bit 0x400; its start-up 0x570100 builds the system object 0xC957A0 | `disable-systems-and-masks.md` 2 |

## 2. The Sound trigger's fire path (A1) [C]

1. **The listener.** The Granny timed-trigger system calls the Sound trigger's listener **0x4C4430** (vtable slot at
   0xA3C5B0, beside the strings "SoundEffect" / "Triggers a sound effect event when fired"). It computes the trigger's
   attachment-bone transform with 0x6FC7B0 (`crash-load-8e81c3.md` 4), takes the trigger's script hash (listener
   +0xA4), applies a variation through 0x4981B0 when the unit's member has one (`$(WEAPON)`-style; with no variation
   found it logs "ERROR: Can't find sound trigger for a variation of '%s'"), and asks 0x8DBAB0 what kind of script
   the hash names (it calls `FAudioManager` 0x908D50; 0 = none, which logs "Can't find sound trigger for '%s'").
2. **The record.** For a known script it reserves a record of **type 0x15** on the 4 MB "A" queue (writer 0x6EF2C0:
   0x80 bytes, 0x10-byte header `[0x80][0x15][0][0]`) and fills the payload: +0x00 the script hash, +0x04 and +0x08
   1.0 (defaults left), +0x0C 0, **+0x10..+0x18 the world position** (the bone's position, or the context's position
   when the trigger has no bone), +0x1C the unit's index in the unit simulation array (0x2D6F5F8), +0x20 a second id
   from the trigger context, +0x24 a byte from the listener (+0x28). The same thread does nothing else with audio.
3. **The handler (main thread).** 0x6D9CE0 registers six listeners of one object on the "A" manager when the game view
   starts; type 0x15's is **0x74C620** (list at 0x1A0E4B4, registered through 0x88F850). It returns at once when
   `FAudioManager`'s test 0x9067B0 says so `[I: audio paused or off]`. It asks the script's kind again (0x8DBAB0):
   - kind 1 (a 2D script): the index by hash (0x8DBA20 with `AUDIOTAG_2DSCRIPT` 3), then the 2D play 0x8DB7F0;
   - kind 3 (a soundscape): `AUDIOTAG_SOUNDSCAPE` 5, then 0x8DBA40;
   - **kind 2 (a 3D script, the unit sounds): the index with `AUDIOTAG_3DSCRIPT` 4, a test of the unit
     (0x53D890: **the tier test of section 13**, the trigger's `tier` against the unit's figure count; *corrected
     2026-11-21 by ticket 4737, was "`[I: visibility]`"*; when it fails the sound is not started, and a flag on the
     handler's object decides whether it is recorded), the unit-loop test 0x8DB850 (section 4.2: an already running loop of this unit and script is renewed
     instead of started), then the 3D play 0x8DC7F0, then 0x8DB910 records the sound in the unit-loop table when the
     script loops for a unit (or the handler's flag says so).**
4. **The call.** `FAudioGame` 3D play **0x8DC7F0**, thiscall on `[0x5F54F34]`, `ret 0x2C`. Arguments as the trigger
   passes them, in order: script index; a reference-counted context pointer (0 from the trigger); a pointer to the
   world position (3 floats, x, y and z with z up, the order of Lua's `HexToWorld`; `Do3DSound` hands Miles (x, z, y));
   **-1.0** (the float constant 0x9EE11C; "the script's own value"); an int (-1); -1.0; -1.0; a factor (-1.0, or 0.0 for
   a unit loop while that unit fights; a value above 0 starts a fade-in through 0x90EAC0 `[C]`); -1; -1; -1.
   *(Corrected 2026-11-20 by the live stack dump of section 9: the first draft read the constant 0x9EE11C as 1.0. With
   1.0 in those places the call returns 0 and nothing starts, measured live; with -1.0 the sound starts.)* The values
   the trigger passed, read live from the stack in three fights: `(index, 0, &pos, -1.0, -1, -1.0, -1.0, 0.0, -1, -1,
   -1)`, and on to `Do3DSound` `(index, 0, 0, &pos, 0.0, -1.0, -1, -1.0, -1.0, -1, 1, -1, -1)` `[L]`. It returns the `FSound`
   pointer or 0 (0 also when the script did not load: it then marks the index in a set at object +8). Inside, it hands
   over to `Do3DSound` (0x90BD00, `ret 0x34`), which: refuses when the manager is not initialised ("Audio MGR not
   init'd."); finds the script data (0x907080); applies the script's play rules (0x908260: the volume knob of its sound
   type, `DontPlayThisSoundEver` / zero max volume, `PercentChanceOfPlaying`, the instance caps; the "INFO:
   FAudioManager::DoSound()" messages); takes an `FSound` from the 3D pool (0x90B240; "Could not alloc 3D sound
   object from shared pool!"); sets volume, pitch and position; does not start a sound beyond its cut-off distance
   ("INFO: Not starting sound ID '%d' - too far away."); and starts it (0x90FBC0), down to `FAudioSystemMiles` and
   Miles (`AIL_set_named_sample_file`, `AIL_set_sample_3D_position`, `AIL_start_sample`; section 5).

So a call from the shell with a script index and a world point, on the main thread, is the same call the Sound
trigger makes, with stock's attenuation, volume options, script rules and voice limits. What the trigger adds around
it (the tier test of section 13, the unit-loop table) is the trigger's own policy; under route S the server owns that policy
(the Architect's note 1.4-1.6).

## 3. How a script is named [C, D]

- **Files [D]:** `Assets/Sounds/XML/*3DScripts.xml` and the DLC folders' `Sounds/XML/*3DScripts.xml` (236 files),
  root `Script3DSounds`, rows `Script3DSound` with `ScriptID`, `SoundID`, `SoundType` (a volume knob, `GAME_SFX` for
  units), `iMinVolume`/`iMaxVolume`, `iPitchChangeUp/Down`, and optionally `bLooping` (3,943 rows), `bDontTriggerDuplicates`
  (3,325), `iDontPlayMoreThan` (6,237), `bStartFromRandomPosition` (3,816), `iPercentChanceOfPlaying` (326),
  `iMin/MaxTimeMustNotPlayAgain`, `fMinDistance` (90), `bOnlyTriggerOnUnitRuns` (49), `bDontTriggerDuplicatesOnUnits`
  (35), `bIsMusic` (12). Sound defines (`SoundData` in `*AudioDefines.xml`): `SoundID` -> `Filename` (no extension),
  `LoadType` (`DYNAMIC_RES`, ...), `bDontCache`, `bOnlyLoadOneVariationEachTime`. Sound files: `Resource/Common/SoundData.fpk`
  and loose `.wav` / `.ogg` / `.mp3` under `Assets/Sounds/` (streamed ones in `Streamed/`). The 2D scripts
  (`*Audio2DScripts.xml`) and the soundscapes (`AudioSoundscapeScripts.xml`) have the same shape.
- **Database [C]:** the App reads `Audio_Sounds` joined with `Audio_SoundLoadTypes`, `Audio_2DSounds`, `Audio_3DSounds`
  (columns `ScriptID`, `SoundID`, `SoundType`, `MinVolume`, `MaxVolume`, `Looping`, `DryLevel`, `WetLevel`,
  `StartFromRandomPosition`, `DontPlayMoreThan`, `OnlyTriggerOnUnitRuns`, `PercentChanceOfPlaying`,
  `DontPlayThisSoundEver`, `DontTriggerDuplicates`, `DontTriggerDuplicatesOnUnits`, `Min/MaxTimeMustNotPlayAgain`,
  `IsMusic`, `Min/MaxTimeDelay`, `TaperSoundtrackVolume`, `PitchChangeDown/Up`, `Priority`, the pans, `StartPosition`,
  `EndPosition`, `Channel`, `Min/MaxVelocity`, `DistanceFromListener`, `MinDistance`, `CutoffDistance`) and the
  soundscape tables, for mod and DLC scripts (`cvAudioManager::LoadExtraContentScripts`) (strings 0xA72E8C-0xA73690).
- **At run time [C]:** `FAudioManager::GetAudioTagIndex` (0x908A80, thiscall, `ret 0x10`: tag kind, name, 0, 0) picks
  the kind's table (`AUDIOTAG_*` of `CvEnums.h`: 0 sound id, 1 context, 2 sound type, 3 2D script, 4 3D script, 5
  soundscape, ...), lower-cases the name, hashes it with the EXE's string hash and looks the hash up; -1 and "ERROR:
  GetAudioTagIndex(): Could not find '%s'" when absent. 0x908C40 is the same lookup by a hash already computed (the
  trigger records). The script names in the triggers carry `$(WEAPON)` / `$(TERRAIN)`-style parts that the listener
  resolves through the member's variations (step 1 of section 2); the server resolves them before sending
  (Architect's note 1.3).

## 4. Handles, stop and loops (A2) [C unless marked]

### 4.1 The handle

`Do3DSound` returns the `FSound` it took from the pool; the trigger's handler keeps it only in the unit-loop table
(4.2). Stop paths:
- **`FSound::Stop` 0x90EDE0** (thiscall, no arguments, returns a bool): when the sound is live, logs its name at info
  level, calls `FAudioSystemMiles`' stop through its vtable (+0x58, by sample type and index: `AIL_stop_sample` or
  `AIL_end_sample` / the stream calls), and clears the state bits ("FSound::Stop(): Sound has no shared sound data." when
  it has none).
  **It loses the voice** (ticket 5051, 2026-11-22, [C path, L outcome]): for a 3D sample the stop is `AIL_stop_sample` (a
  pause), bit 0 of +0x18 stays set and nothing on the path gives the voice or the object back; the EXE never uses that
  Miles 3D voice again (196 of 196 in five route S sessions; 64 voices). Stock calls it on a unit sound only when the
  64-slot unit-loop table is full; everything else ends through the fade below. A caller must end a sound through the
  fade, never through `FSound::Stop` (`../05-graphics-composition/unit-combat-server.md` 18.24).
- **The fade 0x90EB90** (thiscall: seconds, 0.0, 0; `ret 0xC`): with a positive time and a volume ramp it sets the
  ramp (`FAudioSystemMiles` vtable +0xD4, Miles `AIL_sample_stage_property` "Ramp To" / "Ramp Time" on the "Volume Ramp
  Filter" opened at start-up) and lets the sound end at the ramp's end; otherwise (0.0 s, or no ramp on the sample) it
  only requests the end (+0x08 = 4, +0x28 = 1, no Miles call) and the manager ends the voice at its next update with
  `AIL_end_sample` and uses it again *(corrected 2026-11-22 by ticket 5051 [C], was "otherwise it stops at once";
  `../05-graphics-composition/unit-combat-server.md` 18.24)*. Live, a 3D unit loop
  given a 1.0 s fade ended at the manager's next update (`AIL_end_sample` 0.6 ms after the call), with no ramp written:
  the 3D samples carry no volume ramp, so for them the fade is an immediate stop; only the 2D ambience beds were
  ramped `[L, section 9]`. The heartbeat's 0.5 s "fade" is therefore an immediate end for unit sounds `[I from the same
  path]`.
- The type-0x16 handler (the StopSound trigger, section 6) stops a unit's sounds through the fade with the heartbeat
  fade length.

### 4.2 Loops: a property of the script, kept alive by a heartbeat

- The loop is decided by the script: `bLooping` / `Looping` (section 3). The 3D play has no loop argument; its three
  trailing ints (-1 from the trigger) set a playback position when not -1 (0x90F040, through `FAudioSystemMiles` +0x6C) `[C
  for the -1 test and the call; I which of the three]`. `FSound::SetLooping` (0x90E850: a flag and a count; no loop or
  count 0 -> Miles count 1, count -1 -> Miles count 0 = loop forever) reaches Miles through `FAudioSystemMiles` vtable
  +0x74 (0x920C90, `SetSoundLooping`, by sample type) `[C]`. Live, every unit sound of three stock melee fights got
  `AIL_set_sample_loop_count(h, 0)` (loop forever) and was ended by the manager `[L, section 9]`.
- **The unit-loop table** of `FAudioGame`: 64 slots of 16 bytes at object +0x6C: the `FSound`, an age in seconds, the
  unit's index, flags (bit 0 used, bit 1 the sound loops (0x90E840), bit 2 the unit was fighting).
  `FAudioGame` copies the three heartbeat parameters from `FAudioManager` at start (0x8DC2D6: +0x40 =
  `fUnitLoopHeartbeatLength`, +0x44 = `fUnitLoopCombatHeartbeatLength`, +0x48 = `fUnitLoopHeartbeatFadeLength`).
  - **Renew instead of start (0x8DB850):** before a unit's looping script starts, a live slot with the same unit and
    the same script has its age set to 0 and its position moved, and no new sound starts.
  - **Record (0x8DB910):** a started sound of a looping unit script takes a free slot; with all 64 taken the new sound
    is stopped at once (`FSound::Stop`).
  - **Age and end (0x8DB660, per frame with the frame time):** each used slot whose sound is still live and has bit 1
    ages by the frame time; at the fighting or the plain heartbeat length it is faded out with the fade length and the
    slot is freed; a slot whose sound has ended is freed.
  - **Stop by unit (0x8DB5B0):** fades out every live slot of a unit (the StopSound trigger's handler, section 6).
- With the install's values (0.07 s / 0.1 s / 0.5 s `[D]`), a unit loop lives only while the clip keeps re-triggering
  it at least every 0.1 s in a fight, and fades over 0.5 s after the last trigger. A looping script started on a path
  that does **not** record it in the table (the handler records only when the script is a unit loop or its flag says
  so; any other caller of the 3D play) has no heartbeat and loops until someone stops it `[C for the condition; I that
  such a path is taken in stock play]`. For 0599 this is the first place to look, with the log of section 5; the
  battle-ambience scripts are such loops (`AS3D_UNIT_<unit>_BAT_AMB_<tier>_<weapon>`, for example 18 `BAT_AMB` and 2 `BATTLE_AMB_VOX` of the
  warrior's 34 looping scripts; the other 14 are runs `[D]`).

### 4.3 What the shell must hold

The shell keeps, per live sound it started, the returned `FSound` pointer and the script index it asked for. The pool
object is reused after its sound ends `[I: the unit-loop table re-checks liveness every frame before touching a slot,
which it would not need if pointers stayed exclusive]`. So before a stop the shell checks that the object is still live
and still carries its script (`FSound` +0xA4's index) `[I]`; a sound that ended by itself needs no stop. Looping
scripts started by the shell are the shell's to stop: there is no heartbeat for them (4.2). This matches the
Architect's design: every sound has an owner, loops only with an owner, and a watchdog (note 1.5, 1.6).

## 5. The instrument: the Miles import-slot log (format 1, for ticket 0599)

`proxy/evidence/0042/4797/mss_log.cpp` (build: `build.bat OUTDIR`, clang-cl x86; not product code). Loaded into a
running game with `tools/harness/native/bin/bg_helper32.exe inject <pid> <dll>`. In `DllMain` it walks the EXE's import
table and replaces, by name, the slots of 15 Miles functions and of `USER32!PeekMessageW` with its own functions (data
writes into the import slots only, D136; nothing in `.text` is touched); each hook logs and then calls the saved
original. It also wraps the EXE's end-of-sample and stream callbacks (registered through the hooked
`AIL_register_EOS_callback` / `AIL_register_stream_callback`) so that the natural end of a sample is logged.

Output: `mss_log.tsv` in `%C5_MSSLOG_DIR%`, else the DLL's folder, appended, flushed every 250 ms. One line per event,
tab-separated:

| Field | Meaning |
|---|---|
| 1 | milliseconds since the DLL loaded (QueryPerformanceCounter) |
| 2 | Windows thread id (the main thread is named once in a `MAIN` line) |
| 3 | event: `start_sample`, `stop_sample`, `end_sample`, `resume_sample`, `set_named_sample_file`, `set_sample_loop_count`, `set_sample_3D_position` (only the first after a new file on that sample), `register_EOS_callback`, `EOS` (the sample ended by itself), `open_stream`, `start_stream`, `pause_stream`, `close_stream`, `set_stream_loop_count`, `register_stream_callback`, `STREAM_END`, `set_sample_stage_property` (writes only: the fades), and the tool's own `START` (process id and the local wall time of field 1's zero), `SLOT` (each hooked slot, its old target), `SLOTS`, `MAIN` (the main thread), `PROTO`, `CMD` (prototype commands `play`, `stop`, `diag` and their results), `STACK` (with `diag_dump_ret` in the local file: the 40 dwords after that return address on the stack, from the `AIL_resume_sample` hook), `MARK` (a label written by the driver through the command file, logged on the main thread: the fight's order, the end of its quiet time) |
| 4.. | `h=<handle>` (the Miles sample or stream handle: samples come from a pool allocated at start-up, so a handle names a voice, not a sound), then the call's own fields: `name=`, `size=`, `crc4k=` (CRC-32 of the first 4 KB of the file image, to name the file offline), `ret=`; `count=` (0 = loop forever); `pos=x,y,z`; `cb=`; `on=`; `stage=`, `name=`, `value=` |
| last | up to six return addresses into the EXE's `.text` found on the stack above the hook (a value counts when the bytes before it are a call): the first is the Miles wrapper, the next ones name the `FAudioSystemMiles` / `FSound` / `FAudioManager` method and its caller. A heuristic: frames without such a value are skipped |

Reading it for 0599 [I, the method]: a sample's life is `set_named_sample_file` (which file) -> `set_sample_3D_position`
-> `set_sample_loop_count` (if looping) -> `start_sample` -> `EOS` (ended by itself) or `stop_sample` / `end_sample`
(stopped) or a `set_sample_stage_property` ramp to 0 followed by the stop (faded). A repeating melee sound shows as a
`start_sample` with `count=0` and no stop or `EOS` long after the fight's `EndCombatSim`, or as repeated starts of the
same `crc4k` at a period; the callers field tells the unit-loop table's path from any other. The file behind a `crc4k`
is found by hashing the install's sound files the same way (`proxy/evidence/0042/4797/map_samples.py INSTALL
mss_log.tsv`, which adds a `file=` field; read from the player's files at run time; nothing committed); `analyze_log.py` then prints one line per sample life (start, end, how it ended, loop count, position, file, callers) beside the Lua combat stamps. The driver of
the live check is `live_4797.py` beside it (claim check, guarded-file backup, launch, inject, scene, fight, prototype,
stop, restore). Checked on the harness's stand-in window (2026-11-20 04:22): the DLL loads through `bg_helper32 inject`,
hooks the stand-in's one matching slot (`PeekMessageW`), finds the main thread and runs a `mark` there; without
`mss_proto.ini` the prototype refuses `play`.

## 6. ASK-1 / premise A3: StopSound rows [D, C]

- **Count [D, 2026-11-20 03:50 EST]:** `proxy/evidence/0042/4797/trigger_census.py` over the instance copy c5h-b
  (Goldberg, build 4390913): 673 `.ftsxml` files in the install (archives under `Resource/` and loose files): 18,737
  `FTimedTriggerSound`, 7,049 `FTimedTriggerEffect`, 1,278 `FTimedTriggerAction`, 603 `FTimedTriggerTransfer`, **0 rows
  of any stop type**, and no file contains `FTimedTriggerStopSound`, `StopSound` or `SoundStop`. The 18 `.ftsxml`
  files in the instance's mods (`Documents\My Games`): 604 Sound, 732 Effect, 107 Action, 14 Transfer, **0 stop**.
  (The Effect and Transfer counts equal `effects-formats.md` 6; the Sound count covers all files, not only the unit
  files the controller reaches.)
- **The EXE's side [C]:** the class `FTimedTriggerStopSound` ("Stops a specific sound when fired") and a listener of
  the App ("SoundStop", "Stops a sound effect when triggered", vtable slot at 0xA3C794, listener 0x5635A0) exist; the
  listener posts a type-**0x16** record (writer 0x6B4FA0, same layout as 0x15) whose handler 0x57D5F0 (registered by
  0x6D9CE0 through 0x88F950, list 0x1A0E4BC) fades out every unit-loop slot of the record's unit. (`crash-load-8e81c3.md`
  4 read 0x5635A0 as "an Action trigger for the UI" `[I]`; corrected here: it is the StopSound listener.)
- **So A3 holds in its first form:** no shipped trigger file carries a StopSound row, so the controller has none to
  emit, and a loop's end must come from its owner (the Architect's OwnerEnd), as in stock it comes from the heartbeat.

## 7. E1 vs E2 [C]

The EXE's Lua event names (the string table around 0xA58800-0xA58C70) hold five audio events: `AudioPlay2DSound`
(registered by 0x6FE710), `AudioVolumeChanged`, `AudioDebugChangeMusic`, `AudioAdvanceCurrentPlaylistTrack`,
`AudioRewindCurrentPlaylistTrack`. None takes a position, a handle or a stop; the Lua debug functions of the App
(`GetSoundInstanceInfo`, `Get3DSoundInstanceChannelInfo`, `GetSoundListenerInfo`, `Begin/EndAudioLogging`,
`GetAudioLog`, `PauseSounds`, `Get/SetSoundRolloffFactor`, the volume knobs) read or tune but do not play. **E1 is
not enough; E2 (section 2's call and section 4's stop) is the entry.** This agrees with the Architect's search of the
mods' Lua.

The debug functions live (W68, ticket 5051, 2026-11-22, `[L]` on build 26577625, unit-combat-server.md 18.24.1 (b) and
(c)): they answer in the **InGame** Lua state (the harness's channel). `BeginAudioLogging()` then
`GetAudioLog().TriggeredSounds` (Sound, Time, Volume, Pan, Filtered, Denied, Unit, UnitMember) logs the EXE's own
trigger path only (its 3D ambience, its units' Fortify and vox sounds): sounds started through `Do3DSound` by the
proxy's executor do not appear in it. `GetSoundInstanceInfo(1, i)` for i = 0, 1, ... until "" lists the live 3D
`FSound` instances (an id, the script name, numbers, a yes/no flag); a sound ended by `FSound::Stop` stays in this list
for the rest of the process, a save load included (it reached 64 and stayed there), so the list counts lost voices
directly. `GetSoundInstanceInfo(0, i)` lists the 2D ones (3-6 in game).

## 8. The entries for epic 0590's build table (named; asked of lead-0590 on thread 0590 #7700, answered #7706)

| Name (lead-0590's form, 0590 #7706) | Kind | Old build (4390913) | What it is | Run-time check before the first call |
|---|---|---|---|---|
| `audio_play3d` | code | 0x8DC7F0 | `FAudioGame` 3D play: thiscall, (script index, context 0, &world position, 1.0, -1, 1.0, 1.0, 1.0, -1, -1, -1) -> `FSound*` or 0, `ret 0x2C`; main thread only | cross-check: its one caller of interest, the type-0x15 handler, calls it at 0x74C82A; it calls `Do3DSound` 0x90BD00 |
| `audio_game_obj` | data | 0x5F54F34 | the global pointer to the `FAudioGame` object (`this` of the play) | the object's vtable pointer equals 0xA99630 (a `vtable` row beside it if 0590 wants one) |
| `audio_sound_stop` | code | 0x90EDE0 | `FSound::Stop`: thiscall, no arguments, returns a bool | a short look-alike: checked by its operands (it pushes the string 0xA9FC38) and by its caller 0x8DB93F in the unit-loop record function |
| `audio_sound_fade` | code | 0x90EB90 | the `FSound` fade: thiscall (seconds, 0.0, 0), `ret 0xC` | checked by its caller 0x8DB6CD in the heartbeat ager and the string 0xA9FD74 it pushes |

How they are added (lead-0590, 0590 #7706): once 0590's stage 2 is on `main`, one row per address in
`exebuild/symbols/<area>.tsv` (name, kind, the old build's VA, consumer, note), the two `gen_table.py` commands of
`exe-build-table.md` section 6 (local tables only, D202), and for each code symbol the cross-check of that file's
section 8. Product code reads them through `VpExeAddr` (`ProxyExe::Addr` in the proxy) and never holds a literal. Not
done by 4797: stage 2 was not on `main` when this was written; it is the first step of the build ticket (4795).

Not an entry: the name -> index lookup goes through the DLL interface `ICvEngineUtility1::GetAudioTagIndex` (slot 127,
`proxy/model/policy-table.tsv` row 335) with `AUDIOTAG_3DSCRIPT`, which the proxy already carries. The prototype of
section 9 calls `FAudioManager::GetAudioTagIndex` (0x908A80, object 0x606BAF0) directly only because it runs without
the proxy.

## 9. The live check [L]

**Run.** 2026-11-20 04:34-05:19 EST on the workstation, Goldberg instance c5h-f (F:, build 4390913, stamp 0x546CD0A8
checked, F: read latency 5.4 ms max), claimed by w5-0042 on MC-B's grant (thread 0042 #7728). Stock: the instance's
32-bit DLL `i0ship-26234371-ia` (`3c2f47da...`), mods cp, vp, eui, no 64-bit patch; config.ini `[DisableSystems]` with
only OverlayDebug and ColorKeyEdit set (Combat, Particle, Audio on); Quick Combat off (read back false). Save
`C5H-4797-TINY_0018` (a copy of `C5H-3000-TINY-T0018`, SHA-256 `e337244c...`, Tiny, turn 18). The harness's no-focus
launch (cloaked, frame cap 30, game session muted by the harness), `live_4797.py` steps; the EXE ran relocated at base
0x3F0000 (every address in the log is the VA minus 0x10000). Guarded files backed up by SHA-256 and found equal after
the stop; the front end restored by hash; no dialog at any check; `cli.py stop` rc 0; leftovers clean. Logs and the
event file are in `C:/c5-64bit-work/0042/w5/` (scratch, not committed).

**The instrument.** `mss_log.dll` injected into the running game (`bg_helper32 inject`) hooked all 16 slots by name
(15 Miles, `PeekMessageW`), found the main thread (the one that makes every Miles call) and logged 1,384 sample-file
lines over 37 minutes; `map_samples.py` named every one of them from the install's files (0 unnamed). The first build
read the prototype's globals unrelocated (it took the mapped header's ImageBase, which the loader rewrites to the
actual base) and refused to play; later builds were injected beside it from other folders (the hooks chain; each build
logs to its own folder).

**Three stock melee fights** (a human warrior attacks a barbarian warrior on the next plot; `analyze_log.py` over the
named logs, Lua's `RunCombatSim` / `EndCombatSim` stamps put on the log's clock):

| Fight | Run -> End (s) | 3D sounds that reached Miles | Last `AIL_end_sample` after End |
|---|---|---|---|
| 1 (units 1052 vs 1053) | 110.598 -> 115.031 | 8 lives: `battleambvoxt2` + `clubvsclubt2` (2.66 s), `leavesrunt2` + `warriorrunt2` (1.59 s), `clubvsclubt1` + `battleambvoxt1` (0.19 s), `leavesrunt2` + `warriorrunt2` (0.71 s) | +0.12 s |
| 2 (1054 vs 1055) | 393.335 -> 397.642 | 12 lives, the same kinds (`dirtt*` runs on that plot) | +0.25 s |
| 3 (1056 vs 1057) | 40.677 -> 45.210 (other log) | 10 lives, the same kinds | +0.15 s |

- Every 3D sample was set to Miles loop count 0 (loop forever), started with `AIL_resume_sample` from the 3D play's
  chain (return addresses at VA 0x8DC899 in 0x8DC7F0, 0x90C028 in `Do3DSound`, 0x90FC01 / 0x90FA29 in the `FSound`
  play) and ended by `AIL_end_sample` from the manager's update; no `EOS` (no sample ended by itself). The sounds come
  in pairs at one position (a battle-ambience loop with its voice loop, a run loop with its surface loop). This is the
  heartbeat of 4.2 working: each loop lives while its clip re-triggers it and ends a few tenths of a second after the
  last trigger.
- Only four 3D sample handles were ever used in the session (2A91F5E4, 2A91FD80, 2A92051C, 2A920CB8), and all four
  were busy at once in fights 2 and 3: a fight of more figures meets the voice limit `[I: the number of 3D voices
  allocated at start-up is not read]`.
- The one-shot unit scripts tried with the prototype's `diag` (the script lookup and `Do3DSound`'s play rules 0x908260
  only): `AS3D_UNIT_WARRIOR_ATTACK_VOX_B`, `..._SWORD_SWING`, `..._VICTORY_VOX_B`, `..._VICTORY_VOX_T1` were all refused
  by the rules, the looping `..._BAT_AMB_T1_BLUNT` and `..._RUN_T1` passed. No one-shot unit sound appeared in any of
  the three fights. *Why (2026-11-21, section 12) [C]: the instance's effects slider is 0, and the rules refuse a
  non-looping, non-music script when its sound type's effective volume is at most 0.01; loops pass that test.*
- 0599's repeating melee sound did not occur (three fights, 25 s, 25 s and 10 s of quiet after each).
- The 2D ambience bed (`DeepOceanBed.mp3`, a 2D script started by the soundscape, Miles loop count 0) restarted every
  2.0 s with a volume ramp from 0 to 1 over 1 s, and was faded and ended by the manager each time; not a fight sound.

**The trigger's own arguments**, read from the stack in the hook of `AIL_resume_sample` (the dwords after the return
address 0x8DC899, i.e. `Do3DSound`'s arguments, then 0x8DC7F0's after its return address 0x74C82F): section 2 step 4.
That dump is what found the prototype's error (1.0 where the trigger passes -1.0).

**The prototype** (commands from the driver's file, run on the main thread at the top of a frame from the
`PeekMessageW` hook; the name to index through `GetAudioTagIndex`, the call through `audio_play3d`, the stop through
`audio_sound_stop`):

| Time (EST) | Command | Result in the Miles log |
|---|---|---|
| 05:12:27 | `play AS3D_UNIT_WARRIOR_BAT_AMB_T1_BLUNT` at (-1190.69, 12.29, 387.59) (axes in the wrong order) | started: `clubvsclubt1.mp3`, loop count 0, `AIL_resume_sample` from VA 0x8DC899; returned `FSound` FDCF5C00 |
| 05:12:59 | `stop 0` | `AIL_stop_sample` from `FSound::Stop` (VA 0x90EE78); `FSound::Stop` returned 1. The loop had played 32 s: **no heartbeat ends a loop started outside the trigger path** |
| 05:14:36 | `play AS3D_UNIT_WARRIOR_BAT_AMB_T1_BLUNT` at the world point of plot (11,22) from `HexToWorld` (-1056.00, 221.74, 18.51) | started; Miles position (-1056.00, 18.51, 221.74): the call takes (x, y, z) with z up |
| 05:14:40 | `stop 1` | `AIL_stop_sample` 3.93 s after the start |
| 05:15:58 | `play AS3D_UNIT_WARRIOR_BAT_AMB_T2_BLUNT`, then `stop 2 1.0` (fade 1 s) after 2.9 s | started; the fade ended it at once (`AIL_end_sample` at the next update, no ramp) |
| 05:16:07 | `play AS3D_UNIT_WARRIOR_SWORD_SWING` | refused by the play rules: returned 0, no Miles call |
| earlier, 04:45-05:09 | five `play` calls with 1.0 in place of -1.0 | all returned 0, no Miles call (the error above) |

The game session's peak meter (`tools/harness/audio.py meter`) read 0 throughout, also for the stock ambience, so it
proves nothing about audibility here; the Miles log is the evidence that the sound was started and stopped.

**What it decides for the build ticket (4795).** E2 as in section 8: the shell calls `audio_play3d` with the script
index from `GetAudioTagIndex` (kind 4) and the plot's world point in `HexToWorld`'s axes, passing -1.0 / -1 for every
"use the script's value" argument exactly as above, on the main thread; it keeps the returned `FSound` per owner and
ends it with `audio_sound_stop` (the fade gives nothing more for 3D sounds). *Corrected 2026-11-22 by ticket 5051: `audio_sound_stop` loses the Miles voice for the rest of the process (4.1); the executor ends sounds through the fade with 0.0 s since 5051.* Every looping script the shell starts is
the shell's to stop: the EXE's heartbeat does not see it (shown live: 32 s until stopped). Scripts that the play rules
refuse return 0, which the shell must treat as "not started", not as an error. Four 3D voices are shared with the EXE's
own sounds (the walk loops while the EXE's figures walk, 1.6 of the Architect's note).

## 10. Not verified

- Every `[I]` above; the exact meaning of the 3D play's fourth to eleventh arguments beyond the values the trigger
  passes (live: -1.0 / -1 work, 1.0 in the float places makes the call return 0); which of the three trailing ints sets a
  playback position.
- The pool reuse of `FSound` objects (4.3) and whether a pointer to an ended sound can be stopped harmlessly (the
  prototype's stale-handle test did not run: the one-shot it needed was refused by the play rules).
- ~~Why the play rules refuse the one-shot unit scripts in this configuration, and whether stock plays any one-shot unit
  sound at all~~: answered 2026-11-21 by section 12 [C]: the volume knob rule with the instances' sliders at 0; stock
  on an instance plays none, stock with the effects slider above 0 passes that rule. Still open: the number of 3D voices,
  and a Miles log of stock and route S with the slider above 0 (section 12.4).
- Whether the type-0x15 handler's flag that records non-looping sounds is ever set in stock play.
- The thread of the Miles mixer and of the end-of-sample callbacks (the log names them).
- What 0599's repeating melee sound is: section 4.2 names the first suspect only (section 11: the camera-variant
  reproduction attempt on stock; 11.1, 2026-11-23: not reproduced on the product path either, where route S removes
  the stock form of 11).

## 11. Ticket 0599: reproduction attempt on stock with the camera moved (W23, 2026-11-20)

Ticket 0599, the owner's answer of 2026-11-20 11:14 [reworded for privacy]: the fault was seen mostly with swordsmen early in the game, possibly
while panning or with the camera moving away once combat began. 4797's three fights (section 9) and 4771's S2 (8 fights) were watched
with a still camera. Suspect (b) of the Architect's note 1.7 (a StopSound trigger never reached) is already out: no
shipped trigger file has a StopSound row (section 6). Suspect (a) is the unit-loop heartbeat of 4.2.

**Method.** Driver `proxy/evidence/0042/0599/live_0599.py` (reuses `../4797/live_4797.py` for check, prep, launch,
inject, stop, restore; the instrument is section 5's `mss_log.dll`, logging only, no `mss_proto.ini`). Scene: Iron
Working granted, human swordsmen next to barbarian swordsmen (`UNIT_SWORDSMAN` / `UNIT_BARBARIAN_SWORDSMAN`) near the
capital, area made visible, Quick Combat off. Camera variants per fight: `still` (baseline), `jump` (`UI.LookAt` a far
plot DELAY s after the order), `pan` (the arrow-key camera events `SerialEventCameraStartMovingRight` /
`...Stop...` for 2 s), `select` (another unit selected and looked at: a unit cycle), `offscreen` (camera away before
the order), `endturn` (a forced end of turn DELAY s after the order), and `aiturn` (barbarian swordsmen placed next to
damaged human units, camera far, forced end of turn: the barbarians attack in the AI turn). After 20 s of quiet the
camera is put back on the fight's plot for 10 s (a loop held while off screen would show then). A repeat shows in the
Miles log as a 3D `start_sample`/`resume_sample` with no end long after `EndCombatSim`, or as repeated starts of one
file after it (section 5's reading).

**Run.** 2026-11-20 from 12:55 EST on the workstation, Goldberg instance c5h-c (C:, build 4390913, stamp 0x546CD0A8
checked), claimed by w23-0042 on MC-B's grant (thread 0042, 12:45). Stock: the current pin's 32-bit DLL
`i0ship-e96457fd-ia` (Release-5.4.8-49-ge96457fd51) put in for the run by `provision.py set-dll` and taken out after
(`--previous`), mods cp, vp, eui, no 64-bit patch; config.ini `[DisableSystems]` with only OverlayDebug and ColorKeyEdit
set (Combat, Particle, Audio on). Save `C5H-0599-TINY_0018` (a copy of `C5H-3000-TINY-T0018`, SHA-256 `e337244c...`,
Tiny, Spain, turn 18). The harness's no-focus launch (cloaked, frame cap 30); in game 12:57, `mss_log.dll` injected
13:05 (16 of 16 slots), fights 13:06-13:43, `cli.py stop` rc 0 at 13:50, no dialog at any check, the DLL put back
(`e589306e...`), the 14 guarded files equal (8) or absent as before (6) by hash, front end restored, leftovers clean,
c5h-c released 13:55. Logs in `C:/c5-64bit-work/0042/w23/` (scratch, not committed): `bin/mss_log.tsv`,
`mss_log.named.tsv`, `events.jsonl`, `lives.txt` (`post.sh`: `map_samples.py` then `analyze_log.py`), `overrun.py`
(each sample's end against the combats).

**Fights [L]** (a human swordsman attacks a barbarian swordsman on the next plot, all at full health; times on the
Miles log's clock, Lua's `RunCombatSim` / `EndCombatSim` stamps put on it; "camera" is the moment of the camera step;
"last end" is the last 3D sample's `AIL_end_sample` of that fight against `EndCombatSim`):

| # | Camera variant | Run -> End (s) | 3D sample lives in the fight | Last end vs End | After End |
|---|---|---|---|---|---|
| 1 | still | 3.97 | 6 (vox + sword-vs-sword pair, run + surface pairs) | +0.03 s | nothing in 16 s |
| 2 | jump away 0.5 s after the order | **12.07** | 0 (the camera left before the first trigger) | - | nothing; camera back 9.1 s after End: nothing |
| 3 | pan right (arrow-key camera events) 0.5 s after, 2 s | **12.07** | 4, all ended 1.3 s after the pan began | -10.1 s | camera back 11.3 s after End: `battleambvoxt1` + `battleambvoxt2` start again, 0.73 s, then end |
| 4 | select another unit, look at it, 0.5 s after | 3.83 | 14 | +0.29 s | nothing |
| 5 | camera away before the order | 4.70 | 9 (the attacker's selection brought the camera back `[I]`) | +0.26 s | nothing |
| 6 | jump away 2.0 s after | **12.03** | 5, ended at the jump (+0.33 s) | -9.7 s | camera back 10.8 s after End: `battleambvoxt2` 0.93 s |
| 7 | pan 1.5 s after | **12.07** | 5, ended 1.4 s after the pan began | -9.1 s | camera back 12.2 s after End: `battleambvoxt1` 0.10 s, `battleambvoxt2` 0.17 s |
| 8 | select another unit 2.0 s after | 3.97 | 5 | -0.77 s | nothing |
| 9 | jump away 0.5 s after, back after 2 s | 6.37 | 12, all from the return on (run, fight, run again) | +0.26 s | nothing |
| 10 | jump away 0.5 s after, back after 5 s | **12.07** | 18 from the return on: run, fight, run, fight cycles until End | +0.06 s | nothing |
| 11 | jump away 1.0 s after, back after 8 s | **12.04** | 2 before the jump, 10 from the return on | +0.27 s | nothing |
| 12 | jump away 2.5 s after, back after 1 s | 5.63 | 14 | +0.26 s | nothing |
| 13 | jump away 0.1 s after | **12.07** | 0 | - | camera back 8.8 s after End: nothing |

What the 13 fights show [L unless marked]:
- **No sound outlived its fight.** In every fight every 3D sample was a Miles loop (count 0) started by the 3D play
  and ended by the manager's `AIL_end_sample`, at the latest 0.29 s after `EndCombatSim`; no `EOS`, no sample left
  open, no start of a fight's file after its End except the case below. 0599's fault (a sound "over and over long
  after the combat") did **not** reproduce.
- **A fight the camera leaves runs to the EXE's 12 s limit** (`../05-graphics-composition/combat-choreography.md` 1
  item 4): 7 of 7 fights where the camera left and stayed away (or came back late) ended 12.03-12.07 s after
  `RunCombatSim` instead of 3.8-6.4 s; the damage messages wait behind it (`SerialEventUnitSetDamage` 0.03 s after
  End). The figures do not play their choreography while off screen `[I]`, so nothing ends the combat but the limit.
  *(2026-11-21, ticket 4737: the handler's test of section 2 step 3 is a tier test, not a visibility test (section
  13); the silence off screen comes from the figures not being stepped, `../05-graphics-composition/unit-controller.md`
  18, not from the handler `[I]`. Replaced: "the same visibility test that keeps their sounds from starting".)* This is new: 0557's 25 combats
  (`community-melee-combat-stall.md`) were all watched with the camera on them.
- **While off screen the fight is silent** (the figures are not stepped, so no trigger fires `[I]`; *was "the
  handler's visibility test, section 2 step 3": that test is the tier test, section 13, corrected 2026-11-21*): the unit loops end at
  the next heartbeat after the camera leaves (fights 3, 6, 7) and none starts while it is away (fights 2, 13).
- **Coming back to a fight that is still running** replays its choreography from where the figures stood: approach
  runs, sword-vs-sword and battle vox loops in cycles (fight 10: 18 sample lives in 5.8 s) until the 12 s limit, then
  everything ends with End. To a player this is a melee sound that goes on for up to 12 s after the attack began.
- **Coming back after End** to a fight that had started its sounds before the camera left gives one short restart of
  the two units' battle-ambience vox loops (files `battleambvoxt1.mp3` / `battleambvoxt2.mp3`, 0.1-0.9 s, then ended
  by the heartbeat), 10.8-12.2 s after End: fights 3, 6, 7, three of three; not when the fight had made no sound
  before (2, 13) `[I: the figures keep a fighting pose until seen again and play its clip once]`. These are the only
  five 3D samples of the whole run that ended outside a combat more than 0.28 s after an `EndCombatSim`.

**More combats [L]** (same session):
- Fights 14 and 15 (the end-of-turn variant; the end of turn was refused, see below, so the camera stayed): 4.00 s
  each, last end +0.15 s and +0.05 s.
- **The AI turn** (turn 18 -> 19): four barbarian swordsmen put next to four human units (damage 40), the camera on a
  far plot, a forced end of turn. 15 combats ran in the AI turn, all with barbarian attackers (swordsmen and others), on
  human swordsmen and on units of players 1 and 2 (spearmen among them), Run -> End 2.93-6.00 s; every one had its sounds (the
  game shows a watched combat outside the human's turn), and every 3D sample ended at most 0.28 s after its End
  (the last 0.23 s after the last End); nothing played on into turn 19.
- Fights 16 and 17 with the defender killed (damage 90 first): still camera 4.77 s, last end +0.25 s; camera away
  0.5 s after the order and back 5 s later: 12.07 s (the limit), only a run pair on the return, nothing after End.

**Totals:** 32 combats (17 human attacks under the camera variants, 15 AI-turn attacks), 318 3D sample lives: every
life was a looping script ended by the audio manager; no sample outlived its combat by more than 0.28 s except the five
short vox restarts on the camera's return. **0599's repeating melee sound was not reproduced on stock** `[L]`. Suspect
(a) (a heartbeat-kept loop never cleared) did not show: every unit loop ended within one heartbeat of its last trigger,
also when the fight hit the 12 s limit, when its figures were off screen, and when the defender died. Suspect (b) is
ruled out by section 6.

**What stays open and where to look next** `[I]`:
- What the owner hears may be the 12-s case seen from the player's side: a fight the camera left and came back to plays its
  approach-and-strike loops again until 12 s after the attack (fight 10: 5.8 s of cycling sounds), long after the
  player saw it start. That is stock's own behaviour; under route S it cannot occur (the server owns the fight's clock,
  the Architect's note 1.7).
- The product path: **run 2026-11-23, section 11.1** (W105): not reproduced on route S, no fight at the 12 s limit, no
  replay on the camera's return. *(Replaced 2026-11-23: "The product path today (D147 ...) was not tried here", with two
  guesses for what it would do; D147's EXE-paced form is no longer what main ships, route S is.)*
- The end-of-turn camera jump was not run as planned: the save's turn 18 had an `ENDTURN_BLOCKING_PRODUCTION`
  ("Madrid is ready for a new construction project", notification 12) that a queued Worker, a dismissal of all 22
  notifications and `turns.py end` did not clear (the same stuck block as `community-melee-combat-stall.md` 3.2 on
  another save). A direct `CONTROL_FORCEENDTURN` did not end the turn within 150 s, but turn 19 came about 5 min later,
  right after the driver's popup-closing step (`UIManager:DequeuePopup` over the list of `live_4797.py`) `[I: a hidden
  popup held the turn]`. The AI turn's combats above are the end-of-turn camera case in effect.
- `live_4797.py restore` returns 6 although every guarded file is back: its check counts only the statuses
  ok/same/match, while `backup.verify` reports `equal` and `absent` (8 and 6 here) `[L]`.

### 11.1 The same reproduction on the product path: route S as main ships it (W105, 2026-11-23)

**Verdict `[L]`: 0599's repeating melee sound did not reproduce on route S in 30 fights, and the stock form W23 found
(a fight the camera leaves runs to the EXE's 12 s limit and replays its loops when the camera comes back) does not
occur on route S.** No fight came near the 12 s limit, no loop of a fight was alive 3 s after its End, and no sound of a
fight started after its End when the camera came back. The cause W23 named for stock (11 above) is in the EXE's own
fight playback, which route S does not run; nothing in route S needed a fix for 0599. This is the section to read for
0599 on the product path (`../05-graphics-composition/unit-combat-server.md` 18 has the route S fight timing work).

**Setup.** Workstation, c5h-c (C:, build 4390913 by the build registry, stamp 0x546CD0A8), MC-B's grant #11862. Built
from origin/main def1c1444 through quietlock, no LTO (`proxy/evidence/0042/0599/w105/scripts/build_all_w105.sh`,
`mklayout_w105.sh`): server.exe `91a95108`, x64 DLL `b84aa3e9`, proxy `35cfded1`, composite `348df4ba`; the Miles
logger built from `../../proxy/evidence/0042/4797/mss_log.cpp` (`6f339d72`). One launch 15:54-16:12 EST of
`server/evidence/0042/4779/run_4779.py` session **S35**, arm **p** (route S as main ships it: ServerUnits 1, no TEST key;
route S ARMED read back from the logs), `--miles`; save `C5H-4779-TINY_0018` (a copy of `C5H-3000-TINY-T0018`,
`e337244c...`, W23's save), Quick Combat off and the Effects volume knob 0 -> 1, each read back. Stop by `cli.py stop`
(0700's wait), restore by SHA-256 ok, `provision verify` rc 0, no box, no dump; c5h-c released 16:20.

**The cases** (W23's driver re-done as `run_4779.py` cases, the same scene: a human Swordsman attacks a barbarian
Swordsman on the next plot near the capital; W23's fight number in brackets): `art_still` (1), `art_jump` 0.5 s (2),
`art_jump2` 2.0 s (6), `art_jump01` 0.1 s (13), `art_pan` 0.5 s (3), `art_pan15` 1.5 s (7), `art_select` 0.5 s (4),
`art_select2` 2.0 s (8), `art_offscreen` (5), `art_jumpback1` away 2.5 s, back after 1 s (12), `art_jumpback2` away 0.5 s,
back after 2 s (9), `art_jumpback5` 0.5 / 5 s (10), `art_jumpback8` 1.0 / 8 s (11), `art_kill_still` (16),
`art_kill_jumpback5` (17); then `defence` (AI turn, camera on the attacked pairs) and `defence_far` (AI turn with the
camera far, back on the first pair after it: W23's AI turn). The jump cases look back 20 s after the order (after End
here, since no fight lasted that long) and record 10 s more. The end of turn ended normally on this save (two AI turns,
turn 18 -> 20): W23's stuck end of turn did not recur.

**Per fight [L]** (`proxy/evidence/0042/0599/w105/S35/table_S35.txt`, `scripts/table_0599_w105.py`: 3D sample lives
within 250 world units of the fight's centre, terrain ambience by file name counted apart; times on the wall clock via
the probe's Lua sync; "away" = the camera step after `RunCombatSim`, "back" = the return against `EndCombatSim`):

| Case (W23 #) | Run -> End s, route S (stock W23) | Away s after Run | Back s after End | 3D lives (loops) | Last end vs End | After End at the fight |
|---|---|---|---|---|---|---|
| art_still (1) | 4.10 (3.97) | - | - | 32 (4) | +0.55 | nothing |
| art_jump (2) | 4.27 (**12.07**) | 0.49 | 16.3 | 37 (6) | +0.50 | nothing (stock: nothing) |
| art_jump2 (6) | 3.63 (**12.03**) | 2.03 | 18.6 | 30 (4) | +1.15 | nothing (stock: battle vox restart 0.93 s on return) |
| art_jump01 (13) | 4.87 (**12.07**) | 0.10 | 15.4 | 37 (6) | -0.02 | nothing |
| art_pan (3) | 4.07 (**12.07**) | 0.50 | 18.6 | 25 (4) | +0.70 | nothing (stock: two battle vox restarts on return) |
| art_pan15 (7) | 3.90 (**12.07**) | 1.50 | 19.9 | 31 (6) | +0.62 | nothing (stock: two restarts) |
| art_select (4) | 4.43 (3.83) | 0.58 | 16.6 | 36 (6) | +0.35 | nothing |
| art_select2 (8) | 4.00 (3.97) | 2.00 | 18.1 | 28 (4) | +0.52 | nothing |
| art_offscreen (5) | 3.30 (4.70) | far before | 16.8 | 34 (4) | +1.38 | nothing |
| art_jumpback1 (12) | 3.83 (5.63) | 2.50 | -0.22 (back before End) | 32 (4) | +1.04 | nothing |
| art_jumpback2 (9) | 3.87 (6.37) | 0.50 | -1.23 | 31 (4) | +0.69 | nothing |
| art_jumpback5 (10) | 3.43 (**12.07**) | 0.51 | 2.45 | 30 (4) | +1.03 | nothing (stock: 18 lives of replay cycles until the limit) |
| art_jumpback8 (11) | 3.67 (**12.04**) | 1.00 | 5.48 | 35 (4) | +0.76 | nothing |
| art_kill_still (16) | 6.10 (4.77) | - | - | 47 (6) | +0.37 | nothing |
| art_kill_jumpback5 (17) | 4.77 (**12.07**) | 0.50 | 0.85 | 22 (6) | +0.49 | nothing |
| defence, 6 AI-turn fights | 2.49-5.81 (2.93-6.00, 15 fights) | camera on | - | 26-47 (2-6) | -0.02 to +2.87 | fortify vox and sword swings 1.0 s and 22.6 s after the last fight's End: the units' fortify clips (one-shots, 0.2-0.6 s), not the fight's |
| defence_far, 9 AI-turn fights | 2.18-7.43 | far all turn | 3.9-45.0 | 23-75 (2-6) | +0.13 to +2.49 | nothing at the return; after the last fight a fortify vox and two animal calls (snort, moo), one-shots |

What the 30 fights show [L]:
- **No fight ran to the EXE's 12 s limit** (longest 7.43 s, an AI-turn fight; the 15 human attacks 3.30-6.10 s). On
  stock 9 of W23's 17 human attacks hit it, every one where the camera left and did not come back in time. Route S's
  fight clock does not depend on what the camera sees: a fight the camera left ends on its own schedule (the hold
  dropped by OwnerEnd 30 of 30, 0 by the watchdog, 0 at a limit).
- **No replay on the camera's return.** In the four cases with the camera back while the fight still ran
  (`art_jumpback1`, `art_jumpback2`, `art_kill_jumpback5` returns before or within 1 s of End, `art_jumpback5` 2.45 s
  after) and in the eleven returns after End, no 3D sample of the fight started after its End. W23's stock run had the
  return replay (fight 10: run/strike loops cycling to the limit) and the battle-vox restarts of 0.1-0.9 s (fights 3, 6,
  7); neither showed here.
- **Loops are short and all ended.** The whole launch has 1,683 sample lives, 1,460 of them 3D; 140 3D loops, the
  longest 2.47 s (a run loop, `swordsmanrunt2` / `leavesrunt2`), every one ended by `AIL_end_sample`; none open at the
  log's end but two 2D terrain beds and two coast-wave one-shots. Outside every fight's window (Run - 0.5 s to End +
  0.5 s) 127 non-ambient 3D samples started, none a loop: fortify vox and fortify foley (`fortifyvox*`, `fortify`,
  `staff*`), animals (`sheepbaa`, `elephantvox`, `snort`, `moo`, `pigoinksqueal`), footsteps in forest, three sword swings
  with the fortify vox at the AI turn's end; no flesh, clash or battle-vox file.
- Last ends of a fight's sounds fall at -0.02 to +2.87 s after End (stock W23: +0.03 to +0.29 s): route S lets one-shots
  ring out by themselves (4734's change, `server/evidence/0042/4779/miles_4779.py`). The late ones are one-shots, never
  loops: the charge vox `runchrgvoxt2-*` (+2.2 to +2.9 s, AI-turn fights), fortify vox and foley, victory vox and body
  falls (+0.8 to +1.7 s).

Not run: the human-input form of the case (`art_hi`: mouse and keys) and other unit types; W23's stock numbers are from
a different day's DLL pin (i0ship-e96457fd-ia) with the same save, so the stock column is W23's, not a same-launch pair.
Raw logs (local only, they carry EXE return addresses, D83): `F:/c5-64bit-work/0042/w105/miles-S35-w105/mss_log.tsv`
sha256 `383fd453ea1bba9953881720028fb7f69c45680a2ddbb25baf7f23e7dbe2a6db` and its named copy beside it.

## 12. The play rules in full, and why the test instances start no one-shot unit sound (ticket 4734, W37, 2026-11-21)

Written 2026-11-21 from 11:17 EST by w37-0042 (team-0042, account B), branch `w37-4734`, offline. `[C]` read by hand
with llvm-objdump in the analysis copy of the old build (PE stamp 0x546CD0A8 checked before reading), written here as
behaviour (D83); `[D]` read from the files of the Goldberg instance copies at run time (counts and names only);
`[L]` the recorded runs named; `[I]` inferred.

### 12.1 The rules (`FAudioManager` 0x908260, thiscall, (script data, is-3D byte), returns a bool; `ret 8`) [C]

`Do3DSound` calls it with the is-3D byte 1, right after the script lookup and before it takes an `FSound` from the
pool (section 2 step 4). In order, a script is refused (returns 0) when:

1. the audio system's 3D (or 2D) side is not up (a field of the `FAudioSystemMiles` object at manager +0x9C);
2. a manager counter (+0xA58) is above twice another (+0xDC) (it logs three strings first) `[C for the test; I what
   they count]`;
3. the script data is missing;
4. the script is marked music (`bIsMusic`, flag 0x04) and a music-side test says no;
5. **the script's sound type's volume knob is disabled ("INFO: FAudioManager::DoSound(): Soundtype disabled due to
   volume knob being disabled."), or the knob's effective volume is at most 0.01 and the script is neither looping
   (`bLooping`, flag 0x10) nor music (flag 0x04) ("... Soundtype disabled due to volume knob being turned all the way
   down.")**. The effective volume is the knob's own value times its parent's, recursively, clamped to 0..1;
6. `bDontPlay` (flag 0x40) is set or `iMaxVolume` is 0 ("... marked as DONT PLAY in XML, or zero max volume.");
7. `iPercentChanceOfPlaying` (byte +0x1C, 100 when absent) loses its roll ("... percent chance of playing.");
8. the sound define's instance record (one per `SoundID`, kept apart for 2D and 3D: a count of playing instances and
   a "not again before" time) says no: when the time is set, any playing instance refuses, and so does a time still
   in the future; the time is set when the last instance of a define with `iMaxTimeMustNotPlayAgain` ends (now plus a
   random time between the min and max);
9. the playing count reaches the cap: `iDontPlayMoreThan` (byte +0x1D) when set and not above the global cap, else the
   global `iMaxNumSimultaneousSameSounds` (`AudioParams.xml`: **10** `[D]`). The count is per `SoundID`, so every
   unit's `ATTACK_VOX_B` shares one count. Rules 8 and 9 log nothing.

The script data fields, from the XML reader (0x916400 onwards, the tag strings pushed beside each store) [C]: +0x0C the
sound type (`SoundType`), +0x0D `iMinVolume`, +0x0E `iMaxVolume`, +0x1C `iPercentChanceOfPlaying`, +0x1D
`iDontPlayMoreThan`, +0x1E flags: 0x01 `bDontTriggerDuplicates`, 0x02 `bDontTriggerDuplicatesOnUnits`, 0x04
`bIsMusic`, 0x08 `bOnlyTriggerOnUnitRuns`, 0x10 `bLooping`, 0x40 `bDontPlay`, 0x80 `bStartFromRandomPosition`.
After the rules, `Do3DSound` still refuses a sound beyond its cut-off distance (0x907230, section 2 step 4) and when
the 3D pool has no free `FSound`.

### 12.2 The knobs and the test instances' sliders [D]

- `AudioParams.xml` `SoundTypes`: `GAME_SFX` ("most unit sounds") and `GAME_INTERFACE` are children of
  `USER_VOLUME_SFX`, the knob of the options screen's effects slider; `GAME_MUSIC*` of `USER_VOLUME_MUSIC`, the speech
  types of `USER_VOLUME_SPEECH`, `GAME_AMBIENCE` of `USER_VOLUME_AMBIENCE`.
- The unit 3D scripts (`AS3D_UNIT_*`, 14,079 rows in 233 `*3DScripts.xml` files of c5h-f's install): 14,062
  `GAME_SFX`, 17 `GAME_INTERFACE`.
- **Every test instance's `UserSettings.ini` has `Music.Volume = 0`, `Effects.Volume = 0`, `Ambience.Volume = 0`,
  `Speech.Volume = 0`** (read 2026-11-21 11:25 EST on c5h-a, c5h-b, c5h-c, c5h-d, c5h-f, c5h-new-a; the provisioning
  writes it, `../06-tooling-and-workflows/silent-start.md` 1 and `tools/instances/tests/test_provision.py`). The
  harness's own mute (`tools/harness/audio.py`, the Windows session's `SetMute`) is separate and does not touch the
  game's knobs.

### 12.3 What it explains [C for the rule, L for the counts named]

On a test instance every unit script that does not loop is refused at rule 5, before any voice is taken, and every
looping one passes (at volume 0). That accounts for every observation so far:
- 4797's prototype (section 9): the one-shots `ATTACK_VOX_B`, `SWORD_SWING`, `VICTORY_VOX_B`, `VICTORY_VOX_T1` refused
  by the rules, the loops `BAT_AMB_T1_BLUNT` and `RUN_T1` passed; the 2D ambience bed (looping, `GAME_AMBIENCE` at 0)
  started every 2 s.
- Stock on the instances (sections 9 and 11, c5h-f and c5h-c, both sliders 0): 318 3D sample lives in 32 combats,
  every one a loop, no one-shot: **stock meets the same rule**, at the same place in `Do3DSound`.
- 4723's smoke (`../05-graphics-composition/unit-combat-server.md` 10.6, c5h-a): two loops played, the one-shot
  refused; the same on build 26577625 (c5h-new-a, 10.5).
- W24's route S runs on c5h-b (`unit-combat-server.md` 10.7): played 44 = stopped by their SoundStop 44 (all loops),
  refused 2846 (the one-shots and a few loops).
- The sound executor calls the 3D play with the same eleven arguments as the Sound trigger's handler (section 2
  step 4; `proxy_sound_exec.cpp` `CallPlay`) [C]: no argument, flag, 2D/3D choice or listener state differs. The
  visibility test and the unit-loop table are the handler's, before the call (section 2 step 3).

So the one-shot refusals measured so far are a property of the test instances' settings, not of route S. A player
whose effects slider is above 0.01 (the game's default is not read here) passes rule 5 on both sides; whether the
other rules (8, 9, the cut-off, the voices) then treat route S's starts like stock's is 12.4's open question.

Census of the unit scripts by kind (`server/evidence/0042/4734/script_kinds.py <install>`, 2026-11-21 on c5h-f; the
kinds are classes by name suffix `[I]`; looping includes music, none here) [D]:

| Kind (by name) | Rows | Looping | One-shot |
|---|---|---|---|
| victory vox | 275 | 0 | 275 |
| death vox | 101 | 0 | 101 |
| attack vox | 103 | 0 | 103 |
| battle-ambience vox (`BAT_AMB_VOX`, `BATTLE_AMB_VOX`) | 172 | 172 | 0 |
| battle ambience, weapon against weapon (`BAT_AMB_*`) | 1,591 | 1,530 | 61 |
| weapon swing | 209 | 0 | 209 |
| impact / hit | 3,228 | 15 | 3,213 |
| fire / shot / launch | 1,139 | 103 | 1,036 |
| death / fall | 1,119 | 14 | 1,105 |
| run / walk / move | 2,722 | 1,139 | 1,583 |
| charge / attack (other) | 5 | 0 | 5 |
| other vox | 272 | 0 | 272 |
| other | 3,143 | 38 | 3,105 |
| all | 14,079 | 3,011 | 11,068 |

Of the one-shots, none has `bDontPlay`, 161 have `iMaxVolume` 0, 253 a chance below 100, 1,854 an
`iDontPlayMoreThan`. Everything a fight voices (attack, victory and death cries, swings, hits, shots) is a one-shot;
the loops are the weapon-against-weapon beds, the battle vox beds and the runs.

### 12.4 Open (2026-11-21)

- ~~A Miles log of the same fights on stock and under route S with the effects slider above 0~~: run 2026-11-21
  11:39-11:57 EST on c5h-c (`../05-graphics-composition/unit-combat-server.md` 10.8) [L]: with the knob raised at run
  time (`SetVolumeKnobValue(USER_VOLUME_SFX, 1)`) the executor's refusals fell from 98.5 % to 18 % (748 of 912 played),
  and route S started about as many one-shots per shown fight as stock (549 / 483 placed, 686 / 713 in the window);
  the gaps are run / surface loops and the 3 s ring-out. Melee and air on the Tiny save (12:15-12:37, 10.9) [L]: route S
  voices every melee fight and nothing outlives one by more than 1.9 s on either side, but starts 2-5 times more
  one-shots than stock (per-figure footsteps, arrow pulls, swings), 36 % of its starts refused. ~~Still open: why
  stock's Sound-trigger path starts fewer per-figure one-shots~~: answered 2026-11-21 by section 13 (ticket 4737): the
  handler's tier test, which route S does not apply, plus route S's extra 1600 / 1280 entries. Still open: which rule
  refuses route S's surplus (rule 9's count of 10 is met by the 10 simultaneous `FORTIFY_VOX` starts, section 13.3;
  the cut-off and the voices are not read).
- The de-duplication flags (`bDontTriggerDuplicates` 0x01, `bDontTriggerDuplicatesOnUnits` 0x02) are tested only in
  the App's database loader (0x409F1E, 0x40A499), not in the play rules or `Do3DSound` `[C]`; which part honours them
  at play time is not read.
- The options screen's default for every knob is 1.0 (`OptionsMenu.lua`'s defaults button sets each knob to 1)
  `[D]`; the ini keys map to the knobs `Music.Volume` -> `USER_VOLUME_MUSIC`, `Effects.Volume` -> `USER_VOLUME_SFX`,
  `Ambience.Volume` -> `USER_VOLUME_AMBIENCE`, `Speech.Volume` -> `USER_VOLUME_SPEECH` (the pairs pushed together at
  0x4B4FDA-0x4B5023) `[C]`. A default player passes rule 5. *(Replaced 2026-11-21: "the game's default effects volume
  for a new player is not read".)*
- Rule 2's counters and rule 4's music test are named only by their tests.

## 13. The tier test: why stock starts fewer per-figure one-shots than route S (ticket 4737, W39, 2026-11-21)

Written 2026-11-21 from 12:52 EST by w39-0042 (team-0042, account B), branch `w39-4737`, offline (no instance). `[C]`
read by hand with llvm-objdump in the analysis copy of the old build (PE stamp 0x546CD0A8 checked before reading),
written as behaviour (D83); `[D]` read from c5h-f's install and VP's database copy at run time (names, attribute values
and counts only; nothing copied into git); `[L]` W37's recorded Tiny pair (`../05-graphics-composition/unit-combat-server.md`
10.9, logs `F:/c5-64bit-work/0042/w37/tiny2/`, read only). The route S side and the proposal:
`../05-graphics-composition/unit-combat-server.md` 10.10.

### 13.1 The rule [C]

- **The trigger's tier.** Every timed trigger of the `.ftsxml` files carries a `tier` attribute. The Sound trigger's
  reader (0x8E7670; beside the strings "Plays a sound when fired", `event`, `ec`, `track`, `tier`) stores it as a signed
  byte at trigger +0x1C, **-1 when absent**. The Effect, Action and Transfer readers read a `tier` the same way
  (0x8E7267, 0x8FD3B1, 0x8FD809, each into +0x1C).
- **The listener passes it on.** The Sound listener (0x4C4430) copies a byte of its object (+0x28) into the type-0x15
  record's payload +0x24 (section 2 step 2). That this byte is the reader's +0x1C is `[I]`: the listener's fields sit
  0xC above the reader's (+0xA4 the script hash, +0x28 the tier), consistent with a base sub-object at +0xC; the live
  pattern of 13.3 (stock plays exactly one of every T1/T2 pair) confirms the effect `[L]`.
- **The handler tests it** (0x74C620, kind 2, before the play): 0x53D890 (thiscall; the record's unit index +0x1C, the
  tier byte +0x24, a pointer to an out byte) returns "play" when the unit index is -1, or the tier byte is -1, or the
  unit's tier (0x429BA0) is -1 or **equal to the trigger's tier**; otherwise "do not play", and the handler returns
  without the 3D play. The out byte is 1 only on the equal (or unit tier -1) branch and decides, with the script's loop
  flag, whether a started sound enters the unit-loop table (0x8DB910). **0x53D890 has one caller, the type-0x15
  handler** (call-site scan of `.text`), so no other path, Effect triggers included, applies this test. The unit-loop
  test 0x8DB850 passes every non-looping script: the tier test is the handler's only per-trigger gate for a one-shot.
- **The unit's tier** (0x429BA0, the unit's index in the unit simulation array 0x2D6F5F8, stride 0x344): -1 for an
  index out of range or a slot not in use; else it copies the unit's per-figure records (0x65A900) and counts the
  figures whose record field +0x28 is non-zero `[I: a figure present / alive; the field is not named]`: **0 when the
  count is 3 or less, 1 for 4 to 8, 2 for 9 or more**.

So a Sound trigger with tier -1 always plays; tier 0 only while the unit shows at most 3 figures; tier 1 at 4-8; tier 2
at 9 or more. The play rules of section 12 then apply as before.

### 13.2 What the trigger files tier [D]

Read 2026-11-21 13:00 EST from c5h-f's install (`UnitModels.fpk`: the swordsman's, warrior's and archer's trigger
files, taken out with `server/evidence/0042/4737/extract.py` into the scratch folder `C:/c5-64bit-work/0042/w39/`; the
scripts' rules with `scripts_of.py`; outputs not committed; `callers.py` is the call-site scan of 13.1):
- **tier 0, the per-figure detail:** sword swing, attack vox, leather creak, armour move, foot run / foot on terrain,
  sword run clink, sword rattle, shield / axe / club drop, club hand, staff, bow creak / twang / whoosh / rattle, arrow
  pull, arrow quiver, the plain throw torch.
- **tier 1 / tier 2, one of a pair by the unit's size:** the beds (`BAT_AMB_T1/T2_*`, `BATTLE_AMB_VOX_T1/T2`), the run
  loops (`RUN_T1/T2`, `T1/T2_RUN_$(TERRAIN)`), `RUN_VOX_T1/T2`, `RUN_CHRG_VOX_T1/T2`, `ARROW_FIRE_T1/T2`,
  `THROW_TORCH_T1/T2`.
- **tier -1, every figure always:** death vox, impact, body fall (death clips 1200 / 2200), victory vox (1800), fortify
  and fortify vox (1500), and two `FOOT_ALWAYS_$(TERRAIN)` plus a `FORTIFY_VOX` in the combat-ready entry 1600 and in
  bombard defend 1280.
- Figure counts (VP's database copy, `ArtDefine_UnitInfoMemberInfos.NumMembers`): swordsman 6+3+3 = 12, archer 12,
  infantry 12, barbarian swordsman 12, spearman 12. A whole unit is tier 2; it reaches tier 0 at 3 figures or fewer.

The design reads as `[I]`: a whole unit sounds as a crowd (one bed of its size per clip, its cries, falls and deaths
per figure); the per-figure detail is heard only from a unit down to its last few figures.

### 13.3 Against the recorded fights [L]

W37's Tiny pair (stock arm a, route S arm b, 8 fights; 3D one-shot lives attributed to a fight, per file stem:
`python -I server/evidence/0042/4737/perfile.py <repo> F:/c5-64bit-work/0042/w37/tiny2`, output
`C:/c5-64bit-work/0042/w39/perfile.txt`, scratch; one fight in time order: `timeline.py <repo> <work> a|b <index>`):
- **One of each pair on stock, both under route S:** stock `battleambvoxt2` 8 / `t1` 3, `swordvsswordt2` 5 / `t1` 0,
  `swordsmanrunt2` 14 / `t1` 0, `dirtt2` 11 / `t1` 1, `arrowfiret2` 1 / 0, `riflefiret2` 1 / 0 (the 12-figure units
  T2; the warrior and the def3 spearman T1); route S `battleambvoxt1` 7 / `t2` 7, `swordvsswordt1` 5 / `t2` 5,
  `swordsmanrunt1` 6 / `t2` 6, `dirtt1` 6 / `t2` 6 (the whole Miles logs).
- **The tier-0 detail is absent on stock** in the five fights between whole units: swing, attack vox, leather creak,
  sword run clink, sword rattle, armour move, wood shield drop, arrow pull and the bow sounds 0 each (route S 6, 6, 6,
  5, 4, 4, 3, 15 and 8 / 5 / 5 / 5 in the matching fights). Stock plays a few only where a unit is down to its last
  figures: the kill case (swing, attack vox, leather creak 1 each) and the 12 s def3 fight.
- **The tier -1 death sounds match:** death vox 21 / 21, sword flesh (impact) 28 / 27 over the eight fights.
- **What the tier test does not explain:** route S's `FOOT_ALWAYS` and `FORTIFY_VOX` (tier -1, clips 1600 and 1280):
  10 fortify vox per melee fight (in fight 0 all ten within one frame at +0.82 s: rule 9's count of 10 is met) and
  10-20 foot dirt; none on stock in the eight fights. Two readings: (a) stock's figures do not enter those clips in
  these fights: stock's attacking swordsman run-charges in all three melee fights (`runchrgvoxt2`: 1120 then 1140,
  which skips 1600), route S's runs (1400) and enters 1600 and never run-charges there. **For melee attackers (a) is
  [C]:** the target step 0x5C7100 puts a figure that has its target into state 0xC (the charge state, request 1120)
  when it stands farther from the target than its member's `MoveRadius` (combat record + 0x00, the squared distance
  minus the squared radius compared with 2.5e-5), else into 3; phase 3 sends every attacking figure to 0xC
  (`../05-graphics-composition/unit-combat-server.md` 5.1 M3); and state 10 then picks 1140 after a 1120 when the member
  takes it (`../05-graphics-composition/combat-choreography.md` 5.2). No rush test in the part read (0x5C7540-0x5C7625;
  the start of 0x5C7100 was not read), and the Tiny data agree (the swordsman, no `RushAttackFormation`, charges);
  (b) a rule of the EXE's Granny trigger walk (which sources of a graph fire) that was not read. (a) alone does not
  cover the ranged fight (stock's archer should enter 1600 before 1180 and its warrior target 1280, yet stock shows
  neither clip's -1 sounds) `[I]`. **(b) is unlikely** `[L]`: in W37's huge pair (10.8's logs, `perfile.py` over
  `F:/c5-64bit-work/0042/w37/pair`, fights paired by index, so per-fight numbers are indicative only) stock plays
  `FORTIFY_VOX` B / E / N at the starts of late-era ranged and siege fights (Gatling, field gun and dreadnought fights:
  2-5 per fight; B 21, E 4, N 10 over the first 61 fights), so stock's 1600 / 1280 entries do fire their tier -1
  sounds when its figures play those clips; route S plays as many B and N there but more E (16 against 4) and a D
  voice stock never plays (9 against 0). What
  differs is which figures enter 1600 / 1280: a choreography question (unit-combat-server.md 10.10, P2).

- **Which figures request 1600, from the state setter 0x512DE0 [C, 2026-11-21 14:30 EST]** (its jump table for states
  2-0x12 at 0x513230; `../05-graphics-composition/combat-choreography.md` 5.2 gave state 2's request as "1000 idle, or
  1600 combat ready when the figure is set up for it"): in states 2, 3, 6 and 0x10 (waiting, approaching) a figure
  flagged to move (bits 14, 15 or 17 of figure + 0x98) requests 1400; otherwise, when its current request (+0x70), or
  with none its last action (+0x74), is a movement (1400 run, 1450 shuffle or 1120 run charge: 0x6E2DA0), it requests
  1600 if its member enables 1450 (the enable mask tested by 0x56C3D0) and 1000 if not; **a figure that was not moving
  gets no new request**, so an idle defender waiting for its turn stays in its idle clip. State 5 (ready to strike)
  always requests 1600 (and takes its member's `RangedAttackRadius`), state 0xE always 1280. Route S's choreography
  requests 1600 for every figure it puts into Wait or Ready (`combat.cpp` `ready()`), idle or not: offline on 0557's
  s-w1 (`server/evidence/0042/4737/tlog_sum.py` / the trigger log), with the charge built, the 7 attacking warriors that
  wait for a wave slot and all 10 barbarian defenders still enter 1600 (17 `FORTIFY_VOX`, 34 `FOOT_ALWAYS`), where
  stock's idle defenders would not [I for the outcome; C for the rule].

### 13.4 Not verified

- That the listener's +0x28 is the reader's +0x1C (13.1 `[I]`, backed by 13.3's pairs).
- What the figure record's +0x28 is (present, alive or drawn), so whether a figure in its death clip still counts.
  Route S counts the living figures only (held and not dead: `Fig::alive`; lead-0042 #9324) `[I]`; the paired run's
  kill case (a unit falling through 3 figures) is the check (`../05-graphics-composition/unit-combat-server.md` 10.10).
- Reading (b) of 13.3: the Granny trigger walk (the `FGrannyTimedTriggerSystem` update) was not read.
- Whether the Effect, Action and Transfer triggers' `tier` is tested anywhere (not by 0x53D890).
