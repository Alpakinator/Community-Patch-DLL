# CvLocalization A/B against the closed 32-bit library (issue 1702)

One command compares our CvLocalization (issue 1701) with the closed `CvLocalizationWin32Final Release.dll` on x86,
and our x86 build with our x64 build, over a corpus of about 250,000 lookups and compositions. Modelled on the
CvGameDatabase A/B (`tools/x64-dll/cvgamedatabase/ab/`). Written 2026-11-10 by worker-ab, team 1700.

```
python tools/x64-dll/cvlocalization/ab/run_ab.py --out DIR [--closed-only] [--config release|debug] [--src FORK]
                                                 [--unlocked] [--skip-build]
```

`DIR` should be outside the repository (for example `%LOCALAPPDATA%\Temp\claude\cv64-1700\ab\<run>`); a full run
writes about 0.5 GB there (outputs of every arm). `--closed-only` runs the closed library alone (the reference);
without it `build_db.py` and `build_loc.py` build ours for x86 and x64 first. Exit 0 when every comparison has no
unexplained difference, 1 otherwise, 2 on a setup error (wrong hash, build failure, missing `build_loc.py`).

## Files

| File | What |
|---|---|
| `run_ab.py` | the driver: hashes and copies the inputs, generates the corpus, builds, links, runs, compares; `EXPLAINED` holds the regexes of understood differences |
| `gen_corpus.py` | writes the test database copy and the corpora from read-only inputs; deterministic (sorted, no clock) |
| `locquery.cpp` | the query program, linked once per arm; layout-agnostic (below) |

## Inputs (read only, hash-checked or hash-logged)

