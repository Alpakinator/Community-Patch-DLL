# Save-layout audit: x86 VC9 DLL vs x64 MSVC 14.43 DLL (issue 1004, decision D10)

Date: 2026-11-10. Fork tree: `x64` at b25fd45c0 (Release-5.4.8 + the x64 compile baseline). Toolchains: x86 = clang-cl
19.1.7 `-m32` with the VS2008 (VC9) headers, as `build_vp_clang.py` builds it; x64 = clang-cl 19.1.7 `-m64` with the
MSVC 14.43.34808 STL and Windows SDK 10.0.26100.0, flags as in `evidence/2026-11-10-probe/probe-pch.bat`. Config:
debug (`VPDEBUG`, `FINAL_RELEASE`). Confidence marks: `[C]` confirmed by compile or disassembly, `[I]` inferred from
the source. No save has been written by an x64 DLL yet; the byte compare is package I1-E's job.

## 1. Result

- **Bitness drift (D10 width and layout):** after the pins below, no value reaches `FDataStream` with a different
  width on x64 than on x86 `[C]`, by a compile census of all 173 translation units on both builds (section 2). Before
  the pins the census found 318 x64-only 64-bit stream records; 18 of them are in files this task does not own and
  are handed in as a patch (section 3.2). With the fork commit and the patch applied: 0 `[C]`.
- **Pinned in the fork (committed):** `FStlContainerSerialization.h` (every STL container count, read and write) and
  `CvInfosSerializationHelper.h` (four vector counts). x86 object code for these is unchanged (section 4).
- **Pinned by patch, not committed (files owned by others):** `CvGame.cpp/.h` (three saved containers of string
  hashes held as `size_t`), `CvAStar.cpp` (`CvUnit::m_kLastPath` count), `CvEspionageClasses.cpp` (14 counts, and
  the x64 build without the patch cannot even read its own saves). Patch:
  `tools/x64-dll/save-audit/other-files-save-width-pins.patch` (applies cleanly to b25fd45c0).
- **Not bitness, still breaks byte identity:** three saved `unordered_*` containers are written in hash-table
  iteration order, which differs between the VC9 and MSVC 14 STLs (section 6.1). Fixed 2026-11-10 by D18 / issue
  1008 (sorted writes, fork 3ac5819ba); headless check: x86 saves differ from stock only in those containers.
- **Closed-library byte formats** the replacement libraries must reproduce: `FDataStream` primitives and strings
  (section 5, for issue 1006), `FCRC::Calc` (every saved info-type hash), the CvGameCoreDLLUtil enum serializers,
  `FAutoArchive`.

## 2. Method: the save-width census

`tools/x64-dll/save-audit/save-width-census.py <fork worktree> <out dir>` copies the fork's sources to a scratch tree
(the fork is not modified), instruments two headers there, and compiles every TU with `/Zs` (syntax only) for x86 and
x64:

- `FDataStream` gets non-template, deprecated `operator<<` / `operator>>` overloads for `long long` and
  `unsigned long long`; its 64-bit `Write`/`Read` members are marked deprecated too.
- `CvStreamSaveVisitor` / `CvStreamLoadVisitor` (`CvSerialize.h`) get the same overloads on `operator()`,
  `operator<<`, `operator>>`.

A non-template exact match wins over the member templates, so every direct `stream << x` or `visitor(x)` of a 64-bit
integer warns at its own line, in either build. Values that reach the stream through a container or wrapper
template warn once per template instantiation per TU, with the instantiation chain naming the first requester and
the element type. A site that is 64-bit only on x64 is a `size_t`/`ptrdiff_t`-class value (x64-only = x64 set minus
x86 set). Runtime: about 4 min x86 + 6 min x64 with 16 jobs.

| Run (2026-11-10) | x86 records | x64 records | x64-only | Evidence |
|---|---|---|---|---|
| Baseline b25fd45c0, no pins | 9 | 327 | 318 | `evidence/2026-11-10-save-census/baseline-*.txt` |
| All pins (fork commit + other-files patch) | 9 | 9 | **0** | `evidence/2026-11-10-save-census/allpins-*.txt` |

The x86 records are identical before and after the pins (same 9 lines) `[C]`. Both runs: 173 of 173 TUs compiled on
both builds. Record format: `tag, root requester, warning site, first instantiation`; raw logs (47 MB) stayed in the
session scratchpad.

### 2.1 What the baseline found `[C]`

