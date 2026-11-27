# Upstream draft: a pantheon's holy-city coordinates are never set (for the owner; NOT sent)

Bug 0508, done by ticket 4805, 2026-11-17. Nothing below has been sent (D130: drafts wait for the owner's yes to the exact
text, after the beta). It names no internal ids, decisions, project names or cherry-pick lines (D95). The defect was
found by the PGO identity run (`research/08-performance/speedup-p1-pgo.md` section 6 on the epic 0504 branch); the fork
branches, builds and checks are in section "Fix branches and checks" below.

**Checked first [C, 2026-11-17 18:37 EST]:** upstream `LoneGazebo/Community-Patch-DLL` has no issue or pull request on
it (`gh search issues` and `gh search prs` for `m_iHolyCityX`, "pantheon holy city", "uninitialized pantheon",
"pantheon uninitialised", `IsReligionOwner`, "CvReligion constructor": no match on the topic; the only hit, #2943 of
2017, is a crash with an admiral and a missing holy city). Master `499b958b4` (2026-11-17) still has the constructor as
described (`CvReligionClasses.cpp` 199-213 there); its last commits to the file (2026-11-09 to 11-17) do not touch it.

If the owner says yes: an issue, and the pull request below from a fork branch on master.

**Issue title:** Pantheons keep uninitialised holy-city coordinates (saved and read)

**Issue text:**

> `CvReligion::CvReligion(ReligionTypes, PlayerTypes, CvCity* pHolyCity, bool bPantheon)` sets `m_iHolyCityX` and
> `m_iHolyCityY` only inside `if (pHolyCity)`. `CvGameReligions::FoundPantheon` constructs the pantheon with
> `pHolyCity == NULL`, so both fields keep whatever was on the stack. They are serialized with the religion, so saves
> carry the garbage, and they are read: the religion-owner checks of the statistics loggers
> (`CvEconomicAI.cpp` `PlayerOwnsReligion`, the owner block in `CvPlayer.cpp`) look up
> `GC.getMap().plot(m_iHolyCityX, m_iHolyCityY)` and treat the pantheon as owned by its founder only when that plot is
> NULL. The value depends on code layout, so two builds of the same source (different compiler settings, profile-guided
> optimisation, an unrelated code change) can log different values for the same game: we saw
> `ReligionBeliefYields.IsReligionOwner` flip from 1 to 0 on pantheon rows between two builds of one commit, with every
> game-state table identical. Game logic reads the same fields: `CvReligionBeliefs::IsBeliefValid` gives a pantheon's
> founder benefits to its founder only when `plot(m_iHolyCityX, m_iHolyCityY)` is NULL and otherwise asks that plot's
> owner, and `CvGameReligions::GetHolyCityReligion` returns the pantheon for a city that happens to sit at the garbage
> coordinates. Whether a game is affected therefore depends on the build.
>
> The default constructor already sets both to -1.

**Pull request title:** Initialise a pantheon's holy-city coordinates

**Pull request text:**

> The `CvReligion` constructor that takes a holy city now initialises `m_iHolyCityX` and `m_iHolyCityY` to -1, as the
> default constructor does; a religion with a holy city still sets them from it. A pantheon now always reads as having
> no holy city (`plot(-1, -1)` is NULL), which is what the readers expect.
>
> Tested on 5.4.8 (release builds, AI-only autoplay): with the change, a pantheon-founding game and two loaded
> mid- and late-game saves give the same statistics as before the change (the builds we tested happened to read the
> garbage as "no plot"; another compiler or build can differ); two builds of one source that used to log different
> values for the same game now log the same.

## Fix branches and checks

Fork branches (pushed 2026-11-17 19:13 EST), one commit each with the same text ("Initialise a pantheon's holy-city
coordinates", the two initialisers of the scratch check `23b822875`):
- `fix-0508-x64` **77d110cc4** from the x64 pin `4f4daac70` (x64-batch3)
- `fix-0508-i0ship` **36b4f0ee1** from the i0-ship pin `39f17f754` (i0-ship-batch3)

Builds [C, 2026-11-17]:
- x64 release, `tools/x64-dll/build_all.py --src <worktree of fix-0508-x64> --config release --offline --unlocked` at
  this repository's ticket branch (main `21f97e95f` + ticket 4805's commits): every step rc 0, 0 unresolved, narrowing
  gate 794 against 798 (0 new), 18:27-18:38 EST; DLL `f98b6370704435d86cfece42ca8195e4ff9b37c7489062626bdffefb009f47c7`
  (23,669,760 B), `C:/c5-64bit-work/4805/b-0508-x64/dll/`.
- 32-bit release, `build_vp_clang.py --config release` (clang-cl 19.1.7, LTO) in the worktree, through `quietlock.py
  run`, 19:04-19:12 EST (link 361 s); DLL `fe70782d6c269da997a882b2c471105238159d268c624b6b3b68656632c25502`,
  `C:/c5-64bit-work/4805/dlls/0508-i0/`. Not repeatable (as the pin's): the hash belongs to this build.

Today's x64 game unchanged [L, 2026-11-17 19:20-19:36 EST, `../a9-wonder-specialization.md` section 2 runs]: pin
(`2d737bd5...`) = fixed x64 on M 150-160 (31,126 rows), H 300-305 (111,122) and S 0-50 (19,016; S founds pantheons):
IDENTICAL. So on the batch-3 pin, as on batch 2 (4801's scratch check), the pin's garbage already led every reader to
the founder branch.

Save test (D24) and x64 = x86 parity on the two fixed DLLs [L, 2026-11-17 19:35:54-19:57:06 EST, workstation]:
`tools/d10/run_d10.py --x86-dll <fixed 32-bit DLL> --srv-bin <pin layout bin with the fixed x64 DLL> --huge-save
<Release-5.4.8-HUGE_0350, c499d3c6...> --order-exception none --d18-x86 yes --d18-x64 yes --jobs 6`, AI logging off in both
arms: **PASS**, every part of tiny 0-100, standard 0-200 and huge 350-400 (writer, behaviour, cross-load; only the
version-string exception; versions "Release-5.4.8-45-g36b4f0ee1c Clean" and "Release-5.4.8-58-g77d110cc44 Clean").
x86 direct = x64 direct: 30,666 / 351,496 / 689,879 rows, the same counts as the pins' own save test (0463). Report
`perf/evidence/0504/4805/d10-0508-report.txt`; run folder `F:/c5-64bit-work/4805/d10-0508`.

The longer x64 game [L, 2026-11-17 20:19-20:25 EST]: the Standard fixed-seed game 0-200 (`perf/evidence/0504/4805/
a9runs.py --sets T`), pin against the fixed x64 DLL: IDENTICAL, 351,496 rows. With the D24 standard game's x86 = x64 on
the fixed DLLs (351,496 rows, above) and the pins' own x86 = x64 (0463), the fixed 32-bit DLL plays this game as the
pinned 32-bit DLL does [I: by transitivity, no direct x86 pin-against-fix run].

**Verdict for the MC (bug 0508):** a defect fix that keeps today's games (no behaviour question); ready for the pin
batch with A9. Not verified: a game where a pantheon's garbage happened to hit an on-map plot in today's builds (none
of these games shows one; such a build or game would change with the fix, towards the intended result).