- Closed DLLs from the headless toolkit's game copy (`...\VP_MODPACK_5_4_8_NEUTRALS\Sid Meier's Civilization V`):
  `CvLocalizationWin32Final Release.dll` (SHA-256 `9b783bc3...c50eac4`), `CvGameDatabaseWin32Final Release.dll`
  (`f9563739...6949563`), `lua51_Win32.dll`, `Locale\icudt44l.dat` (`2bdb5ffe...2646fda`). Copied into `DIR/run-closed`.
  MSVCR90/MSVCP90 come from WinSxS.
- Fork headers and closed import libraries (`CvLocalization/lib/CvLocalizationWin32.lib`,
  `CvGameDatabase/lib/CvGameDatabaseWin32.lib`), `--src` checked against `deps/dll.lock`.
- `Localization-Merged.db` (c5h pinned, SHA-256 `ff9a0997...f156be0`).
- The game's Lua under the toolkit game copy's `Assets` (744 files).
- stats.db of issue 1505's runs: x64 `team1500-runs\{std200,tiny50r-a}` (raw `TXT_KEY_` values), x86
  `team1500-ref\{standard,tiny-a}` (composed text). Skipped if absent.

## Layout-agnostic query program

`locquery.cpp` is compiled with the fork's headers but never relies on the size or layout of `Localization::String`
or `Database::Connection` (the closed library has VC9 STL members inside): objects are built with placement new into
1 KB / 4 KB aligned buffers by the library's exported constructors and destroyed through the virtual destructor.
Functions returning a `String` by value (`Localization::Lookup`, `StringDictionary::Lookup`/`LookupLanguage`) are
called through a function pointer that passes the hidden return pointer explicitly (x86: cdecl `ret, args` /
thiscall `this` in ecx then `ret`; x64: rcx/rdx), so the caller never needs `sizeof(String)`. `ParameterTransforms`
members are never called (the header is out of date: the closed DLL's take an extra `vector<ParameterArgument>`).
Optional groups `LOC_HAVE_DICT` (StringDictionary) and `LOC_HAVE_EXTRA` (String form/trait members,
`Language::GetPluralityRule`) are compiled in for an arm only when its DLL exports every name of the group.

Robustness, because the closed library crashes on some inputs: every command runs under SEH (`X CRASH 0x... in
<command>` line, stack overflow reset with `_resetstkoflw`); an invalid-parameter handler is installed in the UCRT
and in MSVCR90 (found by walking the loaded modules: it is a side-by-side assembly, `GetModuleHandle` does not see
it), so `sprintf_s` overflows in the library print `INVALID_PARAMETER` instead of killing the process. Output is
written per command; a library log message that arrives in the middle of a line is placed right after it.

## Corpus (gen_corpus.py)

The test copy of the database adds: the 9 other languages to `Languages` (PluralRule de_DE/es_ES/it_IT 2, fr_FR 3,
ja_JP/ko_KR/zh_Hant_HK 1, pl_PL 9, ru_RU 7) with `LanguagePriorities` (self 100, en_US 50); 18 probe languages
`zz_R00`..`zz_R17` with PluralRule 0..17; 17 `TXT_KEY_AB_EDGE_*` keys (en_US) for edge cases. Five corpora, each run
in its own process because some states break the closed library for the rest of the process:

| Corpus | Contents |
|---|---|
| main | Initialize(en_US, fallback); every en_US key (KEY: GetNumForms, IsEmpty, toUTF8(bytes, f) and GetPlurality/GetGender(f) for f = 1..n+1, toUTF8()); every en_US key with placeholders composed with 7 synthetic argument sets matched to its slot types, plus none and too many; a sweep of every distinct placeholder spec (raw `String`) over ints, floats (as float and double), int64, wrong types; stats.db keys; Lua `Locale.ConvertTextKey/Lookup` calls with literal keys; malformed syntax and edge keys; the hard-coded edge block (NULL/empty key, default String, forms 0/-1/huge, PushArgument after toUTF8, copies and assignments, 160 KB string, 40 arguments, 300-deep braces, StringDictionary members, SetCurrentLanguage edge cases, logger swap); then per other language: SetCurrentLanguage, every key, 3 argument sets, spec sweep, 200 en_US-only keys through the fallback; then the plural probe (GetPluralForm for n = -200..1200 and large values, a plural choice string for n = -3..30 and more, per probe rule) |
| nofallback | Initialize(en_US, fallback disabled); a key sample; de_DE with en_US-only keys |
| dictinit | ShutdownDictionary + InitializeDictionary with fallback disabled, de_DE, an unknown language |
| reinit | Shutdown + Initialize(de_DE) |
| cycles | a self-referencing key and a two-key cycle |

Argument encoding in the corpus: `i` int, `I` int64, `f` float, `d` double, `s` `const char*`, `n` `(const char*,
len)`, `0` NULL `const char*`, `k` a looked-up `String`, `r` a raw `String`. Lua arguments follow the 32-bit
headless host's `Locale.Lookup` (integral numbers as int, others as double, strings with length) `[I]`: the EXE's own
conversion is not decompiled.

## Results

Evidence: `tools/x64-dll/evidence/2026-11-10-1702/` (summary, log, extracts of the closed reference). Issue 1702.

- Closed reference, 2026-11-10, release, VP 5.4.8 localization database `[L]`: 253,660 corpus commands; main corpus
  253,160 output blocks (147,409 KEY, 105,565 FMT lines; 67,039 error, 32 warning messages; 51 access violations,
  612 invalid-parameter reports), 33-45 s; the four small corpora 1-5 s each; `--closed-only` about 70 s in all,
  `--aa` 131 s.
- A/A (the closed library twice, `--aa`): identical except one line, the out-of-bounds read below, now normalized
  (`NORMALIZE` in `run_ab.py`); the comparator found exactly that line. The `reinit` corpus differed in one of three
  A/A runs (Initialize after Shutdown crashed in one arm, worked in the other), so it is `INFORMATIONAL`: reported,
  not part of the result.
- stats.db: of 902 `TXT_KEY_` values the x64 server wrote (issue 1505 runs), the closed library's `Lookup(key)` text
  is in the same column of the x86 game for 863; the other 39 are city, building and belief names that only occur in
  the x64 games (the games diverge) `[I]`, listed in `stats-check-closed-x86.txt`.
- Ours vs closed (2026-11-10, corrected 08:15 by the team-1700 coordinator; was "not run yet"): **PASS**, end to end,
  `run_ab.py --config debug`, 297.9 s. Closed vs ours x86: main 253,132 blocks each, aligned, 369 differing, all
  explained (README of the library, differences 1-4, plus the per-run totals line); `nofallback` 453 and `dictinit`
  187 blocks, 2 differing each (Lookup before Initialize, totals), explained; `cycles` 17 blocks, 6 explained; `reinit`
  informational (47 differ, the closed library's undefined behaviour). Ours x86 vs ours x64: 0 differing blocks in all
  five corpora. Log: `tools/x64-dll/evidence/2026-11-10-1705/run_ab-debug-endtoend.log`; worker-lib's release run
  (197 s) gave the same counts.

## Closed-library behaviour the replacement must reproduce (`[L]`, measured with this harness 2026-11-10)

Lookup and dictionary
- Missing key: the String holds the key itself (also through the fallback); key match is exact and case-sensitive
  (`txt_key_unit_settler`, trailing or leading space: missing). `Lookup(NULL)`, `Lookup("")`, `String()`,
  `String(NULL)`: 0 forms, `IsEmpty` true, `toUTF8` "" (not NULL). Lookup before `Initialize`: access violation.
- Fallback on (`Initialize(..., false)`): a key missing in the current language comes from en_US. Fallback off:
  the key itself. A row with NULL or empty Text: 0 forms, empty.
- `SetCurrentLanguage(unknown)`: false, current unchanged, error "Could not set language to %s.  No table exists
  for it in the dictionary." Every successful Initialize/SetCurrentLanguage logs error "Plurality Starts at 1.".
- `Shutdown()` deletes the logger; `Initialize` after `Shutdown` in the same process: access violation in 5 of 6
  runs, a working re-initialization (to de_DE) in 1 (undefined behaviour; after the crash lookups still answer from
  the old en_US dictionary). `ShutdownDictionary` + `InitializeDictionary` works.
  `SetLogger(new)` deletes the previous logger.
- `StringDictionary`: 28 languages in the test copy (the `Languages` rows); `HasKey(key, len)` honours `len`;
  `Lookup(key, len-4)` echoes the truncated key; `LookupLanguage(key, idx)` gives the same text for idx 0..2.

Forms, gender, plurality
- Text is split at `|` into forms (`a||b|` is 4 forms, two empty); Gender and Plurality are split the same way.
  Defaults: plurality 1, gender 1 (= `GENDER_MASCULINE`) for a form without a value; an unknown gender word gives 0,
  an unparseable plurality 1. `toUTF8(bytes, f)` for f = 0, < 0 or > n: "" with bytes 0; `GetPlurality`/`GetGender`
  out of range: 0. `GetFormByPlurality(2, 1)` = the second form with plurality 2; none: 0.

Composition
- A missing argument anywhere at the top level makes the whole result "" (bytes 0) and logs two errors per
  composition ("Missing argument %d. (%s)", "Failed to evaluate parameter %d. %s"); the DLL then shows the key
  (its `GetLocalizedText` fallback). Inside a nested `{TXT_KEY_...}` the nested part becomes "" and the rest stays
  (`L1 L2 L3 L4 `). Nested keys see the outer arguments. Extra arguments are ignored. A NULL `const char*` argument
  is "". Composition is redone after a later `PushArgument`; `operator=(const char*)` keeps the pushed arguments.
- Wrong types: string to `number` gives "" and "FormatNumber: Unsupported, must be numeric argument"; number to
  `textkey`: "FormatTextkey: Unsupported argument, must be string."; number to `gender`: "'gender' transform:
  Unsupported argument, must be string."; `{1_X: title}`, `ordinal`, `roman`, `percent`, `currency` with a string: "".
- Numbers (ICU 4.4): a plain `{1_X}` int is grouped (`1,234,567`); a float or double plain is `#,##0.###`
  (`1,234.568`, `0.005`); floats are widened to double and formatted from 15 significant digits, so `spellout` of
  float 0.005 is "zero point zero zero four nine nine nine nine nine nine eight eight eight two four one two nine";
  rounding is half-even on that decimal (`#.##`: double 2.675 -> "2.68", float 2.675 -> "2.67", double 0.005 -> "0");
  int64 5000000000 formats fully. The first number formatting per language logs warning "A resource bundle lookup
  returned a result from the root locale (not an error)." Full table: `closed-number-sweep-en_US.txt`.