The 9 x86 records are the DLL's genuine 64-bit members, 64-bit on both builds and correct:
`CvPlayer::m_lJONSCultureEverGeneratedTimes100` (visitor at `CvPlayer.cpp:45173` and sync var `CvPlayer.h:3854`),
`CvPlayerCulture::m_lLastTurnLifetimeCultureTimes100` (`CvCultureClasses.cpp:5772`), `CvRandom::m_ullRandomSeed`
(`CvRandom.cpp:187`).

The 318 x64-only records:

| Records | Site | Cause | Fix |
|---|---|---|---|
| 99 W + 102 R | `FStlContainerSerialization.h:71/:87` | sequence container count (`size()` / `size_type`) | pinned, fork |
| 47 W + 48 R | `FStlContainerSerialization.h:133/:150` | associative container count | pinned, fork |
| 1 W + 1 R | `FStlContainerSerialization.h:76/:138`, chains `vector<unsigned long long>`, `unordered_set<unsigned long long>` | elements of `CvGame::m_aszDestroyedCities`, `m_aszGreatPeopleBorn` (`vector<size_t>`), `m_AdvisorMessagesViewed` (`unordered_set<size_t>`) | patch (CvGame) |
| 2 W | `CvInfosSerializationHelper.h:646` from `CvPreGame.cpp:3631`, `:3640` | `WriteTypeArray` count | pinned, fork |
| 1 W + 1 R | `CvAStar.cpp:4135`, `:4122` | `CvPathNodeArray` count (saved as `CvUnit::m_kLastPath`, `CvUnit.cpp:29034`) | patch |
| 14 W | `CvEspionageClasses.cpp:6674-6715`, `:10585-10621` | `saveTo << vec.size()`; the readers read `int`/`uint`, so an unpatched x64 DLL misreads its own saves | patch |

Counted per instantiation, so the 400-odd container records stand for every saved STL container; they all pass
through the four lines pinned in `FStlContainerSerialization.h`.

Limits of the census: it sees only instantiated templates (uninstantiated serializers were grepped instead:
`WriteDataArray`, `WriteHashedDataArray`, `WriteHashedTypeArray` in `CvInfosSerializationHelper.h` are unused today and
pinned anyway); it sees widths, not layout of raw byte dumps (section 3.3 covers those by reading the code); it covers
the DLL's own sources, not the closed libraries (section 5).

## 3. Sites and verdicts

### 3.1 Pinned in the fork (commit 25feae4dc, parent b25fd45c0)

| File:line (after the change) | Before | After | x86 |
|---|---|---|---|
| `FirePlace/include/FireWorks/FStlContainerSerialization.h:44-55` | - | `FStlSerializedCount(size_t)`: x64 inline function, `FAssertMsg(count <= 0xFFFFFFFF)`, returns `unsigned int`; x86 macro `(count)` | macro is the identity |
| same `:84` (sequence write) | `m_saveTo << container.size()` | `m_saveTo << FStlSerializedCount(container.size())` | unchanged expression |
| same `:99` (sequence read) | `typename ContainerType::size_type count` | `unsigned int count` | same type (`size_type` is `unsigned int` on VC9) |
| same `:145` (associative write) | `size_t count = container.size()` | `unsigned int count = FStlSerializedCount(...)` | same type |
| same `:162` (associative read) | `size_t count` | `unsigned int count` | same type |
| `CvGameCoreDLL_Expansion2/CvInfosSerializationHelper.h:572, 588, 646, 658` | `kStream << aArray.size()` | `kStream << (uint)aArray.size()` | cast to the same type |

Both read paths keep the `count == 0xFFFFFFFF` sentinel on the 32-bit value (it could never match a 64-bit
`size_t`; finding from 1002, 2026-11-10).

### 3.2 Pinned by patch (other owners; `save-audit/other-files-save-width-pins.patch`)

| File | Change | Why |
|---|---|---|
| `CvGame.cpp:86-98` `stringHash` | return and accumulator `size_t` -> `uint`, char cast `(size_t)` -> `(uint)` | the hash values are saved. The function is FNV-1 (not `std::hash`), and the low 32 bits of the 64-bit computation equal the 32-bit one, so the pinned x64 values equal the x86 values `[I]` (mod-2^32 arithmetic) |
| `CvGame.h:899, 962, 963` | `unordered_set<size_t>` / `vector<size_t>` -> `<uint>` | `m_AdvisorMessagesViewed`, `m_aszDestroyedCities`, `m_aszGreatPeopleBorn` are saved (`CvGame.cpp:10887, 10949, 10950`) |
| `CvGame.cpp:5088, 8372, 8387` | matching iterator types | |
| `CvAStar.cpp:4119, 4135` | `size_t count` -> `uint`, `<< (uint)readFrom.size()` | `CvUnit::m_kLastPath` is saved |
| `CvEspionageClasses.cpp` 14 lines | `saveTo << (uint)x.size()` | writer/reader width mismatch on x64 |

