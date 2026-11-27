# CvLocalization replacement (issue 1701)

A real implementation of the closed `CvLocalizationWin32Final Release.dll` (namespace `Localization`: `String`,
`SharedString`, the `StringDictionary`, `Lookup`, `Initialize`/`Shutdown`, the `{n_Tag}` parser and composer,
plural rules, gender and noun traits, multi-form strings, number/spellout/ordinal/roman/case/textkey transforms),
built as `CvLocalization_<arch>.dll` plus import library for x64 (the game DLL and the server) and x86 (the A/B),
from the same sources. No ICU and no other third-party code. Written 2026-11-10 by worker-lib, team 1700, from the
fork's `CvLocalization/include/CvLocalization.h`, a Ghidra 12.1.4 decompile and disassembly of a copy of the closed
DLL (SHA-256 `9b783bc3...c50eac4`) and the 1702 A/B (`ab/`). Confidence: `[C]` decompile or disassembly, `[L]`
verified by the A/B, `[I]` inferred.

## Result

- **x86 A/B against the closed library** `[L]` (`ab/run_ab.py`, 2026-11-10, numbers in the issue log and
  `tools/x64-dll/evidence/2026-11-10-1702/`): main corpus 253,132 output blocks, every differing block matches an
  `EXPLAINED` entry (the differences below); `nofallback` and `dictinit` identical; `cycles` explained; `reinit`
  informational (undefined behaviour of the closed library).
- **ours x86 vs ours x64: identical** `[L]`.
- **Exports** `[C]`: the x86 DLL exports the closed library's 100 names except the 12 `ParameterTransforms` members
  (their exported signatures take an extra `vector<ParameterArgument>` the fork header does not declare; nothing
  calls them) and `SharedString::operator==` (not declared in the header); it adds 12 implicit members of
  `ParameterArgument`/`StringParameter` (the header exports those classes) and `GetLogger`. The game DLL's 16
  imports are all present.

## Build and use

```
python tools/x64-dll/cvlocalization/build_loc.py --out DIR --db-dir DIR_WITH_CvGameDatabase_<arch>.lib
                                                 [--arch x64|x86] [--config debug|release] [--src FORK] [--unlocked]
python tools/x64-dll/cvlocalization/ab/run_ab.py --out DIR [--config release|debug] [--closed-only]
```

