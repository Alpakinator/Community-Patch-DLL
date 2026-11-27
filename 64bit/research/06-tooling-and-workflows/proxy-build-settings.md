# Proxy build settings: one VC9 STL checking setting per module (ticket 0609)

Written by impl-0609, 2026-11-20. Code: `proxy/build_proxy.py` (`STL_SETTING`, `stl_setting_args`, `check_stl_objects`),
test `proxy/tests/test_build_settings.py`, evidence `proxy/evidence/0609/`. Root cause of the crashes that led here:
epic 0042's `research/05-graphics-composition/combat-choreography.md` 3.5.8 ("The 4725 defect class") and
`effects-formats.md` 8.1.1; the earlier per-file rule: `research/03-dll-engine-boundary/proxy-design.md` ("Build rule
for every proxy source and header: the fork's STL layout", and the purity loader's "layout trap", 16.9).

## 1. The rule

Every x86 translation unit that `proxy/build_proxy.py` compiles is built with ONE setting, from the build script:
`/D_SECURE_SCL=0 /D_HAS_ITERATOR_DEBUGGING=0` (`STL_SETTING`). It covers both 32-bit modules this repository builds
against the VC9 runtime and ships:

| Module | TUs | CRT | Before 0609 |
|---|---|---|---|
| the proxy `CvGameCore_Expansion2.dll` (x86) | vpipc (`ipc/src/*.cpp`, through `vpipc_x86.lib`), the generated code (`<gen>/common/vp_ids.gen.cpp`, `<gen>/proxy/*.cpp`), `proxy/src/*.cpp`, `options/src/*.cpp`, `shell/composite/farm_guard.cpp` | msvcr90/msvcp90 (`/MD`) | per file: a `#ifndef _SECURE_SCL / #define _SECURE_SCL 0` in the proxy headers (`proxy_rt_logic.h`, `proxy_session.h`, `proxy_report.h`, `proxy_pack.h`, `proxy_purity.h`) and in `proxy_purity.cpp`; a TU that included an STL header before one of those got the VC9 default 1 |
| `vp64helper.exe` | `proxy/helper/vp64helper.cpp`, `proxy_cfgini.cpp` | VC9 libcmt (`/MT`, static) | the VC9 default (1) in both TUs |

Not covered, by design: `shell_composite.dll` and its x86 test hosts are built by `shell/build.cmd` with the VS 2022
toolset, whose own headers already emit `#pragma detect_mismatch("_ITERATOR_DEBUG_LEVEL", ...)`; the x64 server and
DLL use the VS 2022 STL. Test programs that compile proxy sources on their own (`proxy/tools/test_proxy_rt.py`,
`test_farm_guard_proxy.py`, `hutest`, `tools/drift`) are separate modules, not the product; they are not changed here
`[C, read 2026-11-20]`.

The per-file `#ifndef` lines in the proxy headers stay: with the macro set on the command line they do nothing, and
they keep the headers right for those test programs.

## 2. Why 0, from evidence

- The DLL's own build turns both off (`CvGameCoreDLLPCH.h`), the proxy marshals the DLL's types and reads its
  containers in that layout (`proxy-design.md`: "two arrays of 64 vectors of 16 bytes (VC9 layout with `_SECURE_SCL 0`)"),
  and 30 of the proxy's 34 container TUs already compiled at 0 (`scl_census.py` of 4725 on proxy 4e61fe3f).
- Sizes of the VC9 STL types under each setting, `clang-cl -m32 /MD /O2` in the VS 2008 environment, 2026-11-20
  `[C, F:/c5-64bit-work/0609/probe]`:

  | setting | string | vector&lt;int&gt; | vector&lt;string&gt; | vector iterator | map, set | list | deque |
  |---|---|---|---|---|---|---|---|
  | `_SECURE_SCL=0`, HID=0 | 28 | 16 | 16 | 4 | 12 | 12 | 32 |
  | default (`_SECURE_SCL=1`), HID=0 | 28 | 24 | 24 | 8 | 32 | 28 | 32 |

  `std::string` has one size in both, and `msvcp90.dll` serves both: VC9's `xstring` imports `basic_string<char>` from
  msvcp90 under `/MD` and, for `_SECURE_SCL 0`, the unchecked overloads it exports for that case
  (`_DEFINE_DLL_OVERLOADS || !_SECURE_SCL`, `xstring` around line 1597). msvcp90 therefore fixes no value; the DLL does.