On x86 every changed type is the same type as before (`size_t` is `unsigned int`), so the patch is a no-op there.
CvTacticalAI.cpp and CvHomelandAI.cpp (epic 1300 is editing them) contain no stream-width sites `[C]`.

### 3.3 Checked and equal on both builds

| What | Verdict |
|---|---|
| `long`, `unsigned long`, `DWORD` (`CvGame::writeSaveGameDB` writes the DB size as `DWORD`) | 4 bytes on both (LLP64) `[C]` census silent |
| `long long` members | 8 bytes on both, listed in 2.1 `[C]` |
| `time_t`, `clock_t` | not serialized anywhere (no members of these types) `[C]` grep. VC9's default `time_t` is 64-bit anyway |
| `wchar_t` | `/Zc:wchar_t` on both, 2 bytes `[C]` build flags |
| `bool` | 1 byte, `FDataStream::Write(const bool&)` = `WriteIt(1, ...)` `[C]` disassembly |
| enums (incl. negative values) | no enum in the DLL sources declares an underlying type; MS ABI makes them `int` on both; written by the CvGameCoreDLLUtil serializers (1006 confirms 4 bytes) `[I]` |
| `std::vector<bool>` | elements written one `bool` at a time via `const_reference` (a `bool` in both STLs); count pinned `[I]` |
| `std::pair`, structs | written member by member (`FStlContainerSerialization.h` `operator<<(pair)`); no padding reaches the stream `[C]` |
| raw dumps: `CvBitfield` (`CvBitfield.h:131,195`), `ReadAndRemapDataArray` (`CvInfosSerializationHelper.h:487`, instantiated with `bool`/`int` arrays in `CvTechClasses.cpp:2238-2242`), save DB blob (`CvGame.cpp:11125,11167`) | byte / `uint32` / `int` / `bool` arrays, same layout on both `[C]` |
| `FFreeListTrashArray::Read/Write` members (raw `sizeof(T)` struct dumps, `FFreeListTrashArray.h:472,499`) | never instantiated: they call `FDataStream::Read(int*)`, which has no overload, and the DLL compiles `[C]`. The `operator<<`/`>>` used instead write `int`s |
| `FFastVector`/`FStaticVector` counts (`FDataStream.h:586,611`) | `size()` returns `unsigned int` (`FFastVector.h:164`) `[C]` |
| `ArrayWrapper`, `CvEnumMap`, `CvSpan`, `tr1::array` | count is `int` or not written `[C]` |
| `CvWeightedVector` (`CvWeightedVector.h:288`) | `size()` returns `int` `[C]` census silent |
| `CvString` / `std::string` / script data (`m_strScriptData`, plot script data) | closed-lib string writer, 4-byte length (section 5) |
| `float` / `double` | same bit format; values drift because x87 vs SSE2 (x64-dll-port.md section 5), not a layout issue |
| Lua mod save data (`SaveUtils`, `Modding.OpenSaveData`) | lives in `Civ5SavedGameDatabase.db`, embedded with a `DWORD` size. Its bytes are SQLite's file format; identity needs the same SQLite version and the same statement sequence (issue 1005 / the server's DB owner) `[I]` |
| network: `ICvNetMessageHandler` calls | typed C++ calls through the EXE, not stream bytes; `ICvDLLScriptSystem.h:23` and `ICvDLLUtility.h:204-205` take `size_t` lengths across the EXE boundary (an interface concern for the proxy, not a save byte) |
| sync archive (`CvSyncVar::save/load/compare`) | same `FDataStream` operators; covered by the census (the sync var `m_lJONSCultureEverGeneratedTimes100` is legitimately 64-bit) `[C]` |

## 4. Why the x86 save bytes are unchanged

