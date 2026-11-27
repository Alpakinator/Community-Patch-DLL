# Building the 64-bit patch

One command builds the patch's release set from this folder and the DLL source beside it:

| File | What |
|---|---|
| `bin/CvGameCore_Expansion2.dll` | the game DLL, built for x64 |
| `bin/lua51_x64.dll`, `bin/CvGameDatabase_x64.dll`, `bin/CvLocalization_x64.dll` | the x64 builds of the three libraries the DLL links (Lua 5.1.4, the database library over SQLite 3.7.17, the localization library) |
| `bin/server.exe` | the 64-bit server that runs the DLL and draws the world |
| `bin/civ5save.exe` | the save container tool (not shipped to players) |
| `proxy/bin/CvGameCore_Expansion2.dll` | the 32-bit proxy DLL the game loads in place of the DLL |
| `proxy/bin/vp64helper.exe` | the 32-bit helper (config switch, failure popup) |
| `proxy/bin/shell_composite.dll` | the compositor that puts the server's picture into the game's frame, with its `.sha256`, `.ini` and MinHook's licence |

Each comes with its PDB; `SHA256.txt` lists the hash of every `.dll` and `.exe`.

## 1. Prerequisites

Windows 10 or 11, x64. The build scripts open the toolchain at the folders below, so each must be installed **at
its default folder**:

| Tool | Version | Where the build looks | Notes |
|---|---|---|---|
| Visual Studio Community 2022 | 17.13 (tested 17.13.7) | `C:\Program Files\Microsoft Visual Studio\2022\Community` | workload "Desktop development with C++" with MSVC v143 14.4x (tested 14.43.34808) and "C++ CMake tools for Windows" (CMake 3.30, Ninja 1.12); the Build Tools edition installs to another folder and is not found |
| Windows SDK | 10.0.26100.0 | `C:\Program Files (x86)\Windows Kits\10` | the Visual Studio component "Windows 11 SDK (10.0.26100.0)" |
| LLVM | 19.1.7 exactly | `C:\Program Files\LLVM` | `LLVM-19.1.7-win64.exe` from the llvm-project release page `llvmorg-19.1.7` on GitHub |
| Visual C++ 2008 with SP1 (VC9) and Windows SDK v6.0A | VC9 SP1 | `C:\Program Files (x86)\Microsoft Visual Studio 9.0` (`Common7\Tools\vsvars32.bat`), `C:\Program Files\Microsoft SDKs\Windows\v6.0A\bin\mt.exe` | the 32-bit proxy and helper link the game's own C runtime (msvcr90); Visual C++ 2008 Express Edition with SP1 installs both folders. Microsoft no longer offers it as a public download: use a Microsoft source you have (Visual Studio subscription downloads, or your own installation medium) |
| Python | 3.11 or later (tested 3.11.5) | `python` on `PATH` | standard library only |
| Memory and disk | 32 GB RAM recommended (the DLL's whole-program link alone peaks at 13.4 GB), 4 GB free disk | | the output folder holds about 1.5 GB |

The third-party sources, checked by SHA-256 before use (the list and the hashes are in `deps/third-party.lock`):

| File | From | SHA-256 |
|---|---|---|
| `lua-5.1.4.tar.gz` | `https://www.lua.org/ftp/lua-5.1.4.tar.gz` | `b038e225eaf2a5b57c9bcc35cd13aa8c6c8288ef493d52970c9545074098af3a` |
| `sqlite-amalgamation-3071700.zip` | `https://www.sqlite.org/2013/sqlite-amalgamation-3071700.zip` | `022ef41bd83a1333faf40dc8f1f8469205f4a18c30dc5e137889ba7ea924ef30` |
| `zlib-1.2.3.tar.gz` | `https://zlib.net/fossils/zlib-1.2.3.tar.gz` | `1795c7d067a43174113fdf03447532f373e1c6c57c08d61d9e4e9be5e244b05e` |

Put them into one folder and name it with `--third-party`, or let the build download them (no `--offline`).

From your own copy of the game (they are only read; nothing of the game is in this source):

- `CivilizationV_DX11.exe` of every game build the patch knows (`tools/exebuild/builds.py`): Steam build 4390913 (the
  2014 EXE, the build the address catalogue is written against) and Steam build 26577625 (the update of
  2026-11-19). The build generates the EXE address tables from them into the output folder.
- The answers for the symbols the matcher cannot carry from build 4390913 to 26577625 by itself (a
  `hand-review.tsv`, `--hand-review 26577625=<file>`): without them that step refuses. See section 4.
- `Civ5DebugDatabase.db` with Vox Populi active (the game writes it into
  `Documents\My Games\Sid Meier's Civilization 5\cache\` when it loads the mods): the DLL's smoke run reads it.

## 2. The folder layout

This folder is `64bit/` at the top of the DLL repository (Community-Patch-DLL), so the DLL source is `..`:

```
Community-Patch-DLL\
  CvGameCoreDLL_Expansion2\   CvGameCoreDLLUtil\   CvGameDatabase\   CvLocalization\   ...   build_vp_clang.py
  64bit\                      <- this folder
```

The 32-bit proxy needs no second, 32-bit DLL source: it compiles against the same DLL source's headers with the
VC9 toolchain. `deps/dll.lock` names the DLL commit this folder was tested with; a DLL source at another commit is
built with `--unlocked`.

## 3. The command

From `64bit\`, with an output folder outside the repository:

```
python -I tools/export/cleanbuild.py --out C:\vp64-build --exe "<path>\CivilizationV_DX11.exe" --exe "<path of the other build>\CivilizationV_DX11.exe" --db "<path>\Civ5DebugDatabase.db" --hand-review 26577625=<path>\hand-review.tsv --third-party <folder of the three archives> --offline
```

A DLL source at another commit than `deps/dll.lock`'s adds `--unlocked`; a copy without git adds `--repo-commit <sha>`
so the build id names it.

It checks the prerequisites (and names every missing one), generates the EXE address tables, builds the DLL and its
libraries, the server, the proxy and the helper (`server/build_server.py --config release`), then the compositor
(`shell/build.cmd`), and lays the release set out in `<out>\release\`. The last line is
`cleanbuild: OK <out>\release` or `cleanbuild: FAILED: <reason>`; each step's log is `<out>\<step>.log`. Exit codes:
0 built, 1 failed, 2 usage. `--check-only` stops after the prerequisites and the tables; `--no-composite` leaves the
compositor out. `python -I tools/export/cleanbuild.py --help` lists every option.

The build took 23 minutes on the project's test workstation; most of it is the DLL's whole-program link (8 minutes,
one thread). Everything it writes is under `--out`, except Python's `__pycache__` folders beside the build scripts.

The binaries are reproducible but for two inputs: `__DATE__`/`__TIME__` come from `SOURCE_DATE_EPOCH` (default: the
commit time of the checkout's git HEAD, or 0 without git), and the build id names the repository's commit
(`--repo-commit` for a copy without git). With the same `SOURCE_DATE_EPOCH` in the environment, the DLL's code is
byte-identical to the release build's.

## 4. Known limits

- The toolchain folders are fixed (section 1). Installs elsewhere, the Build Tools edition of Visual Studio and
  other LLVM versions are not found.
- Visual C++ 2008 is needed for the 32-bit pieces and is not publicly downloadable from Microsoft any more.
- The address table of build 26577625 needs hand answers for the symbols the matcher cannot carry; they are not part
  of this source. Builders without them cannot build for that game build yet.
- Both game builds' EXEs are needed, while a player's install holds only one.