`build_loc.py` writes `DIR/CvLocalization_<arch>.dll`, `.lib`, `.pdb`, `.map`, `.exports.txt`,
`build_loc_<arch>.log`; objects in `DIR/obj-loc-<arch>-<config>/`. `--db-dir` must hold
`CvGameDatabase_<arch>.lib` from `cvgamedatabase/build_db.py` (same arch and config): the dictionary runs the closed
library's SQL through our `Database::Connection`/`Results`, so on the same SQLite 3.7.17 the rows are the same.
`--src` is checked against `deps/dll.lock` as by every script here. Toolchain, defines and flags are those of
`build_db.py` (clang-cl 19.1.7, MSVC STL/CRT, `/std:c++14 /MD`, the game DLL's per-config defines, `/W4 /WX` for our
sources, the fork's headers as system includes); `CvLocalizationAPI=__declspec(dllexport)`. About 6 s per arch.
At run time `CvLocalization_<arch>.dll` sits beside the game DLL with `CvGameDatabase_<arch>.dll`.

`Initialize` still needs the ICU data file path it is given to exist and be non-empty (the closed library reads
it whole and fails without it); the file is not used.

## Layout

The fork header fixes the layout of `String` (the DLL builds Strings on its stack), `SharedString` and
`SingleFormString`; this library uses exactly those members (the header is included with `private` defined as
`public`, so the implementation can reach the data members; MSVC mangling does not encode the access of non-static
data members, the export names are unchanged). `Language`, `StringDictionary` and `StringDictionary::Language`
have no data members in the header: every such object this library hands out is a `Detail::LanguageImpl`,
`Detail::DictImpl` or `Detail::DictLanguage` and the exported members cast `this`.

## Members (as implemented = as the closed library unless a difference is listed)

Lifetime and logging
- `Initialize(icuDat, db, languagesTable, language, bDisableFallback)`: false if already initialized, if
  `icuDat` is NULL, missing or empty; else `InitializeDictionary`. `Shutdown`: deletes the logger, shuts the
  dictionary down, allows a new `Initialize` (the closed library crashes there, undefined behaviour).
- `InitializeDictionary`: warning "Trying to initialize the string dictionary when it already exists o_0!" and
  false if one exists; builds the dictionary, `SetCurrentLanguage(language)`; on failure warning "No languages in
  string dictionary." (false) or "Falling back to an existing language." and the first language.
- `ShutdownDictionary`: deletes the languages and the dictionary (the current index is kept, as closed).
- `SetLogger` deletes the previous logger; `LogMessage/Warning/Error` -> `Log(msg, 0/1/2)` when a logger is set.
- `SetCurrentLanguage(type)`: finds or creates the `Language` (plural rule from the `PluralRule` column), then the
  dictionary switches and preloads; the new index is set after the preload, so the preload's `SetPlurality` checks
  against the previous language's rule `[C]`. Unknown type: error "Could not set language to %s.  No table exists
  for it in the dictionary." (256-byte buffer), false. `GetCurrentLanguage` returns the current `Language`.
- `Lookup(key)`: the dictionary's `Lookup`; before `InitializeDictionary` the key itself (difference 4).

StringDictionary
- Constructor: `Count(languagesTable)` (<= 0: error "Languages table not found or is empty. %s"),
  `SelectAll` (failure: "Failed to enumerate languages table (%s). %s"), columns `Type`, `Name`, `PluralRule`;
  prepares the lookup query: with fallback `select Text, Plurality, Gender from LocalizedText where rowid = (select
  rowid from (select rowid, Language from LocalizedText where Tag = ?2) as T1 left join (select Language, Priority
  from LanguagePriorities where LanguageSet = ?1 ) as Priorities on T1.Language = Priorities.Language order by
  Priority DESC LIMIT 1)`, without `SELECT Text, Plurality, Gender from LocalizedText where Language = ? and Tag = ?
  LIMIT 1` (?1 = language type, ?2 = key); and `SELECT DISTINCT Tag from LocalizedText`.
- `SetCurrentLanguage(idx|type)`: preloads every distinct tag into a cache (missing -> the key itself). A lookup
  for the current language answers from the cache only (a key not in the table -> not found); other languages, or
  an empty cache, query the database. A found row: `String(Text)` (NULL -> empty), then `SetPlurality(Plurality)`
  and `SetGender(Gender)` when not NULL.
- `Lookup(key[, len])`, `LookupLanguage`: the String, or `String(key, len)` when not found; NULL or empty key ->
  empty String. `TryLookup*`: true when found. `HasKey(key, len[, idx])`: runs the lookup query. Language accessors
  return the `Languages` rows. `NotifyDatabaseUpdated`: preload again. A critical section guards each call.

String, SharedString, forms
- `SharedString(s, n)`: `malloc(n + 1)`, zeroed, copied (buffer length n + 1); copies share the buffer.
- `String(s[, n])`: empty/NULL -> base "" and 0 forms; else forms split at every `|` (`a||b|` = 4 forms), each form
  plurality 1, gender 1 (masculine). `operator=(const char*)` keeps the arguments and the cached composition.
- `IsEmpty`: base length 0. `GetNumForms`, `GetBaseString`, `Get/SetLanguage`.
- `toUTF8()`: composes form 1 into a NUL-terminated copy once (an empty result is retried). `toUTF8(bytes, n)`:
  composes form n every call (later `PushArgument`s count); "" with bytes 0 when n is out of range or a parameter
  fails. A form without parameters returns the form inside the base buffer (not NUL-terminated before a `|`).
- `PushArgument`: int, int64, float, double, String; `const char*` (+len) as a `String` (NULL -> "").
- `SetPlurality(text)`: needs a plural rule (none: false); one value (<= 7 chars, `atoi`): 0 -> error "Plurality
  Starts at 1." false; <= max forms -> every form; else `|`-separated values, one per form (count must match),
  each `1..max`. `SetPlurality(n)` / `(form, n)`: `n < max` (strict, as closed). `GetPlurality(form)`: 0 out of
  range.
- `SetGender(text)`: `|`-separated per form or one spec for all. Spec `<gender>[:traits]`: gender compared
  case-insensitively over the length before `:` (any prefix of masculine/feminine/neuter, also the empty one),
  `*` = any; an unknown word gives 0 (neuter); traits: for every `,` the text from the traits' start up to it, then
  the whole traits text (closed quirk), none if they start with `*`. `SetGender(form, Gender)` always returns false.
- `GetFormByPlurality(p, nth)`; `GetFormByGender` / `GetFormByPluralityAndGender`: first form matching gender and
  all traits; the offset argument is ignored (closed).

Composition (parser and evaluation)
- Parameters `{[@]n[_Name] ...}` or `{KEY ...}`, optional `[form]` or `^gender` / `*plural` specifiers (a nested
  parameter, a `'literal'` or a number), `<< {param}` injections (literals/numbers rejected with "Injected ... are
  not supported in this release."), `: transform style-args` to the matching `}`. Every parse error message of
  the closed library is produced with the same text (512-byte buffers, the whole form as `%.*s`).
- A parse error makes the form fail. Evaluation of each parameter: injected values replace the first arguments;
  index -> the argument ("Missing argument %i. (%.*s)" when absent); a string argument whose text is a dictionary key
  is replaced by that key's String; `@` requires a string; `{KEY}` looks the key up. A string argument's form is
  chosen by `[n]`, by plurality (`*`: the rule's form of the number, or a nested string's plurality) and/or gender
  and traits (`^`), with the closed messages ("Could not deduce form based on ..."). Its text is composed with the
  current arguments. A failure logs "Failed to evaluate parameter %i. %.*s" and the composition gives "".
- Transforms: `number [pattern]` (default `#,##0.###`), `currency`, `percent`, `spellout`, `ordinal`, `roman`,
  `upper`, `lower`, `title` (always "": the closed library's ICU data has no break rules), `textkey` (look the
  text up again, keep it if not a key), `gender` and `plural` choices (`key?value; ...; other?value;`, value
  composed with the arguments; plural keys are prefixes of the plural form number of the current language's rule;
  gender keys match when form 1 of the string is the first form matching the spec). Unknown transform: "Cannot
  find style. (%.*s) (%s)". The type errors ("FormatNumber: Unsupported, must be numeric argument", ...) as closed.
- The first use of a formatter per language logs the closed library's ICU status: number and roman "A resource
  bundle lookup returned a result from the root locale (not an error)." (warning), spellout and ordinal "ICU: A
  resource bundle lookup returned a fallback result (not an error)", currency and percent nothing.

Number formatting (ICU 4.4 DecimalFormat semantics, `[L]` on the corpus)
- A double is reduced to 15 significant digits (`%+1.14e`), a float widened to double first; rounding to the
  maximum fraction digits is half-even on those digits (`#.##`: 2.675 -> 2.68, float 2.675 -> 2.67, 0.005 -> 0).
- Patterns: quoted and plain prefix/suffix literals, `#`, `0`, `,` grouping, `.`, `;` negative subpattern (else
  `-` + positive prefix), `%`; `#.##` is read as `#0.##`; an empty integer part prints `0` unless a fraction is
  printed. The pattern is taken between the first `"` and `substr(first + 1, last - 1)` (closed off-by-count: a
  pattern quoted with a leading character keeps the trailing quote as suffix text).
- Pattern leak (closed, reproduced): a styled `number` with a string argument fails after applying its pattern and
  never restores the default; later unstyled formatting (plain `{1}`, `{1:number}`) of that language uses the
  leaked pattern until the next successful styled format.
- Symbols per language: en/ja/ko/zh `.` `,`; de/es/it `,` `.`; fr/pl/ru `,` U+00A0. Spellout: English
  numbering words, "minus", "point" + one word per fraction digit of the 15-digit form, >= 10^18 as digits.
  Ordinal: digits + superscript st/nd/rd/th. Roman: upper case, 0 = N, >= 5000 digits.
- Case maps: `LCMapStringEx` with linguistic casing for the language (invariant for unknown locales).

Plural rules (`PluralRule` 1..16, disassembly 0x10006c00..0x10006ff0 `[C]`, `[L]` probe -200..1200)
1 one form; 2 n=1; 3 n=0,1; 4 n=0 / n%10=1 && n!=11 / other; 5 1 / 2 / other; 6 1 / 0 or n%100<=19 / other;
7 n%10=1 / n%10=0 / other; 8 n%10=1 (not 11) / n%10 in 2..4 (not 12..14) / other; 9 1 / 2..4 / other; 10 1 / n%10
in 2..4 (not 12..14) / other; 11 n%10 = 1 / 2 / 3,4 / other; 12 1 / 2 / 3..6 / 7..10 / other; 13 1 / 2 / n%100
3..10 / 11..99 / n!=0 / 0; 14 1 / 0 or n%100<=10 / 11..19 / other; 15 n%10 = 1 / 2 / other; 16 n%10=1 (not 11) /
other. C remainders (negative n keep the sign). 0 or > 16: no rule (`GetPluralForm` 0, SetPlurality false).

## Differences from the closed library (each an `EXPLAINED` entry of `ab/run_ab.py`)

| # | Where | Closed library | Ours |
|---|---|---|---|
| 1 | "Failed to evaluate parameter", "Missing argument", "Failed to transform argument" messages | `vsprintf_s` into 512/1024 bytes: a longer text calls the invalid-parameter handler (the game process ends with 0xC0000417; the harness logs "") | truncated message |
| 2 | `{KEY << {n_X}}` with argument n missing (22 en_US keys and their translations) | reads the argument without a bounds check: access violation | "Missing argument" + "Failed to evaluated injected parameter.", the composition gives "" |
| 3 | self-referencing / cyclic key | stack overflow | stops at 64 nested compositions: error "Composition nested too deeply (cyclic text key?). %.*s", the innermost part "" |
| 4 | `Lookup` before `Initialize` | access violation | the key |
| 5 | `Initialize` after `Shutdown` (`reinit` corpus, informational) | undefined behaviour (crash in 5 of 6 runs) | works |
| 6 | `Language::SetPluralityRule` | releases the old rule and deletes the new one at once (dangling pointer) | takes ownership of the new rule |
| 7 | `Language::Compare` | ICU collator | `CompareStringEx` for the language (not exercised) |
| 8 | `ConvertUTC*` helpers | ICU date formats | `GetDateFormat`/`GetTimeFormat` (not exercised, not imported by the DLL) |
| 9 | `ParameterTransforms` members, `SharedString::operator==` | exported | not exported (see Result) |

## Files

| Path | What |
|---|---|
| `build_loc.py` | builds the DLL + import library for x64/x86 |
| `src/LocInternal.h` | shared declarations (implementation classes, helpers) |
| `src/LocCore.cpp` | globals, logger, Initialize/Shutdown, languages, plural rules, date helpers |
| `src/LocDictionary.cpp` | the StringDictionary |
| `src/LocString.cpp` | SharedString, SingleFormString, ParameterArgument, String |
| `src/LocFormat.cpp` | parser, composer, choice transforms, gender specs |
| `src/LocNumber.cpp` | number/percent/currency patterns, spellout, ordinal, roman, case maps |
| `ab/` | the A/B (issue 1702; `EXPLAINED` above) |