Every edit either keeps the expression textually identical on x86 (`FStlSerializedCount` is `#define ... (count)`
unless `_WIN64`) or replaces `size_t` / `size_type` with `unsigned int`/`uint`, which are the same type on VC9 x86. The
overload `FDataStream::operator<< <T>` therefore instantiates with the same `T` and calls the same
`Write(const UINT32&)`, and the reads call the same `Read(UINT32&)`. The x86 census after the pins records the same 9
sites as before `[C]`. Built after the change, 2026-11-10: x86 `python build_vp_clang.py --config debug` compiles and
links `clang-output\Debug\CvGameCore_Expansion2.dll` (exit 0) `[C]`; x64 probe compile (PCH + all 173 TUs, flags of
`evidence/2026-11-10-probe/`) 173 of 173, 0 failed `[C]`. The x86 object files were not byte-compared: the added lines
shift `__LINE__` and debug info, so they differ for reasons unrelated to the save format.

## 5. FDataStream members that carry a width (for issue 1006)

The members live in `FireWorksWin32.obj` (`.\Final Release\FDataStream.obj`). Every primitive writes through the
virtual `WriteIt(byteCount, buffer)` (vtable slot 2, `+0x8`). Widths the x64 replacement must keep:

| Member (header `FDataStream.h`) | Bytes in the stream | Source |
|---|---|---|
| `Write/Read(bool)` | 1 | `[C]` disasm: `WriteIt(1, &v)` |
| `Write/Read(char)`, `(unsigned char)` | 1 | `[I]` type |
| `Write/Read(INT16)`, `(UINT16)`, `(WCHAR)` | 2 | `[I]` type |
| `Write/Read(INT32)`, `(UINT32)` | 4 | `[I]` type |
| `Write/Read(unsigned long)` | **4** (`unsigned long` is a distinct type from `UINT32` on x64; must stay 4) | `[C]` disasm: `WriteIt(4, ...)`, with a byte swap when `m_eEndianness` (offset 8) differs from native |
| `Write/Read(INT64)`, `(UINT64)` | 8 | `[I]` type |
| `Write/Read(float)` / `(double)` | 4 / 8 | `[I]` type |
| `Write/Read(GUID)` | 16 | `[I]` type |
| `Write/Read(std::string)` | **4-byte length** (`int`, VC9 `_Mysize`), then `length` bytes, no terminator; nothing after the length when it is 0 | `[C]` disasm. On x64 `std::string::size()` is 64-bit: narrow it to 32 bits |
| `Write/Read(std::wstring)` | **4-byte length in characters**, then `2 * length` bytes (UTF-16) | `[C]` disasm |
| `WriteString(std::string)` | same bytes as `Write(std::string)`; returns bytes written | `[C]` disasm |
| `Write(int count, T[])` arrays | `count` elements of the widths above, no count written | `[I]` header |
| `FStringA`/`FStringW` (`Read(FStringA&)` etc.) | not used by the DLL's saves as far as the census shows; format unknown | - |
| `operator<<(FDataStream&, const std::string&)` (`FStlContainerSerialization.h:287`) | not disassembled; expected to forward to `Write(std::string)` | `[I]` |

The endianness swap path exists in every multi-byte writer `[C]` for `unsigned long`; the replacement should keep it
or assert that the desired endianness is native.

Other closed-library code whose output reaches the save bytes:

- `FCRC::Calc` / `g_CRC32` (`FCrc32.h`, polynomial 0xEDB88320, init `~0`): `FStringHash` = `g_CRC32.Calc(...)`
  (`CvGlobals.cpp:70-78`) produces every saved info-type hash (`CvInfosSerializationHelper::WriteHashed`) and the
  save header's game-data hash (`CvGame.cpp:11091`). Whether a final xor is applied is not known: check the
  replacement against the x86 lib with test vectors.
- CvGameCoreDLLUtil's 121 enum `operator<<`/`>>`: expected 4-byte `int` each (1006 confirms by disassembly).
- `FAutoArchive::save/saveDelta` (sync archive, network): `m_deltas` is a `std::set<FAutoVariableBase*>`
  (`FAutoArchive.h`); every variable of one archive lives inside one `CvSyncArchive<T>::SyncVars` object, so its
  pointer order is member declaration order, the same on both builds and in every run `[C]`. (Corrected 2026-11-10
  per epic 1600, `research/03-dll-engine-boundary/x64-dll-port.md` section 9 and `stl-drift-sites.md` P15; replaced
  "ordered by heap address ... the delta order in a network message depends on addresses, on both builds [I]".)

## 6. STL drift that reaches saves, game logic or the RNG (listed, not fixed)