- Plural rules: the `PluralRule` number k behaves like Mozilla's rule k-1 (hence "Plurality Starts at 1."), with
  deviations: rule 7 gives form 1 for every n ending in 1 (11 too), form 2 for n ending in 0, else 3; rule 11 uses
  the last digit, not the last two. Rules 0 and 17: no rule object, `GetPluralForm` 0, every plural choice falls to
  `other`. So the probe's ru_RU = 7 and pl_PL = 9 are not Russian and Polish (they would be 8 and 10); this matters
  only for the test copy. Exact tables for -200..1200: `closed-plural-probe.txt`.

Defects of the closed library (the replacement may differ; each needs an `EXPLAINED` entry)
- Access violation in 22 en_US keys (and their other-language versions) that use the injection syntax
  `{TXT_KEY_X << {1_Y}}` when argument 1 is missing (all of `TXT_KEY_NOTIFICATION_TECH_STOLEN_SPY_*`,
  `TXT_KEY_ATOMIC_BOMB_INTERCEPTED`, ...): `closed-crashes.txt`.
- The "Failed to evaluate parameter %i. %.*s" message is formatted with `sprintf_s` into 512 bytes: for a text of
  about 480 bytes or more it overflows, MSVCR90's invalid-parameter handler runs (default: the process ends with
  0xC0000417; under this harness the message is logged as ""): 612 times in the main corpus. "Missing argument"
  messages up to 1,019 bytes are fine.
- For the 300-deep brace string the error message is printed past the end of the string (out-of-bounds read).
- A self-referencing or cyclic key: stack overflow (0xC00000FD), caught here.
