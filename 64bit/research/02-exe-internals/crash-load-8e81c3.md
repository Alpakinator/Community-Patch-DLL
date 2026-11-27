# The EXE crash at VA 0x8E81C3 at the end of a Tiny product-path load (ticket 0539, 2026-11-18)

One silent EXE crash: 2026-11-16 08:36:53 EST, instance c5h-c, the second launch of 0392's task 4510. Row 5 of
`../06-tooling-and-workflows/crash-ledger-audit.md` (section 4.1). This file covers three things: what the faulting
instruction reads, which EXE system it belongs to, and the leading candidate for the cause with its confidence.
Addresses are DX11 VAs at ImageBase 0x400000. The EXE copy read was c5h-c's `CivilizationV_DX11.exe`, SHA-256
`04cfdc71...` (TimeDateStamp 0x546CD0A8). Only addresses, offsets, the EXE's own type and file names, and a description
of behaviour are written here (D83). The disassembly was read with the SDK's `cdb -z` on a scratch copy at
`C:/c5-64bit-work/0539`; no process was touched and nothing of it was committed. No game was run for this ticket.

## 1. Result in short

- **The site [C].** VA 0x8E81C3 is inside a small getter at **0x8E81B0** that belongs to a model object: a unit's (or
  other) model instance of the class with vtable 0xA9A748, whose +0x88 points at the node tree inside its holder
  [C, section 9.1-9.2; corrected 2026-11-18 by 0544, was "most likely the EXE's `FGrannyContext`"]. The getter
  takes an output pointer and a node index, and returns a 4x4 matrix. First it reads the pointer at **`this+0x88`**, the
  model's node table. If that pointer is 0, the getter returns the identity matrix. Otherwise it reads the node array
  pointer at **node table +8**: **this read is the faulting instruction.** Then it copies the 64-byte matrix at
  `+0x50` of node `index` (nodes are 0xB0 bytes each). The read of `this+0x88` succeeded, so the fault means the model
  held a **non-zero value at +0x88 that was not a readable address**.
- **The system [C].** The getter has one caller: **0x6FC7B0**. That function computes the world transform of an
  **attachment bone for a Granny animation trigger**. It looks up the entity's `FGrannyContextTrigger` component, takes
  the component's model (+0x1C) and trigger record (+0x20), and asks the model for the bone's matrix (0x8E81B0) and for
  the named attachment node (0x8E8530).
  - It has five callers. All of them are listeners of the EXE's **timed triggers** in a model's animation (the
    `.ftsxml` Effect, Sound, Action and Transfer triggers of `../05-graphics-composition/effects-formats.md`).
  - The particle ones (0x4A6390, 0x76F390) are posted into the particle system's per-thread deferred buffers at
    **0x2D70240** and run when that thread's buffer is drained.
  - One (0x4C4430) uses the Audio system's object.
  - One (0x5635A0, the StopSound listener: corrected 2026-11-20 from `exe-audio.md`, see section 4) posts a record on
    the 4 MB game-to-UI queue.
  - The classes come from `FGXModelComponentsFromGranny.h` (the source file name the EXE carries next to their
    vtables).
- **When [L, logs; clocks aligned to about 0.5 s].** The fault came within about half a second after the first
  `ICvGame1::Update` began. That is the first frame of play, when unit animations start and their triggers begin to
  fire. The save was a Tiny turn-8 game.
- *(2026-11-18, 0544, section 9.5: the candidate below is now a mechanism. Each link is [C]: animation triggers reach
  their listeners through by-value copies that carry a raw model pointer; nothing invalidates these copies when
  0x734DE0 destroys a unit's model; and the getter can fault only on a model that is no longer alive. Confidence
  "medium" that a destroyed unit model was reached through a trigger. The thread or timing that opened the window is
  not shown.)*
- **Cause: not determined. Leading candidate [I, low]: a stale model pointer in the trigger path.** When a unit's
  animation fires a trigger, the model reached through the trigger's context (`FGrannyContextTrigger` +0x1C), or that
  model's node table (+0x88), has already been freed or replaced. The value at +0x88 is then left-over heap data. That
  makes it a lifetime fault in the EXE's own animation-trigger code.
  - The patch's own writes into EXE memory at that moment are ruled out as the source: the view mask word, the
    white-mask global and the white-mask pool pass. The pool pass writes only inside the static draw-object pool in the
    image [C, section 5].
  - Whether the product route only widened the window (its first `Update` lasts longer, because every call to the DLL
    crosses to the server) is open.
- **Rate [C, records].** This is the only crash at this site in every record read so far:
  - the 22 Goldberg crash records of the workstation's instances, 2026-11-10..18;
  - the dialog watcher's 15 ledger signatures and 45 sightings (0534);
  - the first and the third launch of the same set-up, which loaded.
- **No reproduction.** The static read names no trigger that a test could provoke on demand (section 6). Nothing was
  run, and no game place was asked for.
- **What settles it:** the address that the next sighting reads (section 6). Since 0419 the proxy's chained filter
  writes it into `exe-crash.txt` ("access: read of 0x...").
  - An address below 0x10000 means +0x88 held a small integer: the model object was overwritten or reused.
  - A heap address means the node table was freed while the model kept pointing at it.
  - A full dump of the EXE at the fault would name the object and the thread.

## 2. The record [L]

Evidence of 4510's second launch: `F:/c5-64bit-work/0392/4510/crash-083739/` (`vpproxy.log`, `server.log`,
`server-report.txt`, `proxy-calls.tsv`). The set-up is in `../06-tooling-and-workflows/human-input-testing.md` 12.2:

- the pins `x64-04b1211b-pfdee656` and a composite built from fdee65668;
- D33 with Overlay on, the composite keys of 3308, and `[Renderer] ServerRenderer = 1`;
- `WhiteFogMask = 1` (the default) and `pack=0`;
- mods `cp,vp` (EUI from the install's DLC, without its VP compatibility files);
- client size 1914 x 1051;
- save `HI4510-TINY_0008`, a copy of the Tiny Polynesia save `C5H-3600-TINY_0008`, turn 8.

The run folder in the instance's `Logs/VP64/run` was removed by `keep_runs=10` before this ticket. The game logs of
that launch (Lua.log and others) were overwritten by later launches on c5h-c. What 12.5 recorded of them stands:
Lua.log ended with EUI's "Finished loading EUI top panel".

- Goldberg's crash printer (`c5h-c/run/gbe/crashes.txt`): `[2026/11/16 - 13:36:53]` (UTC), `code 0xc0000005`,
  `@address = 0x012881C3`. That is the exception address, so the faulting instruction itself. The EXE base was
  0x00DA0000 (`vpproxy.log`: `shell: view mask: active: exe=DX11 ... base=0x00da0000`, and the white-mask line), so
  **VA = 0x012881C3 - 0x00DA0000 + 0x400000 = 0x8E81C3** [C, arithmetic].
- `vpproxy.log`, last lines, in proxy seconds (0 = proxy boot, run folder `20261116-083535-...`):
  - 75.28 and 75.33: the view-mask watcher clears the Terrain bit (0x1) and the Waves bit (0x200000) after
    `GVS::StartupGame` set them again.
  - 77.53: thread 4588 (start `+0x42A360`, the game-core thread of `game-core-thread.md`) makes its first
    `ICvGame1::Update` call.
  - The log then has the "Lua channel" lines and nothing more.
- `server.log`, in server seconds:
  - 74.67: `render: terrain: state HIDDEN -> NO_FIELD`.
  - 78.22: `remote: scratch Lua state ... for channel 1`. This is the server's own work inside that first `Update`; it
    needs nothing from the EXE.
  - 82.29: `peer pid 15008 hung: no heartbeat for 5027 ms`.
  - 122.41: `PeerDied`.
  - 122.60: the EXE exited with 3221225477 (0xC0000005).
- **Clock alignment.** The EXE's heartbeat comes from the proxy's vpipc monitor thread every 100 ms tick
  (`ipc/PROTOCOL.md` 7.3). It therefore stopped at about server 77.27 s, which is when WER suspended the EXE after the
  fault (as in `../03-dll-engine-boundary/server-host.md` 19: "WER holds the EXE"). The server process started 0.2-0.7 s
  after proxy 0, so server 77.27 s is proxy 77.5-78.0 s. **The fault therefore came 0-0.5 s after the first `Update`
  began** [I, alignment]. The game-core thread was then most likely still inside that call, waiting on the server.
  The EXE then stayed alive for about 45 s, held by WER, with no box and no crash log, and ended with the exception
  code.
- **Faulting thread: not known.** Goldberg does not record it, and there was no dump. The candidates are:
  - the main thread, stepping a unit's animation in the frame;
  - a worker thread, running animation jobs (the deferred buffers in section 4 are per thread);
  - the game-core thread, serving a DLL-to-EXE call during the first `Update`.

## 3. The faulting function 0x8E81B0 [C]

`thiscall`, two stack arguments (output pointer, node index), `ret 8`.

| Step | What it reads or does |
|---|---|
| 1 | `this+0x88`: the model's node table pointer |
| 2 | if 0: writes the identity matrix to the output and returns |
| 3 | **node table +8** = the node array (the faulting read at 0x8E81C3) |
| 4 | node `index` = array + index x 0xB0; copies its 64 bytes at +0x50 (a 4x4 matrix) to the output |

**The node table [C].** Two neighbours read the same structure, so its shape is known:

- 0x8E8530 (find a node by key) and 0x43E7D0 (first descendant from a node) read it through the same `this+0x88`.
- 0x41A130 is the pre-order "next node" walk of the same array.
- It is an index-linked **tree** of 0xB0-byte nodes:
  - +0x00: the next sibling;
  - +0x08: the first child;
  - +0x0C: the parent;
  - +0x50: the 4x4 world matrix;
  - +0x90: the name key that 0x8E8530 compares;
  - 0x0FFFFFFF marks "none".
- That fits a skeleton or attachment-point hierarchy of a model, in which the trigger's bone index selects a node and
  its name key selects an attachment point.

*(Corrected 2026-11-18 by 0544, section 9.1-9.2. The class match below is wrong. The getter's `this` is the model
instance of vtable 0xA9A748, named by the call chain that builds the trigger. Its +0x88 points into the holder that
owns it (holder+0x10, the tree) and is never freed through the model. The paragraph is kept as a trace.)*

**Which class, and who frees +0x88 [C for the code; I for the class match].** A scan of 0x8D0000-0x900000 for writes
of a register into `+0x88` found four.

- **The best match is the class with vtable 0xA9B660** (constructor 0x8F5820, destructor 0x8F5600). The `.rdata`
  string just before its vtable is `FGrannyContext`. Its destructor:
  - frees `+0x84` through 0x9BCAF0, which lies in the Granny library's code range (`addresses.md`, the unit model
    load path);
  - frees **`+0x88` through the engine's delete 0x813D10**;
  - then writes 0 into +0x80, +0x84 and +0x88.

  So `+0x88` is an owned, separately allocated block (the node table) of a Granny context. A context that has been
  destroyed reads 0 there, the safe case, until its memory is reused. The constructor builds the sub-object at +0x60
  (0x8F44E0); the code that stores a non-zero node table at +0x88 was not found in that range.