VC9's `std::tr1::unordered_*` / `stdext::hash_*` and MSVC 14's `std::unordered_*` use different hash functions
(VC9: `hash_value` scrambling; MSVC 14: FNV-1a over the bytes) and different bucket growth, so iteration order differs
for the same contents. Lookups (`find`, `operator[]`, `count`) are unaffected.

### 6.1 Iteration order reaches the save bytes (fixed 2026-11-10 by D18 / issue 1008, see below)

| Container | Declared | Serialized | Notes |
|---|---|---|---|
| `CvCity::m_miInstantYieldsTotal` `tr1::unordered_map<YieldTypes,int>` | `CvCity.h:2093` | `CvCity.cpp:32427`; also a sync var (`CvCity.h:2508`) | sync compare uses `==`, so desync detection is fine; save bytes differ |
| `CvExplorationPlotSet::m_aValues` `tr1::unordered_map<int,ExploreEntry>` | `CvEconomicAI.h:88` | `CvEconomicAI.cpp:4765`, three per player (`m_vPlotsToExploreLand/Coast/Ocean`, `CvEconomicAI.cpp:353-355`) | |
| `CvGame::m_AdvisorMessagesViewed` `tr1::unordered_set<size_t>` | `CvGame.h:899` | `CvGame.cpp:10887` | width pinned by the patch; order still differs |

**Fixed by D18 (the owner, 2026-11-10 03:02 EST, option A; issue 1008), fork commit 3ac5819ba on `x64` f3d2f5669.**
The two writers in `FStlContainerSerialization.h` for `std::tr1::unordered_map` and `std::tr1::unordered_set` now
copy into `std::map` / `std::set` and write that: sorted by `std::less` on the key value (never a pointer or a hash),
same count and same bytes per element. The readers are unchanged and insert element by element, so saves in the old
hash order still load. One change covers all three containers and the sync variable. x86 save bytes change in these
containers only (D18 amends D10).

Headless check, 2026-11-10 03:11-03:36 EST `[L]` (civ5-headless 0.3.0 host
`out/Release/20261027-120431/civ5headless.exe`, game folder = the toolkit's `VP_MODPACK_5_4_8_NEUTRALS` copy, pinned
5.4.8 cdrift DB copied to scratch, `--no-mods`; all writes under the session scratchpad; evidence and scripts in
`evidence/2026-11-10-save-sorted-1008/`):

1. Two stock saves: the official 5.4.8 modpack DLL (SHA-256 `4E91C58A...C3F82`) played a Small Continental Drift
   game (seeds map 1008 / sync 1808, quick speed) to turn 60 and, separately, to turn 150.
2. Each save was loaded and saved again at once (`save-now.lua`: `UI.SaveGame` before any turn) by four x86 release
   DLLs: the official stock DLL; clang builds of Release-5.4.8 (7307aefdf, "srcstock"), of the `x64` tip f3d2f5669
   ("pins": 1002 + 1004 changes) and of 3ac5819ba ("sorted"). The three clang builds share one fixed version string,
   so code is the only difference.