- `_HAS_ITERATOR_DEBUGGING` is forced to 0 by VC9's `yvals.h` whenever `_DEBUG` is not defined (a release `/MD`
  build), so only `_SECURE_SCL` ever differed in practice; both are pinned so that a debug define cannot split them.

## 3. The cause it removes

`std::vector<T>::_Insert_n` (and other members) has one mangled name under both settings, while its argument list and
stack effect differ (the checked form passes an iterator of two words). With two settings in one module the linker
keeps one COMDAT copy and a caller of the other form returns with ESP off by one word. Seen three times: the
`IsRunFolderName` crash (2026-11-10), the purity loader (3503, 2026-11-13) and the four crashes of the 4798 spike on
c5h-g (2026-11-20). Reproduced offline by `test_build_settings.py` case (b) (2026-11-20 `[C]`): two TUs that
`push_back` into a `std::vector<std::string>`, one at 0 and one without the setting, linked by lld-link without an error;
the program dies with an access violation (`mixed_b.exe`, exit 139 under Git Bash), and the same program with one
setting (`same.exe`) exits 0.

## 4. The check

Three layers, all in `proxy/build_proxy.py`, so that a new file or a new compile list cannot escape them:

1. **A forced include on every TU** (`/FI <out>/obj/stlsetting/vp_stl_setting.h`, written by the build): one
   `#pragma detect_mismatch("vp_stl_SECURE_SCL", <value>)` and one for `_HAS_ITERATOR_DEBUGGING`. clang-cl puts
   `/FAILIFMISMATCH:"vp_stl_SECURE_SCL=0"` into each object's `.drectve`; lld-link refuses two objects of one link with
   different values (`lld-link: error: /failifmismatch: mismatch detected for 'vp_stl_SECURE_SCL'`).
2. **`-Werror=macro-redefined`** (after `-Wno-everything`, which it re-enables for that one warning): a file that
   `#define`s either macro to another value fails to compile. An identical redefinition (`#define _SECURE_SCL 0`) is
   allowed by the language and harmless. The one way past both is `#undef`; `test_build_settings.py` fails on any
   `#undef` (or a non-0 `#define`) of either macro in the module's sources (76 files on 2026-11-20).