- Another class (vtable 0xA9A748, constructor 0x8EAB50, from `FGXModelComponentsFromGranny.h`) also writes 0 into its
  `+0x88` in its constructor. Nothing else in that range touches it.
- The two remaining writes belong to a third class (0x8F3000 / 0x8F3280), whose `+0x80..+0x88` is another member.
- **Not determined:** that 0x8E81B0's `this` is an `FGrannyContext`. The match rests on the name string, the destructor
  and the trigger's component name (section 4), not on an observed object.

## 4. Who calls it: the Granny timed-trigger listeners [C]

The callers are in `tools/harness/evidence/0539/xrefs-out.txt`.

**0x8E81B0 has one caller.** The call is at 0x6FC826, in **0x6FC7B0**. That function:
- takes an output block, the entity and an attachment key;
- returns 0x99 bytes: the bone's matrix at +0x00, the attachment node's matrix at +0x40, the trigger record at +0x80, a
  float at +0x88 and an id at +0x90, with a valid flag at +0x98.

Its steps:

1. On the entity's interface at +8, the virtual method +0xC is called with the type descriptor from 0x61F0A0. That
   descriptor is a function-local static whose name string is **`FGrannyContextTrigger`**.
2. If no context trigger is found, the valid flag stays 0.
3. Otherwise the model is the context's +0x1C. The trigger record is the context's +0x20: the bone index at +0x10, a
   16-bit id at +0x18. Both are read through one-line getters that the compiler folded with other classes' (0x48F990,
   0x558220).
4. The bone matrix comes from 0x8E81B0 (the crash), and the attachment node from 0x8E8530.

**0x6FC7B0 has five callers.** Each one is a trigger listener: a vtable method beside `FGXModelComponentsFromGranny.h`'s
path string, or a function posted by one.

| Caller site | Function | How it is reached | What it does with the transform |
|---|---|---|---|
| 0x4A6459 | 0x4A6390 | posted by 0x7B5CD0 (vtable slot at 0xA3C9B4) into the deferred buffer | particle effect at the bone (Particle object 0xC8EB50, UnitSim 0x2D6F5F8) [I: the Effect trigger] |
| 0x76F3B0 | 0x76F390 | posted by 0x50E960 (vtable slot at 0xA3C380) into the deferred buffer | particle effect handed on (0xC8EB50, UnitSim records 0x344 bytes) [I: Transfer] |
| 0x4C4462 | 0x4C4430 | vtable slot at 0xA3C5B0 | Audio object 0xC957A0 [I: the Sound trigger] |
| 0x5635E1 | 0x5635A0 | vtable slot at 0xA3C794 | reserves a record on the 4 MB queue (0x1A0E400, channel at 0x1A0F080 + c x 0x800180): the **StopSound** listener [C], it posts a type-0x16 record that fades out the unit's loop slots (`exe-audio.md`, ticket 4797). Corrected 2026-11-20 (lead-0042, MC #7851); replaced "[I: an Action trigger for the UI]" |
| 0x4F7B1B | 0x4F7B00 | vtable slot at 0xA3C1B0 | stores the matrix into an object [I: kind not read] |

**The deferred buffers at 0x2D70240 [C].** There are 16 slots of 12 bytes each: used bytes, capacity, data. The
Particle start-up 0x67B360 sets them up through 0x74DAF0.

- **Posting.** 0x528440 appends a 0x50-byte record to the calling thread's slot; the slot number comes from 0x81D930,
  the job system's thread index. A record holds:
  - the context-trigger pointer;
  - the handler and its `this`;
  - 48 bytes of payload.
- **Draining.** The records run in order, and the slot is then reset:
  - 0x763330 runs a given slot. The unit pass 0x5599C0 (reached from Unit render 0x7058B0) calls it for slot 0 right
    after a unit's animation step, and so does 0x70F0D0.
  - 0x63EA50 runs the current thread's slot after its own update step.
- So a record normally runs within the step that posted it. It carries a **raw** pointer to the context trigger, and
  nothing in this path checks that pointer when the record runs.
  *(Corrected 2026-11-18 by 0544, section 9.3: the record holds a 48-byte copy of the context trigger at +0x20, not a
  pointer to it. The copy carries raw pointers to the model (+0x1C) and to the model's trigger entry (+0x20). Nothing
  checks those pointers when the record runs.)*

## 5. What the patch writes into EXE memory at that time [C]

Product-only writes that could, in principle, have put a bad value into a model object:

- **The view-mask watcher.** It writes one dword, `GameViewState+0x1B4` (75.28 s and 75.33 s in this run), and nothing
  else (`disable-systems-and-masks.md` section 5).
- **The white mask.** It writes the global 0xDA12AC and, in the pool pass at boot and from the `Update` hook at most
  once a second, the 16-bit texture slots of entries in the EXE's **static** draw-object pool (VA 0x313DE88 + 0x80 x
  index). The index is clamped to 0x20000 and to the pool's high-water mark, and the pool range is checked against the
  image size before any write. Checked in the run's own source: `git show fdee65668:proxy/src/proxy_shell.cpp`, lines
  1102-1111, 1200 and 1261-1264.