3. Results (`headless-check-1008.txt`):
   - official stock DLL, twice: byte-identical (the procedure is deterministic);
   - official stock DLL vs srcstock: identical except the DLL version string in the body header;
   - **srcstock vs pins: byte-identical at turn 60 and turn 150.** The 1002/1004 changes (all 318 x64 width pins
     and 1002's out-of-process code) leave x86 saves unchanged `[L]`;
   - **pins vs sorted: equal lengths, every differing byte inside a re-ordered container** (record multiset equal,
     sorted copy in ascending key order): turn 60, 8,776 bytes in 18 `m_aValues` maps (6 players x 3); turn 150,
     7,402 bytes in 15 `m_aValues` maps and 31 `m_miInstantYieldsTotal` maps; 0 bytes unattributed;
   - the official stock DLL loads the sorted turn-60 save (host status passed, no load warnings, body consumed
     exactly), so the old reader accepts the new order.
4. Not exercised: `m_AdvisorMessagesViewed` is empty in an autoplay game (no human advisor UI), so its sorted write is
   verified by code only. The x64 build of 3ac5819ba compiles (probe flags, 173 of 173 TUs).

### 6.2 Iteration order reaches other logic

| Use | Effect |
|---|---|
| `CvGlobals::m_infosMap` `stdext::hash_map<string,int>` (`CvGlobals.h:169`), iterated in `Lua/CvLuaGameInfo.cpp:104` to build Lua's `GameInfoTypes` | insertion order of a Lua table; `pairs()` order over it may differ for mods that iterate it. Low |
| `CvDatabaseUtility::m_storedResults` `stdext::hash_map` (`CvDatabaseUtility.h:113`), iterated at `CvDatabaseUtility.cpp:28` | deletion only; no effect |
| `TContainer::m_items` `tr1::unordered_map<int,T*>` (`TContainer.h:86`), iterated at `:99`, `:159` | deletion only; game order comes from `m_order` |

### 6.3 Lookup-only unordered containers (no order or logic effect)

`CvBuilderTaskingAI::m_futureYieldBonuses` (`CvBuilderTaskingAI.h:281`), `m_extraPlotsForXAdjacentImprovements`
(`:295`, pointer keys), `CvDealAI::m_dealItemValues` (`CvDealAI.h:229`), `CvHomelandAI.h:180` `CitySet`,
`CvTacticalAI.h:879-882` (`gReachablePlotsLookup`, `gRangeAttackPlotsLookup`, `gSafePlotCount`,
`gDistanceToTargetPlots`), `CvTacticalAI.h:1102,1112` (`dangerStats`, `attackStats`), `CvTypes.cpp:92,109,147,173`,
the closed DB headers (`Database.h:172-173`, `DatabaseResults.h:125`, `ResultsCache.h:38`; their replacement is issue
1005's), `FVariableSystem.h:49`.

### 6.4 Hash values used as data

| Use | Effect |
|---|---|
| `CvGame.cpp:86` `stringHash` (FNV-1, own code) | values saved; STL-independent; width pinned by the patch |
| `CvUnit.h:295-324` `GetHash()` (`size_t`, `(k << 4) ^ v`) and `CvTacticalAI.h:1031` `DefendKey::iDamageHash` | the hash is part of the cache key's equality (`CvTacticalAI.h:1038`). On x86 the shift truncates to 32 bits, so collisions (two damage states sharing a cached danger value) happen in different cases than on x64. AI drift, bitness not STL `[I]`. CvTacticalAI.cpp/.h belong to epic 1300's edits: listed only |
| `tr1::hash<int>` in `SDealItemValueParamsHash` (`CvDealAI.h:213-221`), `SPathFinderStartPosHash`, `SIntPairHash` (`CvTacticalAI.h:843-875`) | bucket placement only; keys compare by value. No effect |

### 6.5 Other STL algorithms whose results depend on the implementation

VP already uses `stable_sort` / `StableSortItems` (244 calls), which are deterministic for a given comparator. The rest:

**Corrected 2026-11-10 (epic 1600, `research/03-dll-engine-boundary/x64-dll-port.md` section 9, [C] by reading both
libraries' headers):** `sort`, `make/push/pop_heap` (hence `priority_queue`), `nth_element` and `random_shuffle` with a
caller RNG make the same comparisons, swaps and RNG draws in VC9 and MSVC 14, so the same input gives the same
permutation, ties included. The first three rows below are corrected in place (they said the RNG draws, the
permutation and the tie order depend on the STL).

| Call | Where | Risk |
|---|---|---|
| `std::random_shuffle(..., GetJonRand)` | `TContainer.h:219` (`TContainer::Shuffle`) | consumes the synchronized game RNG. Same N-1 draws with arguments 2..N and the same permutation in VC9 and MSVC 14 `[C]` (section 9 of x64-dll-port.md; replaced "depend on the STL's algorithm ... highest risk [I]"). No effect |
| heap ops with comparators (`make/push/pop_heap`) | `CvAStar.cpp:496,500,704` (pathfinder open list); `CvTacticalAI.cpp:9561,11483,12392,12399,12822,12829` | same sift steps in both libraries, so equal-priority elements come out in the same order `[C]` (replaced "can come out in a different order"). No effect |
| `priority_queue<SPlotWithScore>` | `CvPlayerAI.cpp:3197` (culture-bomb plots) | heap-based, same as above: no effect `[C]` |
| `std::sort` | `CvCityConnections.cpp:691`, `CvTacticalAI.cpp:5639` | plain value vectors: the result is unique. No effect |
| `std::nth_element` | `CvGame.cpp:10556,10624-10628`, `CvMinorCivAI.cpp:12071,12247,12457`, `CvTacticalAI.cpp:12312` | only the nth value is read (medians of ints/floats): unique. No effect `[I]` |

Pointer-ordered containers (`std::set<T*>`, `std::map<T*,...>`) iterate in heap-address order on both builds and are
not deterministic across runs even on x86; out of scope here.