3. **`check_stl_objects`** before each link (the proxy: every object of vpipc and of step 4; the helper: its two
   objects): each object must carry both markers exactly once with `STL_SETTING`'s values. This catches what lld-link
   cannot: an object compiled WITHOUT the forced include (no marker at all, the 4798 spike's own case). The result is
   in `build-proxy.json` as `stl` (proxy) and `helper.stl`, and in `build-proxy.log` as a `check stl` line.

Shown failing on injected units (`proxy/tests/test_build_settings.py`, 2026-11-20, output in
`proxy/evidence/0609/acceptance_0609.txt`): (a) `_SECURE_SCL=1` with the marker: lld-link and the object check both fail;
(b) no setting: the object check fails (lld-link links it, and the program crashes); (c) `#define _SECURE_SCL 1` in the
file: the compile fails. Two units at the one setting link and pass.

## 5. What changed in the binaries [C, 2026-11-20]

Main's `build_proxy.py` (origin/main c76f8656b) against the 0609 one, the same source (fork 1fd51353e), the same
generated folder, compared object by object with `.drectve` and the debug sections left out
(`proxy/evidence/0609/compare_objs.py`), in debug and in release: **3 of 50 objects differ, in both configurations.**

| Object | Module | What changed |
|---|---|---|
| `proxy_luaexec.obj` | proxy | its `std::string` calls now take msvcp90's unchecked overloads (`basic_string::_Size_type_nosscl`); debug: the import of the checked `basic_string::operator[](unsigned)` from msvcp90 is gone (388 -> 387 imported symbols), release: the imports are equal (362) |
| `vp64helper.obj`, `proxy_cfgini.obj` | helper | unchecked `std::string`/`std::vector` code: release 44 -> 39 and 59 -> 55 std:: functions defined, `.text` 250,249 -> 244,041 bytes |

The other three of the four TUs that built at `_SECURE_SCL=1` before (`proxy_budget.cpp`, `proxy_farm_guard.cpp`,
`proxy_shell.cpp`, `<string>` only) produce the same code and data: `std::string` has one layout and its members come
from msvcp90 either way. `proxy_luaexec.cpp` was the only one of the four with the other containers in reach (the
DLL's `CvGameCoreDLLPCH.h` through `proxy_rt.h`, whose `#if !defined(_SECURE_SCL)` guard kept the 1 that
`proxy_luaexec.h`'s `<string>` had already set), and it defines no vector, map or list function (llvm-nm, release). Everything else in the proxy
(43 other objects, vpipc included) is unchanged. Proxy DLL `.text`: release 898,334 -> 898,318 bytes, debug 1,555,546 ->
1,555,732. No behaviour change is intended or seen.

**Checked iterators that were relied on** [C, read 2026-11-20]: none. In the four TUs every `std::string` index is
guarded by a size test first (`proxy_luaexec.cpp` 760, 766-767, 776; `proxy_shell.cpp` 162 indexes a `const char*`), and
`substr`/`at` still throw `out_of_range` at 0 (VC9 checks those always). The helper's indexing is guarded the same way;
with 0 a bad index there is undefined behaviour instead of `_invalid_parameter_noinfo`'s fast fail, as in every other
proxy TU since 2026-11-10.

## 6. Measurements and runs (2026-11-20, workstation)

- Builds: `server/build_server.py --config debug --deps <lead-0590's debug deps of 1fd51353e> --src <a plain copy of
  1fd51353e>` into `F:/c5-64bit-work/0609/b-after` (11:25-12:33): server OK, proxy steps 0-6 OK (`check stl ok: 48
  objects` and `2 objects`), proxy b7605113...755a, helper 570a8b9e...652f. Its step 7 (the full purity test) was
  killed by `build_proxy.py`'s 1,800 s timeout: its scratch was on F:, which was saturated by other teams' tar
  extractions that hour (a 364 MB source tar extracted at about 1 MB/s). Run by hand with the scratch on C: it passed
  66/66 in 147 s. Release proxies for the comparison: `build_proxy.py --config release --no-purity-test` 196-366 s.
- The object check on main's own debug proxy (p-before): fails on all 48 objects (no marker), as intended.
- Suites on the rebuilt proxy: `test_build_settings.py` 4/4 (pytest), `test_proxy_session.py --asan` 154/154 (714 s),
  `test_farm_guard_proxy.py` 11/11, `test_purity.py` 66/66, `test_vp64helper.py` 193/193, `test_proxy_rt.py`
  199/203 with main's own proxy giving the same 199/203: the 4 failures (d28's server exit code, three strict cases)
  are the server's `FAILED: database` at boot with the debug x64 DLL's extra `Results::Bind(int, float)` import, not
  the proxy (fixed in the server by ticket 0752, 2026-11-25: `upstream-fix-batch-2.md`, "Fixed by ticket 0752").
  Combat hold tests: none on main.
- Live smoke `[L]`, c5h-a (Goldberg, EXE 4390913, mods548), proxy b7605113 with lead-0590's debug x64 DLL and the
  0609 server, `proxy/evidence/0609/live_0609.py`: a new duel game, 5 turns by `checklist.py` (status, ui_states,
  ai_turns, save_game on the main thread, status_end all pass), save `c5h-c5h-a-t0005-*.Civ5Save` (192,363 bytes),
  stop, a launch that loaded it (turn 5), one more turn (5 -> 6), stop, `set-proxy --original`, front end restored by
  hash, `provision.py verify` exit 0. No box, `CvAssert.log` empty in both runs, `vpproxy.log` without a LOUD or
  FALLBACK line. The server logged one known LOUD line per boot, `database: LOUD: the DLL imports
  ?Bind@Results@Database@@QEAA_NHM@Z, which the write recording does not cover` (and its `FAILED: database` summary):
  the non-LTO x64 DLL's extra import, `research/03-dll-engine-boundary/upstream-fix-batch-2.md` (fast configuration);
  it comes from the debug x64 DLL used here, not from the proxy.