- **Neither writes the heap**, where model objects and their node tables live [C for the proxy's code; I for "model
  objects are heap objects": not read].
- The composite is not known to write EXE data objects (its work is on Present and its own copies; `fog-of-war.md` 9
  lists its one EXE write, the white handle's view-table entry). [I: not re-read for this ticket.]

## 6. Cause, confidence and what would confirm

**Leading candidate [I, low]: a lifetime fault in the EXE's timed-trigger path.** A trigger of a unit's (or another
model's) animation fires, at the first frames of play. The model reached through its `FGrannyContextTrigger` (+0x1C),
or that model's node table (+0x88), is no longer the live object:

- either the model was destroyed or rebuilt, with its memory reused, while the context trigger still pointed at it;
- or the node table was freed while the model still pointed at it.

The getter's own null test shows that +0x88 is legitimately 0 for some models, and only a non-null, unreadable value
crashes. If `this` is an `FGrannyContext` (section 3), its destructor frees the node table and writes 0 into +0x88. A
read through a context destroyed a moment before would then return the identity matrix and **not** crash. So the
candidate needs one of two things [I]:

- the freed context's memory was already **reused** by another allocation, whose data sits at +0x88; or
- a node table was freed by a path other than the destructor, leaving the old pointer.

In either case the trigger had to reach a context that was no longer its unit's live one.

Things at the start of play that replace a unit's model, any of which would do it [I, none read]: an era art variation
(`GetUnitArtInfoEraVariation`, called 46 times in this run), a formation rebuilt, a model swapped by an event of the
first `Update`.

**Alternative [I]:** another fault overwrote the model object. That would also explain an unreadable +0x88. Nothing in
this run points at a writer, and section 5 rules out the patch's known writes.

**Why "low":** there is one sighting and no dump. The static read shows what was read and through which path, not which
object it was or why it was bad. It also shows no trigger that could be provoked on demand. So the acceptance's "a
reproduction only if the static read names a trigger" does not apply, and none was asked for.

**What would confirm, in order of cost:**

1. **The next sighting's `exe-crash.txt`** (0419's chained filter, in every product run since 2026-11-16). Its
   "access: read of 0x..." gives the address of the faulting read, which is the value at +0x88 plus 8:
   - below 0x10000 means a small integer in +0x88, so the model object was overwritten or reused (the alternative, or
     a reused block);
   - a heap address means a freed node table (the candidate's second form).

   The dialog watcher's ledger counts boxless crashes since 2026-11-17 01:16, so a recurrence is no longer silent.
   Ticket 0540 is meant to make it a signature of its own.
2. **A full dump at the fault**, for the object, its vtable, the thread and the stack. A process stopped by WER can be
   dumped from outside, read only (`cdb -pv`, as 0515 and 0534 did for hangs). **Done in the harness by ticket 0543
   (2026-11-18): `crash.collect`, and the dialog watcher during a hold with no box; section 6.1.** (Was: "The harness's `crash.collect` would have to do this for a boxless
   EXE crash while WER holds the process (about 45 s here). Proposed for the MC, not done.")
3. **A read of who writes +0x88** across the whole `.text`, and of the unit model's teardown path: whether it clears
   the context trigger's +0x1C, or unregisters its listeners, before the model memory is freed. That costs about 2 h of
   static reading. It would turn the candidate into a mechanism, but could not prove it happened in this run.

*(2026-11-18, 0544, section 9.4: none of the DLL's unit calls read for this replaces a model on the game-core thread.
They post on event manager B's queue, not on the 4 MB queue. So the race described in the next paragraph has no direct
path; the paragraph is kept as a trace. Item 3 of the list above is done in section 9.)*

**Product share [I].** Stock has never shown this site, and the product once in an unknown but large number of loads.
That is no evidence either way at these counts. The product route makes the first `Update` long: every DLL call
crosses to the server, so the game-core thread sits in that call while the main thread draws frames and steps
animations. If the fault is a race between the game-core thread's EXE calls and the main thread's animation step, the
product widens its window. Whether any DLL-to-EXE call in the first `Update` changes a unit model directly, rather than
through the 4 MB queue, was not read.

**Fix: none proposed.** The faulting code is the EXE's. No approved means (D4, D65, D83, D136) is known to make a stale
model pointer safe, and the object is not yet named. Per D185 (COMMON 30), the MC takes this to the Architect for routes
only once item 1 or 2 has named the object. Until then, the cheapest next step is to make item 2 automatic.

### 6.1 The dump while WER holds the EXE (ticket 0543, 2026-11-18)

(Extended 2026-11-18 by ticket 0548: when there is no WER hold, the same dump is taken of a process that a fatal box of
COMMON 29's table holds, VP's "Game Crash" box first; record `reason: fatal_box`. See `crashes-huge-load.md` 8.8.)

**What the harness does now [C, code and offline tests; L, stand-in].** `tools/harness/crash.py` `collect()` (every
caller: the dialog watcher at a box's sighting and at the game's end, `dismiss_dialog` before a press, the driver, the
checklist) first asks `wer_hold(instance, pid)`. A hold is all of:

- the game process still runs;
- the game's VP64 run folder holds the proxy's `exe-crash.txt` naming its pid and not older than it
  (`dialogwatch.game_crash_record`, 0531);
- a `WerFault.exe` is alive whose parent pid is the game and which started after it (one Toolhelp32 snapshot, about
  30 ms). A WER process of `server.exe` or of another descendant is not the EXE's crash.

`dump_wer_hold` then writes `<run>\crash\<stamp>_pid<pid>\full.dmp` with the Windows SDK's cdb, before the copies of
the other artefacts. The command line is `dump.command`'s, the one 9036 proved: `cdb -pv -p PID -c '.dump /ma "<path>";
qd'`. That is a non-invasive attach: no breakpoint, no debug register (D65), no invasive attach. The x86 cdb is used for
the WOW64 EXE.

- **Record.** `report.wer_dump` and `manifest.json` hold the record; `wer_dump.json` in the folder holds it too. It
  gives the state, path, bytes, the WerFault pid and its age, the crash record's first line, code, address and access,
  the command, the cdb pid and `full.dmp.cdb.log`.
- **States.** `written`, `timeout`, `failed`, `no_cdb`, `refused`, `already`, `no_wer`, `off`.
- **Time box.** 40 s from `collect`'s start (`WER_DUMP_WINDOW_S`). After that the harness stops waiting and records
  `timeout`. cdb is **left to finish and detach**: ending a non-invasive debugger part-way would leave the target's
  threads suspended [I, from the debugger's documented model]. Its log says whether it finished.
- **Refusals** (`refused`):
  - `legacy_steam`, or an install under `instance.FORBIDDEN_LAUNCH_ROOTS` (Steam's library, pure_vp): the owner's real
    install is never dumped;
  - a folder inside the repository (D83);
  - less than about the dump's size plus 25 GB free on the drive (COMMON 15).
- **`no_cdb`.** No debugger at `C5_DEBUGGERS` (default `C:\Program Files (x86)\Windows Kits\10\Debuggers`): recorded,
  and the copies go on.
- **`already`.** One dump per game (pid and creation time). A second `collect` names the first dump's record.
- **`off`.** `C5_WER_DUMP=0` switches the dump off.
- The dump stays in the instance's run folder and is never committed (D83).

**Who calls `collect` during the hold [C, code and tests].** Only a `collect` call made during the hold dumps.

- **A hold with a WER box.** The watcher collects when it first sees the box, and `dismiss --fatal` collects before it
  presses "Close program".
- **A hold with no box: the watcher's own path.** 0539's sighting showed no box for its whole ~45 s hold (watcher
  `boxes_seen` 0). Without a change, the watcher collects only after the process has ended (17.3's boxless path), and
  then there is nothing left to dump. First found while 0543 was being written; put to the MC on 0543's thread
  (2026-11-18 08:35, #5475), and the MC widened the ticket.
- **What `dialogwatch.Watcher.step` does now.** It checks when no box is up and no marker is open. If
  `crash.werfault_children` reports a live `WerFault.exe` child of the game, started after it, the watcher runs
  `collect` once, at that poll:
  - one Toolhelp32 snapshot per poll, about 30 ms every 15 s;
  - logged `dialog_wer_hold_collect`, with the dump's state, path and size;
  - a later boxless crash marker (17.3) carries it as `wer_hold_collect`;
  - no marker, queue entry or ledger count for the hold itself.
- **Timing [I].** With 15 s polls, the first poll after the fault comes 0-15 s into a ~45 s hold. That leaves 30-45 s,
  and the 40 s window is counted from `collect`'s start.

Tests: `tools/harness/tests/test_dialogwatch_wer_collect.py`, 7 passed (2026-11-18). The cases:

- a child present: collect once and not again;
- absent;
- a box up: only the box's own collect;
- a hold after a closed box;
- the reader failing;
- the boxless crash marker carrying the collection;
- the default reader wired to `crash.werfault_children`.

**Tests [C, offline, 2026-11-18, workstation; no game, Steam or instance].**
`tools/harness/tests/test_crash_dump_at_wer.py`: 23 passed. They use doubles:

- the game: a sleeping python of the test;
- WerFault: a row of a fake process snapshot;
- cdb: a fake that takes cdb's exact arguments, checked with `test_dump.py`'s checker.

The cases:

- written; dumped before the copies; not dumped twice;
- the window running out (1.5 s in the test): the harness returns at the window, and the fake finishes and writes the
  dump afterwards;
- cdb failing;
- seven no-hold cases: no WerFault, a WerFault of another parent, one older than the game, another image, no record, a
  stale pid, a stale time;
- the game gone;
- cdb missing;
- legacy and forbidden installs, and the literal Steam and pure_vp paths;
- too little disk; a folder inside the repository; switched off;
- `collect` never raising when cdb cannot start;
- a static scan of the new code: no terminating call, no `OpenProcess`, no invasive flag.

The final run was at 2026-11-18 08:42 EST, after 0540 merged. It covered both new files, every offline
`test_dialogwatch*.py`, `test_ops_crash`, `test_safety_crash`, `test_dump` (its live dump included),
`test_safety_dismiss_fatal_werfault`, `test_safety_fatal_box` and `test_safety_dismiss_helper_popup`: **309 passed**.
`test_safety_static.py` alone: **55 passed**. `test_live_dialogwatch.py` was not run: it launches the stand-in, and no
instance was used.

**Live stand-in [L, 2026-11-18 08:32 EST, workstation; no game, Steam, instance or WER].**
`tools/harness/evidence/0543/live_standin.py` uses the real snapshot, the real record reading and the real x64 cdb. The
set-up:

- a windowless python "game" writes `exe-crash.txt` naming its own pid in a fake test instance under
  `C:/c5-64bit-work/0543`;
- it starts a copy of Windows' `PING.EXE` named `WerFault.exe` as its child;
- it then ends with 255 after a stop file (0519's course).

`wer_hold` found the hold; `collect` wrote `full.dmp` (29,996,370 B) in 4.3 s, and the "game" still ran afterwards. A
second `collect` said `already`. The stand-in ended with 255. `cdb -z` opened the dump and named the stand-in's pid.
Output: `tools/harness/evidence/0543/live-standin-20261118.json` and `.txt`.

**Not verified.**

- **A real WER hold [I].** That is, a process stopped in its unhandled-exception filter while WerFault.exe reads it,
  dumped by `cdb -pv`. The stand-in's process was running, not held. Making the real EXE fault on the workstation is
  ruled out (no safe trigger; WER's box could reach the owner's desktop, 17). The place for it is a fault job on the laptop,
  where WER shows its box (0519's `live_0519.py` course).
- **The dump time of a real EXE in the hold [I].** It is likely longer than the stand-in's 4.3 s. 9036 dumped a 3.6 GB
  EXE in about 60 s, which is over the 40 s window (`timeout`, cdb left to finish).
- **Whether WER's hold lasts while cdb runs [I].** It probably does: the faulting thread cannot leave its filter while
  cdb has it suspended.

## 7. Method and tools

- `tools/harness/evidence/0539/xrefs.py EXE_COPY 0x...`: direct `call` and `jmp` targets in `.text`, and dword
  references in `.rdata`, `.data` and (for data addresses) `.text` immediates. `--start-of SITE` lists the references of
  the function that holds a site; the function start is found by a padding heuristic, which a single 0xCC pad can fool
  (0x4C4430 and 0x5635A0 were read by hand). It prints the input's SHA-256 and addresses only.
  Output: `tools/harness/evidence/0539/xrefs-out.txt`.
- The disassembly was read in `cdb -z` (`u`, `uf`, `da`, `dds`, `!dh`) on the scratch copy. The command files and
  outputs stayed in `C:/c5-64bit-work/0539` and are deleted at the merge.

## 8. Not verified (2026-11-18)

- The faulting thread and the object: no dump (section 6, items 1-2). Since 0543 the harness dumps a WER-held EXE during
  the hold, with or without WER's box (6.1). Not yet seen on a real crash.
- That 0x8E81B0's `this` is an `FGrannyContext` (vtable 0xA9B660, section 3).
- Where the EXE stores the node table into +0x88, and whether any path frees it other than the destructor.
- The listener kinds marked [I] in section 4: Effect, Transfer, Sound and Action are inferred from the systems each
  listener touches, not from the trigger loader.
- The clock alignment of section 2 (about 0.5 s).
- The test machines' records (ticket 0541) were not read for this site.
- *(2026-11-18, 0544: the third item is answered in section 9.1; the type of `this` is corrected there.)*

## 9. Ticket 0544: who writes +0x88, how a unit model is torn down, and the first Update (2026-11-18)

Static read only, no game run. Same EXE copy as above (SHA-256 `04cfdc71...`, scratch copy in `C:/c5-64bit-work/0544`,
deleted at the merge). The whole `.text` (0x401000-0x9D5D30) was disassembled with `cdb -z` (`u`) into a listing in the
scratch folder; `tools/harness/evidence/0544/writers.py` lists every store to `[reg+0x88]` in it (addresses and kinds
only, D83; output `tools/harness/evidence/0544/writers-out.txt`: 97 stores, the 115 float stores left out). The
functions named below were then read in cdb.

### 9.1 The getter's object is a model instance of class 0xA9A748, not `FGrannyContext` [C]

Section 3's class match was wrong. The context that a trigger carries is fixed by the call chain that fires it:

| Step | Function | What it passes on |
|---|---|---|
| 1 | **0x8E8F80**, slot 6 of vtable **0xA9A748** (a subclass slot, 0x5D1EE0 in vtable 0xA2D7F4, calls it with the same `this`) | steps one animation of the model `this`; takes the trigger entry `[this+0x80] + i x 0x1C` (count: the word at +0x84), finds its timed-trigger adapter and calls the adapter's advance 0x8F36A0 with (`this`, entry, two time arguments) |
| 2 | **0x8F36A0**, the adapter (vtable 0xA9B3D4, constructor 0x8F3780) | for every listener whose time window (the 0x10-byte entries at adapter +0x18, count +0x1C) the step crosses, calls its own slot +0x28 with (listener, model, entry, ...) |
| 3 | **0x8F3660** (slot +0x28) | builds an `FGrannyContextTrigger` **on its own stack** (constructor 0x8FD4E0: +0x1C = the model, +0x20 = the trigger entry, +0x24 = the next argument), calls the listener's vtable +0x1C with its address, then destroys it (0x8FD530) |

- So `FGrannyContextTrigger`+0x1C, which 0x6FC7B0 hands to the getter 0x8E81B0 as `this`, is **the model instance
  whose slot 6 fired the trigger**: an object of the class with vtable 0xA9A748 or a subclass. The interface-name
  strings stored next to that vtable in `.rdata` are `FGrannyAssetInstanceProvider` and
  `FGrannyStateMachineVariableContext`; the class's own name was not found.
- `FGrannyContext` (vtable 0xA9B660) is a different class. Its +0x88 is the +0x28 of its sub-object at +0x60, a raw
  buffer from `FGrannyStateMachineInstance.cpp` line 50 (allocated in 0x8F4620 at 0x8F46D3, freed in its destructor).
  It plays no part in this crash.

### 9.2 Every writer of the model's +0x88 [C for the code; I that the scan is complete]

| Site | Function | What it writes |
|---|---|---|
| 0x8EABEC | 0x8EAB50, the model's constructor | 0 |
| 0x404551, 0x5054C6, 0x55DDCC, 0x5D2A06 | the four **holder factories** 0x4044B0, 0x505430, 0x55DD30, 0x5D2970 (one template, source line 0x407) | **holder + 0x10** (0x787B70 returns its argument + 0x10) |

- **The holder** is a 0x60-byte object (constructor 0x8EB0C0, teardown 0x8EB140): +0 the asset, +4 the model, and at
  **+0x10 the node tree** (built by 0x8D23B0, filled by the asset load 0x4C6FD0, torn down by 0x8D2C50 / 0x8D3060).
  Each factory allocates a model (0xD8 bytes, or 0xDC for the subclass with vtable 0xA2D7F4; through the engine
  allocator 0x82C090), stores it at holder +4 and points **model+0x88 at holder+0x10**. So the node tree that section 3
  read is **embedded in the holder**, and the model only borrows a pointer to it.
- The model's destructor 0x8EAC40 frees the trigger array at +0x80 (0x8E9060) and other members, and **never writes
  +0x88** (it does not own the tree).
- A model made by the asset's own instance call (0x8ECEA0, `FGrannyAsset.cpp`) keeps +0x88 = 0: the getter then returns
  the identity matrix.
- No other store in `.text` has a register base and +0x88 for this class. Stores through `memcpy`, through `rep movs`
  or through a base pointer to a sub-object cannot be told apart by this scan. Section 3's four writes in
  0x8D0000-0x900000 are the constructor above (0x8EABEC), the `FGrannyContext` destructor (0x8F5640) and a third class
  (0x8F30C0, 0x8F32E9).

**Consequence [C, given the table].** A live model holds 0 or holder+0x10 at +0x88, and its holder is always mapped
while the model lives. The holder's teardown deletes the model first, then the tree, and only then frees the holder
(9.3). So `[+0x88]+8` can fault only when `this` is **not a live model**: the model was destroyed before the getter
ran. Either its block was reused, so that other data sits at +0x88, or the holder's memory was released. The
"overwritten by another fault" alternative of section 6 stays possible but needs a writer nobody has found.

### 9.3 How a unit's model is torn down [C]

- A unit's visual record holds its holder at **+0x4C**. The record is 0x380 bytes, from the pool that
  `cvUnitVisSystem.cpp`'s 0x6C5F80 allocates from; the callers pass the Unit_System object 0xD02200 along [I that the
  pool is that object's]. It is not the UnitSim record of 0x344 bytes.
- **0x734DE0** resets it: if a holder is there, its teardown 0x8EB140 runs first. That deletes the model through its
  virtual destructor (the scalar delete, flag 1) and tears down the tree. Then the holder is freed (0x813D10), and a new
  empty holder of 0x60 bytes is allocated.
- **0x795050**, which instantiates a unit's model (the function with the error text "Tried to instantiate a unit that
  has not been initialized in the library"), calls 0x734DE0 first. Then it calls one of the factories (0x5D2970 or
  0x505430). **So every re-instantiation destroys the old model and holder.**
- 0x795050 has three callers:
  - 0x6C5F80 (`cvUnitVisSystem.cpp`), for a new record;
  - 0x5723F0, a loop over every record;
  - 0x735360, for the records of one key.

  0x5723F0 and 0x626D30 (which calls 0x735360) are delegates that the Unit system's event set-up (0x710060, and
  0x641B00) subscribes. The UnitSim creation handler 0x5B03E0 does not instantiate the model itself: at 0x5B06E2 it
  posts a deferred call of 0x6C5F80 (0x743BD0 -> 0x869DF0, a per-thread buffer of 0x40-byte records).
- **Nothing on this path touches the trigger buffers at 0x2D70240.** Their only code references are:
  - the two posting sites, 0x50E99D and 0x7B5D0D;
  - the drains 0x763330 (called at 0x559B9B, 0x559C7D and 0x70F199) and 0x63EA50;
  - the reset 0x625110, which empties all 16 slots without running them;
  - the start-up 0x67B39D.

  Nothing checks a queued record against a destroyed model.
- **What a queued record holds (corrects section 4) [C].** The posting function 0x528440 does not store a pointer to the
  context trigger. Into its 0x50-byte record it copies:
  - the handler and its `this` (+0x10, +0x14);
  - **a 48-byte copy of the `FGrannyContextTrigger` itself** (+0x20).

  The copy is needed because the original is a stack object that dies when 0x8F3660 returns. The copy carries three
  raw pointers:
  - the **model** (+0x1C of the copy);
  - the **trigger entry**, which points into the model's +0x80 array, freed with the model;
  - the listener object (the record's +0x10).

  The drains pass the copy's address to the handler (0x4A6390 and 0x76F390), which pass it on to 0x6FC7B0.
- **The synchronous listeners** (0x4C4430, 0x5635A0, 0x4F7B00) run inside 0x8F3660, while slot 6 of the model is still
  executing, so their model is live unless the step itself runs on a dead model.
- **Listener registrations [C].** The model's +0x80 entries hold counted references to the adapters, which hold the
  listeners. The model's destructor drops them (0x8E9060: count -1, delete at 0). Who owns the listener objects
  themselves was not read.

**Answer to the acceptance's item 2:** the teardown does not clear any trigger's +0x1C, since there is no persistent
trigger object, only stack temporaries and the queued copies. It drops its adapter references, but it **does not remove
queued copies**. A queued copy outlives its model whenever the model is destroyed between the post and the drain of that
slot [C].

### 9.4 The window, and the first Update [C where marked, else I]

- **The posts and drains in the unit pass [C].**
  - In the unit pass 0x5599C0 (`UnitRender`, reached from Unit render 0x7058B0), each unit's model step (0x5AF520 or
    0x73F7C0 on the record's holder) is followed at once by a drain of slot 0.
  - In 0x70F150 (a job function, pointer at 0x9F0CC4), the step is followed by a drain of the slot number it was given.
  - So a post is normally drained in the same step on the same thread, before the next model can be destroyed.
  - The window therefore needs one of two things [I]:
    1. a **post into a slot that the step does not drain**: the slot is the posting thread's job index (0x81D930), so
       a step on a thread whose index is not the drained slot posts into a slot that runs only at that thread's next
       drain, 0x63EA50 or a later job;
    2. the model's destruction (0x734DE0 from 0x795050, through the deferred 0x6C5F80 call or the two re-instantiation
       delegates) **running on another thread** while the step or its drain runs.
- Which threads drain the 0x869DF0 buffer that carries 0x6C5F80, and which run 0x70F150, was not read.
- **Item 3, the DLL's calls in the first Update [C for the handlers read by ticket 4459, `unit-formations.md` 12.7.1].**
  The engine-utility unit handlers do not post on the 4 MB queue; they post records on **event manager B's queue**
  (0x2A10E08):
  - `GameplayUnitCreated` 0x5579C0, `GameplayUnitDestroyed` 0x68F260, `...InCombat` 0x58C9D0, `Embark` 0x408310 and
    `Paradrop` 0x58C1D0.
  - `GameplayEraChanged` 0x492980 reads the record table and posts removals, then calls the DLL back
    (`ICvUnit1::SetupGraphical`), which posts again.
  - None of them calls 0x795050, 0x734DE0 or a holder factory.
  - So **no unit-model replacement runs on the game-core thread during a DLL call**. Models are created and destroyed
    when the Unit system dispatches those records, which happens in the frame [I: the dispatching thread was not
    read].
  - The other engine-utility slots were not read for this.
  - This removes the basis of section 6's "product share" race, in which the longer first Update would let the
    game-core thread's calls race the animation step. The product's longer Update can still change **which frame** a
    batch of creations lands in. It does not move model destruction onto another thread [I].
- **The first frame of play fits the deferred creations [I].** At a load, every unit's record and model is created
  through the deferred 0x6C5F80 calls, and the first `Update` adds more (12.7.3 S2). A unit whose model is instantiated
  twice in quick succession (two creations for one record, an embark swap, an era re-creation) has its first model
  destroyed by the second 0x795050. If a trigger of the first model was posted and not yet drained, its copy is stale.

### 9.5 Verdict and what 0543's dump would show

**The candidate is now a mechanism [C for each link; I that it is what happened on 2026-11-16].**

1. Animation triggers reach their listeners through by-value copies that carry a raw pointer to the model (9.3).
2. A model is destroyed whenever its unit is re-instantiated or torn down (0x734DE0), and nothing invalidates queued
   copies (9.3).
3. The getter can fault only on a model that is no longer alive (9.2).

What is not established is a path that puts a destruction between a post and its drain in the first frame (9.4, the two
conditions). Confidence: **medium for "a destroyed unit model reached through a trigger"** (was low), **low for the
deferred-copy path** against a step that runs on a dead model.

**What a dump of the next sighting (0543) would show:**

| Read in the dump | Deferred copy (9.3) | Step on a dead model |
|---|---|---|
| Stack above 0x8E81B0 (return 0x6FC82B) | 0x4A6390 or 0x76F390, then **0x763330** (from 0x5599C0 or 0x70F150) or **0x63EA50** | 0x4C4430, 0x5635A0 or 0x4F7B00, then **0x8F3660 <- 0x8F36A0 <- 0x8E8F80** (<- 0x5D1EE0) |
| `this` (ECX at the fault) | the stale model; `[this]` is not a 0xA9A748-family vtable (0xA2D7F4 and others) once the block is reused | the same |
| The bad value | EAX at 0x8E81C3 = `[this+0x88]` | the same |
| Where the stale pointer came from | the 0x50-byte record under the drain (ESI in 0x763330's frame): its +0x20 is the trigger copy, and the copy's +0x1C is the model | the holder at record +0x4C no longer points to this model |
| The thread | the drain's thread; the slot index shows whether it was posted elsewhere | the stepping thread, set against the thread in 0x734DE0 / 0x795050 |

**Lever [proposal for the MC, D185; no code].** None can be named within the approved means until the dump decides
between the two forms:

- The trigger buffers at 0x2D70240 are `.data`, but emptying them is the EXE's own reset 0x625110, which drops every
  pending effect.
- A guard would have to sit in code (D4 forbids code patches).

The cheapest next step stays 0543's dump at WER.

**Not verified (0544):**

- the thread that dispatches event manager B's unit records;
- the thread that drains the 0x869DF0 buffer;
- the thread that runs 0x70F150, and that the main thread's job index is 0;
- whether 0x82C090 serves 0xD8, 0xDC and 0x60 from the small-object allocator or the CRT heap;
- that no `memcpy` or sub-object store writes +0x88;
- the owner of the listener objects;
- the class name of 0xA9A748.
