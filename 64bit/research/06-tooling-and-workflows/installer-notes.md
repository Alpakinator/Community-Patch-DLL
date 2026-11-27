# Installer: what Vox Populi's installer does, and the shipping list and install process of ours

Status: sections 1-4 rewritten on 2026-11-16 by lead-2600 (epic 2600, issue 2601) as the **final shipping list and
install process, approved by the owner on 2026-11-16 (D171, MC #3304 [reworded for privacy]: approved as proposed, with the five choices at their proposals: 43-civ by the installed DLL, removal from the same .exe, an update keeps
mode=off, two VP copies refused, no cache clearing; tracker 0082 done)**. A later change a player would notice goes back
to the owner through the MC. (Was: "for the owner's confirmation", until 2026-11-16 23:55.) The earlier draft (written 2026-11-10 13:13 EST by the MC) was first confirmed by the owner on
2026-11-10 13:16 EST ([reworded for privacy] approved the draft overall, decision D28, section 5); what changed against it is marked "changed from
the draft". Decisions: D26 (nothing goes into the owner's real install until the project is complete), D27 (the patch ships
with an installer in the style of Vox Populi's), D28 (section 5), D166 (the owner, 2026-11-16 22:00: one self-contained
`.exe` per GitHub release, the 43-civ variant inside if possible, the installed VP's version checked with a link to
the right VP on a mismatch), D167 (epic 2600 runs now on account B).

## 1. Vox Populi's installer [C: `git show Release-5.4.8:VPSetupData.iss`, 230 lines, read 2026-11-16]

VP's installer is one Inno Setup script, `VPSetupData.iss`, at the repository root (there since `Release-4.9`,
`player-limits.md` 1.3). At `Release-5.4.8` (tag object `7d4dd837`, commit `7307aefdf`):

- **Setup.** `MyAppVersion "5.4.8"`; `Compression=lzma`, `SolidCompression=yes`; `PrivilegesRequired=admin` (it writes
  into the game folder); `Uninstallable=no` (no entry in Windows' installed programs, no uninstaller file);
  `LicenseFile=License.rtf`, `InfoBeforeFile=Opener.rtf`; `DefaultDirName={userdocs}\My Games\Sid Meier's Civilization
  5\`. So **the `My Games` folder is Inno's standard "Select Destination Location" page**, pre-filled with the Documents
  known folder (which follows a Documents folder moved to OneDrive [I: Inno resolves `{userdocs}` through the shell]) and
  correctable by the player.
- **The game folder** is a page of its own (`CreateInputDirPage` after the component page, "Select the Civilization V
  folder", with the hint "right-click on Civilization V in Steam and select Show local files"). Pre-fill
  (`NextButtonClick`): only when the field is empty **and** `{commonpf}\Steam\steamapps\common\Sid Meier's Civilization
  V\Assets\DLC` exists, the field becomes `HKCU\SOFTWARE\Firaxis\Civilization5` `LastKnownPath`, or that default Steam
  path when the value is missing. A game in another Steam library therefore gets an empty field and the player browses
  [C]. (`{commonpf}` is `Program Files (x86)` for a 32-bit installer on 64-bit Windows [I: Inno's documentation].)
  The check: the folder must hold `Assets\DLC`, and the ten DLC folders `DLC_01`-`DLC_07`, `DLC_Deluxe`, `Expansion`
  and `Expansion2` must exist; otherwise a message lists the missing DLC by name and the page does not advance.
- **No version check of anything installed, no check that the game is running, no runtime check** [C: the whole
  `[Code]` section is the four functions `CurPageChanged`, `InitializeWizard`, `NextButtonClick`,
  `UpdateReadyMemo`/`GetCIVDir`]. Instead `[InstallDelete]` (31 lines) deletes, for every component, the old mod
  folders under `MODS` (among them the whole `(1) Community Patch`, `(2) Vox Populi`, `(3a)`, `(3b)`, `(4a)`, `(5)` and
  18 older names), the game folder's `Assets\DLC\UI_bc1`, `Assets\DLC\VPUI`, `Expansion2.Civ5Pkg` and
  `MinorCivSounds_VoxPopuli.xml`, and `My Games\...\cache`, then copies.
- **Components**, mutually exclusive: Vox Populi with EUI, without EUI, Community Patch only, the three 43-civ twins of
  these, and "Uninstall all". A 43-civ component installs what its twin installs, plus the `(3b) 43 Civs Community
  Patch` folder's `.modinfo` and `AdvancedSetup.lua`, and writes the 43-civ DLL over `MODS\(1) Community
  Patch\CvGameCore_Expansion2.dll`. "Uninstall all" is the same `[InstallDelete]` plus a copy of the stock
  `Expansion2_Base.Civ5Pkg`: removal is a choice inside the same `.exe`, not a Windows uninstaller.
- **The modpack form** is not made by VP's installer: a player makes it with the `(5) Modpack Maker for VP` mod, and it
  lives in a DLC folder of the game whose name is not fixed (`VP_MODPACK`, `VP_MODPACK_5_4_8_NEUTRALS`,
  `MP_AUTOPLAY_VP_5_4_7` are the ones seen here) with the DLL at `Assets\DLC\<name>\Mods\(1) Community
  Patch\CvGameCore_Expansion2.dll` [C: `build-and-install-dll.md`, `tools/instances/provision.py` PROFILES; I: that the
  Modpack Maker is how players make it].

**How a VP release can be recognised [C: `tools/installer/vp_releases.py` and a survey of the tags `Release-5.3.1` to
`Release-5.4.8` in the local clone, 2026-11-16].** The `.modinfo` versions do not identify a release: every tag from
`Release-5.4.0` to `Release-5.4.8` has Community Patch `d1b6328c...` v151, Vox Populi `8411a7a8...` v17, EUI
compatibility `24923240...` v1. The committed 32-bit DLL does: every one of those tags has a DLL of its own SHA-256
(for example `ff190355...` 5.4.7, `262aee88...` 5.4.8; the 43-civ DLLs `1d84960e...` 5.4.7, `01451bd7...` 5.4.8). Over
all tags the table has 363 distinct DLLs; four later tags reuse an earlier tag's bytes (`Release-1.3.1`/`1.3.2` 43-civ,
`3.10.6`, `4.2.5a`) and are listed under the first. Not checked: whether VP's published installer `.exe` carries the
tag's DLL byte for byte (no installer is extracted, MC 2026-11-13 #206); the owner's install, made with VP 5.4.8's
installer, holds `262aee88...` [C, W3.2], which agrees.

**What ours takes from VP's, and what it does that VP's does not.** Taken: the `My Games` page as Inno's destination
page with the same default; the game folder page with the same pre-fill, the same `Assets\DLC` check and the same DLC
check; one `.exe` with mutually exclusive choices, removal among them, and no Windows uninstaller entry
(`Uninstallable=no`); lzma solid compression. **New behaviour, not a copy of VP's** (VP's installer checks none of it):
the installed VP is identified by its DLL's SHA-256 and a mismatch is refused with the link to the right VP release
(4.3); the installer refuses while the game runs; it checks for the Visual C++ runtime; it backs up what it replaces
and keeps a record, so that removal restores by hash; it deletes nothing of VP's and does not clear the `cache` folder.
It also looks in the other Steam libraries when VP's pre-fill finds nothing (4.1), and needs no administrator rights.

## 2. What a release ships today [C: `tools/release/README.md` 5, `stages/build.py`, `docs/OPTIONS.md`, `installer-notes` 11.1; 2026-11-16]

A release is taken by `tools/release/take_release.py <tag>` into a work folder. The installer is built from that
folder's outputs, never from loose builds:

| File | Bits | From the work folder | What it is |
|---|---|---|---|
| `CvGameCore_Expansion2.dll` (proxy) | 32 | `build` stage: `<x64 layout>/proxy/bin/` | takes VP's DLL's place in the EXE; starts the server; the D28 fallback |
| `vp64helper.exe` | 32 | the same `proxy/bin/` | the off switch, the D33 configuration (`on`/`off`), the D118 popup (section 9) |
| `vpproxy.ini` | text | written by the installer | `mode=on`, `fallback_dll=CvGameCore_Expansion2_x86.dll`, `x64_dir=VP64`, `pack=1` (the shipped values, 11.1) |
| `VP64\` = the pack | 64 | `pack` stage: `<work>/pack/vp64pack/` | `manifest.json`, the x64 DLL, `purity.tsv`, `server.exe`, `lua51_x64.dll`, `CvGameDatabase_x64.dll`, `CvLocalization_x64.dll` (section 7) |
| `shell_composite.dll`, `shell_composite.sha256` | 32 | the proxy's build | **increment 2 configuration only** (`ServerRenderer = 1`, `docs/OPTIONS.md`); the compositor, with MinHook linked in |

The 43-civ variant is a second take, `take_release.py <tag> --major-civs 43` (work folder `<tag>-m43`): its pack holds
the 43-civ x64 DLL, its purity file and a manifest whose `x86_dlls` is the tag's 43-civ DLL (`01451bd7...` for 5.4.8);
the proxy, the helper and the server are the same programs (`player-limits.md` 4.2, 8.1-8.2).

The release's **shell configuration** is a property of the release, not of the installer build (MC #3193, 2026-11-16):
`thin` = increment 1's thin client (`ServerRenderer = 0`, `config.ini` untouched, no `vp64helper on`, no compositor;
what `INSTALL-FIRST-PLAYABLE.md` ships), `i2` = increment 2's D119 A'' (`vp64helper on`, `ServerRenderer = 1`, the
compositor). Today's pack is the increment 1 release candidate: `thin`. Issue 2602 adds an optional descriptor field
`shell_config` (absent = `thin`), copied into `manifest.json` as an informational key (MC #3197).

Needed on the player's machine and not shipped: the Microsoft Visual C++ 2015-2022 x64 runtime (D15, D28).

## 3. The shipping list (final; approved by the owner, D171, 2026-11-16)

One file per GitHub release: **`VP64-Patch-<release>-for-VP<x.y.z>.exe`**, an Inno Setup installer, signed by nobody
(neither is VP's). It holds:

1. The 32-bit proxy DLL, which takes the place of VP's DLL in `(1) Community Patch`.
2. VP's own 32-bit DLL is **not** in the installer: the installer copies the player's own DLL to
   `CvGameCore_Expansion2_x86.dll` beside the proxy (the D28 fallback and off switch) after checking its hash.
3. `vp64helper.exe` and `vpproxy.ini` beside the proxy.
4. The pack `VP64\` beside the proxy (D28): the 64-bit server, the 64-bit game DLL **as `CvGameCore_Expansion2_x64.dll`**
   (MC #3403, 2026-11-17, issue 2606: under its old name `CvGameCore_Expansion2.dll` the EXE's DLC scan loaded it in the
   modpack form, 14.9 and 15; not a change a player notices, so no question for the owner), the three 64-bit libraries, the
   purity file and the manifest. **Two packs, the standard (22 majors) and the 43-civ one; the installer chooses the one
   that fits the installed VP's DLL by its hash** (changed from the draft: the 43-civ variant is new, D166). The player
   does not pick it; the ready page names it.
5. Increment 2 configuration only: `shell_composite.dll` and `shell_composite.sha256` beside the proxy.
6. Licence and notices: `VP64-LICENSE.txt` (GPL-3.0) and `VP64-NOTICE.txt` beside the proxy (where the source is: this
   repository and the DLL fork at the release's commits; the third-party notices of Lua 5.1.4, SQLite, zlib 1.2.3,
   MinHook when the compositor ships, and Inno Setup's licence for the installer itself); the notice is also shown on
   the installer's first page.
7. Written by the installer, not shipped: the install record and the backups (4.6), in
   `My Games\Sid Meier's Civilization 5\VP64\install\` (outside `(1) Community Patch`, because VP's installer deletes
   that folder, 11.3).

Not shipped: the game, its EXE (never modified, D4) and any file of the game (D83); Vox Populi itself (D28: add-on), except Vox Populi's own fixed Lua files for the release's VP version (ticket 0638, D225, 2026-11-21): for 5.4.8 up to 16 files, upstream's bytes at the fix commits of #13463, #13467 and #13539, each replaced only where it holds 5.4.8's exact installed SHA-256, backed up and restored by hash, behind one box ticked by default (`/VPFIXES=0`); `research/03-dll-engine-boundary/upstream-lua-fixes.md` 7-8;
the Visual C++ runtime (D28: detected and linked); debug symbols; `civ5save.exe`. `VP64Options.ini` is written by the
proxy at its first start, not by the installer (11.2).

## 4. The install process (final; approved by the owner, D171, 2026-11-16)

### 4.1 Pages, in order

1. **Welcome and notice.** The patch's name, the release and the VP version it is for; the notice (3, item 6).
2. **What to do**, mutually exclusive as in VP's installer: *Install* (or *Update*, when a previous version of the
   patch is found) and *Remove the patch*. *Remove* is offered only when the patch is found installed.
3. **`My Games` folder**: Inno's destination page, default `{userdocs}\My Games\Sid Meier's Civilization 5`, as VP's.
4. **Game folder**: VP's page with VP's pre-fill (section 1). When it finds nothing, the installer also looks in every
   Steam library (`HKCU\Software\Valve\Steam` `SteamPath`, then `steamapps\libraryfolders.vdf`) for
   `steamapps\common\Sid Meier's Civilization V`. The folder must hold `Assets\DLC\Expansion2` and
   `CivilizationV_DX11.exe` (the patch is for the DX11 EXE); VP's DLC check is not repeated, VP's installer did it.
5. **Checks**, all on one page and all before anything is copied (4.2-4.5). Any refusal says what to do and changes
   nothing.
6. **Ready**: the VP form and folder, the VP version found, the variant (standard or 43-civ), the configuration, the
   folders written.
7. **Install** (4.6), then **Finished**: how to tell the patch is running, the off switch, where the report goes (the
   text of `INSTALL-FIRST-PLAYABLE.md` 5, 6 and 9, shortened), and the folder of the install record and backups,
   `<My Games>\VP64\install\`, so the player can find it (MC #3201, 2026-11-16).

No administrator rights (`PrivilegesRequired=lowest`; changed from VP's `admin`). 64-bit Windows only. Nothing is
written to the registry.

### 4.2 Refused while the game runs (new; VP's installer does not check)

A running `CivilizationV.exe`, `CivilizationV_DX11.exe` or `CivilizationV_Tablet.exe`, or a `server.exe` started from a
`VP64` folder, stops the installer: "Civilization V is running. Close the game, then click Next again." (Also on
*Remove*.)

### 4.3 Which Vox Populi is installed, and what the player sees on a mismatch (new; VP's installer does not check)

1. **Find VP.** Mods form: `<My Games>\MODS\(1) Community Patch\CvGameCore_Expansion2.dll`. Modpack form: every
   `<game>\Assets\DLC\<any folder>\Mods\(1) Community Patch\CvGameCore_Expansion2.dll`. None found: "Vox Populi was not
   found in <My Games>\MODS or in a modpack under <game>\Assets\DLC. This patch is an add-on to Vox Populi <x.y.z>:
   install it from <link>, start the game once with it, then run this installer again." More than one found (the mods
   and a modpack, or two modpacks): refused, naming each, "Vox Populi is installed more than once. Keep one, then run
   this installer again." (VP itself supports one form at a time, `build-and-install-dll.md`.)
2. **Identify it** by the SHA-256 of that DLL; when the patch is already installed there, by the kept
   `CvGameCore_Expansion2_x86.dll` instead.
3. **Match.** The hash is one of the standard pack's `x86_dlls`: the standard variant. One of the 43-civ pack's: the
   43-civ variant. Nothing else installs.
4. **Mismatch.** The hash is looked up in the installer's version table (`tools/installer/vp_releases.tsv`, every VP
   release's 22- and 43-civ DLL, generated from the tags; 2601). The player sees, for example: "This patch is for Vox
   Populi 5.4.8. Your Vox Populi is 5.4.7. Install Vox Populi 5.4.8 from
   https://github.com/LoneGazebo/Community-Patch-DLL/releases/tag/Release-5.4.8 and run this installer again." When
   the hash is in no row: "Your Vox Populi DLL is not one of a Vox Populi release (a newer release, a modified DLL, or a
   DLL of another mod). This patch is for Vox Populi 5.4.8: <link>." Both refuse; the link can be opened from the
   message. (The owner [reworded for privacy] floated, for maybe later, a fourth column naming the patch release for each VP release, once there are several.)
   This replaces the proposal of section 6 that the installer may only warn: the proxy's run-time check (section 6)
   stays as the second line.

### 4.4 The Visual C++ runtime (D28: detected and linked)

`HKLM\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64` (64-bit view) `Installed = 1` and `Version` at least the
one the release was built with (recorded by the build). Missing or older: "The patch needs the Microsoft Visual C++
2015-2022 x64 runtime. Download it from Microsoft: https://aka.ms/vs/17/release/vc_redist.x64.exe", with *Continue*
(the player installs it later) or *Stop*, as the draft said. The installer never runs Microsoft's installer.

### 4.5 Other checks

- **Write access** to the folder of the VP DLL (the modpack form is inside the game folder): a test file is written and
  deleted. Refused with the folder named when it fails; the installer never asks for administrator rights.
- **Increment 2 configuration only:** `<My Games>\config.ini` with its `[DisableSystems]` section must exist (the EXE
  writes it at its first start; `vp64helper on` needs it, section 9). If the EXE is shown to keep a `config.ini` that
  holds only `[DisableSystems]` at its first start (test in 2604), the installer may write that file itself, backed up
  and removed by hash; otherwise this check refuses with "Start the game once with Vox Populi, then run this installer
  again" (MC #3193). Never after a half install.

### 4.6 Install, update and removal

**Install.**
1. Back up every file that is replaced or changed into `<My Games>\VP64\install\backup\`, named by SHA-256, and check
   each copy by its hash.
2. Copy VP's DLL to `CvGameCore_Expansion2_x86.dll` and check the copy against the manifest (section 7, item 2); then
   put the proxy in its place (written under a temporary name, then renamed).
2b. Vox Populi's own Lua fixes when the box is ticked (step kind `vpfix`, 13.2; ticket 0638): never counted by
   the stale check; restored only while the proxy holds the recorded place of VP's DLL and the file still holds
   exactly what was placed.
3. Copy `vp64helper.exe`, `vpproxy.ini`, the variant's `VP64\` pack, the licence files and (increment 2) the compositor.
4. Write the install record `<My Games>\VP64\install\record.txt` last: the patch release, the variant, the VP form and
   paths, every file added with its SHA-256, every file replaced with its backup's SHA-256 (format: section 13.2;
   corrected 2026-11-16 by issue 2603; was `record.json`).
5. Increment 2 configuration only: `vp64helper on` with `--config` and `--vp64-options` named explicitly. Not 0: the
   install is undone from the record.
6. Any failure undoes what was done so far from the record. Nothing is left half installed.
Not done (changed from the draft's step 7): clearing the game's `cache` folder. The patch changes no mod content, and
the hand install of 11.2 started without it [L].

**Update** (a record of an earlier patch version is found): the earlier version is removed by its own record, then the
new one is installed. The player's `mode=off` is kept across an update [proposal for the owner]. The player's
`VP64Options.ini` is kept across an update too: the removal for an update does not put it back, and the new release's
option set does not override a value the file sets (ticket 0733, 2026-11-24, section 16.12; before, an update reset it).

**Removal** (*Remove the patch*):
1. `vp64helper off` (the player's `config.ini` values back, section 9).
2. Every file the record lists as added is deleted when its hash still matches; a file changed since is left and named.
3. Every replaced file is restored from its backup and checked by its SHA-256; VP's DLL is back under its own name,
   byte for byte.
4. The record and the backups are removed last. `VP64Options.ini`, `Logs\VP64`, the compositor's
   `Logs\shell_composite*` files and `VP64\reports` stay unless the player ticks "also remove the patch's logs, reports
   and VP64Options.ini" (caption corrected 2026-11-20, ticket 0623, MC #8776; was "... logs and reports").
A hand install (`INSTALL-FIRST-PLAYABLE.md`, no record) is recognised by `vpproxy.ini` and a kept
`CvGameCore_Expansion2_x86.dll` whose hash is in the version table, and removed the same way. A record whose files are
gone (VP was reinstalled over the patch, 11.3) is treated as "not installed" and removed.

**Never touched:** saves, the EXE, Steam's files, VP's other files. No junctions or symlinks.

### 4.7 For the tests and the build (not shown to players)

- Silent runs for the hermetic tests and the instances: `/VERYSILENT /SUPPRESSMSGBOXES /DIR=<My Games> /GAMEDIR=<game>
  /ACTION=install|remove /LOG=<file>`; a refusal ends with a non-zero exit code and its text in the log.
- `python -I tools/installer/build_installer.py --work <take_release work> [--work43 <the -m43 work>] --out <dir>`:
  reads both work folders' `state.json` (the `pack` and `build` outputs), stages the payload, writes the notices and the
  version table into the script, and compiles with the installed Inno Setup 6.2.2 (`ISCC.exe` SHA-256 `29856cf8...`,
  the owner's own install at `C:\Program Files (x86)\Inno Setup 6`, used as it is, MC #3196). Same work folders in, same
  payload hashes out; whether the `.exe` itself is byte for byte reproducible is measured in 2602.
- A build for the owner requires a work folder whose `take_release --art-check` passes (D162); the build refuses it otherwise
  unless it is marked a test build.
- D162 for an installer build (MC #3343, 2026-11-17; measured by 2604, #3333 [L]): both HI release journeys run on the
  same build, the product route on the installer-installed layout and the stock route on that build's default
  stage-laid path with the instance-aware DLL, each verdict line naming its layout, because the stock route needs the
  Lua channel that VP's own kept DLL lacks and no DLL is swapped inside an installed layout (#3211).

## 5. Choices put to the owner (tracker 0082) and the answers (D28, 2026-11-10 13:16 EST)

The owner [reworded for privacy]: approved overall; 1 add-on to existing VP, 2 detect and link to download, 3 yes, 4 yes.
Answers: 1 add-on; 2 detect and link (not the MC pick); 3 beside the proxy; 4 keep the fallback and the switch.
Consequence [I]: with nothing written to the Steam folder and no runtime installer to run, the installer needs
no administrator rights at all, unlike Vox Populi's (`PrivilegesRequired=admin`). Constraint on the real-EXE
epic: the proxy starts the server from a folder beside itself under `My Games` (mods form) or under the
install's `Assets/DLC/VP_MODPACK` (modpack form); if that proves impossible, it comes back to the owner.

1. Add-on to an installed Vox Populi of the matching version (MC pick), or one installer that installs Vox Populi
   and the patch together (it would be a fork of `VPSetupData.iss`).
2. Bundle Microsoft's redistributable installer (MC pick) or only detect and link to it.
3. Where the 64-bit files live: a folder beside the proxy inside `(1) Community Patch` (MC pick: no write to
   the Steam folder, so no administrator rights unless the redistributable must be installed), or a folder in
   the game install.
4. Keep the original 32-bit DLL as a fallback with a switch to turn the patch off without uninstalling (MC pick:
   yes).
Answered 2026-11-10 by epic 2500 [L, Goldberg instance c5h-a]: the server starts and runs from `MODS/(1) Community Patch/VP64/` under `Documents/My Games` (the D28 layout), with no administrator rights and no write outside that folder except the engine's cache folder (the DLL's own files); provisioning: `tools/instances/provision.py set-proxy`. The x64 files need the VC++ 2015-2022 x64 runtime (it was installed on the machine). Was: Unknown until later epics and therefore not asked yet: whether the server may be started from a folder under
`Documents` (the real-EXE epic decides the start layout), multiplayer requirements, increment 2 and 3 contents.

## 6. The DLL pack in the shipped layout (epic 3500, issue 3504, 2026-11-13) [C, L]

The 64-bit folder of D28 (`<user>\MODS\(1) Community Patch\VP64\`, `vpproxy.ini x64_dir`) **is** the DLL pack: beside
`server.exe`, the x64 DLL and the three x64 libraries it holds `manifest.json` (format 1: the release, the accepted
32-bit DLLs `x86_dlls` with their origins, the x64 DLL's and the purity file's SHA-256) and `purity.tsv`.
`tools/release/pack.py` writes that folder's content as `<out>/vp64pack/`; the installer copies it into `VP64\` and
writes `pack=1` into `vpproxy.ini`. With `pack=1` the proxy checks the pack before it starts the server and boots the
player's 32-bit DLL (`fallback_dll`, `CvGameCore_Expansion2_x86.dll`) instead when the pack is missing, damaged, made
for another Vox Populi DLL, or the server does not start. It says so in `vpproxy.log` (one FALLBACK line) and in the game
window's title (` - VP64 64-bit patch OFF: <reason>`). A shipped manifest lists only the release tag's committed
32-bit DLL (`262aee88...` for 5.4.8; MC ruling 2026-11-13 22:22). So the installer's step 3 of section 4 ("identify
Vox Populi by the SHA-256 of its DLL") and the proxy's run-time check compare against the same hash. Contract and
details: `tools/release/README.md` section 5; tests and live results: `release-driver.md` section W3.
Consequence for the installer (epic 2600) [I]: an install whose VP DLL is not the tag's (a local build such as the
toolkit's 4e91c58a, or a later VP update) is not refused at play time. The game runs on the 32-bit DLL and says so, so the
installer may warn instead of refusing. *(Superseded 2026-11-16 by issue 2601: the installer refuses a VP DLL that is
not the pack's and names the right VP release, 4.3; the proxy's run-time check stays as the second line.)*
## 7. The DLL pack in detail: what epic 2600's installer takes from a release (epic 3500, issue 3507, 2026-11-14)

Section 6 is 3504's short form; this section spells out the files, the layout and the proposals for epic 2600.

Written for epic 2600 from the pack as built by issue 3504 (W7 of epic 3500). The authoritative format is
`tools/release/README.md` section 5; the run-time behaviour and its tests are in `release-driver.md` section W3
[C: the code of `tools/release/pack.py` and `proxy/src/proxy_pack.cpp` on the epic-3500 team branch, 2026-11-14].
Nothing here is built for the installer yet.

**What a release hands the installer.** `take_release` (stage `pack`) runs `tools/release/pack.py`, which writes one
folder, `<work>/pack/vp64pack/`. Its content is, byte for byte, the content of the patch's 64-bit folder `VP64\` in
the D28 layout (section 5: `<My Games>/.../MODS/(1) Community Patch/VP64/` in the mods form):

| File | What it is |
|---|---|
| `manifest.json` | format 1. `release` (the tag). `x86_dlls` (the 32-bit DLLs the pack belongs to, each `{sha256, origin}`; a shipped pack lists only the tag's committed DLL, for example `262aee88...` for 5.4.8). `x64_dll` `{file, sha256}`. `purity` `{file, sha256}`. Optional: `built` (fork and tools commits) and `files` (hashes, for information only). `pack.py` writes it last, through a temporary name, so a folder without it reads as "no pack" |
| `CvGameCore_Expansion2.dll` | the 64-bit game DLL, built from the release's x64 branch |
| `purity.tsv` | the pure-function lists the proxy answers locally (D30); a `dll_sha256` line binds it to the x64 DLL (format: `tools/release/README.md` section 4, issue 3503) |
| `server.exe`, `lua51_x64.dll`, `CvGameDatabase_x64.dll`, `CvLocalization_x64.dll` | the server and the three 64-bit libraries (section 2) |

The pack does not hold these; the installer adds them itself:
- the 32-bit proxy (`CvGameCore_Expansion2.dll` from the same build's `proxy/bin/`; it must come from the same build as
  the server because of the schema hash, `new-vp-release-checklist.md` step 10);
- the kept original 32-bit DLL;
- `vpproxy.ini`;
- the licence files (section 3, item 6);
- any graphics settings (section 3, item 5).

**The layout the installer writes.** It is the one `tools/instances/provision.py set-proxy` writes into test instances
(issue 2505), in the folder of the installed Vox Populi DLL (`(1) Community Patch`):

```
CvGameCore_Expansion2.dll        the 32-bit proxy (replaces Vox Populi's DLL; the replaced file is backed up, section 4 step 5)
CvGameCore_Expansion2_x86.dll    Vox Populi's own 32-bit DLL, kept: vpproxy.ini fallback_dll (the D28 off switch and the fallback)
vpproxy.ini                      mode=on, fallback_dll=CvGameCore_Expansion2_x86.dll, x64_dir=VP64, pack=1 (the shipped value)
VP64\                            the pack folder's content (above)
```

**What the proxy does with it at every game start** (`pack=1`, issue 3504).
1. Before it starts the server, it reads `VP64\manifest.json` and hashes `fallback_dll`, the x64 DLL and `purity.tsv`.
2. It holds the x64 DLL and `purity.tsv` open without write sharing until the server has loaded the DLL.
3. On any mismatch it falls back to `fallback_dll`, the 32-bit game, as with `mode=off`. A mismatch is any of these:
   - no manifest, or one it cannot read;
   - an unknown format or a missing field;
   - a `fallback_dll` whose SHA-256 is none of `x86_dlls` (for example a Vox Populi update that replaced the DLL);
   - a changed x64 DLL or purity file;
   - a 64-bit boot that fails: the server is missing, exits, or does not attach within `connect_timeout_ms`.

Each fallback writes one loud line to `vpproxy.log` and adds ` - VP64 64-bit patch OFF: <reason>` to the game window's
title (no dialog, no focus). The check costs about 28 ms [L, W3.2]; hashing the player's 16.8 MB 32-bit DLL adds about
two thirds of that again [I].

**What a player gets with the patch switched off (ticket 0635, D224, 2026-11-21)** [C: code; live check in the
ticket]. With `mode=off`, `failed=1` or a pack fallback, the game runs on the player's own Vox Populi DLL
(`fallback_dll`), never on the project's 32-bit pin. Neither DLL has a guard of the EXE's UI message queue. Since
0635 the proxy installs its own queue guard there too, after that DLL's first `DllGetGameContext`, on every EXE build
the patch knows. It never hooks on top of a guard the 32-bit DLL brought itself. `VP64Options.ini` `[Proxy]
GuardsWhenOff = 0` (id 29, default 1; `docs/OPTIONS.md`) leaves it out. The installer writes no line for the key:
its default is the release's value. The farm builder guard with rule 2 runs there too, under the same key (D225).
The UI draw-list growth stays mode=on only: its frame hook needs the proxy on the EXE's Lua path, and in off mode the
player's DLL answers (`ui-queue-guard-product.md` 8.2). A player who sets `mode=off` by hand while config.ini is still
in the patch's form gets no play and a report, not a red map. `vp64helper failed` puts config.ini back, so the next
start is the normal game (MC #9064, `ui-queue-guard-product.md` 8.4). The off switch itself (`vp64helper off`) puts
config.ini back first and is not affected. Details: `research/03-dll-engine-boundary/ui-queue-guard-product.md`
section 8.

**What this means for the installer.** These are proposals for epic 2600, not decisions.
1. Section 4 step 3 ("identify Vox Populi's version by the SHA-256 of its DLL") becomes a check against the manifest:
   the installed `(1) Community Patch/CvGameCore_Expansion2.dll` must hash to one of the manifest's `x86_dlls`. The
   manifest already names the DLL and the release, so the installer needs no table of its own. On a mismatch it refuses,
   naming the release the pack was made for.
2. The installer copies Vox Populi's DLL to `CvGameCore_Expansion2_x86.dll` **before** it puts the proxy in its place,
   and checks that the copy hashes to the manifest's entry. Without that check, a bad copy would turn the patch off at
   the first start.
3. `vpproxy.ini` ships with `pack=1`. Every development and test layout keeps the default `pack=0`, so no test runs on
   the 32-bit DLL without saying so (3504's decision).
4. There is one pack per Vox Populi release. A player who updates Vox Populi with its installer loses the patch
   entirely, because that installer deletes the whole `(1) Community Patch` folder first, and plays the plain 32-bit game
   (corrected 2026-11-15 by issue 0324, section 11.3; replaced: "gets the 32-bit game and the title notice, not a broken
   game"). The title notice and the fallback come only when the kept `CvGameCore_Expansion2_x86.dll` is not the
   manifest's. The installer for the next release replaces the whole `VP64\` folder.
5. Which 32-bit DLL a player has:
   - the tag's committed DLL (`262aee88...` for 5.4.8) is the one in the owner's install and in the instances [C, W3.2];
   - the toolkit's modpack DLL `4e91c58a...` is a local test build and is never in a shipped manifest (MC
     2026-11-13, thread #206);
   - not checked: whether Vox Populi's Inno Setup installer ships the tag's DLL byte for byte.
6. Between releases, upstream master commits no new 32-bit DLL [C, issue 3507, `release-driver.md` W7.1]: master
   ba8aafdca still carries 5.4.8's `262aee88`. A pack made from master is therefore only a test artefact.

## 8. For beta testers: a start without the patch's DLL (D125, the owner 2026-11-14; text for the beta instructions)

Context (MC #575, decisions D119, D122, D125): the patch keeps the game's memory savers (D33) in `config.ini` while it is
switched on, because they must be set before the game starts. If the game then starts a session in which the patch's DLL
is not loaded (Vox Populi's mods not activated, another mod set, the base game, Vox Populi reinstalled over the patch,
or the patch's files deleted), nothing of the patch runs and nothing can warn. The owner chose to keep the memory saving and
let that case fail loudly, so testers report it (D125 [reworded for privacy]: the memory saving is preferred, with a loud failure). The
paragraph below is the draft for the testers' instructions; the off switch it names is the helper of issue 3520
(`vp64helper off`; built by issue 3520 on 2026-11-14 05:01, the contract is section 9; until then this line said
"planned, not yet built").

> **If the map is red and has no terrain.** A game whose map shows flat dark red ground, no terrain, water or rivers
> (units, cities and the interface still work) has started without the 64-bit patch's DLL while the patch's settings
> are still switched on. It usually means Vox Populi's mods were not activated, another mod set or the base game was
> started, or Vox Populi was reinstalled over the patch. Quit the game. To play without the patch, switch it off with
> the patch's off switch (it puts your own graphics settings back in `config.ini`); to keep testing, start Vox Populi
> with its mods activated. Please report what you did as an issue on Vox Populi's GitHub repository
> (https://github.com/LoneGazebo/Community-Patch-DLL/issues/new): issues are public, so do not attach saves or anything with
> your name in it.

*Corrected 2026-11-25 (D260, ticket 0767): the link names upstream's issues; it named the patch's own repository.*


## 8a. Known issue for the beta notes: changing mods after a game (issue 0184, MC 2026-11-14) [C, L]

With Vox Populi 5.4.8's Lua, changing the active mods in the same game session after a game was loaded can leave the game's database half rebuilt; on the patch's route the server then fails and the D125 popup and report appear (exit 3). The cause is Community Patch's `CPK.Misc.CustomModOptionEnabled.lua` (and `CPK.Util.Color`), fixed upstream on 2026-11-10 (commit 14d16fa3d, #13467) for VP's next release. The patch does not ship Vox Populi's files, so the beta notes say: after changing mods, restart the game before starting or loading one. Remove this line once the pinned VP release contains the fix.

A second known issue for the beta notes (MC 2026-11-17 04:30, from issue 0462: `research/03-dll-engine-boundary/upstream-lua-fixes.md`
3.3, an upstream fix in Vox Populi's own Lua, not in our pin) [C]. The player's text, with no internal ids (D95):

> With Vox Populi 5.4.8, loading a save from the game menu while a game is running can crash the game, with or without
> the 64-bit patch. Vox Populi has fixed this for its next release. Until then, quit to the main menu and load the save
> from there.

Remove it, like the line above, when the pinned VP release contains the fix.

## 9. The helper's contract for the installer and the off switch (issue 3520, D119/D125; 2026-11-14; the one switch, issue 0030, 2026-11-16) [C, hermetic tests]

`vp64helper.exe` ships beside the proxy DLL (in `MODS/(1) Community Patch/`, built by `proxy/build_proxy.py` step 5b).
The installer (epic 2600) calls it; this repository does not edit the installer. Without `--options`, the helper uses
`vpproxy.ini` beside the EXE. Without `--config`, it uses `VPPROXY_TEST_CONFIG_INI` (tests only), else `C5_DOCUMENTS`
(test instances), else `Documents\My Games\Sid Meier's Civilization 5\config.ini`.

**The one switch (issue 0030, MC ruling 2026-11-16) [C, hermetic tests 2026-11-16].** `vp64helper on` / `off` is the
whole shell configuration: config.ini's D33 keys in the A'' form AND `VP64Options.ini [Renderer] ServerRenderer`
(`docs/OPTIONS.md`), so that no player reaches config.ini in D33 with `ServerRenderer = 0` (a degraded game: the EXE
draws its terrain, but its fog of war, forests, decals and yield icons are off and the server draws none of them;
**corrected 2026-11-20 by ticket 0624's live run**, `research/07-architecture-decisions/options-file.md` 7.4; replaced
"a game without terrain: the server would not draw it and the EXE's Terrain pass is off", which holds for the 32-bit
DLL's dark red map of section 8, not for the 64-bit path). Until 0030 the switch wrote only config.ini and
ServerRenderer was a second, manual step. The options file is the proxy's: `VP64_OPTIONS` when set, else
`VP64Options.ini` beside config.ini (the helper's `--vp64-options` names another); its record is
`vp64-options-side.ini` in the same folder (the whole file before `on` and the bytes `on` left, as hexadecimal).

**The proxy keeps the one switch too (ticket 0624, 2026-11-20) [C code; L offline suite].** Until 0624 a player who
deleted VP64Options.ini (as `docs/OPTIONS.md` advised for a reset) got it re-created by the proxy at the table's
defaults, `ServerRenderer = 0`, while config.ini kept the D33 form: the very state the one-switch rule exists to
prevent, reached without the helper. Now, in a D33 session, the proxy re-creates a missing file with the release's
option set (`VP64\release-options.txt`, the file install.iss writes from, ticket 0574) and `ServerRenderer = 1`, or not
at all when the set is missing or unusable; and a renderer guard in `Boot()` (`proxy/src/proxy_rt.cpp`, right after
`OptionsBoot`) refuses any D33 boot with `ServerRenderer = 0` in force (deleted file without a set, a hand-set 0, a file
without the key) on the D125 route: NO PLAY, the report, `vp64helper failed`, exit 3. The installer's and the helper's
own writing of the file is unchanged. Details and tests: `research/07-architecture-decisions/options-file.md` section 7.

| Call | When | What it does | Exit |
|---|---|---|---|
| `vp64helper on` | at install, and as the popup's "Try again next start" | refuses unless vpproxy.ini exists and can be written; `ServerRenderer = 1` into VP64Options.ini (the last `ServerRenderer` line replaced, else the proxy's commented default line `; ServerRenderer = 0`, else the line inserted under `[Renderer]`, else `[Renderer]` and the line appended; no file: a new small one), the file as it was recorded first in `vp64-options-side.ini`; stores the player's values of the A'' keys in `vp64-config-side.ini` beside config.ini, then writes ours; then `mode=on` and removes `failed=`. config.ini refused: VP64Options.ini put back; mode= not writable: both put back. A read-only, UTF-16, NUL-holding or over-64-KB VP64Options.ini refuses `on` with nothing changed (0030) | 0 ok; 1 refused, nothing changed |
| `vp64helper off` | the off switch, and at uninstall before the files go | puts the player's values back (only keys that still hold ours; a key the player changed is left alone); then VP64Options.ini: unchanged since `on`, its recorded bytes come back byte for byte (removed when `on` created it); edited after `on`, only the `ServerRenderer` line goes back (the other edits kept, said in the line); `ServerRenderer` changed by the player after `on`, left and named `kept:ServerRenderer` (0030); then `mode=off`. If config.ini cannot be restored, VP64Options.ini and vpproxy.ini are left as they are. If VP64Options.ini cannot be restored, `mode=off` is still written and the line says `FAILED` (ServerRenderer means nothing to a mode=off start). Without a usable side file (`on` never ran, the file lost or damaged) the file concerned is left as it is and the line says so (`no side file ...` / `side file ... unusable ...: ... left as it is`); a damaged side file is moved to `<side file>.unusable` (issue 0378, 2026-11-15; until then the stock values were written, 11.3) | 0 ok; 1 failed |
| `vp64helper status` | diagnosis | prints config.ini's state, the keys that differ, VP64Options.ini's state and value (`status: VP64Options.ini on (<path>): ServerRenderer = 1`), `status: switch partial` when the two files disagree, `mode` and `failed` | 0 off, 3 on (both files), 4 partial, 5 no config.ini |
| `vp64helper failed` | started by the proxy on a failure in a D33 session | after the EXE's end, off's config.ini part, then VP64Options.ini as `off` (0030; a failure there is logged in helper.log and `failed=1` is still written), then `failed=1` and the popup | as before |

Tests (2026-11-16, branch task-0030-shell-configuration, through `quietlock.py run`): `test_cfgini.py` 6/6, with
`--asan` 12/12 (`cfgini_unit` 277/277 plain and under MSVC `/fsanitize=address`, canary caught; 108 new checks, among
them `RendererValue` against the options parser itself, `options/src/vp64_options.cpp` linked into the unit, on 30
texts: 30/30 agree); `test_vp64helper.py` 154/154, with `--asan` 306/306 (the `switch` cases: on then off gives
config.ini and the proxy's default VP64Options.ini back by SHA-256 with the default path beside config.ini; a missing
options file created and removed; `--vp64-options` and `VP64_OPTIONS`; a player's change after on; status partial; a
read-only or UTF-16 options file; the rollbacks; failed and Try again). Live [L, 2026-11-16, c5h-f]: the release
layout of this branch, the game started once, then only `vp64helper on`: the huge save played 10 AI turns in the shell
configuration with the server drawing the terrain; `off` gave config.ini and VP64Options.ini back by SHA-256 and the
next start was the stock game (`research/02-exe-internals/disable-systems-and-masks.md`, issue 0030's section).

- config.ini must exist with its `[DisableSystems]` section before `on`: the EXE writes the file at its first start.
  So the installer runs `on` after one start of the game, or tells the player to start the game once and run `on`.
- The EXE rewrites config.ini unchanged at every start (`release-driver.md` W3c.2). Values set before the start
  therefore stay set [I: tested only with unchanged values].
- Uninstall: run `off` first, then remove the files. Without `off`, config.ini keeps D33 and every later start draws the
  red map (section 8).

## 10. The Steam-launched run on the laptop (issue 0323, 2026-11-15)

Issue 0323 runs the patch in a game that Steam itself starts, on the laptop's own Steam install, never the owner's main one
(D26/D27). The run uses the thin client: `ServerRenderer = 0`, `config.ini` left as it is, and no `vp64helper on`
(MC ruling, 0323/0324). Testbed host `laptop`, in the owner's logged-on session through the session helper
(`quiet-testbed.md` 15).

### 10.1 What the laptop's Steam install is (step 0, read-only) [L, 2026-11-15 11:03-11:05 EST]

Jobs `20261115-110242-aa6e` and `20261115-110439-ea96`: `tools/testbed/jobs/steam_launch.py --phase probe`, headed,
session 1. Nothing was written outside the job's folder and no process was started. The output names no user,
account or machine.

| What | Value |
|---|---|
| Steam | `C:\Program Files (x86)\Steam` (HKCU `Software\Valve\Steam` `SteamPath`), one library; signed in; `steam.exe` and 7 `steamwebhelper.exe` in session 1 |
| Game folder | `C:\Program Files (x86)\Steam\steamapps\common\Sid Meier's Civilization V`, app 8930, build id 4390913, `StateFlags` 4 (fully installed), 23 depots, 8,274,093,310 bytes |
| The EXE | `CivilizationV_DX11.exe`, 16,982,584 bytes, SHA-256 `ba0c491bebe7ab3a05b2a72fdb958108367d080611a538ca48e67f0575603657`, the same copy as `quiet-testbed.md` 15.2 and 15.3 |
| `steam_api.dll` beside it | 120,616 bytes, `b0fd893eaa8a225510389e04c0802312539a01fd2eb3a1a8a38005cee7f1d10f` (the real Steam one, not the gbe one) |
| Files of ours beside the EXE | none: no `vpproxy.ini`, no `VP64Options.ini` |
| Who may write the game folder | inherited `BUILTIN\Users:(F)` from Steam's folder (Steam's own ACL), so a copy into it needs no administrator rights |
| Asset set (D94) | `Assets/DLC` holds DLC_01-07, DLC_Deluxe, DLC_SP_Maps 1-3, Expansion, Expansion2, Shared, Tablet. **No `VT`** (no Vibrant Textures), no `VP_MODPACK`, no `UI_bc1`, no `VPUI`. `Expansion2.Civ5Pkg` is the stock one (22,492 bytes, `8833f39f...`; VP's replaced one is 22,555 bytes, `c5886615...`) |
| Documents of a Steam-started EXE | the session user's Documents known folder: `%USERPROFILE%\Documents` (not redirected to OneDrive); `My Games\Sid Meier's Civilization 5` exists, made by the owner's one start on 2026-11-11 |
| What My Games holds | `config.ini` (7,938 bytes, `5d9d3410...`, has `[DisableSystems]`; `EnableTuner = 0`), `GraphicsSettingsDX11.ini` (`8d8e6df6...`), `UserSettings.ini` (`ab5a4889...`), `cache`, `Logs`, and the empty folders the game makes. `Saves` and `ModdedSaves`: no files |
| **Vox Populi** | **not installed**: `MODS` is empty and there is no DLC modpack (`VP_MODPACK`). Not in mods form, not in modpack form |
| Steam launch options for app 8930 | none set (the owner's `localconfig.vdf`, read only: the app block has no `LaunchOptions`) |
| `HKCU\Software\Firaxis\Civilization5` `LastKnownPath` | not set |

**What the testbed root on the laptop already holds** (the copies the instances use): `base/user/MODS` with `(1) Community
Patch` (737 files, 107.8 MB; its DLL is the tag's `262aee88...`), `(2) Vox Populi` (1,350 files, 147.5 MB) and
`(3a) VP - EUI Compatibility Files` (44 files). The pure_vp base install has `UI_bc1`, `VPUI`, `VT` and VP's
`Expansion2.Civ5Pkg` (`c5886615...`).

**Consequence [L].** Steam's own copy of the game on the laptop has no Vox Populi, so a Steam-started game there cannot
load the patch until Vox Populi is installed into that profile. Step 0 of 0323 stopped there, and the MC was asked
on thread 0323. *(Corrected 2026-11-15 by 10.3: no launch-option chooser appeared. Replaced: "If Steam starts the game
with no launch options set, it shows the game's launch-option chooser [I]".)*

### 10.2 The build and the session-only install (steps 1 and 2) [L, 2026-11-15 11:16-11:18 EST]

**Build** (MC #1728). 0175's candidate no longer exists: its run folder `C:/c5-64bit-work/0175/run3` was removed at
0175's hand-back. The current pin of COMMON 18 replaces it: layout
`%USERPROFILE%/c5-i0/dlls/x64-04b1211b-pfdee656`, which passed 0351's validated tier (parity 4/4).
- The pack is `tools/release/pack.py --layout <that layout> --x86-dll <the tag's 262aee88 DLL> --purity <purity>
  --release Release-5.4.8 --fork-commit 04b1211b8... --tools-commit fdee65668`.
- It holds: manifest `a0947879...`; x64 DLL `29f58e17...`; `server.exe` `5b254430...`; `CvGameDatabase_x64.dll`
  `9c37b39e...`; `CvLocalization_x64.dll` `35863ede...`; `lua51_x64.dll` `30724d6d...`; purity `6a0d7b6b...`.
- The proxy and the helper come from the same layout: proxy `75c40cec...` and `vp64helper.exe` `b8ac9a64...`.
- The layout has no purity file of its own. `proxy/tools/derive_purity.py` made one for x64 DLL `29f58e17...` from the
  04b1211b8 source copy of 0334 (`F:/c5-64bit-work/0334/rebuild/src-x64-04b1211b`): 143 of 143 rows kept. Apart from
  its `dll_sha256` line it equals 0334's file for the other 04b1211b8 build (`3e862705...`).
- Pack folder: `C:/c5-64bit-work/0323/pack/vp64pack`.

**Install** (MC #1730, 1). Job `20261115-111647-53a0`, `steam_launch.py --phase install`, 3 s of backup plus the
copies. Vox Populi 5.4.8 in mods form comes from the laptop testbed root's own copies, and the patch is in the shipped
D28 form. No installer ran and nothing needed administrator rights.
- **Backup first.** The whole `My Games\Sid Meier's Civilization 5` tree (37 files, 119.4 MB) and the three Steam
  install files to be replaced (`Assets/DLC/Expansion2/Expansion2.Civ5Pkg` `8833f39f...`,
  `Assets/UI/FrontEnd/MainMenu.lua` `ba20efc7...`, `FrontEnd.lua` `2c335764...`) were copied and hashed into the job's
  `work/mygames-backup`, with a listing of the whole game folder.
- **Documents\My Games\...\MODS:**
  - `(1) Community Patch` (737 files), `(2) Vox Populi` (1,350), `(3a) VP - EUI Compatibility Files` (44).
  - In `(1)`: the proxy as `CvGameCore_Expansion2.dll`; VP's DLL kept as `CvGameCore_Expansion2_x86.dll` (`262aee88...`,
    checked); `vp64helper.exe`; `vpproxy.ini` (`mode=on`, `fallback_dll=CvGameCore_Expansion2_x86.dll`,
    `x64_dir=VP64`, `pack=1`); and `VP64\` = the pack.
- **Steam install:**
  - `Assets/DLC/UI_bc1` (190 files) and `VPUI` (10 files) added.
  - VP's `Expansion2.Civ5Pkg` (`c5886615...`) and `Expansion2/Sounds/XML/MinorCivSounds_VoxPopuli.xml` copied in.
    These two are the only `Expansion2` files that differ in the testbed's pure_vp base.
  - The vp-dll-dev front end (`MainMenu.lua`, `FrontEnd.lua`). It activates CP, VP and EUI and starts a new game
    (Continents, `WORLDSIZE_TINY`, 3 AIs, Prince) 150 s of menu time after the menu came up with the mods active.
  - `cache\luaexec.enabled` for the Lua channel.
- **Not touched:** `config.ini`, `GraphicsSettingsDX11.ini`, `steam_api.dll` and the EXE. `VP64Options.ini` was made
  by the proxy itself at its first boot, with every option at its default (`ServerRenderer = 0`).

### 10.3 The game started by Steam (step 3) [L, 2026-11-15 11:19-11:41 EST]

Session jobs `20261115-111811-5bd7`, `-112107-3e59`, `-112620-2f5f`, `-113212-e314` and `-113939-6f1c`. One game ran
across them; each session job attaches to the running game, and a job ending never ends the game. Commands came from
the workstation (`tools/testbed/live.py send`).

| Time (EST) | What | Result |
|---|---|---|
| 11:19:01 | `steam.exe -applaunch 8930` | Steam started **`CivilizationV.exe` (DX9)**, parent `steam.exe`, with **no launch-option chooser**. That is not the EXE the patch is for, so it was closed by `WM_CLOSE` to its window at 11:21:58 (gone in 40 s; the game's own close handling, no process kill) |
| 11:22:48 | `steam://launch/8930/Dialog` through the shell | Steam started **`CivilizationV_DX11.exe`** (pid 12256, created 11:22:51), parent `steam.exe` (the running client, pid 13228), again with no chooser seen. Modules: `GameOverlayRenderer.dll`, `steam_api.dll`, `steamclient.dll`, `d3d11.dll` |
| 11:22:51-11:26:55 | start to the main menu | Steam's start-up check passed with nothing of ours in its path: the real `steam_api.dll` was untouched, there was no wrapper and no preload. Steam's overlay UI process connected to the game at 11:26:14 |
| 11:26:55 | mods activated at the menu | the proxy booted: `pack ... ok: release Release-5.4.8, 32-bit DLL 262aee88... matches x86_dlls, x64 DLL 29f58e17..., purity 6a0d7b6b... (checked in 326.0 ms)`. `server.exe` (pid 5680) started as the EXE's child and attached in 1.4 s, `kRt_Boot ok`. **No FALLBACK line**, no D125 report. Run folder `My Games\Logs\VP64\run\20261115-112655-12256-1` |
| 11:29:25 | the deferred new game | load screen; Lua channel: `Modding.GetActivatedMods()` = CP `d1b6328c...@151`, VP `8411a7a8...@17`, EUI `24923240...@1`; turn 0, `WORLDSIZE_TINY`, 56 x 36, single player |
| 11:31:01 | load screen closed through the channel (`frontend.ensure_in_game`) | in game |
| 11:34:39-11:35:31 | 10 AI turns (`diplo.arm`, `turns.end_turn` + `wait_turn`) | turns 0 -> 10: 7.4 s for the first, then 3.8-3.9 s each |
| 11:35:55 | `UI.SaveGame("VP64-0323-STEAM-T10")` on the main thread | `ModdedSaves\single\VP64-0323-STEAM-T10.Civ5Save`, 260,504 B, `03ff4f08...` |
| 11:36:15 | `Events.PlayerChoseToLoadGame(<that save>)` on the main thread | loaded in the same process; in game again at turn 10 with the same three mods; the diplomacy handler re-armed (a new Lua state, gen 2) |
| 11:37:35-11:38:0x | 1 more turn | 10 -> 11 in 4 s |
| 11:41:25 | `UI.ExitGame()` through the channel | the EXE ended in 5.5 s with **exit code 0x00000000**; `server.exe` ended with it; no D125 report, no `vp64helper` popup |

Findings:
- **Steam's launch-option chooser never appeared** [L, 2 of 2 starts]. With no launch options set for app 8930,
  `steam.exe -applaunch 8930` starts option 0, the DX9 EXE, and `steam://launch/8930/Dialog` started the DX11 EXE
  [I: why the URL picks DX11 is not known]. So a tool that starts the game through Steam names the URL, never
  `-applaunch`.
- **Steam injects its overlay into `server.exe` too** [L]. `GameOverlayRenderer64.dll` was loaded in `server.exe`,
  and Steam's own `Steam\logs\gameoverlay_renderer.txt` says "Current process: server.exe ... GameID = 8930,
  OverlayGameID = 8930", hooking SetCursorPos and the cursor functions. This file keeps only the last process that
  loaded the renderer, so the EXE's entry was overwritten. `server.exe` has no window and no swap chain in this
  configuration, and the injection did no visible harm.
- **The game was slow to reach its menu** [L]. From the start to the server's boot at mod activation took 4 min
  04 s. The overlay UI's log shows the game rendered no frame from 11:26:25 to 11:27:23 (the mod activation's database
  rebuild). [I: the rest is the opening movie and the first front-end load of this profile.]
- **The menu's 150 s delay counts menu time from the frame deltas** [L]. The new game began about 150 s after the
  activation (11:26:55 -> about 11:29:25). The planned overlay check at the menu was missed because the commands
  meant for it went out after the game had left the menu.
- **DPI on this laptop** [L]. The panel is 3840 x 2160 at 250 %, and the game runs at 1280 x 800 (its own setting).
  Its window covers the top-left 1280 x 800 physical pixels; a DPI-unaware tool sees it as 512 x 320. Steam's overlay
  log reads "SetScreenSize ... (1280, 800) with DPI scale factor 2.50".
- **Screen grabs of this Steam-started game are not black** [L]. A PIL `ImageGrab` showed the front end, the load
  screen and the map. This differs from the `tb` instances started by the harness (`quiet-testbed.md` 21.3).
  [I: the harness's focus guard or the instance's launch path; not compared.]
- **An elevated, maximized Task Manager on the desktop blocks input to the game** [L, with the cause I]. After the DX9
  window closed, Windows gave the foreground to Task Manager. From then on:
  - our job's injected clicks and keys did not reach the game: a click on the advisor popup's "Thank You" button,
    twice, did nothing;
  - `SetForegroundWindow` from the job was refused, also with `AttachThreadInput`.
  - [I: Task Manager runs elevated, and UIPI drops input that a medium-integrity process injects while an elevated
    window is in the foreground.]
  - The Lua channel, the turns, the save, the load and the exit do not need the foreground and all worked.
- **Logs.** The proxy's run folders are under `My Games\Logs\VP64\run\` (`keep_runs=10`), not under the mod folder.
  `Lua.log` gets only "Initializing Lua 5.1.4" with this profile's `config.ini`, so the front end's prints are not
  visible there.
- **The intro video played at both starts.** This profile's `UserSettings.ini` does not skip it. The owner saw it on the
  laptop, and decision D148 (MC, 2026-11-15) now has every client start skip the intro video and turn tutorials and
  advisors off. That order came after this run's last start, so it was not applied here (ticket 9070 makes it the
  default).

### 10.4 The Steam overlay, the pictures and the restore (steps 4, 5 and the end) [L, 2026-11-15 12:01-12:12 EST]

The owner closed the Task Manager window on the laptop at 12:00 (MC #1738). Session job `20261115-114313-ca88` then made a
second Steam start with `steam://launch/8930/Dialog`: `CivilizationV_DX11.exe` pid 5756, parent `steam.exe`.
`SetForegroundWindow` from the job now worked (`foreground` op).
- **Timing.** From the start to the server's boot took about 1.5 min, against 4 min in the first run while the
  window was in the background. Run folder `Logs\VP64\run\20261115-120314-5756-1`: pack ok, server attached, no
  FALLBACK.
- **Window size.** The game's window is 3840 x 2160 while it is in the foreground and 1280 x 800 otherwise, so a
  click aimed by an earlier picture can miss. One such click opened the in-game menu, which Esc closed.
- **Overlay at the main menu** (mods active, 12:04:00). Shift+Tab opened Steam's overlay: the "Back to Game (Shift+Tab)"
  panel was drawn over the menu, and `gameoverlayui64.exe -pid 5756` ran. Shift+Tab 6 s later closed it, and the menu
  was drawn normally. The EXE stayed in the foreground, `server.exe` stayed alive, and there was no D125 report.
- **Overlay in game** (Tiny, turn 0, CP/VP/EUI active, 12:09:02). The two advisor popups were closed first (D114) with
  `Events.AdvisorDisplayHide()` on the main thread. Shift+Tab opened the overlay over the map; Shift+Tab closed it; the
  map and interface were drawn normally. Then one AI turn (0 -> 1 in 7 s) and `UI.ExitGame()` (exit code 0x00000000
  in 4.4 s, `server.exe` gone, no D125 report).
- **Pictures (step 5)** (MC #1735: grabs allowed, cropped to the game window's rectangle, as DIAGNOSTIC because the
  front end is the test switch). Four entries are in the laptop's Visual Checks store, pulled to the workstation
  (`visualcheck.py pull --host laptop`, issue 0323): `vc-20261115-120451-laptop-403f` (menu, overlay open),
  `-120459-laptop-b5bd` (menu, after closing), `-120948-laptop-2f3d` (in game, overlay open) and
  `-120954-laptop-2779` (in game, after closing). Each names the asset set: the laptop's own Steam install, no
  Vibrant Textures (D94). None is undescribed. No RenderDoc and no wrapper were used (MC #1730, 4); a plain grab works
  for a Steam-started game on this laptop (10.3).
- **Restore** (jobs `20261115-121058-c4ad`, then `-121145-0ee7`).
  - *My Games* was put back to the backup and then checked file by file, all 37 files equal. 2,187 files were removed,
    among them the session's own save, its 8 autosaves (Saves and ModdedSaves held no files before, 10.1),
    `VP64Options.ini`, `GraphicsSettingsDX9.ini` (from the DX9 start), EUI's `ModUserData` database, the mods and the
    proxy's run folders. 12 files were copied back: 4 cache databases and 8 logs.
  - *Steam install*: `UI_bc1`, `VPUI` and the added Sounds XML were removed. `Expansion2.Civ5Pkg`, `MainMenu.lua` and
    `FrontEnd.lua` were copied back and equal their backups by SHA-256.
  - The first pass ended with exit 7, because the listing of the game folder found one file the game had written
    itself: `Config.ini.lnk` in the install folder, made at 12:01:30 by the second start. The shortcuts `Logs.lnk` and
    `Saves.lnk` were there before. The second pass removed it (only an `.lnk` at the folder's top made after the
    install began is removed) and ended `ok`: My Games equal to the backup, Steam install equal to before. The open
    install's state file is gone.
  - Not restored, by design: Steam's own records of starting the game (play time and last played in the owner's Steam
    profile, its overlay logs). These are what any start of the game does.

Evidence: `research/06-tooling-and-workflows/evidence/0323/`. It holds the probe, install and restore records; both
runs' `vpproxy.log` and `server.log`; the session record; and the command results (facts, turns, save, load, exit,
overlay). Every file is scrubbed of the user profile path. No pictures are committed; they are in the Visual Checks
store.

## 11. The first playable build and its install instructions (issue 0324, 2026-11-15)

Issue 0324 staged increment 1's first playable build (the thin client: `ServerRenderer = 0`, `config.ini` untouched, no
`vp64helper on`; MC ruling 0324/0323) and wrote the player's instructions, `docs/INSTALL-FIRST-PLAYABLE.md`. Nothing
was published (D27); nothing went into the owner's install (D26).

### 11.1 What ships, and from where [C, 2026-11-15 15:35 EST]

MC ruling (thread 0324, #1911): the build ships **a copy of the pinned layout** `x64-04b1211b-pfdee656` (COMMON 18,
`deps/dll.lock` `release`; the bytes that 0351's validated tier, 0322's human long game and 0323's Steam-started run
used), never a rebuild (pins are copied, `release-driver.md` 0358). The release driver run of the same ticket checks the
driver on main; its bytes are not shipped (`release-driver.md` section 0324).

Staged by `research/06-tooling-and-workflows/evidence/0324/stage_first_playable.py stage`, which runs main c80733b0's
`tools/release/pack.py` and `proxy/tools/derive_purity.py` (the release driver's own pack and purity tools) on the
pinned layout, after checking the layout's `SHA256.txt` (8 of 8). Folder
`F:/c5-64bit-work/0324/stage/VP64-first-playable-VP5.4.8-x64-04b1211b-pfdee656/`:

| Path in the folder | SHA-256 | Size (B) | Source commit / pin |
|---|---|---|---|
| `MODS/(1) Community Patch/CvGameCore_Expansion2.dll` (32-bit proxy) | `75c40cec...54ec` | 1,097,216 | patch fdee65668 (`proxy/`), pin layout |
| `MODS/(1) Community Patch/vp64helper.exe` | `b8ac9a64...8e0f` | 215,040 | patch fdee65668 (`proxy/helper/`), pin layout |
| `MODS/(1) Community Patch/vpproxy.ini` | `70f2c066...8872` | 324 | written by the script: `mode=on`, `fallback_dll=CvGameCore_Expansion2_x86.dll`, `x64_dir=VP64`, `pack=1`, CRLF, three `#` comment lines |
| `.../VP64/CvGameCore_Expansion2.dll` (x64 DLL) | `29f58e17...7b1d` | 23,655,424 | fork 04b1211b8 + patch fdee65668 stubs, pin layout |
| `.../VP64/server.exe` | `5b254430...40e5` | 3,285,504 | patch fdee65668, pin layout |
| `.../VP64/CvGameDatabase_x64.dll` | `9c37b39e...466e` | 778,752 | pin layout |
| `.../VP64/CvLocalization_x64.dll` | `35863ede...7d8a` | 169,472 | pin layout |
| `.../VP64/lua51_x64.dll` | `30724d6d...0f1a` | 173,056 | pin layout |
| `.../VP64/purity.tsv` | `6a0d7b6b...3922` | 5,746 | derive_purity on the fork source 04b1211b8 for DLL 29f58e17: 143 kept, 0 dropped (the same file as 0323's) |
| `.../VP64/manifest.json` | `79427572...bf0` | 1,032 | pack.py: release Release-5.4.8, `x86_dlls` = only the tag's `262aee88...`, built fork 04b1211b8 / tools fdee65668 |
| `LICENSE.txt` | `230184f6...4809` | 35,823 | this repository's GPL-3.0 text (CRLF) |
| `NOTICE.txt`, `BUILD.txt`, `INSTALL-FIRST-PLAYABLE.md`, `SHA256SUMS.txt` | in `SHA256SUMS.txt` | | written by the script; the doc is a copy of `docs/INSTALL-FIRST-PLAYABLE.md` |

`NOTICE.txt` names the licence (GPL-3.0-or-later, D22) and the source: this repository at fdee65668, and the DLL from
the project's DLL fork (its GitHub address) branch `x64-batch2` at 04b1211b8. Both were public on 2026-11-15 [C: `git merge-base
--is-ancestor` on origin/main, `git ls-remote`]. It also carries the third-party notices: Lua 5.1.4's COPYRIGHT and
zlib 1.2.3's licence block, read at staging time from the archives of `deps/third-party.lock` (SHA-256 checked), and
SQLite's public-domain note. None of these texts is committed. Not shipped: PDBs, `civ5save.exe`, and the composite
(`shell_composite.dll`, only needed for `ServerRenderer = 1`, with the MinHook it links).

**Purity check (acceptance 2) [C, 2026-11-15 15:34].** `stage_first_playable.py check` compares every file of the
folder with every file of the same size under Goldberg instance c5h-e's `install` (a copy of the pure_vp game install
with VP's DLC folders) and `Documents` (VP 5.4.8's mods), by SHA-256. Result **PASS**: 15 files against 15,782; no
file has a Vox Populi file's name except the proxy's `CvGameCore_Expansion2.dll`; the tag's 32-bit DLL is absent;
LICENSE and NOTICE are present. Negative control: the same check against the pinned layout folder fails with exit 1
(7 files with equal bytes). The binaries hold no build path: their only `.pdb` strings are bare names (`server.pdb`,
...), and there is no user name, e-mail address or machine name in them [C, a string scan of all seven].

### 11.2 The literal-follow test on c5h-f [L, 2026-11-15 15:37-15:57 EST]

Goldberg instance c5h-f (F:, claimed through the pool), VP 5.4.8 in mods form.

**Prep (the player's state).** The instance's `(1) Community Patch` DLL is the pin's instance-aware build `3c2f47da...`,
which the harness needs. It was backed up and replaced by the tag's own 32-bit DLL `262aee88...` (`git show
Release-5.4.8:...`), the DLL a player of VP 5.4.8 has.

**Changes from the text.** The document's sections were then followed as written, in Windows PowerShell 5.1, with two
changes:
- `$cp` named the instance's folder. A test instance has its own Documents. The document's `GetFolderPath('MyDocuments')`
  line was evaluated on its own and names `%USERPROFILE%\Documents\My Games\Sid Meier's Civilization 5\MODS\(1)
  Community Patch`.
- `C5_DOCUMENTS` named the instance's Documents for `vp64helper`. Without it the helper would reach the workstation's
  own config.ini.

Scripts and outputs: `evidence/0324/literal/`.

| Section | What was done | Result |
|---|---|---|
| 1.4 runtime check | the registry line | `v14.51.36247.00` |
| 2 DLL check | `Get-FileHash` | `262AEE88...` as the text says |
| 3.2-3.4 rename, copy, check | `Rename-Item`, `Copy-Item -Recurse`, three hashes | the three hashes of the text, in order; the folder as listed |
| 5 first start | harness launch (no focus, capped), main menu, **MODS** (no notice: this profile had accepted it; the three mods already ticked), **Next** | the proxy booted at the activation: `boot: pack ... ok: release Release-5.4.8, 32-bit DLL 262aee88... matches x86_dlls ... (checked in 89.6 ms)`, purity loaded (114 methods, 29 Lua names), `ServerRenderer = 0 (default)`, `VP64Options.ini` created, **server attached after 11.6 s**, `kRt_Boot ok`; no FALLBACK, 0 LOUD lines |
| 5 game | the instance's front end loaded the harness save `C5H-0366-TINYB_0008` by itself (its auto-load, not a document step); "Continue Your Journey" clicked | in game at turn 9, the map drawn by the EXE as stock (DIAGNOSTIC vc-20261115-154717-workstation-d492); two AI turns 9 -> 11 through the server; harness stop rc 0, server.exe ended with the EXE, leftovers clean, no report folder, config.ini unchanged (`b494c28f...`) |
| 6 off | `& "$cp\vp64helper.exe" off \| Out-Host` | `off: ok: config.ini ...: restored (none); ...vpproxy.ini: mode=off`, exit 0, config.ini unchanged |
| 6 off, played | launch, MODS, Next | `Logs\VP64\vpproxy.log`: `mode=off: the original 32-bit DLL ...CvGameCore_Expansion2_x86.dll loaded ...; no server is started`; no run folder, no server.exe; the front end loaded the turn-11 autosave that the 64-bit session had written, and the 32-bit DLL played it (turn 11 on screen); harness stop through `stop_nolua` rc 0 |
| 6 on again | the `mode=` line edited back | a line-only edit gives the shipped `vpproxy.ini` bytes again (`70f2c066`, checked on a scratch copy). This run's own edit also changed the `#` comment, which names both modes: a fault of the test's text replacement, not of the document |
| 7 uninstall | `vp64helper off`, the `Remove-Item` and `Rename-Item` lines, `VP64Options.ini` deleted | `262AEE88...` as the text says; the folder back to VP's DLL and modinfo |

**Restore.** The instance's own `3c2f47da...` DLL was copied back and checked. The six autosaves, the run folder and
`Logs\VP64\vpproxy.log` of this test were removed. `provision.py verify c5h-f` (full) ok, no error, no warning; claim
released. Other tickets' runs under `Logs\VP64` were left (the document's step 7.3 is optional).

**Corrections the test made to the text:**
- the mods are usually ticked already (step 5.2 now says "make sure ... are ticked");
- the activation shows "Configuring Game Data... Please Wait." for 1-2 minutes;
- the log lines' wording (`boot: pack ... ok: release Release-5.4.8`, then `boot: server attached`);
- where a mode=off start logs (`Logs\VP64\vpproxy.log`, no run folder);
- the helper's real output line (`off: ok: ...`);
- the Notepad edit names the `mode=` line.

### 11.3 Findings for the thin client

- **VP's installer deletes the whole `(1) Community Patch` folder before it copies its own** [C: `VPSetupData.iss` of
  Release-5.4.8, `[InstallDelete]` line 92; also `(2) Vox Populi`, the other VP folders and `My Games\...\cache`].
  Reinstalling or updating Vox Populi therefore removes every file of the patch, `VP64\` included. Only what the patch
  wrote under `My Games` stays (`VP64Options.ini`, `Logs\VP64`, `VP64\reports`). This corrects the assumption of 7,
  item 4, that a VP update leaves the patch in place and switched off: that happens only when the kept
  `CvGameCore_Expansion2_x86.dll` differs from the manifest. For epic 2600: the installer must run again after every VP
  install.
- **`vp64helper off` without a side file reset a player's own `1` in the A'' keys: fixed by issue 0378 (2026-11-15),
  see 11.4.** [L hermetic, 2026-11-15 15:28, helper vp64helper.exe `b8ac9a64...` of the pinned layout
  `x64-04b1211b-pfdee656`, scratch copy of c5h-e's config.ini with `FluidFOW = 1`]: output `restored stock:FluidFOW`, the
  key back to `0`. That was the helper's safety path of then (`release-driver.md` W3c.4: "without a usable side file it
  writes the stock values"). In the thin client, where `on` never ran, it changed a hand-set `Decal`, `Forest`, `FOW`,
  `FluidFOW`, `DistanceFog` or `YieldIcons`. With a stock config.ini it changed nothing (`restored (none)`, hash
  unchanged). The instructions told such players to use the Notepad edit (removed 2026-11-15 by 0378). *Corrected
  2026-11-15 (issue 0378): the inference about the session rule is now confirmed, 11.4; was "[I, not tested]".*
- **The helper's output reaches PowerShell when piped** [L]: `& vp64helper.exe off | Out-Host` waits for the
  GUI-subsystem helper, prints its line and sets `$LASTEXITCODE` (0). Without the pipe, PowerShell does not wait for a
  GUI program.
- **A `mode=off` start writes `Logs\VP64\vpproxy.log`** (build line, `mode=off`, the 32-bit DLL loaded, "no server is
  started") and no run folder [L].
- **A save that the 64-bit session wrote played on the 32-bit DLL** [L, one autosave, turn 11, by picture; which file
  the front end chose was not logged]. The save tests (D24, 0351) are the systematic evidence.

### 11.4 `vp64helper off` keeps the player's own values; the session rule on such a config (issue 0378, 2026-11-15)

**The fix [C, hermetic tests 2026-11-15 21:02-21:10 EST, branch task-0378-helper-off-keeps-keys].** `cfgini::Off`
(`proxy/helper/proxy_cfgini.cpp`) without a usable side file no longer writes anything into config.ini:
- **Side file missing** (`on` never ran, as in the thin client, or the file was lost): config.ini is left byte for byte.
  The helper's line says so: `off: ok: config.ini <path>: no side file <path>: config.ini left as it is, no key
  written; keys at 1 left as they are: FluidFOW (the player's own values, or the patch's if its side file was lost);
  <vpproxy.ini>: mode=off`. A stock config.ini gives the same line without the `keys at 1` part. `cfgini::Result` has a
  new `note` field for the case, and names each of our keys at 1 `left:<key>` (not the stock-off pair OverlayDebug,
  ColorKeyEdit). Exit 0, `mode=off` written as before.
- **Side file unusable** (garbage, a key missing, a key neither ours nor former): config.ini left as it is; the side file
  is moved to `vp64-config-side.ini.unusable` (kept for a report; replaces an older one), so that a later `on` no longer
  refuses; the line names the reason (`side file ... unusable (side file line 1 not understood): config.ini left as it
  is ...; the side file moved to ...`). `on`'s refusal text now says that `off` moves the file aside (was "it writes
  the stock values").
- **`failed`** (the D33 failure path): its helper.log line is `not restored: <the same text>` instead of `restored:`;
  `failed=1` is still written; the popup says "Your config.ini was left as it is: the patch's record of your own settings
  (vp64-config-side.ini) was missing or unreadable, so it changed nothing" instead of "is back to your own settings".
- With a side file nothing changed: `on` records the player's values (a key already at 1 is recorded as `1`), `off` gives
  exactly them back (byte compare).
- The trade-off: a config.ini that holds all of D33 by hand (or whose side file was lost after `on`) stays in D33 after
  `off`; the 32-bit DLL then draws the dark red map (section 8). That is now the player's to change; the off line names
  every key left at 1. Before 0378 such a file was reset to the stock values, and a player's own 1 with it.

**Tests [C, 2026-11-15].** `python -I proxy/tools/test_cfgini.py --scratch C:/c5-64bit-work/0378/cfgini --asan`
(through `quietlock.py run`): **12/12**, `cfgini_unit` **169/169** plain and under MSVC `/fsanitize=address` x86 (canary
caught), 0 skipped. New cases: the player's FluidFOW = 1 kept through `on` and `off` (side file `FluidFOW=1`, original
bytes back); `off` without a side file keeps FluidFOW = 1 byte for byte and reports `left:FluidFOW` only; a stock
config.ini without a side file (negative control: unchanged, note, no `left:`); a read-only config.ini without a side
file (`off` ok, nothing written); a corrupt side file and one missing a key (config.ini left, the file moved aside byte
for byte, `off` twice ok, `on` works again); all keys ours with the side file deleted (left at 1, never 0); the
`unknownkey` case (a `Terrain=` line) now on the same path. `python -I proxy/tools/test_vp64helper.py --scratch
C:/c5-64bit-work/0378/helper --asan`: **236/236** (clang build and the ASan build, 111 checks each under ASan, no
report), with the helper-level cases: player 1 through `on`/`off`, `off` with no side file (exit 0, bytes kept, the line
names the case and FluidFOW, no `stock:`), `off` with a corrupt side file (exit 0, `mode=off`, file moved aside, `on`
works again), `failed` with no side file and FluidFOW = 1 (exit 0, bytes kept, `failed=1`, helper.log `not restored:`,
the popup's "left as it is" line, Try again offered). **Negative control:** the same unit against main 0e43a2d0's
`proxy_cfgini.cpp`: **157/169**, the 12 new checks fail (`stock:FluidFOW` written over the player's 1, the read-only
file refused, the unusable file deleted), outputs `C:/c5-64bit-work/0378/cfgini-old.txt` (removed at hand-back).

**Not in a shipped build yet.** The staged first playable build (11.1) carries the helper of the pinned layout
`x64-04b1211b-pfdee656` (`b8ac9a64...`, repository fdee65668), which still has the old behaviour. The fix reaches players
with the next layout built from a repository commit that contains it. `docs/INSTALL-FIRST-PLAYABLE.md` section 6 now
describes the fixed helper (the Notepad workaround for a player's own 1 removed; the `no side file ... left as it is`
line named).

**The proxy's session rule on such a config: a D33 session [C code + unit 2026-11-15; L for the mask 2026-11-14].**
- config.ini `FluidFOW = 1` with every other key stock gives the EXE's config mask **0xFFEF7FF7** (bit 15, 0x8000,
  cleared) [L: `release-driver.md` W3b.4, oldpc tb2, 2026-11-14 02:34, "masks cfg = view = 0xFFEF7FF7"].
- The rule (`proxy/src/proxy_session.cpp` `Classify`): D33 when any rule bit is clear; rule bits **0x0003C090** = Decal,
  Forest, FOW, FluidFOW, DistanceFog, YieldIcons (our keys minus the stock-off pair). So a single hand-set 1 in any of
  the six makes a D33 session [C].
- `python -I proxy/tools/test_proxy_session.py --build <unused> --scratch C:/c5-64bit-work/0378/session --only unit -v`
  (2026-11-15 21:08): `session_unit` **68/68**, among them "the stock mask with any ONE rule bit cleared is a D33 session
  (each of the 6)" and "SessionKind with a test mask, FluidFOW off: D33 ... config mask 0xFFEF7FF7, rule bits 0x0003C090,
  rule keys off: FluidFOW". The loader case `fluidfow-only` (mask 0xFFEF7FF7, a bad pack) gives NO PLAY, exit 3 and
  `vp64helper failed --pid ... --config ...` (proxy-design 22.8, last run there; not rerun here: it needs a server build).
- Consequence for the thin client: a player with one of the six at 1 who meets any 64-bit failure gets the D33 path
  (the game ended, `failed`), not the boot fallback to the 32-bit game that a stock config.ini gets. With 0378 that path
  no longer changes their config.ini (the side file is missing, so `failed` leaves it and writes `failed=1`); the next
  start is the 32-bit game with their own settings. Whether such a player should rather get the boot fallback is a
  question for the session rule's owner, not changed here [I].

## 12. The installer build (issue 2602, 2026-11-16)

Worker-2602 of epic 2600, branch `task-2602-build`. Code: `tools/installer/` (`build_installer.py`, `patch64.iss`,
`notices.py`, the stand-in `install_stub.iss`), tests `tools/harness/tests/test_installer.py`; how to build, the
parameters and the exit codes: `tools/installer/README.md`. Install, update and removal are issue 2603's
`install.iss` (section 13), included inside `patch64.iss`'s `[Code]`.

### 12.1 What the build does [C: the code; L: the hermetic tests, 12.5]

`python -I tools/installer/build_installer.py --work <take> [--work43 <the -m43 take>] --out <dir> [--test-build]`
reads each work folder's `state.json` (`build` and `pack` stages must be `ok`), checks each pack against its manifest
(every named file's SHA-256, no unnamed file), the 43 folder by `"major_civs": 43`, one release and one configuration
for both, and without `--test-build` runs `take_release.py <tag> --art-check --work <dir>` (D162) before staging and
again with `--build-folder` on each staged variant. It stages `<out>/payload/std|m43/` with exactly the files that go
into `(1) Community Patch` (the lead's layout: the proxy as `CvGameCore_Expansion2.dll`, `vp64helper.exe`,
`vpproxy.ini` with 0324's shipped bytes, `VP64-LICENSE.txt`, `VP64-NOTICE.txt`, `VP64\` = the pack byte for byte, and
for i2 `shell_composite.dll` with a `shell_composite.sha256` written by `proxy/tools/shell_manifest.py`'s
`manifest_text`), writes `payload.iss` (the defines, and a Pascal procedure `P64Data` that fills the accepted 32-bit
DLLs per variant, the version table and the payload's SHA-256 list) and compiles `patch64.iss` with ISCC.

- **ISCC** is `C:/Program Files (x86)/Inno Setup 6/ISCC.exe`, SHA-256 `29856cf8...7660f` [C, 2026-11-16 22:44]; any other
  is refused (exit 4). Nothing was installed (MC #3196).
- **The notices** reuse 0324's logic (`stage_first_playable.py` `licence_texts`/`notice`), made general in
  `tools/installer/notices.py`: commits from the build stage's outputs, the Lua and zlib archives through
  `tools/x64-dll/thirdparty.py` (`deps/third-party.lock`, SHA-256 checked, offline), MinHook's licence from
  `shell/third_party/minhook/LICENSE.txt` for i2 only, and Inno Setup's own `license.txt` read from the compiler's
  folder. None of these texts is committed. The notice is also the installer's information page (`InfoBeforeFile`).
- **The patch release's name** in `VP64-Patch-<release>-for-VP<x.y.z>.exe` defaults to the standard build's layout
  name (`x64-<fork8>-p<tools8>`, as 0324 named its folder); `--release-name` sets another. `<x.y.z>` comes from the
  version table's row of the standard pack's first `x86_dlls` hash (the tag's DLL), else from the release name.
- **The configuration** (`thin` | `i2`) is the pack manifest's `shell_config` (12.4); there is no switch.
- **Exit codes of the build:** 0 built, 1 failed, 2 usage, 3 the gate refused the compile (`perf/quietlock/gate.py`
  `require` before ISCC), 4 refused.

### 12.2 The minimum Visual C++ runtime [C: PE headers and build records, 2026-11-16]

- The PE header's linker version does **not** give the toolset: every x64 file of the pinned layout
  `x64-04b1211b-pfdee656` and of the 3500 take (`server.exe`, the x64 DLL, the three libraries) says linker 14.0,
  because they are linked by `lld-link` (LLVM 19.1.7), which writes 14.0 [C, 2026-11-16 scratch `peinfo.py`].
- Their imports are `MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll` (the x64 DLL and `server.exe`) and the
  UCRT `api-ms-win-crt-*`: they link the MSVC STL of the Visual Studio toolset they were built with.
- `tools/x64-dll/build.py` records that toolset in the build's `build-summary.json` (`versions` "MSVC
  (VCToolsVersion)", e.g. `14.43.34808` in 0441's release build). So the build takes the minimum runtime from the
  `build-summary.json` under the build stage's `x64_out` (the highest of the variants; graded C in the build record) and
  the installer compares the registry's `Major`.`Minor` (`HKLM64\SOFTWARE\Microsoft\VisualStudio\14.0\VC\Runtimes\x64`,
  `Installed` = 1) with it. [I: a runtime of the same major.minor or later runs binaries of that toolset; Microsoft's
  rule for the 14.x runtime, not tested here.]
- When the record is gone (the 3500 take's `x64_out` was removed after its run), the build falls back to the build
  machine's installed runtime (here `14.51.36247`) and grades it **I** in the record. A take for a release must keep
  its `x64_out` until the installer is built.

### 12.3 Reproducible: the same work folders give the same payload and the same `.exe` [L, 2026-11-16 23:05]

- Every staged file and `notice.txt` gets the fixed time `FIXED_MTIME` = 2026-01-01 00:00:00 UTC; `[Setup]
  TimeStampsInUTC=yes`. Inno Setup stores each file's time in the setup data (the log shows "Time stamp of our file:
  2025-12-31 19:00:00.000", local time).
- Three builds of the same fixture work folders into three different `--out` folders gave the same `.exe`,
  1,700,005 bytes, SHA-256 `c90476a2...` (0 differing bytes). Two ISCC compiles of one staged folder: equal; the same
  folder with its file times touched to the present: another `.exe` (`dd835e90...`). So the compile time is not in the
  `.exe`; the file times are, and fixing them makes the build byte for byte reproducible. (The out folder's path is in
  `payload.iss` but only as a source path; it does not reach the `.exe`.)
- The hermetic test `test_build_is_reproducible` builds twice and compares the payload list, `payload.iss` (out path
  masked) and the `.exe`'s SHA-256.

### 12.4 The shell_config field (MC #3197) [C, tests 2026-11-16]

- `tools/release/descriptor.py`: optional `shell_config`, `"thin"` | `"i2"`, any other value a `DescriptorError`;
  absent = thin (`d.shell_config` None, `d.shell_configuration` "thin"). No existing descriptor has it.
- `tools/release/pack.py --shell-config thin|i2`: written into the manifest as `"shell_config"`; without the option the
  manifest has no such key. **The build's configuration** is `i2` when the layout carries the compositor
  (`shell_composite.dll`, an x86 image, in `proxy/bin/` or `composite/bin/`), else `thin`; a disagreement is refused
  (exit 1, nothing written), as are two compositors; for i2 a `shell_composite.sha256` in the build must name the
  build's proxy and compositor. `stages/pack.py` passes the descriptor's value only when it has one (inputs hash and
  outputs unchanged otherwise).
- Byte for byte unchanged: `tools/release/tests/test_take_release_shell_config.py` runs the merge base's `pack.py` and
  this one on three fake layouts (22, 43, a layout with a compositor) and compares every file; and the branch's
  `pack.py` on the inputs of the real take `C:/c5-64bit-work/3500/releases/Release-5.4.8` (layout
  `x64-99fb2c1c-p0ea6fd7`, purity, the tag's DLL) gave its recorded pack, 7 of 7 files equal, manifest `285f6da9...`
  [L, 2026-11-16 22:48].
- **Finding (fixed by issue 0464, 2026-11-17):** the build stage laid out no compositor (`stages/build.py` copied `bin/`
  and `proxy/bin/`; the compositor is built by `shell/build.cmd`, laid out by hand in evidence scripts such as 0382's
  `composite/bin/`), so a take of an `i2` release stopped at `pack`. Since 0464 the build stage builds it and lays it out
  in `proxy/bin/` (12.4a).

### 12.4a The i2 take (issue 0464) [C, hermetic tests 2026-11-17]

- **The candidate descriptor dropped `shell_config`** [C, `stages/testdata.py` `s_descriptor` before 0464]: testdata's
  candidate (`<work>/testdata/<tag>.json`, `tools/release/README.md` 1a) was written field by field and had no
  `shell_config`; every stage after testdata reads the candidate (`Driver.desc_for`), so build and pack saw none and an
  `i2` release would have been built and packed as thin with exit 0. Fixed: the candidate carries the base descriptor's
  value (absent stays absent), and `build` and `pack` fail when the run's own descriptor names a `shell_config` and the
  candidate names another or none (MC #3827: fail loud, never read the other descriptor).
- **The i2 layout:** `x64-<commit8>-p<tools7>/proxy/bin/` gets `shell_composite.dll` and `.pdb` (`shell/build.cmd` with
  `C5_SHELL_BUILD=<work>/build/shell-<tools7>`, under `quietlock.py run`, same repository commit as the server and the
  proxy), `shell_composite.sha256` (proxy + compositor, `shell_manifest.py` format), `shell_composite.ini` and
  `MinHook-LICENSE.txt`; the stage ends with `pack.check_shell_config(layout, 'i2')`. A thin take's files, inputs and
  outputs are byte for byte those of the stage before 0464 (`tools/release/tests/test_take_release_i2_compositor.py`
  runs the build stage of 63da63dc7 and the new one on the same fake inputs). No real i2 take ran (needs the i2-rc1
  pins, 0463).
- **Closed by 0490 (2026-11-17, 12.4b): the defaults now equal the ini and the installer ships it.** Kept as found:
  **The compositor's built-in defaults are not `shell_composite.ini`'s** [C, `shell/composite/shell_composite.cpp`
  `struct Config` / `ReadConfig`, 2026-11-17]: each key is read from `VPSHELL_<KEY>`, else `[composite]` of
  `shell_composite.ini` beside the DLL, else the default. Defaults vs the committed ini: `insert` clear vs `tagged`,
  `tap` 0 vs 1, `lockstep` 0 vs 1, `handover` 0 vs `ring`, `depth_write` 0 vs 1, `shadow_tap` 0 vs 1, `ice_tap` 0 vs 1
  (others equal or test-only). The proxy reads `model_fow` from the same file (`proxy/src/proxy_shell.cpp`, ticket
  0339). The installer (`build_installer.py` `stage_variant`, `install.iss`) ships `shell_composite.dll` and `.sha256`
  only, so an installed i2 patch would run the compositor in a configuration no test ran [I: not run live]. The release
  layout carries the ini since 0464; the installer does not copy it yet. The MC opened ticket 0490 for it (2026-11-17,
#3832): built-ins equal to the shipped ini with a test comparing them, the installer ships and removes the ini, a
missing ini logged.

### 12.4b The compositor's ini in the installer; one truth for its defaults (ticket 0490, 2026-11-17) [C code; L hermetic tests]

Closes the open point of 12.4a (0464's finding, MC #3830: the compositor's built-in defaults were not the shipped
`shell_composite.ini`'s, and the installer shipped only `shell_composite.dll` and `.sha256`).

- **Defaults = the shipped ini** (`shell/composite/shell_composite.cpp` `struct Config`): `insert` tagged, `tap` 1,
  `lockstep` 1, `handover` ring, `depth_write` 1, `shadow_tap` 1, `ice_tap` 1 (were clear, 0, 0, v3, 0, 0, 0). A
  compositor without its ini, or an ini missing a key, now runs the shipped configuration. The DLL logs every value it
  uses, how the ini differs from the defaults, and a missing, unreadable or sectionless ini once (CONFIG ERROR with the
  path and the Windows error); `shell/tests/test_config_defaults.py` compares the values with the shipped ini and with
  no ini and fails when either changes alone. Details: `research/05-graphics-composition/renderer-in-server-design.md`
  section 5, "The compositor's configuration has one truth".
- **`build_installer.py`:** for an i2 release `Work` takes `shell_composite.ini` from beside the build's compositor
  (`<layout>/proxy/bin/`, laid out by take_release since 0464, or `composite/bin/`) and refuses a build without it
  (exit 4, "has no shell_composite.ini beside the compositor ... a layout older than ticket 0464 cannot be installed");
  `stage_variant` copies it byte for byte into the variant folder beside `shell_composite.dll`. It is then an ordinary
  payload file: listed with its SHA-256 in `payload.iss`, given `FIXED_MTIME`, installed and recorded as `added`. A thin
  layout is unchanged (no ini copied; the thin payload test is unchanged).
- **`install.iss`:** an i2 install refuses a payload without `shell_composite.ini` (as it refuses one without the DLL or
  the manifest), before anything changes. Removal by the record deletes it like every added file; an ini the player has
  edited since the install is **left in place** with the note "left as it is (changed since the install)", like
  `vpproxy.ini` [L hermetic]. The hand-install removal (`P64RemoveHand`) deletes it too.
- **Not changed:** `shell_composite.sha256` (`proxy/tools/shell_manifest.py`) still names only the proxy and the DLL; the
  proxy does not check the ini (the player may edit it; the DLL re-reads it live).
- **Tests** [L 2026-11-17, branch task-0490-compositor-defaults]: `tools/installer/tests/test_install_removal.py`
  (i2 payload with the ini: installed byte for byte and recorded, removed; i2 without the ini refused, tree unchanged; an
  edited ini left on removal; an i2 hand install removed with its ini) and `tools/harness/tests/test_installer.py`
  (the i2 fixture's layout carries the ini; the payload has it byte for byte and the stub record lists it; an i2 build
  without it refused). Results in the ticket's log.

### 12.5 The installer: pages, checks and silent runs

Pages as 4.1: welcome (`WelcomeLabel1/2` name the release and VP; TEST BUILD marked), the notice
(`InfoBeforeFile`), *What to do* (an exclusive option page; *Remove* enabled only when `P64FindInstalled` finds a record
under the My Games folder chosen so far, or a hand install, and *Install* then reads *Update ... (found: ...)*; a check box
"also remove logs, reports and your settings (VP64Options.ini)" (ticket 0623; was "also remove the patch's logs and
reports")), My Games (Inno's destination page, `DefaultDirName={userdocs}\My
Games\Sid Meier's Civilization 5`), the game folder (VP's pre-fill: `{commonpf32}\Steam\...\Assets\DLC` then
`HKCU\SOFTWARE\Firaxis\Civilization5 LastKnownPath`; then `HKCU\Software\Valve\Steam SteamPath` and every `"path"` of
`steamapps\libraryfolders.vdf`; asked on *Remove* too, because a hand install in a modpack is found through it), the
checks (a memo page, filled when shown; Next re-runs them), ready (form, folder, version, variant, configuration, folders
written), install, finished (how to tell it runs, the off switch, the report and record folders; or the failure text).
`PrivilegesRequired=lowest`, `Uninstallable=no`, `CreateUninstallRegKey=no`, `ArchitecturesAllowed=x64`, no 64-bit
install mode (the registry check names `HKLM64` itself), `Compression=lzma`, `SolidCompression=yes`,
`CloseApplications=no`; nothing is written to the registry.

- **One check function, two callers.** `RunChecks` runs the checks of 4.2-4.5 in order (game running; then for
  *Remove*: a record or a hand install; for *Install*: the game folder, VP found exactly once, the hash, the variant,
  write access, i2's config.ini, the runtime) and is called by the checks page (interactive, with the runtime's
  Continue/Stop box and the offer to open a link) and always by `PrepareToInstall`. A silent run is decided only there.
- **Defined exit codes of a refusal** [L, the tests]: `PrepareToInstall` returning the refusal text ends a silent
  run with **exit 7** and the text in the log (`REFUSED: ...`); `InitializeSetup` returning False ends it with exit
  **1**; failures after the copy use `GetCustomSetupExitCode`: **20** install failed, **21** removal failed, **22**
  payload damaged. `NextButtonClick` returns True in silent mode, so no silent run ends on Inno's own page logic.
- **The payload** is two `[Files]` entries (`payload\std\*`, `payload\m43\*`) with `DestDir: {tmp}\payload\<variant>` and
  `Check: P64ShouldExtract(<variant>)` (true only for *Install* and the variant the hash chose): only the chosen
  variant is extracted, nothing for *Remove*. At `ssPostInstall` every extracted file is checked against the build's
  SHA-256 list (exit 22 on a mismatch), then `P64Install(MyGames, CpDir, {tmp}\payload\<variant>, variant, config,
  release)` runs; *Remove* calls `P64Remove` (a record) or `P64RemoveAt` (a hand install). Inno deletes `{tmp}` at exit.
- **The running game** is read with WMI (`WbemScripting.SWbemLocator`, `Win32_Process`, `ItemIndex`): the three game
  EXE names, and `server.exe` only when its path has a `\VP64\` folder. In test mode only processes whose path lies
  under `/P64PROCROOT` count: the workstation runs other teams' games.
- **Test mode** `/P64TEST=1` refuses to start (exit 1) without `/DIR`, `/GAMEDIR` and `/P64PROCROOT`, so a test never
  falls back on the real Documents folder; it allows `/P64RUNTIME=ok|missing|old` (the registry is then not read) and
  the stub's `/P64STUBFAIL`. A silent run that finds the runtime missing or old refuses (exit 7): no one answers the
  Continue/Stop box.
- **Not verified:** the interactive pages (no window was shown on the workstation: every run was silent on a hidden
  desktop); the Steam library scan and VP's pre-fill against a real Steam install; a running game seen without test
  mode's path filter; the write-access refusal (no test makes a folder unwritable); an install of real release files on a
  test instance and the game started from it (12.6's real-include tests use fixture files; issue 2604 does instances).

### 12.6 Tests [L, 2026-11-16, branch task-2602-build]

All through `perf/quietlock/quietlock.py run`:
- `python -I -X utf8 -m unittest discover -s tools/harness/tests -p test_installer.py -v`: **25/25** in 75 s (final run 23:3x; first 97 s), 0
  skipped (2026-11-16 23:3x). `InstallerTests` (the stub include, 19): the build's payload layout, notice and record;
  the build reproducible (`.exe` SHA-256 equal); the build's refusals (exit 4: pack stage not ok, 43 folder as `--work`
  and 22 as `--work43`, two releases, no `vp64helper.exe`, i2 without a compositor, D162 without `--test-build`,
  `--version-table` without `--test-build`, another ISCC, a pack file changed after its manifest, a non-empty
  `--out`); the toolset fallback graded I; silent runs: the mods form, the modpack form under an arbitrary DLC name,
  the 43 variant chosen by hash, both forms refused, two modpacks refused, no VP refused with the link, a known other
  release (9.9.8) and an unknown DLL refused with the link and the exact texts of 4.3, not a game folder, a running
  `CivilizationV_DX11.exe` (a copy of ping.exe, ended by its handle) refused, a `server.exe` outside `VP64` ignored and
  one in `VP64` refused, the runtime missing and old refused, test mode without `/DIR` or `/GAMEDIR` exit 1, a bad
  `/ACTION` exit 1, removal and "nothing to remove" (7), the stub's failures 20 and 21. Every refusal is checked to
  have written nothing into My Games. `InstallerI2Tests` (2): the compositor and a `shell_composite.sha256` that
  `shell_manifest.py check` accepts, MinHook in the notice; config.ini missing refused, present installed.
  `InstallerRealTests` (4, with 2603's `install.iss` and its stand-in helper built by clang): a thin install (the proxy
  in place, VP's DLL kept byte for byte, the record) and its removal (the folder back to VP's DLL alone, byte for
  byte); the stale i2 record (12.7) and its failure; a stale thin record runs no helper.
- `python -I -X utf8 -m unittest discover -s tools/release/tests` (the whole tools/release suite with the shell_config
  change): 353 tests, 352 passed and 1 failed (`test_descriptor` `test_wrong_types` lists every field and did not know
  `shell_config`); fixed in the test, then `-p "test_descriptor*.py"` **63/63** (2026-11-16 23:2x). The new
  `test_take_release_shell_config.py`: **14/14**.
- 2603's `python -I tools/installer/tests/test_install_removal.py` on this branch's final `install.iss` (with 12.7 and 12.8): **33/33** in 259 s (3 assertions changed for 12.8). `tools/harness/tests/test_safety_static.py`: 55 passed.

### 12.7 A stale i2 record: the new helper's `off` first (2603's open point 13.7; lead-2600, 2026-11-16) [C, L hermetic]

When Vox Populi's installer has deleted an i2 install's files, the record is stale and config.ini may still hold the
patch's settings with the patch's helper gone. `P64Install` now calls `P64StaleHelperOff(MyGames, PayloadDir)` first
(`install.iss`): for a stale record of configuration `i2` while `vp64-config-side.ini` is beside config.ini, it runs
the **new payload's** `vp64helper.exe off --config <My Games>\config.ini --options <{tmp}\p64-stale-off>\vpproxy.ini
--vp64-options <My Games>\VP64Options.ini` (the `--options` file a scratch copy of the payload's `vpproxy.ini`, so the
payload stays as built); exit 0 lets the install go on (`P64CleanStale` then removes the record), anything else refuses
the install with nothing installed (exit 20, "... could not put your own values back (exit N); nothing was installed").
Tested with 2603's stand-in helper: the calls are `on`, `off` (the scratch options file), `on`; the second `on`'s side
file equals the first's (the player's own config.ini was recorded, not the patch's settings); the removal gives the
original config.ini back byte for byte; `off` exiting 5 installs nothing and leaves config.ini and the folder as they
were; a stale thin record runs no helper. Not run with the real helper (13.7's second point).

### 12.8 A test build of real bytes, installed and removed (2026-11-16 23:2x) [L]

`F:/c5-64bit-work/2600/realtrial/` (scratch, removed at hand-back): a synthetic take state naming the pinned layout
`x64-04b1211b-pfdee656` (proxy `75c40cec...`, helper `b8ac9a64...`) and the pack 0324 staged from it (manifest
`79427572...`), built with `--test-build` (no `build-summary.json`, so the minimum runtime was the build machine's
`14.51.36247`, graded I).
- **Size and time:** payload 12 files, 29,423,622 B; the `.exe` **7,508,345 B** (lzma solid), built in 15.1 s and 9.4 s
  (the compile included). The two builds were byte for byte equal (`5785487e...`). A 43-civ variant beside it was not
  built (no 43 pack of this layout); [I] solid compression shares the five equal files, so it adds less than its size.
- **Silent install** (test mode, hidden desktop, a synthetic tree under `C:/c5-64bit-work/2600/w2602/realrun`, VP's
  tag DLL `262aee88...` copied from the 3500 take's inputs): exit 0; `(1) Community Patch` held the proxy
  `75c40cec...`, the kept `CvGameCore_Expansion2_x86.dll` `262aee88...`, `vp64helper.exe`, `vpproxy.ini`, the notices
  and `VP64\`; the record and the backup `backup\262aee88...`.
- **Removal failed, exit 21:** the record's removal ran `vp64helper off --config ... --options ... --vp64-options ...`
  and **the pinned helper `b8ac9a64` exited 2**: it is built from repository fdee65668, before issue 0030 added
  `--vp64-options` (commit e7230034), and its usage text is `vp64helper on|off|status [--config <config.ini>]
  [--options <vpproxy.ini>]`, exit 2 = usage [C: `git show fdee65668:proxy/helper/vp64helper.cpp`, lines 642-646].
  2603's removal refused correctly and changed nothing, but every thin install of a pre-0030 layout could not be removed.
- **Fixed** in `install.iss` `P64HelperOff` (13.4 step 3, corrected): `off` runs only when a side file is beside
  config.ini, i.e. when `on` ran (a thin install never runs `on`; without side files `off` changes neither file,
  section 9 and 11.4). 2603's tests that counted an `off` call for a thin removal and a hand-install removal now expect
  none (`tools/installer/tests/test_install_removal.py`). An i2 install still needs a helper with `--vp64-options`
  (issue 0030 or later): the i2 configuration does not exist without it.
- **After the fix** (2026-11-16 23:3x, the same inputs rebuilt): `.exe` 7,507,699 B, `7dc686d0...`, two builds equal
  (9.1 s, 10.9 s); install exit 0 as before; removal **exit 0** ("no side files ... `vp64helper off` has nothing to put
  back, skipped"), `(1) Community Patch` back to VP's DLL alone, `262aee88...` byte for byte, config.ini unchanged, My
  Games back to `MODS` and `config.ini`. The hermetic thin case now also runs the removal with a stand-in helper that
  exits 2 on `off` (as the old helper does) and expects exit 0 and no helper call.

## 13. Install, update and removal as built (issue 2603, 2026-11-16)

Written by worker-2603 (epic 2600, account B). The code is `tools/installer/install.iss`, Pascal Script for Inno
Setup 6.2.2 (the owner's installed `ISCC.exe`, SHA-256 `29856cf8...`), an include **without a section header**: the main
script (`patch64.iss`, issue 2602) `#include`s it inside its `[Code]` section. Every name starts with `P64`. It
implements section 4.6; where it goes beyond it, this section says so. [C: the code and the hermetic tests of 13.6;
nothing of it has run on a test instance or against a real release pack yet.]

### 13.1 The interface

| Function | What it does |
|---|---|
| `P64Install(MyGames, CpDir, PayloadDir, Variant, Config, Release, var Err): Boolean` | an earlier install found by its record is removed by that record first (an update), a hand install in CpDir likewise, a stale record cleaned; then the install of 13.3. `PayloadDir`'s content is exactly what goes into CpDir. Variant `std`/`m43`, Config `thin`/`i2` |
| `P64Remove(MyGames, RemoveLogs, var Err): Boolean` | removal by the record (13.4). True with a non-empty Err = removed, with notes (files left because they changed) |
| `P64RemoveAt(MyGames, CpDir, RemoveLogs, var Err): Boolean` | added for hand installs: by the record when the record names CpDir, else the hand install in CpDir (13.5) |
| `P64FindInstalled(MyGames, var CpDir, Release, Variant): Boolean` | reads the record (`record.txt`, else `record.partial`); False when there is none, when it cannot be read, or when it is stale. Read-only |
| `P64KeptDll(CpDir): String` | `<CpDir>\CvGameCore_Expansion2_x86.dll` when it and `vpproxy.ini` are in CpDir (the patch is installed there, by the installer or by hand), else `''` |
| `P64CleanStale(MyGames): Boolean` | added: removes a stale record and its backups (True when it did). `P64Install` and `P64Remove` call it themselves |

Every step goes to Inno's log with the prefix `P64:` (`/LOG=<file>` in silent runs). `P64FaultAt` is a test-only
global (13.6); the main script never sets it. `P64KeepModeOffOnUpdate = True` is the one named constant of the
proposal to the owner (13.3, update).

### 13.2 The install record

`<My Games>\VP64\install\record.txt`, UTF-8 (Inno's `SaveStringsToUTF8File`, with a BOM), one item per line, fields
separated by TAB, written through `record.txt.p64tmp` and a rename (`MoveFileExW`, replace, write through), so it is
there whole or not at all:

```
VP64-INSTALL-RECORD<TAB>1                  magic and format version
release<TAB>Release-5.4.8                  the installer's release
variant<TAB>std | m43
config<TAB>thin | i2
form<TAB>mods | modpack                    mods = CpDir is under <My Games>\MODS\
cpdir<TAB>MG\MODS\(1) Community Patch      or AB\<absolute path> (modpack form)
mygames<TAB><absolute path>                informational
<kind><TAB><H1><TAB><H2><TAB><path>        one line per step, in the order done ('-' = no hash)
end<TAB><number of step lines>             'partial' instead of 'end' in record.partial
```

Paths are written `CP\<relative to CpDir>`, `MG\<relative to My Games>` or `AB\<absolute>`, so a record still
resolves when the Documents folder moves. Step kinds:

| Kind | H1 | H2 | Meaning |
|---|---|---|---|
| `dir` | - | - | a folder the install created (removed at the end when empty) |
| `backup` | its SHA-256 | - | a backup file the install wrote: `<My Games>\VP64\install\backup\<sha256>` |
| `added` | SHA-256 placed | - | a new file (the kept `CvGameCore_Expansion2_x86.dll`, the helper, the notices, `VP64\*`, the compositor) |
| `ini` | SHA-256 placed | SHA-256 of the text without `mode=`/`failed=` lines and empty lines | `vpproxy.ini`: still recognised as ours after the off switch or the helper changed `mode=` |
| `replaced` | the original's SHA-256 (= its backup's name) | SHA-256 placed | VP's DLL replaced by the proxy, or any payload target that already existed |
| `saved` | SHA-256 before `vp64helper on`, `-` when absent | - | i2 only: `config.ini`, `VP64Options.ini`, `vp64-config-side.ini`, `vp64-options-side.ini`; used by a rollback only (a removal leaves them to `vp64helper off`) |

**The journal.** While an install runs, the same format is rewritten after every step as `record.partial` (last line
`partial<TAB>n`). An install stopped from outside (killed, power cut) therefore leaves a record that
`P64FindInstalled` reports and `P64Remove` uses; a record whose last line is missing or whose count does not match is
refused as "cut short" (tested).

### 13.3 Install (section 4.6 steps 1-6)

Checks first, nothing changed on a refusal: the folders exist; Variant, Config and Release valid; `CvGameCore_
Expansion2.dll` in CpDir; the patch not already in CpDir and no record left (P64Install removes those first); the
payload holds the proxy, `vp64helper.exe`, `vpproxy.ini` and `VP64\manifest.json` and **not** a
`CvGameCore_Expansion2_x86.dll` (VP's DLL is never shipped); i2: `shell_composite.dll` and `.sha256` in the payload and
`<My Games>\config.ini` present (else "start the game once with Vox Populi, then run this installer again", 4.5); the
manifest's `format` is 1 and VP's DLL's SHA-256 is one of its `x86_dlls`. The manifest is read by a small JSON scanner
(strings with escapes, depth-aware): only `x86_dlls[].sha256` at the right depth counts, not `x64_dll.sha256` nor a
`"sha256"` inside an origin string; each must be 64 hex digits, any case (tested).

Then, each step journaled:
1. Create `<My Games>\VP64`, `\install`, `\install\backup` as needed; back up VP's DLL and every payload target that
   already exists into `backup\<sha256>` (copied through a temporary name, checked by SHA-256; one file per content).
2. Copy VP's DLL to `CvGameCore_Expansion2_x86.dll` (temporary name, hash check, rename), then check the copy against
   the manifest's `x86_dlls`; then write the proxy as `CvGameCore_Expansion2.dll.p64tmp`, check it, and rename it over
   VP's DLL (`MoveFileExW` with replace: VP's DLL is never missing, it is the kept copy or the backup at every moment).
3. Create the payload's folders (`VP64\`), copy every other payload file the same way (temporary name, hash, rename).
4. i2: the four `saved` files, each backed up when present.
5. The record, last (13.2); `record.partial` is deleted.
6. i2: `vp64helper.exe on --config "<My Games>\config.ini" --options "<CpDir>\vpproxy.ini" --vp64-options
   "<My Games>\VP64Options.ini"` (all three named, so `C5_DOCUMENTS` and `VP64_OPTIONS` never matter), hidden window,
   waited for; exit 0 required.
Any exception in 1-6 (a refused copy, a read-only target, a hash mismatch, the helper's non-zero exit) runs the
**rollback**: the journal in reverse; replaced files back from their backups (checked by hash), added files deleted
when they still hold what was placed, i2 `saved` files put back when they differ (so a helper that crashed after
changing config.ini is undone), side files the helper created deleted; then the backups, the record files and the
created folders. If the rollback itself cannot put something back, the record and the backups stay for another
attempt and Err says so.

**Update.** `P64Install` finds an earlier install by its record (`P64FindInstalled`), reads its `vpproxy.ini` `mode=`,
removes it with `P64Remove` (including `vp64helper off`), then installs. A hand install in CpDir is removed with the
hand-install removal first. With `P64KeepModeOffOnUpdate = True` (the proposal to the owner in 4.6) a player's `mode=off` is
written into the new `vpproxy.ini` (its `mode=` line only, line ends kept), and for i2 `vp64helper on` is not run, so
the switch stays off as a whole; the player switches on later. An update whose new install fails leaves Vox Populi
without the patch (VP's DLL restored, byte for byte), never half installed.

Not done, as 4.6 says: no clearing of the `cache` folder; nothing in saves, the EXE, Steam's files or VP's other files;
no junction or symlink is created; a payload holding a reparse point is refused; no administrator rights. Files copied
keep their source's attributes (`CopyFile`); the installer clears the read-only flag only on files it wrote itself
before deleting them.

### 13.4 Removal by the record

1. Read the record (`record.txt`, else `record.partial`). Stale (13.5): only the record and backups are removed.
2. **Before anything changes**, every backup that a restore will need (the replaced file still holds what the install
   put there, or is gone) must exist and hash to its name; else refuse: "the backup ... is missing / damaged ...;
   nothing was changed" (tested: the helper is not even run).
3. `vp64helper off` with the same three explicit options, but only when a side file (`vp64-config-side.ini`,
   `vp64-options-side.ini`) is beside config.ini (*corrected 2026-11-16 by issue 2602, 12.8: was "always"; without side
   files `off` has nothing to put back, and a helper older than issue 0030 refuses `--vp64-options` with exit 2*), and
   only when the helper in CpDir still hashes to the recorded SHA-256. Exit not 0: refused, nothing else changed. Helper missing or changed: refused when a side file
   (`vp64-config-side.ini`, `vp64-options-side.ini`) is beside config.ini (the patch's settings would stay in
   config.ini: the red map of section 8), else skipped with a log line.
4. The journal in reverse: replaced files back from their backups (temporary name, hash check, rename over the proxy;
   VP's DLL under its own name, byte for byte, before the kept copy is deleted); a replaced file that holds neither the
   original nor what the install put there is **left and named** in Err, and its backup and the record are kept;
   added files deleted when their hash matches (`vpproxy.ini` also when only `mode=`/`failed=` changed), else left and
   named; then the backups, `record.txt`, and the created folders when empty.
5. `VP64Options.ini`, `Logs\VP64`, the compositor's files in `Logs` and `VP64\reports` stay unless RemoveLogs; with
   it they are deleted file by file, never following a reparse point. **Corrected 2026-11-20 (ticket 0623; MC #8718,
   #8776)** [C code and hermetic test; L for the gap it closes]: the compositor writes its log, its rotated logs and its
   records into `Logs` itself, outside `Logs\VP64` (`VPSHELL_OUT_DIR = <My Games>\...\Logs` in both forms since 0623,
   `research/02-exe-internals/new-exe-build-26577625.md` C.7.4 A), and no removal deleted them: one run left
   `Logs\shell_composite.log`, an instance used for days 37 more (`shell_composite.<stamp>.log`,
   `shell_composite.before-*.log`, one `.csv`; C.7.6). `P64DelCompositeLogs` now deletes, with RemoveLogs, every file
   of `Logs` whose whole name is `shell_composite.` + anything + `.log` or `.csv` (any case; the name is checked in the
   code, since FindFirst also matches 8.3 short names); never a folder, never a reparse point, never another file: the
   game's own logs (`APP.log`, `Lua.log`, ...) and the player's look-alikes (`my_shell_composite.log`,
   `shell_composite_notes.log`, `shell_composite.log.bak`, a `shell_composite.old\` folder) stay byte for byte
   (`tools/installer/tests/test_install_removal.py` `test_remove_logs_takes_the_compositors_files_in_logs_only`).
   **The box names the options file**: RemoveLogs deletes `VP64Options.ini` (the player's settings for the patch) by
   design, which the caption did not say (found by 0623's run on c5h-d, where a file that predated the install went);
   the caption is now "When removing: also remove logs, reports and your settings (VP64Options.ini)". Its width
   [C, measured 2026-11-20]: the box is the page surface's width, 417 px at 100 %, Tahoma 8 (logged by a throwaway
   installer with patch64.iss's page settings, WizardStyle modern); the text room is about 399 px after the check glyph.
   The MC's first wording ("... the patch's logs, reports and your settings file (VP64Options.ini)", #8827) measured
   451 px there (113 %, clipped); this caption 378 px (Inno) / 377 px (GDI), 94 %; at 150 % (GDI at 144 DPI) 561 of
   599 px, 94 %; the old caption 69 %. `tools/harness/tests/test_installer.py`
   `RemoveLogsCaptionTest` checks it, with the old caption as its negative. Was: `VP64Options.ini`, `Logs\VP64` and
   `VP64\reports` (the compositor's files in `Logs` not named).

### 13.5 Hand installs and stale records

**Hand install** (`INSTALL-FIRST-PLAYABLE.md` 3: VP's DLL renamed to `CvGameCore_Expansion2_x86.dll`, the patch's files
copied, no record): recognised by `P64KeptDll(CpDir)`; `P64RemoveAt` runs `vp64helper off` (no hash to check; refused
as above when it fails), copies the kept DLL over the proxy (temporary name, hash check, rename), deletes the kept
copy, `vp64helper.exe`, `vpproxy.ini`, `shell_composite.*`, `VP64-LICENSE.txt`, `VP64-NOTICE.txt` and the files of
`VP64\` (subfolders and links left and named). Whether the kept DLL is the manifest's is logged, not required (the main
script's version table check, 4.3, decides what it offers). **Stale record**: the record's CpDir is gone, or none of its
added files exists and no replaced file holds what the install put there (VP's installer deleted `(1) Community Patch`,
11.3). `P64FindInstalled` reports it as not installed; `P64Install`, `P64Remove` and `P64CleanStale` delete the record,
its backups and its empty folders. For i2, if side files are still beside config.ini, a note says that config.ini may
still hold the patch's settings and the helper is gone (open point, 13.7).

### 13.6 Tests

`python -I tools/installer/tests/test_install_removal.py` (unittest; started through `quietlock.py run`). It compiles
`tools/installer/tests/p64test.iss` (a test-only Inno script that `#include`s `../install.iss` and calls the routines
from `/ACTION=install|remove|removeat|find|kept|clean` parameters in a `/VERYSILENT` run, result in a `/RESULT=` file,
exit code 0 or 10) with the pinned ISCC (hash checked), and builds the stand-in helper `standin_vp64helper.c` with LLVM
clang 19.1.7 (it takes the real helper's `on`/`off` command line, requires all three paths, records config.ini and
VP64Options.ini in side files and puts them back, writes `mode=`; environment switches make it refuse, or crash after
changing config.ini). Each test builds synthetic trees under `C:/c5-64bit-work/2600/w2603/tests/` (removed at the end):
a My Games folder under a user name with non-ASCII letters (`Tést Üser`), a game folder, VP in the mods or the modpack
form with a fake VP DLL, saves, config.ini, `ModUserData`, `cache`; and payloads with a fake proxy, the stand-in
helper, `vpproxy.ini`, the notices and a `VP64\` whose manifest lists the fake DLL. Every comparison is of every file
of the whole tree by SHA-256 (and read-only flag) and every folder. Nothing of the game or VP is used (D83); the
driver refuses a path outside `C:/c5-64bit-work` or `F:/c5-64bit-work`.

**Results [C, hermetic, 2026-11-16 23:04-23:09 EST, branch task-2603-installer-install, through `quietlock.py
run`]: 33/33 in 268 s, 0 skipped.** What they show:
- install then removal gives every file and folder of the whole tree back by SHA-256, mods form and modpack form, thin
  and i2; the installed layout is checked file by file against the payload, the kept DLL is VP's bytes, the backup
  folder holds exactly VP's DLL, `record.partial` and `*.p64tmp` are gone, `P64FindInstalled` and `P64KeptDll` give the
  install back (the non-ASCII path round-trips through the UTF-8 record); the saves are unchanged after every install;
- i2: the helper is called exactly `on | --config <MG>\config.ini | --options <CP>\vpproxy.ini | --vp64-options
  <MG>\VP64Options.ini` at install and `off` with the same at removal; thin: only `off`, at removal; with an existing
  VP64Options.ini too; i2 refused without config.ini or without the compositor, nothing changed;
- **a fault injected at every step** (`/FAULTAT=k`, k = 1, 2, ... until the install finishes) rolls back to the original
  tree each time: 19 fault points for a thin install (mods), every one for the modpack form, 27 for i2, including faults
  after a successful `vp64helper on` (config.ini, VP64Options.ini and the side files put back);
- a read-only VP DLL (the proxy cannot be renamed over it) rolls back, the read-only flag kept; the helper refusing `on`
  (exit 1) or crashing after it changed config.ini (exit 9) rolls back to the original tree;
- a damaged backup (one bit) and a missing backup each make removal refuse with the tree unchanged and the helper not
  run; with the backup back, removal gives the original tree; `vp64helper off` failing, or the helper missing while side
  files exist, refuse with the tree unchanged;
- update: twice the same release; release 1 to release 2 (other proxy and pack bytes) with mode on and with the
  player's `mode=off` (kept, i2: `on` not run, config.ini stays the player's, helper calls `on, off, off`); then removal
  gives the original tree;
- a hand install (INSTALL-FIRST-PLAYABLE 3) is found by `P64KeptDll`, not by `P64FindInstalled`, and `P64RemoveAt`
  gives the original tree; an install over a hand install, then removal, too;
- stale record (the whole `(1) Community Patch` replaced by VP's files): not found; `P64Remove` cleans it and says the
  files were already gone, original tree; an install over a stale record works and is removable;
- `record.partial` (an interrupted install) is found and removable; a cut record is refused with the tree unchanged;
- a changed `VP64\server.exe` and a `vpproxy.ini` edited beyond `mode=` are left and named, VP's DLL still restored;
- RemoveLogs off keeps VP64Options.ini, `Logs\VP64` and `VP64\reports` byte for byte; on gives the original tree (the
  game's own `Logs\Lua.log` untouched);
- the manifest scanner: VP's hash only as `x64_dll.sha256` or inside an origin string is refused; compact JSON with the
  entry second, upper-case hex and escapes is accepted; format 2, an empty list, a short hash, unbalanced brackets and
  an unterminated string are each refused with their reason; a payload holding `CvGameCore_Expansion2_x86.dll` is
  refused.

**Negative control** (`python -I tools/installer/tests/negative_control.py`, 2026-11-16 23:09, through `quietlock.py
run`): five mutations of `install.iss` compiled into the driver through `/DP64Include=` (no rollback of the `saved`
files, no hash check of backups before removal, no check of VP's DLL against the manifest, no `mode=`-blind ini hash,
no rollback at all), each with its tests: **5/5 caught** (each a test failure, not an error).
`python -I -m pytest tools/harness/tests/test_safety_static.py`: 55 passed (2026-11-16 23:01).
First run (22:58-23:02): 30/32; two faults of the tests themselves (a hand edit of `vpproxy.ini` that also changed a
comment line, so the ini was correctly left as changed; a `Logs` folder the fixture lacked), fixed.

### 13.7 Open points

- **Stale i2 record.** After VP's installer deleted the patch's files, an i2 install's config.ini may still hold the
  patch's settings (the red map, section 8) and the helper is gone. The cleanup only names it. The next install puts a
  new helper in place and its `on` keeps the original values in the old side file, so a later `off` restores them
  [I: from the helper's contract, section 9; not tested with the real helper]. Whether the installer should run the new
  payload's helper `off` before the install in that case is for the lead. *Answered 2026-11-16 by lead-2600, done by
  issue 2602: `P64StaleHelperOff` runs the new payload's helper `off` first (12.7).*
- **The real helper** (`proxy/helper/vp64helper.cpp`, issue 0030) is not run by these tests: only the stand-in. The
  command line is the contract of section 9, checked against the source's usage line (`on|off|status [--config]
  [--options] [--vp64-options]`) [C], not by a run.
- **Long paths.** No path over 260 characters was tried; Inno's file functions and `MoveFileExW` are not given the
  `\\?\` prefix.
- **record.json.** 4.6 named the record `record.json`; it is `record.txt` in a line format (the lead's brief asked for a
  simple line format; corrected in 4.6).

## 14. Proof on test instances (issue 2604, 2026-11-16/17)

Written by worker-2604 (epic 2600, account B), on branch `task-2604-proof`. Instances are Goldberg copies (`c5h-*`,
claimed through `tools/harness/pool.py`), never the owner's install (D26). Asset set (D94): the instances are copies of
pure_vp, whose game files carry Vibrant Textures like every install the project tests on; no picture of this section
compares colours.

### 14.1 How a VP modpack is made, and the 5.4.8 modpack of the profile `modpack548` [C source; L 2026-11-17 00:00-00:06 EST, c5h-d]

**What a modpack is** [C: `(5) Modpack Maker for VP/ModpackMaker.lua` and `CvGame.cpp` 14539-14960 at `Release-5.4.8`,
`git show`, read 2026-11-16]. A modpack is not shipped by VP. A player makes it with the `(5) Modpack Maker for VP` mod
(VP's installer puts it in `MODS` for every component): enable it with the mods, load a game, press Ctrl-Shift-M.
The Lua calls six DLL functions (`Game.DeleteMPMP`, `CreateMPMP`, `CopyModDataToMPMP`, `AddUIAddinToMPMP`, `WriteMPMP`)
that write, relative to the EXE's working folder (the game folder):
- `Assets\DLC\VP_MODPACK\MPModsPack.Civ5Pkg`: a fixed package file (GUID `{b5932ae4-...}`, priority 300, its
  `GameplaySkin` directories `Mods` and `UI`; the same text as pure_vp's `MP_AUTOPLAY_VP_5_4_7`).
- `UI\InGame.lua`, `UI\CityView.lua`, `UI\LeaderHeadRoot.lua` copied from `Assets\DLC\Expansion2\UI\...`; then every
  file of an activated mod named `InGame.lua`, `CityView.lua`, `LeaderHeadRoot.lua`, `MiniMapPanel.lua` or
  `MapGenerator.lua` is copied over them; then one line `g_uiAddins[#g_uiAddins + 1] = "<addin>";` per activated
  addin entry point is appended (an "unmodded" game has no entry points of its own).
- `Override\`: an empty file for every XML under `Assets` that holds `<GameData>` (587 files for this install), so the
  base game's database files load as nothing; `Override\CIV5Units.xml`: **the whole running database**, every table
  (schema and rows, except 7 engine tables and 21 schemas that exist before XML loads) as XML (~23.6 MB);
  `Override\CIV5Units_Mongol.xml`: the text tables `Language_<lang>` of ten languages (~7.3 MB).
- `Mods\<mod name>\`: each activated mod's folder, copied by `CopyModDataToMPMP` **from `<Documents>\My Games\Sid
  Meier's Civilization 5\MODS`, where `<Documents>` is read from `HKCU\...\Explorer\Shell Folders` `Personal`**
  (`RegQueryValueExA`, the user's real Documents folder, as for a player). The Modpack Maker leaves itself out.
So a modpack is a snapshot of a running game's database: it **cannot be made from repository files alone**. And on a
test instance the stock Ctrl-Shift-M would copy from the real user's `MODS` (the harness shim redirects the EXE's
Documents folder, not this registry read: `headless-and-parallel-testing.md` 4.7, row 5 of 4.8, "optional", never
built). For the installer this means: a player's modpack is made from the player's own MODS folder, so the
installer's modpack check (4.3) meets exactly such a folder; the DLL inside is the one in the player's MODS at the time.

**How the profile's modpack was made** (MC #3196 and its three additions via lead-2600, 2026-11-16 23:5x). Script
`research/06-tooling-and-workflows/evidence/2600/make_modpack548.py`, on c5h-d (mods548, claimed by worker-2604,
ticket 2600):
1. `check-mods`: the instance's `(1)`, `(2)`, `(3a)` against `git archive Release-5.4.8` filtered by
   `VPSetupData.iss`'s FullEUI excludes [L]: **every file of the instance is the tag's byte for byte**, except the DLL
   (the instance's is the pin's instance-aware build `3c2f47da...`) and the three `.modinfo`, which differ in line
   ends only (*corrected 2026-11-25, ticket 0766: the installed `.modinfo` are the tag's blobs, LF; the CRLF came from
   `git archive` through a clone with `core.autocrlf=true`; every other installed file is the CRLF checkout form;
   headless-and-parallel-testing.md 6.5. Replaced: "git gives LF, VP's installer CRLF"*). 14 repository files are not in the installed form (`Credits.txt`,
   `Kit/readme.md`, `SampleContracts.xml`, `SampleEvents.xml` of `(1)`; ten `.xcf` atlases of `(2)`): VP's installer
   packs a ModBuddy `Build\` folder, not the repository folder [C: `Source: "Build\(1) Community Patch\*"`].
   So the instance's MODS are VP 5.4.8 as installed, and they are the copy source.
2. `install-mm`: the tag's `(5) Modpack Maker for VP` into the instance's MODS (not activated).
3. `frontend.py prepare --mods cp,vp,eui --load HI0423-TINY_0008`, `cli.py launch` (frame cap, guard and start-up watch
   ok), in game at 00:01.
4. `base`: `Game.CreateMPMP()` through the Lua channel (state InGame): `true`, 4.5 s; the package, three UI files and
   587 empty overrides in `F:\c5-instances\c5h-d\install\Assets\DLC\VP_MODPACK` (the Steam install's `Assets\DLC`
   listed before and after: unchanged).
5. `copy` (the one step of our own, replacing `CopyModDataToMPMP`): `Modding.GetActivatedMods()` = `(1) Community
   Patch` v151, `(2) Vox Populi` v17, `(3a) VP - EUI Compatibility Files` v1; each copied from the instance's MODS into
   `Mods\<name>\`, walking folders in the file system's order as the DLL does; UI entry files taken: `MapGenerator.lua`,
   `InGame.lua`, `MiniMapPanel.lua` from `(1)`, `CityView.lua`, `LeaderHeadRoot.lua` from `(3a)`.
6. `fill`: the instance's `ModpackMaker.lua` (`2db5cc39...`) read at run time up to its entry point, sent through the
   channel with `Game` shadowed so that `CopyModDataToMPMP` answers true, then its own `CopyActivatedMods` (now the
   addins only), `CopyFullDatabase`, `CopyTextDatabase`: 35.2 s in the game, 868 printed lines; the only warnings are
   the Modpack Maker's own "<lang> is not a loaded language database" for the nine languages an English game does not
   load (a player gets the same); `CIV5Units.xml` 23,580,709 B, `CIV5Units_Mongol.xml` 7,277,953 B (pure_vp's 5.4.7
   modpack: 23,580,705 and 7,282,058). `InGame.lua` ends with the addin lines (`RandomVCPopup`, `VassalageOverview`, ...).
7. The harness stop (human-turn hook), `frontend.py restore`, `harvest`: the folder moved to F:, **the tag's DLL
   `262aee88...` put into `Mods\(1) Community Patch\`** (the copy held the instance-aware DLL), the manifest written;
   `cleanup`: the Modpack Maker and `ModpackMaker.log` removed. `provision.py verify c5h-d --quick`: ok, no error.
Nothing of VP's code or files is committed (D83): the Lua is read from the instance at run time, the modpack stays on F:.

**The result** (kept for the profile; never committed):
`F:\c5-64bit-work\2600\modpack548\VP_MODPACK` (2,724 files, 288,147,018 B) and its manifest
`F:\c5-64bit-work\2600\modpack548\VP_MODPACK.manifest.tsv` (one row `sha256 TAB size TAB path` per file), SHA-256
**`0d4b5804f2063cba9f8857d39a9b5ebc6ea029667b5065c957f39ce94c0adcb6`**, made 2026-11-17 00:05 EST, pinned in
`tools/instances/provision.py` `VP548_MODPACK_MANIFEST_SHA256`.

**The owner's Documents untouched** (MC's condition 1) [L]: `evidence/2600/artdocs_listing.py list` before (2026-11-16 23:5x)
and after (2026-11-17 00:06) the generation, of the real `My Games\Sid Meier's Civilization 5` (names, sizes and
SHA-256 of every file; `Saves` and `ModdedSaves` by name and size only, their content not read): 14,031 files and 511
folders each time, **identical** (`compare`: 14,542 rows equal; the listing file's SHA-256 `34a9212c...`). The listings
stay on `F:\c5-64bit-work\2600\w2604\` (they hold the user's file names; not committed).

**The profile** `modpack548` (`tools/instances/README.md`, "VP 5.4.8 in the modpack form"): pure_vp without its 5.4.7
modpack, this modpack as `install\Assets\DLC\VP_MODPACK`, checked file by file against the pinned manifest before
anything is written, the VP installer's two other 5.4.8 files as in mods548, no mods in MODS; the DLL target is the
modpack's `(1)` DLL. Tests [L, 2026-11-17 00:1x, through `quietlock.py run`]: `test_provision.py` **115/115** (5 new in
`Modpack548Test`: the existing profiles' dicts unchanged and a mods548 create without a modpack; the modpack laid and
`verify` ok, a changed file caught; a manifest not the pinned one, an extra file, a changed file, a DLL not the tag's
and a missing source each refused with nothing written); `test_safety_static.py` 55 passed.

### 14.1a The VP 5.5 modpack and the profile `modpack55` (ticket 0779, D259) [L, 2026-11-26 07:14-07:25 EST, workstation]

No 5.4.8 save loads on a 5.5 build (`headless-and-parallel-testing.md` 6.5), so the modpack tier stage needs a 5.5
modpack-form instance and its own turn-19 save. The modpack was made as in 14.1, with
`research/06-tooling-and-workflows/evidence/2600/make_modpack55.py` (the 5.4.8 script adapted). What differs:
- **The source of truth is the Release-5.5 release tree** (`C:/c5-64bit-work/keep/vp-release/vp-Release-5.5`,
  manifest `bc4c091a...`, checked file by file by `provision.vp_tree_sources`), not a `git archive` of the tag: the tree
  is already the installed form (CRLF where the installer has it), so `check-mods` compares byte for byte, with no
  line-end allowance. `(5) Modpack Maker for VP` comes from the tree; its `ModpackMaker.lua` is the 5.4.8 one byte for
  byte (`2db5cc39...`), its modinfo `6c77bbeb...`.
- **The instance**: c5h-55-c (C:, profile mods55, build 26577625) with the Release-5.5 -ia DLL `63e997c1...` (the Lua
  channel); save REL55-TINY_0008 loaded with `--mods cp,vp,eui`.
- The tag DLL put into the result is the tree's `fd2fc01f...` (VP 5.5's own).

The run [L]: `check-mods` **OK**: `(1)` 755 files, `(2)` 1,350, `(3a)` 45, every one the tree's byte for byte (the DLL
aside). `install-mm`, `frontend.py prepare --mods cp,vp,eui --load REL55-TINY_0008`, `cli.py launch` (hidden, 30 fps
cap, guard installed, start-up watch ok), in game at 07:17:07 (one dismiss). `base`: `Game.CreateMPMP()` true in 4.2 s;
the package, three UI files and **587 empty overrides** (as on 5.4.8). `copy`: `Modding.GetActivatedMods()` = CP v152,
VP v17, `(3a)` v1; UI entry files `MapGenerator.lua`, `InGame.lua`, `MiniMapPanel.lua` from `(1)`, `CityView.lua`,
`LeaderHeadRoot.lua` from `(3a)` (the same five sources as 5.4.8). `fill`: 24.4 s, 869 printed lines, no warning but the
nine "not a loaded language database"; `CIV5Units.xml` **23,847,861 B** (5.4.8: 23,580,709), `CIV5Units_Mongol.xml`
**7,307,006 B** (5.4.8: 7,277,953); `InGame.lua` ends with the addin lines (`RandomVCPopup`, `VassalageOverview`, ...).
The harness stop (front end restored by hash), `harvest`, `cleanup` (the Modpack Maker and `ModpackMaker.log` removed,
no `VP_MODPACK` left in the install), `provision.py verify c5h-55-c --quick` ok, no error, no warning. Logs:
`tools/instances/evidence/0779/mm/`.

**The result** (kept for the profile; never committed, D83): `C:\c5-64bit-work\keep\vp-release\modpack55\VP_MODPACK`
(2,743 files, 284,183,552 B; README beside it) and its manifest `VP_MODPACK.manifest.tsv`, SHA-256
**`26dbf95e6795091d9dcd2fb1ae8b6c956ede6302e537c9534fd618cd3d801372`**, pinned in `tools/instances/provision.py`
`VP55_MODPACK_MANIFEST_SHA256`. The owner's Documents were not read (the Modpack Maker's own `CopyModDataToMPMP`, which
reads the registry's Documents folder, is replaced by `copy` from the instance's MODS, as in 14.1).

**The profile** `modpack55` (`tools/instances/README.md`, "VP 5.5 in the modpack form"): pure_vp without its 5.4.7
modpack; the Release-5.5 tree's game files (UI_bc1 and VPUI replaced whole, Expansion2.Civ5Pkg,
MinorCivSounds_VoxPopuli.xml) and `Text\VPUI_tips_en_us.xml` as in mods55; no mods in MODS; this modpack as
`install\Assets\DLC\VP_MODPACK`, checked against the pinned manifest before anything is written (`modpack_pin` names
each modpack profile's folder, pin and tag DLL; the 5.4.8 record is unchanged); the DLL only a Release-5.5 build.
Tests [C, 2026-11-26]: `test_provision.py` **154/154** (5 new in `Modpack55Test`: the profile and the others; the tree
and the modpack laid, `verify` ok, a changed file caught; a Release-5.5 -ia build taken and the 5.4.8 DLL refused by
`create` and `set-dll`; a manifest not the pinned one, an extra file, a changed file and a missing source refused with
nothing written; a modpack holding another DLL and an unpinned manifest refused). The instance and its turn-19 save:
`headless-and-parallel-testing.md` 6.5 ("The 5.5 modpack-form T19 save").

### 14.2 The EXE keeps a config.ini that holds only `[DisableSystems]` (MC #3193 (a)) [L, 2026-11-17 00:06-00:09 EST, c5h-d]

Script `evidence/2600/configini_first_start.py`. c5h-d's config.ini (`2fe27897...`, 8,490 B, ten sections) was backed
up by SHA-256 and replaced by the `[DisableSystems]` section alone: its 24 keys in the EXE's order, `Key = value`,
CRLF, with two values the stock file does not have (`Decal = 1`, `FluidFOW = 1`) and the stock pair `OverlayDebug = 1`,
`ColorKeyEdit = 1` (352 B, `e36ba42c...`). Then `cli.py launch` (no front-end patch, no save), the main menu reached
(front end idle, cache quiet), the harness stop at the main menu (terminate, the 0411 path).
**Result: the file was kept byte for byte** (`e36ba42c...`, 352 B after the start and the stop): the EXE added no
section and changed no value; it started with the other nine sections missing. Restored by hash (`2fe27897...`);
`provision.py verify c5h-d --quick` ok.
- So the installer may write a config.ini of that section alone when the player has none (4.5, i2), backed up and
  removed by hash like every other file (MC #3193 (a)).
- **Not shown:** an exit through the game's own menu (the harness stop terminates at the main menu; whether a clean exit
  writes the missing sections is not known), and a Documents folder with nothing else in it (c5h-d's has `cache`,
  `Logs`, the other ini files). That the EXE applies the values it read is not checked here (a D33 mask reading needs
  the proxy's log; `release-driver.md` W3b.4 reads masks from a full file) [I: it reads them, as from a full file].

### 14.3 HI release journeys on the installer-installed layout (MC #3211) [C code; L hermetic 2026-11-16 23:5x]

`tools/release/stages/hi_journey.py`: `C5_RELEASE_HI_INSTALLED=<instance>` (README 5a). It claims that instance only,
reads the install record (13.2) for the form and the Community Patch folder, and refuses (stage failed, nothing played)
unless the proxy DLL there, `VP64\CvGameCore_Expansion2.dll` and `VP64\server.exe` have the build's SHA-256 (the
record finished, `CvGameCore_Expansion2_x86.dll` and `vpproxy.ini` present). It never runs `provision.py set-dll` or
`set-proxy`, makes no test pack and needs no instance-aware DLL; the product route is `mode=on` on the installed
`pack=1` layout, the stock route `mode=off` (the proxy loads VP's own DLL, the installed fallback). `vpproxy.ini`, the
settings and the saves come back by hash; the restore re-checks the layout by hash instead of `provision.py verify`
(an installed instance is not what its provisioning manifest lists). A modpack-form install is skipped: the journeys
are recorded with `ModdedSaves` and the MODS menu. The report's title and verdict, each journey heading and the D162
line say "on the installer-installed layout (<instance>)". Unset, nothing changes, except that the default path now
skips an instance holding an install record instead of laying its own layout over it. Tests: `test_hi_journey.py`
**60/60** (8 new in class `Installed`; the 52 others unchanged and passing), through `quietlock.py run`.

### 14.4 The mods form on c5h-d: install, boot, refusal while running, removal [L, 2026-11-17 00:14-00:21 EST]

Installer: `build_installer.py --work <take> --test-build` (through `quietlock.py run`) from a copy of issue 4506's work
folder `F:\c5-64bit-work\0392\4506\rel` (state and inputs only; its build stage names the pinned layout
`x64-04b1211b-pfdee656`, its pack stage 0324's staged pack, manifest `79427572...`): `F:\c5-64bit-work\2600\w2604\
inst-std\VP64-Patch-x64-04b1211b-pfdee656-for-VP5.4.8-TEST.exe`, 7,507,699 B, SHA-256 **`7dc686d0...`**, the same bytes
worker A's build of the same inputs gave (12.8): the build is reproducible across two builders and worktrees.
Runs: `evidence/2600/run_installer.py` (silent, on a hidden desktop of its own, `/DIR` and `/GAMEDIR` always the
instance's). Instance c5h-d (F:, mods548), the player's state made first: `provision.py set-dll c5h-d --dll <the tag's
DLL 262aee88...>` (restored with `--previous` at the end). Evidence `evidence/2600/mods-c5h-d/`.

1. **Install, without test mode** (the running-game check sees every process on the machine; no game ran at 00:16 by
   `pool.py list`): exit 0. The log: "Civilization V is not running", VP found in the mods form with `262aee88...`,
   "Vox Populi 5.4.8: the standard variant (22 major civilizations)", the real runtime check "Microsoft Visual C++ x64
   runtime 14.51.36247 (at least 14.51.36247 is needed)", `install ... (std, thin, mods form)`. The tree of the
   instance's My Games after it differs from before by exactly: the proxy `75c40cec...` in place of VP's DLL, VP's DLL
   kept as `CvGameCore_Expansion2_x86.dll` (`262aee88...`), `VP64-LICENSE.txt`, `VP64-NOTICE.txt`, `vp64helper.exe`
   (`b8ac9a64...`), `vpproxy.ini` (`f80d7590...`, 314 B), `VP64\` with the seven pack files (the hashes of 11.1), and
   `VP64\install\record.txt` with `backup\262aee88...`. Every hash equals the staged first playable build's (11.1).
2. **Removal at once**: exit 0; **the whole My Games tree (2,545 files, 354 folders) is identical by SHA-256 to the
   tree before the install**, folders included (`tree-diffs.txt`).
3. **Install again and the game on the 64-bit path**: exit 0; `frontend.py prepare --mods cp,vp,eui --load
   HI0423-TINY_0008`, the harness launch (frame cap, guard, start-up watch ok), in game; the proxy's log
   (`Logs\VP64\run\20261117-001816-...\vpproxy.log`): `boot: pack ...\VP64 ok: release Release-5.4.8, 32-bit DLL
   262aee88... matches x86_dlls`, `boot: server attached, generation 1 ... (waited 873.6 ms)`, `kRt_Boot ok (38.6 ms)`;
   0 FALLBACK and 0 LOUD lines; two `server.exe` in Task Manager (this one and another team's). Four turns ended through
   the harness (turn 9 to 13, the AI turns through the server, 3-4 s each). Harness stop, no leftovers. The 32-bit
   DLL in place was VP's own: **the product route ran the harness (Lua channel, turns) without the instance-aware
   32-bit DLL** [L] (as 0324's literal test found, 11.2).
4. **Refused while the game runs, without test mode**: a removal started during that game: exit 7, "REFUSED:
   Civilization V is running (CivilizationV_DX11.exe (process 66672)). Close the game, then click Next again.", nothing
   changed.

### 14.5 Both D162 HI release journeys on the installer-installed layout [L, 2026-11-17 00:21-01:00 EST, c5h-d]

`C5_RELEASE_HI_INSTALLED=c5h-d take_release.py Release-5.4.8 --tier validated --only hi_journey --work <the copy of
4506's work folder>` (claim ticket 2600, through `quietlock.py run`), on the install of 14.4 step 3. Report
`evidence/2600/mods-c5h-d/5-hi-journeys-report.md` (and `.json`).

| Journey | Product route (mode=on, the installed layout) | Stock route (mode=off: the proxy loads VP's own `262aee88...`) |
|---|---|---|
| release-basic | **exit 0**, 63 steps, checkpoints founded, research, turn5, turn10, loaded, menu all ok, 566 s | exit 5 at step 8: the wait `judge in_game` reached its 60 s deadline |
| release-large | **exit 0**, 21 steps, checkpoints moved, attacked, city, camera, menu all ok, 431 s | exit 5 at step 6: the same wait |

Verdict line: "D162 HI release journeys of Release-5.4.8 on the installer-installed layout (c5h-d): verdict failed".
The restore re-checked the layout by hash (ok); `set-dll` and `set-proxy` never ran.

**The 32-bit DLL question (MC #3211), measured** [L]:
- **Product route: the harness needs no instance-aware 32-bit DLL.** Both journeys passed with VP's own DLL as the
  installed fallback; the Lua channel the HI judge and the harness use is answered on the product route by the x64 DLL
  in the server (as in 14.4 step 3 and 11.2).
- **Stock route: it does.** With `mode=off` the proxy loads VP's own DLL, which has no Lua channel (the channel is the
  fork's, `VPLuaExec`); the HI judge reads the game through it (`in_game`, then every checkpoint), so each stock
  replay stops at its first in-game wait. On the default path the stock route's fallback is the instance-aware DLL
  (`set-dll i0ia`, then `set-proxy`), which is why it plays there. On an installed instance the fallback is VP's DLL
  and #3211 forbids a swap. **Finding for the MC** (queue #3333): (a) the stock route of a D162 check on an installed
  build is played on the default path (same build) and only the product route on the installed layout (recommended);
  (b) an exception to #3211 that puts the instance-aware DLL in as the kept fallback for the stock route and restores
  it by hash; (c) an in-game reading of the HI judge without Lua. Not decided here.

### 14.6 A wrong Vox Populi refused with the link [L, 2026-11-17 01:04 EST, c5h-d]

`provision.py set-dll c5h-d --dll <Release-5.4.7's committed DLL, git show, ff190355...>`, then the installer (test
mode): **exit 7**, "REFUSED: This patch is for Vox Populi 5.4.8. Your Vox Populi is 5.4.7. Install Vox Populi 5.4.8
from https://github.com/LoneGazebo/Community-Patch-DLL/releases/tag/Release-5.4.8 and run this installer again." (the
text of `docs/INSTALL.md` step 6, word for word); My Games identical before and after.

### 14.7 The 43-civilization component [L, 2026-11-17 01:04-01:06 EST, c5h-d]

No pinned 43-civ x64 build exists. The one 43-civ layout is issue 4252's `F:\c5-64bit-work\0334\rebuild\layout\
x64-04b1211b-m43-p9aa30f1` (the DLL pin 04b1211b8 built with `--major-civs 43`, x64 DLL `cf981126...`; server
`d529d5d0...`, proxy `d6a43056...` and helper `e1f8b24c...` from repository 9aa30f190; "NOT a pin"). For this test
only: its purity file derived with `proxy/tools/derive_purity.py` from `git archive 04b1211b8` of the fork (166 kept, 0
dropped; bound to `cf981126...`; the 22-civ pack's purity of 2026-11-15 kept 143 under the baseline of then), a 43
pack by `tools/release/pack.py --major-civs 43 --x86-dll <the tag's 43-civ DLL 01451bd7...>`, a seeded work folder
(`evidence/2600/seed_take43.py`), and `build_installer.py --work <22> --work43 <43> --test-build`: 13,380,746 B,
`b25d4435...`, variants std+m43.
- c5h-d with the tag's 43-civ DLL `01451bd7...` in `(1) Community Patch`: the **standard-only** installer refuses, exit
  7: "Your Vox Populi is 5.4.8 (43 civilizations), and this installer has no variant for it. This patch is for Vox
  Populi 5.4.8: <link>".
- The **std+43** installer: "Vox Populi 5.4.8: the 43-civilization variant of the patch fits it", `install, variant
  m43`, exit 0; the tree gained exactly the 43 files: proxy `d6a43056...`, helper `e1f8b24c...`, VP64 x64 DLL
  `cf981126...`, server `d529d5d0...`, purity `b2677af4...`, manifest `b5478e40...` (and the shared libraries, notices,
  `vpproxy.ini`, the kept `01451bd7...`, the record and the backup); the removal gave the tree back identical.
- **Not done:** a game on the 43 variant (the layout is not a pin, the instance had no `(3b)` mod); the boot of the 43
  build stays with epic 0334's own tests.

### 14.8 Update over an installed patch [L, 2026-11-17 01:01-01:03 EST, c5h-d]

Only one patch release exists, so the update was the same release over itself (13.3's path): "update: removing
x64-04b1211b-pfdee656 (std) ... by its record first; mode=off kept: 0", exit 0, the tree identical to before. With
the player's `mode=off` (set with `proxy/tools/live/set_ini.py`): "mode=off kept: 1", "placed ... vpproxy.ini with
mode=off kept from the earlier install", exit 0, `mode=off` in the new file. Then `mode=on` back, the removal: exit 0.
**The whole My Games tree after everything (install, boot, journeys, two updates, removal) equals the tree before the
first install**, except the game's own run outputs (`cache`, `Logs`, the autosave folder `ModdedSaves\single\auto`):
`evidence/2600/mods-c5h-d/tree-start-vs-end-outside-run-outputs.txt` ("IDENTICAL"). An update from an older release
(other bytes) is covered only by 2603's hermetic tests (13.6). c5h-d: DLL back to the pin's instance-aware build
(`set-dll`), `provision.py verify c5h-d --full` ok, no error, no warning; claim released 01:08.

### 14.9 The modpack form on c5h-mpk: the patch crashes the game at start (finding) [L, 2026-11-17 01:09-01:25 EST]

Instance **c5h-mpk** (new, `F:\c5-instances`, profile `modpack548`, made 01:09 by `provision.py --root F:\c5-instances
create c5h-mpk --profile modpack548 --gbe-dir <1112's gbe-release-x32> --import-wrapper`; `verify` ok), the tag's DLL
put in with `set-dll` (the player's state). Evidence `evidence/2600/modpack-c5h-mpk/`.

1. **Install** (the standard test installer of 14.4, test mode): "Vox Populi (modpack form): ...\install\Assets\DLC\
   VP_MODPACK\Mods\(1) Community Patch\CvGameCore_Expansion2.dll, SHA-256 262aee88...", the standard variant, "The
   folder can be written", `install ... (std, thin, modpack form)`, exit 0.
2. **The game crashes at start.** A new duel game (the front-end patch's `newGameSetup`, no mods: the modpack form is
   an "unmodded" game): the EXE ended about a minute after the launch with **0xC0000005 and no box**
   (dialog watcher: `exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION (no proxy crash record)`), no proxy run folder and
   no `vpproxy.log`; `Logs\GameCore.log` holds one line: **"LoadLibrary failed with error 193: is not a valid Win32
   application."** (193 = ERROR_BAD_EXE_FORMAT).
3. **Control: the same modpack without the patch boots** (the patch removed by the installer, the same new-game
   start): the load screen with VP's Austria ("Habsburg Diplomacy", Begin Your Journey), no crash (DIAGNOSTIC picture in
   the Visual Checks feed, issue 2604; not a world-renderer picture).
4. **The cause** [L]: the patch installed again, then only `VP64\CvGameCore_Expansion2.dll` renamed: no crash; the
   proxy loaded and fell back as designed (`boot: FALLBACK ... pack check file-missing: the x64 DLL
   CvGameCore_Expansion2.dll`, the title notice, the report and the D118 popup). So in the modpack form **the EXE loads
   the game DLL by its file name from the DLC folder's files, finds the x64 `VP64\CvGameCore_Expansion2.dll` that the
   D28 layout puts inside the DLC folder, and fails on it** (a 64-bit image in the 32-bit EXE) [I: the EXE's choice
   between the two files of that name is inferred from the error and the experiment; the mods form is not affected
   because there the DLL comes from the activated mod's own file list, 14.4]. The D28 constraint of section 5 ("or
   under the install's `Assets/DLC/VP_MODPACK` (modpack form)") therefore fails as built. **Finding for the MC**: the
   patch cannot run in the modpack form until the x64 DLL has another file name inside the pack (a change of the
   proxy's pack reading, the server's load and `pack.py`) or the pack lives outside the DLC folder in that form.
   (Decided 2026-11-17, MC #3403: the rename, done by issue 2606, section 15; the server needed no change. Rerun on
   the renamed build 2026-11-17 03:31: the game boots on the 64-bit path in the modpack form, 14.12 [L].)
5. **A second modpack-form finding**: the proxy wrote its log to `<game>\Assets\DLC\VP_MODPACK\Logs\VP64\vpproxy.log`:
   it derives the user folder from its own place (two levels up from `(1) Community Patch`), which in the modpack form
   is the DLC folder; its error report went to the real `My Games\...\VP64\reports` (the EXE's folder). The installer's
   removal does not know that log folder. (Fixed 2026-11-17 by issue 2606, MC #3454: in the modpack form the run
   folders and the log go to config.ini's folder, `My Games\...\Logs\VP64`, section 15.)
6. **Removal in the modpack form** (after step 2): exit 0; the whole `Assets\DLC` tree of the instance (10,342 files,
   1,282 folders) identical by SHA-256 to before the install (11,624 rows), My Games untouched by the install.
The HI journeys are not played in this form (14.3: they are recorded for the mods form). How the experiment's game was
ended and the instance restored: the issue log of 2604.

### 14.10 `docs/INSTALL.md` followed literally once (issue 2604 item e) [L, 2026-11-17, c5h-d; C for the texts]

The page was followed as written on c5h-d, with the instance's My Games and game folders in place of the player's and
the installer run silently (`run_installer.py`, hidden desktop; no window may show on the workstation, COMMON 14), so
the page's clicks were checked against the installer's own texts in `tools/installer/patch64.iss` and its silent runs
(14.4-14.8). Where the text and the installer differ:

1. **The file name.** The page: `VP64-Patch-<release>-for-VP<version>.exe`. The build: `VP64-Patch-x64-04b1211b-
   pfdee656-for-VP5.4.8-TEST.exe`: `<release>` is the layout name by default (`--release-name` sets another), and a test
   build ends in `-TEST`. The page fits a release build named by `--release-name`; say that `<release>` is the patch's
   release name, not its layout.
2. **Pages the page does not name.** Before "What to do" the installer shows a welcome page and the notice page
   (`InfoBeforeFile`, the licence and third-party notices, which the player confirms with Next). Step 2 goes straight
   to "Choose Install".
3. **The option's words.** Page step 3 "Choose **Install**"; the installer's option reads "Install the patch (or update
   it, when an earlier version is installed)", and "Update the patch (found: <release>, <variant>)" when one is found.
   Section 5 "choose **Remove the patch**" matches the option "Remove the patch (offered when it is found installed)".
4. **The logs box.** Section 5 "Tick 'also remove the patch's logs and reports'": the box is on the *What to do* page
   and reads "When removing: also remove the patch's logs and reports". Say where it is. (Since 2026-11-20, ticket 0623:
   the box reads "When removing: also remove logs, reports and your settings (VP64Options.ini)", and
   docs/INSTALL.md's
   removal text says so.)
5. **The game folder hint** matches word for word ("right-click Civilization V, Manage, Browse local files").
6. **The version refusal** (step 6): the page's example is the installer's text (14.6 shows it live for 5.4.7).
   A 43-civ Vox Populi with an installer that has no 43-civ variant gets another text ("Your Vox Populi is 5.4.8 (43
   civilizations), and this installer has no variant for it ...", 14.7); the page says the installer "recognises" the
   43-civ version and installs the 43-civ build, true only for a build with `--work43`.
7. **"No administrator rights"**: true for these runs (a normal user's process; `PrivilegesRequired=lowest`) [L].
   **"Changes nothing in Windows' registry"**: not checked by a registry comparison [C: no registry write in the
   script].
8. **"Windows may warn ... More info, Run anyway"**: SmartScreen warns only for a file carrying a download mark; not
   testable here.
9. **Section 3 "How to tell that the patch is running"**: `server.exe` in Task Manager during the game and gone after it
   [L, 14.4]; the title ending ` - VP64 64-bit patch OFF` not seen (no fallback happened). Matches the finished page.
10. **Section 4 (the off switch, thin)**: `vpproxy.ini` `mode=on`/`mode=off`, as the finished page says; the stage
    `hi_journey`'s stock route is exactly that edit [L, 14.5].
11. **Section 5 "Update"**: 14.8. "Your saves and settings stay": the saves and config.ini were unchanged by every
    install and removal [L, the tree comparisons].
12. **What the installer writes** (`VP64\install\` with the copy of every replaced file and the record): as written [L].
Nothing in the page is wrong for the thin test build; items 1, 2, 4 and 6 are worth a line each in the page (for
lead-2600 / issue 2605, which owns `docs/INSTALL.md`).

### 14.11 The reruns on 2606's build: inputs and how windows were kept off the screen [L, 2026-11-17 03:10-03:54 EST]

Worker-2604b (epic 2600, account B), branch `task-2604b-reruns`, after issue 2606 (section 15) renamed the x64 DLL.
- **Installer under test:** `F:/c5-64bit-work/2600/w2606/inst/VP64-Patch-x64-04b1211b-p3aea9e66-for-VP5.4.8-TEST.exe`,
  15,092,282 B, SHA-256 `5c5bce272f5dec878b3277a49186d745a96acb260da0d67969ca6310f0ff50cf` (checked 03:1x), built by
  worker-2606 from the takes of 15.4: proxy and server of this repository at
  `3aea9e66d625fec825c14976d7358ea384f32dab` (0452 in), the pinned fork DLL `04b1211b8` (`29f58e17...` std, `cf981126...`
  m43). A test build (`-TEST`), not a pin; not rebuilt here.
- **Instances:** c5h-d (F:, `mods548`) for the mods form, c5h-mpk (F:, `modpack548`, 14.9) for the modpack form, both
  claimed for ticket 2600; never more than these two. Every installer run silent on a hidden desktop
  (`evidence/2600/run_installer.py`) in test mode (`--procroot` = the instance: other teams' games ran on the
  workstation), with the instance's own `/DIR` and `/GAMEDIR`. Trees compared by `evidence/2600/treehash.py` (every
  file by SHA-256, every folder; listings on `F:/c5-64bit-work/2600/w2604b/`, diffs in the evidence).
- **The patch's D118 popup kept off the screen (MC #3505, #3630):** 0473 was not merged at the start, so
  `evidence/2600/hide_watcher.py` (worker-2606's draft, finished: it also logs a matching window when first seen while
  still invisible, the nearest point to its creation) ran for each instance from before its first launch to after its
  last process ended, its full text quoted on thread 2600 (#3597) before first use. Self-test on a hidden desktop with
  the build's real `vp64helper.exe popup` (`evidence/2600/hide_watcher_selftest.py`, output
  `reruns-2604b/0-hide-watcher-selftest.txt`): 8/8, the instance's window hidden 28-32 ms after first seen (70-109 ms
  after the helper's start), a second helper outside the instance root logged NOT OURS and left visible. **No popup
  appeared in any run of these reruns** (both watcher logs: 0 windows seen). 0473 merged on main at
  `adcc99d1fa06a55f273beee9352e47d57741ef74` during the runs; at 03:52-03:53 the branch took main, the native harness was
  rebuilt and `wrapper.py install` laid the new import wrapper on both instances (`verify` ok); the default-path
  journeys of 14.14 ran with the 0473 guard and the watcher beside it (MC #3630: until 0475 covers the server-started
  helper).

### 14.12 The modpack form on c5h-mpk with the renamed x64 DLL: install, 64-bit boot, removal by hash (MC gate #3404) [L, 2026-11-17 03:28-03:47 EST]

Driver `evidence/2600/run_modpack_form.py`; evidence `evidence/2600/reruns-2604b/modpack-c5h-mpk/` (`summary.txt`).
The player's state first: `provision.py set-dll c5h-mpk --dll <the tag's DLL 262aee88...>` (`verify` ok before).
1. **Install** (03:29:24-03:29:32): exit 0, "Vox Populi (modpack form): ...\VP_MODPACK\Mods\(1) Community
   Patch\CvGameCore_Expansion2.dll, SHA-256 262aee88...", "the standard variant (22 major civilizations) of the patch
   fits it", `install x64-04b1211b-p3aea9e66 (std, thin, modpack form)`. The `Assets\DLC` tree gained exactly: the proxy
   `a2618e09...` in place of VP's DLL, VP's DLL kept as `CvGameCore_Expansion2_x86.dll`, the two notices,
   `vp64helper.exe` `1121f6db...`, `vpproxy.ini` `f80d7590...`, and `VP64\` with `CvGameCore_Expansion2_x64.dll`
   `29f58e17...`, the three libraries, `manifest.json` `7ac4705c...`, `purity.tsv` `f78cd16f...`, `server.exe`
   `625e0b14...` (14 rows); My Games only `VP64\install\record.txt` and `backup\262aee88...`.
2. **64-bit boot** (launch 03:30:31, a new duel game with no mods through the front-end patch's `newGameSetup`; frame
   cap 30 fps, guard installed, start-up watch ok, dialog watcher on): `vpproxy.log` in
   **`My Games\...\Logs\VP64\run\20261117-033132-9924-1`** (config.ini's folder; section 15), `boot: pack ...\VP64 ok:
   ... x64 DLL CvGameCore_Expansion2_x64.dll 29f58e17...`, server.cfg `dll=...\VP64\CvGameCore_Expansion2_x64.dll`,
   `server attached, generation 1, peer pid 19836 (waited 1402.2 ms)`, `kRt_Boot ok (24.0 ms)`; 0 FALLBACK, 0 LOUD
   lines; no `VP_MODPACK\Logs`; `Logs\GameCore.log` unchanged by SHA-256 (the 01:16 file of 14.9; the game wrote no
   LoadLibrary error). **The crash of 14.9 is gone with the rename** [L].
3. **Nothing new under the DLC folder while the game ran** (03:43:34, listing against the one after the install:
   identical); the run folder's 14 files are in My Games.
4. **Not verified: the AI turns.** The new game sat at its load screen ("Begin Your Journey"; `frontend.py ingame`:
   "LoadScreen state unreadable"); the harness's own channel close of the load screen (`frontend.py -i c5h-mpk
   dismiss`) was **refused by the permission classifier** of the worker account (03:37). Under COMMON 22 nothing else
   closed it (#3633); the MC accepted the boot evidence for the gate's boot part and recorded the turns as not verified
   until the owner answers whether account B may run that close (#3637). (Done 2026-11-17 by ticket 0489, 14.15: 13 AI
   turns on the 64-bit path in this form; the channel close itself cannot read the load screen in the modpack form,
   which is why this step's probe said "unreadable", and the screen was closed by a harness click.)
5. **Stop** on the MC's word (#3637): `turns.stop_readiness` ready ("turn 0 active, newest save settled (no saves)"),
   posted on thread 2600 first, then `cli.py stop --human-turn-hook tools.harness.turns:stop_readiness`
   (03:44:49-03:45:26), `leftovers`: clean.
6. **Removal** (03:45:26): exit 0; **the whole `Assets\DLC` tree (11,623 rows) identical by SHA-256 to before the
   install**; My Games identical outside the run outputs (`cache`, `Logs`, the autosave folders, `VP64\reports`) except
   **`VP64Options.ini`**, written by the patch's first run on this instance and kept by the removal by design (4.6:
   `VP64Options.ini`, `Logs\VP64` and `VP64\reports` stay unless the player ticks the logs box).
7. Restore: `frontend.py restore`, `set-dll --previous` (the pin's instance-aware `3c2f47da...`), `provision.py verify
   c5h-mpk --full` ok, no error, no warning. Claim transferred back to lead-2600 at 03:54.
A first driver attempt stopped at its launch (the harness refused a launch without `C5_POOL_OWNER` for a claimed
instance) and a later one at the stop (a game needs `--human-turn-hook`): both fixed in the driver, nothing launched or
stopped by them.

### 14.13 The mods form on c5h-d: install, removal, the 43 component [L, 2026-11-17 03:20-03:48 EST]

Scripts `reruns-2604b/modsA.sh`, `modsB.sh`; evidence `evidence/2600/reruns-2604b/mods-c5h-d/`. The player's state:
`set-dll c5h-d --dll <the tag's DLL 262aee88...>`.
1. **Install** (03:21:30): exit 0; My Games gained exactly the take's files (the same 14 rows as 14.12 under
   `MODS\(1) Community Patch`, the record and the backup).
2. **64-bit boot** in both product-route games of 14.14 (`2-boot-lines.txt`): `pack ...\VP64 ok`, server.cfg
   `dll=...\MODS\(1) Community Patch\VP64\CvGameCore_Expansion2_x64.dll`, `server attached` (1167.5 and 1713.6 ms),
   `kRt_Boot ok`, 0 FALLBACK, 0 LOUD; the AI turns ran in the server (release-basic reached its turn-10 checkpoint).
3. **Removal** (03:46:40-03:46:47): exit 0; My Games (2,757 rows) **identical by SHA-256 outside the run outputs**.
4. **The 43 component**: `set-dll c5h-d --dll <the tag's 43-civ DLL 01451bd7...>`, the same installer: "the
   43-civilization variant", exit 0; the tree gained exactly the m43 files: the proxy `a2618e09...` (one proxy for
   both variants), kept `01451bd7...`, `VP64\CvGameCore_Expansion2_x64.dll` **`cf981126...`**, `manifest.json`
   **`c27b7598...`**, `purity.tsv` **`ced4e32f...`**, the shared libraries, `server.exe` `625e0b14...`, the helper, the
   notices, `vpproxy.ini`, the record and `backup\01451bd7...`; removal exit 0, **identical** (2,764 rows). No game on
   the 43 variant (epic 0334's tests own its boot).
5. `set-dll --original` (`3c2f47da...`, the instance's first DLL).

### 14.14 D162 on this exact build: the product route on the installed layout, the stock route on the default path (MC #3343, 4.7) [L, 2026-11-17 03:22-04:5x EST, c5h-d]

Both runs `take_release.py Release-5.4.8 --tier validated --only hi_journey` through `quietlock.py run`, claim ticket
2600 by worker-2604b, on copies of the std take's work folder (`F:/c5-64bit-work/2600/w2604b/work-*`), the build of
14.11 (proxy `a2618e09...`, x64 DLL `29f58e17...`, server `625e0b14...`; the instance-aware DLL `3c2f47da...`).

| Journey | Product route, installer-installed layout (c5h-d) | Product route, default stage-laid path | Stock route, default stage-laid path (the -ia DLL) |
|---|---|---|---|
| release-basic | **exit 0**, 63 steps, founded/research/turn5/turn10/loaded/menu ok, 575 s | exit 0, 63 steps, all ok, 624 s | **exit 0**, 63 steps, all ok, 517 s; compare 6 of 6 the same |
| release-large | **exit 0**, 21 steps, moved/attacked/city/camera/menu ok, 453 s | exit 0, 21 steps, all ok, 478 s | **exit 0**, 21 steps, all ok, 424 s; compare 5 of 5 the same |

Verdict lines: "D162 HI release journeys of Release-5.4.8 **on the installer-installed layout (c5h-d)**: verdict passed
(stage ok)" (product route only, `C5_RELEASE_HI_ROUTES=product`; reports `reruns-2604b/mods-c5h-d/2-hi-product-
installed-report.*`); on the default stage-laid path: run 1 (03:54-04:18) "verdict incomplete (stage failed)" with
release-basic passed on both routes (`6-hi-default-run1-report.*`), see below; run 3 (`C5_RELEASE_HI_JOURNEYS=
release-large`, 04:44-05:05, with the fix below) "verdict passed (stage ok)", restore ok, `verify` ok
(`8-hi-default-large-report.*`). So on this build **both D162 journeys passed on both routes**: the product route on
the installer-installed layout and on the default path, the stock route on the default path, the comparisons all
"same"; not as one take (`--art-check` would need one run of both journeys and routes; the stage's verdict lines say
"for the owner: NO" because each run played a subset: none of these was a take for the owner).

**The stock game's exit crash and a stage gap (fixed).** After release-basic's stock replay had passed and the game had
left by the journey's own EXIT, the EXE ended with 0xC0000005 and no box (04:18:43; dialog watcher signature
`exe-crash: 0xC0000005 EXCEPTION_ACCESS_VIOLATION (no proxy crash record)`, `6-exit-crash-marker.json`). The MC
identified it as the -ia test DLL's known exit crash (ticket 0484; `proxy-design.md` 21, 0164: test builds only, the
release ships no -ia DLL; not an installer finding; the journeys' passes count, MC #3689). The stage then found the
crash marker before its next launch and treated it as a box on a running game (`LeftRunning`): stage failed, the laid
layout and the claim kept, no restore; a second run at 04:24 failed the same way at once (the marker stays until the
next launch moves it aside). c5h-d was restored twice by hand from the stage's undo list, by hash
(`evidence/2600/restore_after_hi_crash.py`, `7-restore-after-exit-crash.txt`; `verify --full` ok each time). **Fix**
(MC #3689, `tools/release/stages/hi_journey.py` `ended_crash`): before a launch, a marker of kind `crash` (the game
ended without a box) on an instance where no game runs is recorded in the row (`earlier_exit_crash`) and the replay
goes on (the launch archives the marker); a box, or a crash marker while a game still runs, still stops the stage.
Tests [L, 04:27-04:44, through `quietlock.py run`]: `test_hi_journey.py` **64/64** in 982 s (two new in `Failures`: an
exit crash after a passed replay plays on, restores and releases; a crash marker while the game still runs stops as
before); `test_safety_static.py` 55 passed. Live: run 3 at 04:44:54 logged "c5h-d: the earlier game crashed at its exit
without a box (exe-crash: 0xC0000005 ...); nothing runs; going on" and launched.

### 14.15 The modpack form plays a short game on the 64-bit path; the harness channel's gaps in that form (ticket 0489) [L, 2026-11-17 10:55-11:3x EST, c5h-mpk]

Implementer impl-0489 (account B), branch `task-0489-modpack-short-game`; driver `evidence/2600/0489/run_0489.py` (from
14.12's), evidence `evidence/2600/0489/` (`summary.txt` with every turn, `4-turns.json` for turns 10-13 and both memory
readings, `acceptance.txt`). Installer 5c5bce27 (14.11), the player's
state as 14.12 (`set-dll` 262aee88...), a new duel game with no mods (Continents, 1 AI, 2 city states).
1. **Install, boot**: as 14.12 (exit 0; the same 14 DLC rows and 4 My Games rows); launch 10:56:35, frame cap, guard,
   start-up watch ok; `vpproxy.log` in `My Games\...\Logs\VP64\run\20261117-105740-37348-1`: pack ok, the `_x64` DLL,
   `server attached` (3225.2 ms), `kRt_Boot ok`, 0 FALLBACK, 0 LOUD. (`Logs\GameCore.log` still holds 14.9's 01:16
   line; this run did not write it.)
2. **Finding: the proxy-served Lua channel cannot see UI objects in the modpack form** [L]. `frontend.py -i c5h-mpk
   dismiss` ran (D177: not refused) and ended exit 1, "nothing to dismiss: LoadScreen state unreadable (error)"
   (`3b-frontend-dismiss.txt`). Through the channel every UI object's method is nil: `ContextPtr:IsHidden()` and
   `Controls.ActivateButton:IsHidden()` give "attempt to call method 'IsHidden' (a nil value)" in the LoadScreen,
   InGame and VPUI_loader states; the objects exist (`LuaContext: 0F838C80`, metatable `CTypeName=LuaContext` with a
   C `__index` that returns nil for `IsHidden` and `SetHide`); `UI`, `Game`, `Players`, `GameInfo` work in the named
   states (`ls_probe.lua`, `ctx_probe.lua`). **Second gap**: the channel's default environment (no `--state`, which
   `turns.py`'s chunks use) lacks the game's globals in this form (`type` and `GameInfo` nil), so `turns.py end` failed
   40 tries with "attempt to index global 'GameInfo' (a nil value)" (`4a-turnspy-end-turn-01-main-state-failed.log`);
   `turns.turn_state` and `stop_readiness`, which read only `Game`/`Players`, answered. Both are harness-channel
   gaps, not game faults: **the game's own UI works** [L]: the load screen closed by a harness click on its own
   "Begin Your Journey" button (`cli.py click 0.868 0.707`, MC #4189), Elizabeth's greeting closed by a click on
   Goodbye, F6 opened the tech tree and Esc closed it (Visual Checks `vc-20261117-112248-workstation-767d`). On the
   mods form (14.4, 14.5) the same channel read UI states and drove turns [L]. (Corrected 2026-11-17 by ticket 0498,
   14.16: the cause is traced and fixed in the proxy; this entry said "not traced [I: the proxy's `VP_LUAEXEC` thread
   and its environment choice]", which was the right suspect.) The MC opens the harness ticket from this entry.
3. **AI turns** [L]: `turns.end_turn` / `turns.wait_turn` through their `runner` parameter with the chunks run in the
   InGame state (driver `--runner-state InGame`): **13 turns (0 to 13), 3.3-3.4 s each** (end 0.1 s, AI turn 3.1-3.3 s),
   the turn read from the game after each; turn 9 waited on the AI's first-contact greeting (the in-game diplomacy
   handler is not armed with an injected runner; it would need the UI methods too) until the Goodbye click. No box at
   any turn (dialog marker checked before each). Private memory: EXE 1,467.0 MB at turn 0, 1,488.6 MB at turn 13;
   `server.exe` 157.9 MB and 162.8 MB.
4. **DLC tree while running** (11:24): identical to the listing after the install. **Autosave check** (11:24:44):
   `turns.stop_readiness` ready, "turn 13 active, newest save settled (`AutoSave_0012 BC-3520.Civ5Save`)", posted on
   thread 0489 (#4200) before the stop; `cli.py stop --human-turn-hook tools.harness.turns:stop_readiness` exit 0
   (newest save stable 11.4 s), `leftovers` clean. **Removal** (11:25:13): exit 0; **the whole `Assets\DLC` tree
   (11,623 rows) identical by SHA-256 to before the install**; **My Games identical outside the run outputs**
   (`cache`, `Logs`, the autosave folders, `VP64\reports`: 64 differences there). `VP64Options.ini` was already in
   the "before" listing (written by 14.12's run, kept by design, 4.6), so it shows no difference this time.
   **Restore**: `frontend.py restore`, `set-dll --previous` (262aee88... back to the instance-aware `3c2f47da...`),
   `provision.py verify c5h-mpk --full` ok, no error, no warning.
This closes 14.12 item 4 (the AI turns) for the modpack form [L], with the deviations of item 2: the load screen and
one leader screen closed by harness clicks on their own buttons, the turns driven through `turns.py`'s functions in
the InGame state rather than its CLI.

### 14.16 Why the Lua channel could not see UI objects in the modpack form, and the fix in the proxy (ticket 0498) [L, C; 2026-11-17 11:50-1x:xx EST, c5h-mpk]

Implementer impl-0498 (account B), branch `task-0498-modpack-lua-channel`; driver
`evidence/2600/0498/run_0498.py` (from 0489's), probes `env_probe.lua`, `swap_probe.lua`, `key_probe.lua`,
`key_bisect.lua`, `exposed_probe.lua` (all read-only: they read the game's states and run calls in coroutines of their
own; nothing in a game state is written); evidence `evidence/2600/0498/` (`diag-mpk/` the installed proxy
`a2618e09...`, `fix-mpk/` the fixed proxy builds). Installer 5c5bce27 and the player's state as 14.15; a new duel game
with no mods; every probe at the load screen (turn 0).

1. **How a UI object finds its methods** [L, `fix-mpk/key-probe-b1.txt`, `key-bisect-b1.txt`]. A UI object
   (`ContextPtr` is a `LuaContext`, the controls are `...Control`) is a userdata whose metatable has `CTypeName`,
   `BaseType`, `__eq`, `__tostring` and a C `__index`. That `__index` looks the type's method table up in the global
   **`ExposedMembers`** of the **calling thread** (`LUA_GLOBALSINDEX`, the thread's own globals, not the environment of
   the Lua function that makes the call): `ExposedMembers` holds 62 tables named `"Metatable for type: <type>"`
   (`ImageControl`, `ButtonControl`, `UIManager`, `const SliderControl`, ...). Tried from a coroutine whose globals
   were set by `debug.setfenv`: LoadScreen's environment, InGame's (another context), and a table with only
   `__index = LoadScreen's environment` all resolve `ContextPtr:IsHidden()`; `_G`, the channel thread's own
   environment, `{ContextPtr}` and `{ContextPtr, Controls}` do not; of LoadScreen's 248 keys exactly one alone
   makes the method resolve: **`ExposedMembers` (LoadScreen's table)**, but then `IsHidden()` returns nil; a real
   answer (`false`, a boolean) needs **`ExposedMembers` and `ExposedTypes`** (the minimal set, `value-bisect-b2.txt`).
   The lookup goes through metatables (the proxy table works).
2. **Why the channel failed in the modpack form** [L, `diag-mpk/probe-diag-default.txt`, `fix-mpk/exposed-probe-b1.txt`].
   The channel runs every chunk on its own engine thread `VP_LUAEXEC` (made by the proxy with
   `ICvEngineScriptSystem1::CreateLuaThread` in `CacheGameDatabaseData` at the boot). The runner gives the chunk an
   environment that reads through to the target state, but the thread's globals stay the thread's own environment, so
   a UI call made by the chunk was resolved against that environment's `ExposedMembers`. In the modpack form:
   - the thread's own environment holds only the DLL's registration globals: **75-76 keys** (`Game`, `Players`, `Map`,
     `Teams`, `Events`, `LuaEvents`, `MapModData`, the `*Types` tables, `StateName = "VP_LUAEXEC"`; in one boot also an
     `ExposedMembers`, in the next not), **no metatable, no base library (`type`, `pairs`, `print`), no `GameInfo`,
     `UI`, `Locale`, `DB`, `GameDefines`**. That is gap 2: the runner's "Main" environment is this thread's own
     environment whenever it sees `Game` (it does), so `turns.py`'s chunks found `GameInfo` and `type` nil;
   - `_G` (`_LOADED._G`, 149 keys) has the base library, `GameInfo`, `UI`, `Threads` and its own `ExposedMembers`
     (62 type tables), but **152 of the 154 Lua states share another `ExposedMembers` table** (the UI contexts; the
     other two are `VP_LUAEXEC` and `Automation Monitor`, with none). `_G`'s table is not the UI contexts' current one,
     so a UI call through `_G` fails too. That is gap 1: `ContextPtr:IsHidden()` "a nil value" in every state through
     the channel, while the game's own UI (each context's own thread) works.
   **The mods form, same probe** [L, c5h-c, `diag-mods/probe-diag-default.txt`, installed proxy `a2618e09...`, the
   same installer, a new duel game with CP, VP and EUI]: the channel thread's own environment is a **full
   environment of 212 keys** (base library, `GameInfo`, `UI`, `Locale`, `DB`, `include`, the DLL's globals; no
   metatable) whose `ExposedMembers`/`ExposedTypes` are the UI contexts' current ones: `ContextPtr:IsHidden()` resolves
   on the thread itself. `_G` as globals fails there too. So the forms differ in what the EXE puts into the
   environment of a thread made by `CreateLuaThread` at the boot's `CacheGameDatabaseData` (in both forms on the
   boot thread, at 14.1 s and 20.5 s): a full one in the mods form, the DLL's registration globals only in the
   modpack form. [I] why: in the modpack form the DLL loads with the EXE's own DLC set, before the UI's Lua
   environment is set up; both forms call `RegisterScriptLibraries` a second time at the new game's start (mods
   42.6 s, modpack 51.7 s), so that call is not the difference.
3. **The fix** [C, `proxy/src/proxy_luaexec.cpp`; unit checks `tools/harness/tests/test_luaexec_modpack.py`]. In the
   runner (the Lua half, otherwise the fork's text): (a) while the chunk runs, the thread's globals are set
   (`setfenv(0, ...)`) to the chunk's target environment, seen through a small table that puts the UI contexts' current
   `ExposedMembers` and `ExposedTypes` (those of the environment whose `ExposedMembers` most states' environments hold)
   in front of it when the target's own are others (`setmetatable(pair, {__index = base, __newindex = base})`: nothing
   is written into a game state), and set back after the chunk's `xpcall`; (b) when the thread's own environment has no `type` and no metatable, it
   gets `setmetatable(T, {__index = _G})` once (the thread's own table; a "Main" chunk then sees the base library and
   `GameInfo` beside the DLL's globals). In C++ `RunExternalLua` keeps the thread's own environment in the registry
   (`VP_LUAEXEC_HOME`) and puts it back as the thread's globals before and after every run, so an error outside the
   runner's `xpcall` cannot leave a state's environment there. `frontend.py` and `turns.py` are unchanged.
   Builds (all `proxy/build_proxy.py --config release` from this branch against the pin source 04b1211b8, swapped
   for the installed proxy by SHA-256 and put back after): **b1** `6f40c02d...` did (a) without the pair, and (b): gap 2
   closed (`type`, `GameInfo` seen in "Main"), gap 1 not, because frontend's probe runs in "Main" and calls other
   states' UI objects; hence the key probes of item 1. **b2** `57b63c99...` put only `ExposedMembers` in front: the
   methods resolved but `IsHidden()` returned nil (`fix-mpk/value-bisect-b2.txt`: a boolean needs both
   `ExposedMembers` and `ExposedTypes`, the minimal set found by reducing a copy of LoadScreen's 248 keys; `_G`'s
   `ExposedTypes` differs from the UI contexts' too). **b3** `de3de9e1...` (the branch's code): both, `pair_probe.lua`
   by hand first (`T` or `_G` with the pair in front: `false/false`, the load screen's real answer).
4. **Acceptance with b3, the unchanged commands** [L]:
   - **Modpack form, c5h-mpk** (`fix-mpk/summary.txt`, 12:19-12:26 EST): boot as 14.15 (pack ok, `_x64` DLL, server
     attached 1736 ms, `kRt_Boot ok`, 0 FALLBACK, 0 LOUD); frontend's probe read **`load_screen_ready`** 82 s after the
     launch; **`frontend.py -i c5h-mpk dismiss` exit 0**, `dismiss -> ['closed']`, method `channel`, after: "in game:
     load screen hidden, InGame state present (hidden=false), not paused" (`3b-frontend-dismiss-b3.log`); **`turns.py
     end` + `turns.py wait`, 3 AI turns 0->3, no workaround**, 5.8, 2.6 and 5.0 s (`4-*-b3-*.log`); no box at any
     turn; the DLC tree while running differed from the installed listing only by the swapped proxy; autosave check
     posted (stop_readiness ready, turn 3), `cli.py stop` exit 0, leftovers clean. Private memory EXE 1,487.1 MB at
     turn 0, 1,504.2 MB at turn 3; server 158.3 / 159.8 MB.
   - **Mods form, c5h-c** (profile mods548, `fix-mods/summary.txt`, 12:32-12:37 EST; the same installer, CP + VP + EUI,
     a new duel game): load screen ready at 95 s; **dismiss exit 0** (`closed`, method `channel`); **3 AI turns 0->3**
     with the CLI (6.0, 6.2, 2.5 s); stop exit 0. With b3 the mods form's channel thread runs with its own environment
     unchanged (212 keys, no metatable added, the pair already current, so the runner uses the target itself): the
     mods form behaves as before. Then the installed proxy back by hash, removal exit 0, **My Games identical by SHA-256
     outside the run outputs**, `set-dll --previous`, `provision.py verify c5h-c --full` exit 0 (one warning: a stale
     `cache/stats.db-journal` of 2026-11-14 03:17, older than this run, left as found).
   - Not verified: a loaded save (only new games), a game in which the UI is reloaded, the 43-civilization variant,
     and the channel's behaviour at the front end before any game (no UI context with an `ExposedMembers` table there
     would leave the target's globals as they are, as before the fix [I]).

## 15. The x64 DLL as `CvGameCore_Expansion2_x64.dll`; the modpack form's logs in My Games (issue 2606, 2026-11-17)

Worker D of epic 2600 (worker-2606), branch `task-2606-x64-name`. Cause: 14.9 [L]. Decision: MC #3403 (option (i), the
rename, one name for both VP forms and the 43-civ variant, no question for the owner), files granted #3410, the run-root rule
accepted #3454.

*Added 2026-11-20 (ticket 0620, `research/02-exe-internals/new-exe-build-26577625.md` C.7.3, C.7.4 A) [L, C]:* the run-root rule covers the proxy's run folder only. With the compositor (i2, i3) the proxy also sets `VPSHELL_OUT_DIR` by `proxy_shell.cpp` `OutDirFor`, the last `\MODS\` of the composite's path in any case: in the modpack form that is `<game>\Assets\DLC\VP_MODPACK\Mods\`, so `shell_composite.log` (and the farm guard's records) go to `<game>\Assets\DLC\VP_MODPACK\Logs\`, inside the game folder, and the removal leaves that folder (the DLC tree is not identical after it; everything the install replaced does come back by hash). The modpack form of build 26577625 itself works installer-laid (start, load, 10 turns, exit by the Lua channel, instance c5h-new-mpk).

### 15.1 What changed [C: the code, 2026-11-17 01:40-02:00 EST]

- **The proxy** (`proxy/src/proxy_rt_logic.cpp/.h`, `proxy_rt.cpp`, `proxy_purity.cpp`; the rules are written once in
  `research/03-dll-engine-boundary/proxy-design.md` 14, "The x64 DLL's file name ..."). `X64DllName`: with `pack=1` the
  checked manifest's `x64_dll.file` (server.cfg's `dll=` gets `ProxyPack`'s checked path); otherwise
  `VP64\CvGameCore_Expansion2_x64.dll` when it exists, else `VP64\CvGameCore_Expansion2.dll`. Used for server.cfg's
  `dll=`, the error report's "x64 DLL" hash line and the pack=0 purity binding (`LoadBesideX64`). `UserLogsDir`: a
  proxy path under `Assets\DLC` (modpack form) puts the run root and the mode=off log in `<config.ini's folder>\Logs\VP64`
  (the report's rule, `ProxySession::ConfigIniPath`); under `MODS` (mods form) 0148's rule is kept (the same folder in
  an installed game; a test layout under a temporary MODS never writes the real My Games). `run_root=` is kept as written.
- **The server**: no change. It loads what server.cfg's `dll=` names; `srv_exe_crash.cpp` names only the proxy and the
  fallback [C, lead-2600 01:35].
- **`tools/release/pack.py`**: copies the build's `bin\CvGameCore_Expansion2.dll` into the pack as
  `CvGameCore_Expansion2_x64.dll` (`X64_DLL_PACK`, `PACK_FILES`), `manifest.json` `x64_dll.file` names it, `files` lists
  the other four; with `--pdb` the PDB keeps its name `CvGameCore_Expansion2.pdb` (the DLL's debug directory names it).
  A pack written before 2606 (old name in its manifest) still verifies: the installer's pack check
  (`build_installer.py` `check_pack`), the proxy and `hi_journey` read the name from the manifest.
- **`tools/release/stages/hi_journey.py`**: the installer-installed check (14.3) hashes `VP64\<manifest x64_dll.file>`
  (`installed_x64_name`: else `_x64` when present, else the old name; a name with a path separator is ignored);
  the layout record gets `x64_file`.
- **`tools/instances/provision.py set-proxy`**: `--bin` may hold the x64 DLL under either name (a build's `bin\` the old
  one, a pack the new one; `hi_journey` and `stages/instance.py` pass `vp64pack` as `--bin`, which the rename alone
  would have broken). New `--x64-name auto|new|old`: auto = the pack manifest's name when `--bin` is a pack; else
  `_x64` in the modpack form when the proxy knows that name (the bytes `CvGameCore_Expansion2_x64.dll` in the proxy
  image); else the old name. The new name with a proxy built before 2606 is refused (it would start the server with no
  x64 DLL). `instance.json` `proxy.x64_dll` records the name.
- **`tools/installer/notices.py`**: the notice names `VP64/CvGameCore_Expansion2_x64.dll`. `install.iss` names only
  the proxy and the kept DLL; `build_installer.py` reads the pack's names from the manifest: no change needed [C].

### 15.2 The fork DLL looks up its own name nowhere [C, 2026-11-17 01:56 EST]

How: `git grep -n -I -P <pattern> 04b1211b --` in the fork clone `D:/c5-64bit-work/9045/fork` (04b1211b resolves to
04b1211b8d5e4bc9465b581947ced284bfbc728f, the pin) for `CvGameCore_Expansion2` (42 hits), `GetModuleHandle` (4),
`LoadLibrary` (4), `GetModuleFileName` (5), `GetProcAddress` (3), `\.dll"` (17) and `(?i)gamecore[^"]*\.dll` (25),
over the whole tree (C++, Lua, SQL, XML, scripts). Result: no load-time or run-time lookup of the DLL's own file name.
`GetModuleHandle`/`GetModuleFileName` name the EXE (NULL), ntdll and dbghelp, or a crash address's module; the one
compare is `CvGlobals.cpp:3008` in the crash dialog (`bFromDLL` = the crashing module is named
`CvGameCore_Expansion2.dll`, which picks the "code error in the gamecore dll" wording over "unknown reason"). Renamed,
a crash inside the x64 DLL gets the "unknown reason" wording; cosmetic, and only where that handler shows a dialog in the
server at all [I]. Every other hit is the 32-bit mod's own files (`.modinfo`, `.civ5proj`, `.Civ5Pkg` `<GameCore>`,
`VPSetupData.iss`, `scripts/release.py`, `build_vp_clang*.py`, `CvGameCoreDLL.def` `LIBRARY`). The fork is not changed.
Same result as proxy-design 14 ("Off switch", 2502). Script and output: `evidence/2606/fork_selfname.py`, `.txt`.

### 15.3 Other tools that still read only `VP64\CvGameCore_Expansion2.dll` [C, 2026-11-17 02:00 EST; not changed: outside 2606's files]

`tools/hi/session.py:206` (`build_ids` `dll_sha256`: None on an installer layout), `tools/harness/framecap.py:175`
(`game_dlls`: the timing refusal does not see the x64 DLL), `tools/testbed/jobs/product_path.py:110` (`files_info`
`x64_dll`: None), `perf/profilekit/acceptance.py:200` (exits "client binary dll missing" on such a layout). Test
instances laid by `set-proxy` keep the old name in the mods form (auto), so nothing changes for them today; an installer
layout or `--x64-name new` needs these four to read the name as the proxy does. Reported to lead-2600 (#3467).

### 15.4 The team layouts and the two takes for 2604's reruns (not pins) [L, 2026-11-17 02:12-02:20 EST]

MC #3410: the team builds its own layout and takes; the MC places the release pin at 2600's merge. No DLL link (D121).

- **Build** of this repository at `3aea9e66d` (task-2606-x64-name with origin/epic-2600-installer 2ffdea5fa merged, so
  main cbd78456d with 0452's server change, MC #3484; repo_dirty=0 in both build ids): `python -I perf/quietlock/
  quietlock.py run ... -- python -I server/build_server.py --src F:/c5-64bit-work/0334/rebuild/src-x64-04b1211b --out
  C:/c5-64bit-work/2600/w2606/build2 --config release --offline --deps F:/c5-64bit-work/0334/rebuild/m43/deps`, 145 s
  (proxy 58 s). The `--deps` folder gives only the three libraries the server links (`lua51_x64`, `CvGameDatabase_x64`,
  `CvLocalization_x64`: byte-identical to the pin layout's, checked by `make_layouts.py`); the server has no
  compile-time civilisation count (G1), so one server and proxy serve both layouts. A first build at 0e82f8c08 (before
  the merge of 0452) was discarded.
- **Layouts** (`evidence/2606/make_layouts.py`), `F:/c5-64bit-work/2600/w2606/layout/`:
  `x64-04b1211b-p3aea9e66` (the pinned fork DLL `29F58E17...` and its libraries from
  `%USERPROFILE%/c5-i0/dlls/x64-04b1211b-pfdee656`) and `x64-04b1211b-m43-p3aea9e66` (0334's 43-civ DLL `CF981126...`
  from `F:/c5-64bit-work/0334/rebuild/layout/x64-04b1211b-m43-p9aa30f1`); both with server.exe `625E0B14...`
  (7,184,384 B), the proxy `A2618E09...` (1,155,584 B) and vp64helper.exe `1121F6DB...`; each has README.txt and
  SHA256.txt.
- **Takes** (`evidence/2606/take2606.py`): work folders seeded from `F:/c5-64bit-work/0392/4506/rel/state.json` (as
  2604's were), the build outputs replaced by the layouts, then the purity and pack stages run by their own tools with
  the stages' arguments (`derive_purity.py --src <the source copy> --dll <layout>/bin/CvGameCore_Expansion2.dll`;
  `pack.py --layout --purity --x86-dll <the tag's player DLL by git show, checked against the descriptor> --release
  Release-5.4.8 --fork-commit --tools-commit [--major-civs 43]`) and recorded in `state.json` as the stages record them
  (`inputs_sha` `seed-2606`).
  - `F:/c5-64bit-work/2600/w2606/takes/Release-5.4.8-std`: manifest SHA-256 `7ac4705c22f0d739a62b0eee561f55c01b1b5b6bd9788dac16379c87ff75e0d4`;
    `x64_dll` `CvGameCore_Expansion2_x64.dll` `29f58e17...`; purity `f78cd16f...` (137 methods, 29 Lua, 0 dropped);
    `x86_dlls` the tag's `262aee88...`.
  - `F:/c5-64bit-work/2600/w2606/takes/Release-5.4.8-m43`: manifest SHA-256 `c27b7598c5dbcbd7f163d1adb9d25c52167e1b16a318c93ea09f2254c996e5d4`;
    `x64_dll` `CvGameCore_Expansion2_x64.dll` `cf981126...`; purity `ced4e32f...` (137, 29, 0 dropped); `x86_dlls` the
    tag's (3b) DLL `01451bd7...`; `major_civs` 43.
  The purity file differs from the first playable build's (`6a0d7b6b...`, 0324): the audited lists on main have grown
  since (166 rows now; 0439, 4460) [C: the counts]. Each pack holds `CvGameCore_Expansion2_x64.dll` and no
  `CvGameCore_Expansion2.dll`.

### 15.5 Tests [L, 2026-11-17 02:18-02:5x EST, branch task-2606-x64-name, build2 of 3aea9e66d]

- `proxy/tools/test_proxy_rt.py --build build2`: 107/107 (126 s); its unit part 115/115 with the new `x64 name (2606)`
  (both names, a folder of the new name not taken, the manifest name winning, server.cfg naming each) and `user logs`
  checks (mods form, modpack form with and without config.ini, `MyAssets\DLC`, `Assets\DLCs`, `MODSX`, development
  layouts).
- `proxy/tools/test_purity.py`: unit `purity_unit` 66/66, also under MSVC /fsanitize=address (the three new `beside
  (2606)` checks: `_x64` wins, another `_x64` refused with no fall back to the old name, `_x64` alone). Its seven
  `derive` checks fail on a count the test hard-codes (114 methods / 143 rows) while main's audited lists have 137 / 166
  rows since 0439 and 4460: not 2606's, reported to lead-2600 (#3514).
- `proxy/tools/test_proxy_pack.py --build build2`: 70/70 (32 s); new: the pack holds `_x64` and no old name; **the
  chain check**: a pack made by `pack.py` from the build, laid as the installer lays it, booted by the real proxy and
  server through `loader.exe` on a hidden desktop: server.cfg's `dll=` is `VP64\CvGameCore_Expansion2_x64.dll`; and the
  same in the modpack form (`<tmp>\game\Assets\DLC\VP_MODPACK\Mods\(1) Community Patch\`, config.ini named by
  VPPROXY_TEST_CONFIG_INI): the 64-bit boot, the run folder in `<config.ini's folder>\Logs\VP64\run`, nothing under the
  game's DLC folder.
- `tools/release/tests/test_pack_x64_name.py` (new, 6), `test_hi_journey.py` Installed (10, two new), shell_config and
  major (byte-for-byte comparison with the merge base's pack.py allowing only the rename): 34 passed with the new file.
- `tools/instances/tests/test_provision.py`: 119 passed (116 s), with `test_choose_x64_name` and
  `test_set_proxy_x64_name` (auto in the mods form keeps the old name; `--x64-name new`; a pack as `--bin` laid under
  the manifest's name; an older proxy with a 2606 pack refused before anything is written; a pre-2606 pack; round trips
  restore everything).
- `tools/installer/tests` + `tools/harness/tests/test_installer.py`: 57 passed, 1 failed on the payload list's old name
  (`VP64/CvGameCore_Expansion2.dll`), fixed, the rerun passed.
- `tools/harness/tests/test_safety_static.py`: 55 passed.
- `tools/release/tests` (through `quietlock.py run`): 369 passed in 2003 s (02:29-03:03).
- The test installer built from the two takes (`build_installer.py --work <std> --work43 <m43> --test-build`):
  `F:/c5-64bit-work/2600/w2606/inst/VP64-Patch-x64-04b1211b-p3aea9e66-for-VP5.4.8-TEST.exe`, 15,092,282 B, SHA-256
  `5c5bce272f5dec878b3277a49186d745a96acb260da0d67969ca6310f0ff50cf`; both payloads' `VP64\` hold
  `CvGameCore_Expansion2_x64.dll` and no old name (the installer's pack check read the new manifests). Not verified here:
  a live game on the renamed layout in either form (2604's reruns).

## 16. The options page, taken from the code (one-off 9081, 2026-11-21)

The owner ordered (2026-11-21, one-off 9081, directed in person): the installer gets a mod-manager-style options page, a tree
of checkboxes and radio buttons grouped by the sections of `VP64Options.ini`, and the installer must take its option
list from the real code, never from a hand-kept list. The owner chose the route: the shipped `server.exe` is run at installer
build time to write the options file with the same code the proxy uses. The page's look is being designed with the owner
separately (mockups); what is built here is a plain first cut whose layout lives in one Pascal procedure.

### 16.1 server.exe's two verbs [C code; L 2026-11-21 23:33 EST]

`server.exe --write-options <ini> --write-options-schema <tsv>` (either alone or both;
`server/src/srv_write_options.h`/`.cpp`, called first thing in `main()` of `server/src/srv_main.cpp`, before the window
guard, the log and any DLL load). `--write-options` writes exactly `Vp64Opt::DefaultFileText` (CRLF, ASCII: the text
the proxy creates a missing file with). `--write-options-schema` writes one row per option with a non-empty key, in
table order, UTF-8, LF, no header: `id, section, key, default, min, max, category (0 none, 1 switch, 2 bugfix,
3 visual), needsKey or -, title, help`, title and help with a backslash as `\\`, a line break as `\n`, CR as `\r`, tab
as `\t`. The `title` column (added the same evening on the owner's word, 2026-11-21 about 23:55 EST): `Vp64Opt::Def::title`,
the last member of `Def`, a short title per option (1-45 characters, no trailing period; the owner's wording, kept only in
the table so that a change is one edit; `options/tests/test_options.cpp` `TestTitles`); `DefaultFileText` does not
change (11,108 B before and after).
Exit 0 written, 1 a file not written (one stderr line; a half-written file deleted), 2 usage. The source file is
self-contained (no `srv_common.h`): `server/build_server.py` compiles every `server/src/*.cpp`, so it is in the server
with no builder change, and the hermetic tests link it with `options/src/vp64_options.cpp` alone into a stand-in
(`tools/installer/tests/standin_server.cpp`). No other builder compiles `srv_main.cpp` [C, grep of every builder,
2026-11-21; the limits test extracts only its "4253 PLAYER LIMITS" block, untouched].

Measured [L, 2026-11-21 23:31-23:33 EST]: `server/build_server.py --src C:/c5-64bit-work/keep/pins-0.1b/src-x64-1b87dd185
--deps C:/c5-64bit-work/keep/pins-0.1b/deps-x64-release --config release --no-proxy --out
C:/c5-64bit-work/9081/impl/server-build`: OK in 69 s, `srv_write_options.cpp` rc=0, `server.exe` SHA-256
`93b7f56da95e7c765081f138d3d3dfa7ec6d0cdf0bdc00e8af5e6fff49738c54`. Both verbs: 58 ms wall, exit 0, only the two files
appear in the working folder; ini 11,108 B SHA-256 `86b3f1e26cee8f4acb7ef72f044ce6da7d8405758445dd64c392182cafa0b0cb`
(byte-equal to the 9081 mock folder's `VP64Options.default.ini`), schema 10,108 B, 26 rows, SHA-256
`fdcc612b11d029c82e9cebaaf3a6e7c0fd55e421d0ae385f682c4eab41588c41`; both byte-equal to the stand-in's. `server.exe`'s
static imports are system DLLs and the VC++ runtime only (lua51, the database, localisation, d3d11, dxgi and
D3DCompiler are delay-loaded) [C, `llvm-readobj --coff-imports` of the d234 take's server.exe], so the verbs run from a
staged folder on any machine with the runtime. (Those numbers are of the build before the title column.)

With the title column [L, 2026-11-21 23:55-23:58 EST]: `build_server.py ... --out C:/c5-64bit-work/9081/impl/server-build2`
(server and proxy, 141 s, both OK: the new `Def` member compiles in the x86 proxy and the x64 server), then
`--no-proxy --out .../server-build3` with the final titles (68 s): `server.exe` SHA-256
`7b2d53c87c52031d09afdde58a8721298021330ef13ec8c9722c9c617b8e89a5`; both verbs 123 ms wall, exit 0; ini unchanged
(11,108 B, `86b3f1e2...`); schema 10,990 B, 26 rows, SHA-256
`7decd07506a9a01e8ada14849566893dc71a2d068cf03533b070f3a9cb3325c9`; both byte-equal to the stand-in's. The options
unit tests (`ipc/build.py --arch both --target test_options`): 821 passed in the x86 and the x64 tree. The other
builders of `vp64_options.cpp` (runbook 2.10, 18) were not linked one by one.

After the owner's wording of 2026-11-22 (in person, through the 9081 session: AiSkipVisualDrain's and GameEventsOneCrossing's
titles; BakedTileCameraRadius' -1 line "prevent gray tiles by caching the whole map at the cost of resource usage
(default)"; AiUpdateTickMs' 0 line "(default, highest performance)") [L, 2026-11-22 00:10-00:11 EST]:
`.../server-build4` (`--no-proxy`, 70 s), `server.exe` SHA-256
`0dab56353fffad5d1fd9038197179048e40e02c893bb411dbabf21e0b1211ea6`; verbs 107 ms, exit 0; ini 11,198 B
`e3e05586e9d3a2ebfbb193d041a2216637f7cd538f7b54f241b9dbef762e22dd` (the default file changes with the help, as it
must); schema 11,083 B `c56a4a87550ae6b959557f85759eab71da93c4faed0ecbbfed948fe0db978b9a`; both byte-equal to the
stand-in's; options unit tests 821 passed in both trees (one check pinned the old "(default)" and now pins the new
words). These are the current numbers.

A server.exe older than 9081 has no such verb: the d234 take's (`C:/c5-64bit-work/d234/run/pack/vp64pack`) cannot feed
the page; `build_installer.py` refuses it (16.3).

### 16.2 The checks and the tree (`tools/installer/options_schema.py`) [C]

- **The default file's option lines.** A block is the lines between blank lines; its option line is its LAST line and
  counts only as `; <Key> = <whole number>` with nothing after it. The trap: ServerPath's help wraps so that one help
  line in `[Renderer]` reads exactly `; ServerRenderer = 1).` (found by the 9081 session). Section headers repeat
  (`[Renderer]` three times, `[Performance]` twice): sections are grouped by name in the order of first appearance
  (`Renderer, Performance, Proxy, Extras`).
- The blocks must name exactly the schema's keys, in the schema's sections, with its defaults and its help verbatim.
- Every schema row has a title of 1-45 characters without a trailing period, tab, line break or `|`; else refused. The
  page shows `<title>   (<key>)` as the line's label and the title above the help in the description.
- An option from 0 to 1 is a checkbox. Every other option's help must list value lines `  <n> = ...` or
  `  <a> to <b> = ...` (continuation lines indented further; also the one-line form `  0 = off (default). 1 = on ...`)
  covering min..max exactly once; each becomes a radio button whose caption is the value and its first clause (up to
  `: ` or `. `). Today: BakedTileCameraRadius (-1; 0 to 128), AiUpdateTickMs (0; 1 to 100; -1), AiSkipVisualDrain
  (0; 1; 2).
- The tree: `[Extras]` switches (category 1) at the top of `[Extras]`; an extra under the switch its help names
  ("needs <Switch> = 1"; all extras of a category must name the same switch and the two categories different ones),
  or under the extra its needsKey names; outside `[Extras]`, an option whose help says "needs <Key> = 1" with <Key> a
  checkbox of the same section sits under it (ServerUnits, ServerArc, ServerPath, ServerFarmFields, ServerCities,
  ServerLandmarks under ServerRenderer; ServerCombat under ServerUnits); AiDrainUnordered ("needs AiSkipVisualDrain
  = 1 or 2", not a checkbox) stays at the top of `[Performance]`. A cycle is refused.
- Starting values: the table's default, overlaid by the staged `VP64/release-options.txt` (i3). `[Renderer]
  ServerRenderer` is owned by `vp64helper on` (install.iss refuses it in a set): shown ticked and greyed for i2 and i3,
  unticked and greyed for thin (thin never runs `on`; the brief said "checked", which would be untrue for thin).
- std and m43 must give byte-equal schemas and default files.

Any failure refuses the build (exit 4, one `build_installer: REFUSED:` line). The only key named in Python is
`ServerRenderer` (as `HELPER_OWNED`); no option list is kept in `tools/installer`.

### 16.3 The build (`tools/installer/build_installer.py`) [C; L hermetic 2026-11-21]

After staging (and the D162 checks), each variant's STAGED `VP64/server.exe` runs with both verbs, working folder
`<out>/options/<variant>/` (where the two files stay for inspection), timeout 60 s, `CREATE_NO_WINDOW`. A server.exe
that cannot start, times out, exits non-zero or leaves a file missing is refused (exit 4) naming one-off 9081.
`payload.iss` gets `procedure P64OptData(Opts, Help, Vals, Items)` (rows: 16.4); `build-record.json` gets
`options_page`: each variant's server.exe SHA-256, the schema's and the default file's SHA-256, the option set used and
the tree as text lines.

### 16.4 The page (`tools/installer/patch64.iss`, "the options page") [C; L hermetic self-test 2026-11-21]

After the game-folder page, before the checks, skipped on Remove. `OptionsPageCreate` holds the whole layout (list,
number box, description); the rest is logic. Data rows from `P64OptData`: `Opts` 'section|key|default|min|max|kind
(B/R)|start|locked|parent|title', `Help`, `Vals` 'option|lo|hi|caption', `Items` 'S|section' or 'O|depth|option'.

**Finding [L, ISCC 6.2.2, 2026-11-21 23:40 EST]: `TNewCheckListBox.State[i]` cannot be written from a script** ("Column
5: Read-only property") although the help (`ISetup.chm`, topic_scriptclasses) lists it `read write`. Its `Checked[i]`
write runs the list's own tree logic: a checkbox's state is made from its children's (a parent on with all children
off shows unticked, or grayed with AHasInternalChildren), and unticking a parent unticks every child [I, from the help's
AddCheckBox parameters and Inno's components page; not stepped through]. That fights a mod-manager page, where a parent
is its own switch and a child keeps its tick while greyed. So every option line is a sibling at level 1 under its
section (radio buttons at level 2 under their option's group line), the tree is drawn by indenting captions (6 spaces
per depth), and the page keeps the values itself (`OptValue`) and greys a line whose parent is off (`ItemEnabled`).
A tree with real levels needs another control.

Starting ticks: the starting values of 16.2, then every uncommented `Key = value` line of the player's
`<My Games>\VP64Options.ini` that the proxy would take (a known key, a whole number in range, an inline `;`/`#` comment
cut, the last one wins; ServerRenderer ignored); loaded again when the My Games folder changes. An "a to b" radio's
number goes in a box under the list (checked on Next). The description box shows the selected option's help verbatim.
The ready page lists the choices that differ from the defaults. `WizardSizePercent=120`.

Silent runs: `/P64OPT=Section.Key=V;Section.Key=V` (test parameter; checked against the data, else the run refuses with
exit 1 and a `refused: /P64OPT ...` log line; ignored with a log line in an interactive run). Without it a silent run
has no choices: the behaviour before 9081. Test mode only: `/P64PAGETEST=<file> [/P64PAGECLICKS=Key;Key=V;...]` runs the
page's logic without a window (each click as the list's own toggle plus the page's handler) and writes every line's
state and the choices.

### 16.5 install.iss: the choices [C; L hermetic 2026-11-21]

Globals `P64OptSecs/Keys/Vals/Defs` (empty = no choices). `P64MergeChoices` (before anything changes): the release's set
(i3), each row overridden by the choice of the same key; every other choice added when it differs from the table's
default or when the player's file already sets it uncommented. ServerRenderer, a malformed or repeated choice, or a
UTF-16/binary options file refuse the install. Rows are written by the existing `P64WriteOptions` in every
configuration (before 9081: i3 only). Record: i2/i3 as before ('saved' entries, then 'options'); a thin install with
rows gets a 'saved' entry for `VP64Options.ini` (the rollback) and the 'options' entry (the removal puts the file back
by SHA-256, or deletes it when the installer created it). `install_stub.iss` records the choices as one line
`choices S.K=V(default);...`.

### 16.6 Pre-existing: install.iss took a help line for a commented default line [C; fixed 2026-11-21]

`P64IniKeyLine` counted any comment line that began `Key =` as the commented default line, and `P64IniSet` replaces the
FIRST commented match in the section when there is no uncommented one. In today's default file every key's own line
comes before any help line that looks like it (simulated for all 26 keys on the default file, 2026-11-21), and
ServerRenderer, the one key with such a help line (`; ServerRenderer = 1).` in ServerPath's help), is never in a set,
so no wrong line was written so far [C]. A player's or an older file with such a help line first would have had the
help line replaced. Fixed: a commented line counts only as `; Key = <whole number>` with nothing after it (as
vp64helper's scanner since ticket 4931). Test `InstallOptionsChoices.test_player_file_commented_default_line_not_a_help_line`
fails against install.iss without the fix (negative control through `P64TEST_INSTALL_ISS`, 2026-11-21) and passes
with it.

### 16.7 Tests [L hermetic, 2026-11-21, branch claude/oneoff-9081-installer-customizations-a02082]

- `python -I -X utf8 -m unittest discover -s tools/installer/tests -p test_options_schema.py -v`: the real verb outputs
  from the stand-in; the tree; ServerRenderer per configuration; the Pascal rows; the one-line value form; refusals
  (default, key, help, malformed rows, unknown escape, coverage gaps and overlaps, option-set rows, switch names, needs
  cycle; a server without the verb; std/m43 mismatch).
- `tools/installer/tests/test_install_removal.py`, class `InstallOptionsChoices`: thin file created and deleted on
  removal, choices at their defaults write nothing, the player's file (help-line trap, uncommented line), ServerRenderer
  refused, UTF-16 refused, a fault at every step rolls back, an i3 choice over the set, i2.
- `tools/harness/tests/test_installer.py` (the synthetic packs now carry the stand-in server.exe): the record and
  `payload.iss` data, the page self-test, silent `/P64OPT` reaching the install, bad `/P64OPT` refused, a server
  without the verbs refused.
Final runs [L, 2026-11-22 00:12-00:21 EST, through `quietlock.py run`, on the branch's last code]:
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider tools/installer/tests`: 95 passed in
426 s (was 36 in the runbook's 0627 row; this folder now holds test_options_schema.py's 22 and the include tests'
`InstallOptionsChoices` 8); `python -I -X utf8 -m unittest discover -s tools/harness/tests -p test_installer.py -v`: 56
tests OK in 117 s (51 before 9081's five); options unit tests 821 passed in each tree;
`tools/harness/tests/test_safety_static.py` and `test_runbook_paths.py`: 60 passed.

### 16.8 Not done, not verified

- The page has not been seen on a screen (hermetic self-test only); its look is being designed by the owner with the 9081
  session.
- No real installer was built: the d234 take's server.exe has no verb, and `build_installer.py` takes server.exe only
  from a take's pack (no code path for another one was added). The pipeline is proven by the hermetic build tests with
  the stand-in and by the real server.exe's byte-equal outputs (16.1).
- `docs/OPTIONS.md` said "the installer never writes an extra's own line" (Extras design, D170): the page lets the
  player tick an extra, and its line is then written. The owner's order of 2026-11-21 is the newer; the text now says what
  the page does.

### 16.9 [Renderer] UiDrawListGrowth is no longer an option (the owner, in person, 9081, 2026-11-22) [C; L builds and tests]

The owner's order (2026-11-22, in person, through the 9081 session [reworded for privacy]: the option is to be removed): the growth of the EXE's UI
draw buffers (ticket 0487) is always installed; the option goes. Done within the table's own rules: id 3 is a reserved
row (empty key, `Renderer`, no title; `kUiDrawListGrowth` renamed `kReserved3`, the `Options::UiDrawListGrowth()`
accessor removed), so no id renumbers. The proxy (`proxy/src/proxy_ui_bridge_rt.cpp`) calls `ProxyUiDraw::Configure(1)`
where it read the option; `proxy_uidraw.cpp` keeps its own build and byte checks and log lines (they still decide
whether the growth can be installed on the EXE build in use); its log and report no longer say "UiDrawListGrowth = 0
turns this off"; `Configure(0)` stays for the unit test only ("configured 0: off"). A player's file that still says
`UiDrawListGrowth = 0` gets the parser's normal warning "unknown option 'UiDrawListGrowth' (ignored)" and changes
nothing (`options/tests/test_options.cpp` `TestUiDrawListGrowth`, rewritten). `docs/OPTIONS.md` lost the section and
says in the format rules that the buffers are always enlarged; `docs/BETA-TESTERS.md` lost the table row. The options
page lists 25 options now.

Measured [L, 2026-11-22 00:20-00:26 EST]: `build_server.py ... --out C:/c5-64bit-work/9081/impl/server-build5` (server
and proxy, 112 s, OK); `server.exe` SHA-256 `63c46a2156f163ad516c69dd37399f6ee9ae9ea7bf73a6b0ec4dc624fe9167ee`; the
verbs give an ini of 10,681 B (`b8a6572effb07584835de3e8e59264f3bf55867ab7fbc67451825fc350dcd634`, no
`UiDrawListGrowth`) and a schema of 25 rows, 10,524 B (`9ba9903fec3cc73cea21a64c5e3c9b16551281a1a0e8fad325767fadd71a6722`).
These replace 16.1's numbers as the current ones. Builders linked for this change: `server/build_server.py` (server and
proxy), `proxy/tools/test_proxy_rt.py` (219/219 checks, `uidraw_unit` 274/274), the options tests in both `ipc/build.py`
trees (838 passed each; 10,681 B default text), `proxy/tests/options_boot/test_options_boot.py` (13/13, ASan included),
`server/tests/extras/run_extras_test.py` (OK), `server/tests/hidden_embark/run_hidden_embark_test.py` (PASS), all
2026-11-22 00:27-00:40 EST through `quietlock.py run`. The other builders of `vp64_options.cpp` (runbook 2.10) were not
linked: a grep of the whole tree finds no other use of `UiDrawListGrowth`, `kUiDrawListGrowth` or an aggregate `Def`
initialiser outside `options/` [C, 2026-11-22].

### 16.10 The page from a real take; FixAirBases ticked by default (one-off 9081, 2026-11-22) [C; L builds and tests]

A real installer with the page [L, 2026-11-22]: a take of the branch at `6a46d1d36` (stages survey to pack, standard
variant, no game stage; `--tier validated --carry pins`, the descriptor a copy of D234's `Release-5.4.8-i3-preview.json`)
ran 00:44:52-01:16:04 EST, the build stage 00:46:33-01:07:48 with its one LTO link; `build_installer.py --work <take>/run
--test-build` then gave `VP64-Patch-9081-options-for-VP5.4.8-TEST.exe`, 9,952,830 B, SHA-256
`3d7842f4bf3c9cebb3ab98bb2f7f48d5ba7970bdddef3285c9a5f42d826343a4` (test build: never to GitHub or players). The
command pattern is in the runbook, 1.13. This corrects 16.8's "no real installer was built". The page itself has still
not been checked on a screen by an agent; the owner has the test installer to click through.

FixAirBases default 1 [C]: the MC's D235 (2) (the owner, 2026-11-22; ticket 4985's live smoke at 01:26 passed) set
`[Extras] FixAirBases` to default 1 in `options/src/vp64_options.cpp` (row 26, listed in the options tests'
`kApprovedDefaults`), so the page shows it ticked, greyed while EnableBugfixes is unticked. Tests that had assumed 0
and now carry the row's default: `options/tests/test_options.cpp` (epic 0455's row check: default, the state under
the defaults, the out-of-range fallback), `tools/harness/tests/test_installer.py` (the page self-test: FixAirBases
`checked, disabled` under EnableBugfixes unticked), `server/tests/extras/units/test_units_extras.cpp` and
`test_fix_air_bases.cpp` (the module is on under the defaults; the "nothing on" checks now say `FixAirBases = 0` in the
file).

Merge check on the branch with main `6fb97b4bb` merged in [L, 2026-11-22 04:42-05:10 EST, through `quietlock.py run`]:
`tools/installer/tests` 95 passed; `test_installer.py` 56 OK; `test_hi_install_job.py` and `test_safety_static.py`
74 passed; options tests 838 passed in each `ipc/build.py` tree; `test_units_extras.exe` 13 of 13 cases;
`test_fix_air_bases.exe` 7 of 7; and the 18 builders of `vp64_options.cpp` in the runbook's 2.10 table linked one
after another (script `C:/c5-64bit-work/9081/links/run_links.sh`, results listed in ticket 9081's log).

### 16.11 RoadLayout a visual addition; the road options together on the page (the owner, in person, 2026-11-23) [C; L builds and tests]

The owner's decision (one-off 9081, 2026-11-23, after seeing the real page): [reworded for privacy] the main roads section is to move out of the
bugfix extras into the visual options, grouped with the rest of them. Asked whether the tidy layout (value 1)
should stay a fix, the owner chose to make all of it visual. `[Extras] RoadLayout` (id 18) is now `kCatVisual` with no `valueCategory`:
values 1 and 2 both need `EnableVisualAdditions = 1`, `EnableBugfixes` governs neither. This replaces ticket 4940's
category per value (1 a bugfix, 2 a visual addition, MC #8261); the `valueCategory` mechanism stays in the code with
no row using it (its rule is tested on synthetic rows). Default stays 0, so a default install is unchanged; a file with
`RoadLayout = 1` and visual additions off now gets stock roads. The reserved road bugfixes (ids 19-20, FixRoadBridges and
FixRoadClearance, which need RoadLayout) will need visual additions on too.

Order: `DefaultFileText` writes each [Extras] category by id, except that keys starting with `Road` close their
category together, so the default file and the page (which follows the file) show RoadLayout, RoadJunction, RoadJitter
last among the visual additions. RoadLayout's help names the switch as "(also needs EnableVisualAdditions = 1)", the
form `options_schema.py` reads to place an extra under its switch.

Mock 2 (built from the server.exe with the change): board "Roads" on the options-page canvas artifact.
Measured [L, 2026-11-23 22:22-23:55 EST, through `quietlock.py run`]: options tests 902 passed in each tree;
`test_options_schema.py` 22 OK; the 18 builders of `vp64_options.cpp` linked with their tests (`test_proxy_rt` 219/219,
`options_boot`, `run_extras`, `hidden_embark`, `test_units_extras`); `server/tests/extras/test_roads_scenes.py` 12
passed (830 s); `test_installer.py` 56 OK; `test_hi_install_job.py`, `test_safety_static.py`, `test_runbook_paths.py`
79 passed. Not verified: the page on a screen with this change (only the mock).

### 16.12 An update reset the player's VP64Options.ini; the update now keeps it (ticket 0733, 2026-11-24)

**What happened [L, the MC, 2026-11-24 23:29 EST].** Beta 4 (`0.1b4-std-only`, i3) installed silently over beta 3 on the
owner's game (`/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /ACTION=install /DIR=... /GAMEDIR=...`, no `/P64OPT`, exit 0;
log `C:/c5-64bit-work/d254-backup/install-beta4.log`). Before: `[Extras]` with EnableVisualAdditions, RoadLayout,
GarrisonVisible, FixAirBases, AirInterceptPatrol set, and `[Performance] AiDrainUnordered = 0`, `AiSkipVisualDrain = 0`.
After: no `[Extras]` section, both Performance keys 1. The MC put the file back from its SHA-256 backup.

**The cause [C: install.iss and that log].** An update is the earlier install's removal by its record, then a new
install (4.6). The removal ran `vp64helper off` (exit 0), which gave back the bytes `on` had found: the file as beta 3's
installer had written it, with the player's choices. Then `P64Revert` (mode 1) met the record's `options` entry (ticket
0574: H1 = the file before beta 3's install, H2 = after beta 3's write): the file hashed to H2, so it was "unchanged
since the install" and was put back to H1, the file as it was before beta 3 (log: `restored ...VP64Options.ini from its
backup (9497f85b...)`). The new install then wrote beta 4's option set over that old file: `AiDrainUnordered = 1`,
`AiSkipVisualDrain = 1` among its rows (log: `options: [Performance] AiDrainUnordered = 1 in ...`). The run was silent
without `/P64OPT`, so there were no choices (16.4: such a run has none). The options page code had loaded the player's
values (log lines `options page: EnableVisualAdditions = 1 from ...`), but a silent run does not use them. Two
consequences of the same design that had not been met yet:
- an interactive update kept the page's values for the keys the page lists (the page preloads them from the file before
  the removal, 16.4). It lost only what the first install wrote for keys the page's choices do not cover, because the
  removal put back the file before the FIRST install [C: code; L: hermetic, ticket 0735, 2026-11-25]. With the file as
  the first install left it, the page's choices cover every key that install changed, so main's put-back plus the
  page's values gives the same bytes as the fix (`test_page_update_over_an_unchanged_file` passes on both). A page that
  lists every option but ServerRenderer covers every key of a release's set, so in practice nothing was lost on this
  path [I]. Anything the player added after the install (other sections, comments, keys the page does not list)
  changes the file's hash and takes the next case's path instead. *Corrected 2026-11-25 (0735); was: "but lost
  everything else the file held since the first install (other sections, the player's comments, keys the page does not
  list)".*
- an update where the player had edited the file after the install (its hash no longer H2) left the file and KEPT the
  backup, so the record stayed and the new install refused with "an install record is already in" [C, code; L:
  hermetic on oldpc, 2026-11-25, rc 10, thin (#13542) and i3 with the page (0735)]. This holds for the page's path too:
  the real helper's `off` keeps an edit made after `on` and gives back only the ServerRenderer line (section 9).

**The fix (`tools/installer/install.iss`) [C; hermetic tests written, not run on the workstation: ticket 0728].**
- `P64Install` sets `P64ForUpdate` when an earlier install is found (a record, live or stale, or a hand install); the
  body is `P64InstallUpdate`, and a `try/finally` clears the flags after it.
- `P64Remove` for an update reverts in mode 3: as mode 1, but the `options` entry is not put back; the file stays as
  `vp64helper off` gives it back (the player's file without the `ServerRenderer` line `on` wrote). The entry's H1 and
  the bytes of its backup are carried in memory (`P64CarryOpt/Sha/Bytes`); a missing or damaged backup carries nothing
  (one log line) and the new install records the file as it finds it.
- `P64InstallCore`: on an update, `P64KeepPlayerValues` drops every row of the release's option set whose key the file
  already sets on an uncommented line to a whole number (`P64OptPlayerValue`: the last such line in the section, an
  inline ` ;`/` #` comment cut, as the proxy reads it); log `options: [S] K: the player's V kept over the release's W
  (update)` or `... = V already in ...`. Rows for keys the file does not set are written as before. Then the choices
  (page or `/P64OPT`) override as before (16.5), so a value the player changes on the page is written.
- The carried entry goes into the new record: its backup written again (`P64BackupBytes`, recorded as a `backup` step)
  and the new `options` entry's H1 = the file before the FIRST install, also when the update writes no row (a `saved`
  entry is then taken for the rollback, as for thin with choices). So the final removal still gives back the file as it
  was before the patch was first installed, and deletes a file the first install created.

**Keys the release must set** (the only ones an update writes over the player's file): `[Renderer] ServerRenderer`,
by `vp64helper on` (installer-notes 9), never by the option set (install.iss refuses it in a set). No other key is
forced. A release that must change a value for every player (a renamed meaning, a value that became harmful) would need
a marked row in `release-options.txt` that `P64KeepPlayerValues` does not drop: not built.

**The trade-off [I].** The installer cannot tell a value the player set from one an earlier release's set wrote: both
are uncommented lines. So on an update a value the earlier release wrote counts as the player's, and a later release
that changes its set's value for that key does not reach players who updated (a fresh install, or the options page,
does).

**Not changed:** a first install (no earlier install) writes the set over the file's values as before (silent), and
the page preloads the file's values as before (`patch64.iss` `OptLoad`, covered by `test_installer.py`'s page
self-test: `options page: RoadLayout = 1 from`). The removal (Remove the patch) is unchanged (mode 1).

**Tests** (`tools/installer/tests/test_install_removal.py`, class `InstallOptionsUpdate`; the stand-in helper's `on`
replaces the file and keeps the bytes it found in `vp64-options-side.ini`, so the tests read what the installer wrote
there):
- `test_silent_update_keeps_the_players_file`: a player's file with `[Extras]`, `AiDrainUnordered = 0`,
  `AiSkipVisualDrain = 0`, an unknown section with a non-ASCII note and an inline comment; release 1 (i3) installed,
  then release 2 (i3, a set with the two Performance keys at 1 and `ServerCities = 1`) silently without choices: the
  file the update wrote equals release 1's file with only `; ServerCities = 0` turned into `ServerCities = 1`; the
  final removal gives the whole tree back as before release 1.
- `test_page_update_keeps_the_players_file`: the same with the page's choices (every option at the player's value, one
  changed on the page) and, after release 1, the player's own lines added to the file (a note and `Later = 3` in the
  player's section; written into the stand-in's side file, because the real `off` keeps such an edit, section 9): the
  update's file is release 1's file with the edit, with only that key and ServerCities changed, and the final removal
  gives back the tree before release 1. Before the fix the update refused (rc 10, "an install record is already in").
  *Changed 2026-11-25 (0735): without the edit this test passed on install.iss before 0733 as well (#13542, oldpc job
  20261125-000029-379c), so it did not prove the fix; the reason is in the first consequence above.*
- `test_page_update_over_an_unchanged_file` (0735): the earlier form, without the edit. Passes before and after the fix
  by design, and stays as a check of the common case.
- `test_thin_update_keeps_a_file_the_installer_created`: thin, the file created by release 1's choices; release 2
  silent: the file kept byte for byte (before the fix the update's removal deleted it), the final removal deletes it.
- `test_player_edit_after_the_install_kept_on_update`: thin, the player edits the file after the install; the update
  goes through and keeps the edit (before: refused, the record kept).
Run on the workstation: only `ISCC /Q /O<scratch> tools/installer/tests/p64test.iss` (compiles, exit 0, 2026-11-24
before 23:41 EST; no window) and `py_compile` of the test file. NOT run: the tests themselves, which start silent Inno Setup
processes that take the foreground (ticket 0728). On a test machine (oldpc or the laptop, nobody at the desk):
`PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -I -m pytest -q -p no:cacheprovider tools/installer/tests/test_install_removal.py -k "InstallOptionsUpdate or InstallOptionsChoices or InstallRemoveI2 or test_install_twice or test_install_over_hand"`,
then the whole folder. Negative control: `git show main:tools/installer/install.iss > <scratch>/install-main.iss`, then
`P64TEST_INSTALL_ISS=<scratch>/install-main.iss` with `-k InstallOptionsUpdate`: the four tests must fail (expected
from the code: the first two on the file compare, the thin one on the missing file, the last on the refused update).

**Runs on oldpc [L, hermetic].** 2026-11-25 00:00 EST, job 20261125-000029-379c (the 0733 checker, #13542): the fix
passes; against install.iss before 0733 three of the four fail and `test_page_update_keeps_the_players_file` passes
(the gap 0735 closed). 2026-11-25 01:23 EST, job 20261125-012307-6e69 (0735), drivers compiled on the
workstation console (ISCC 6.2.2, pinned SHA-256 `29856cf8...`; clang for the stand-in helper; no window, no
installer run there):
- against install.iss at 9aa69eb9b (the one before 0733, SHA-256 `e783903b...`, driver `a1a0cfb44ab0`), `-k
  InstallOptionsUpdate`: 5 run, 4 fail. `test_page_update_keeps_the_players_file` FAIL (rc 10, "an install record is
  already in"), `test_silent_update_keeps_the_players_file` FAIL (the file compare: `AiDrainUnordered = 1`),
  `test_thin_update_keeps_a_file_the_installer_created` ERROR (VP64Options.ini missing), and
  `test_player_edit_after_the_install_kept_on_update` FAIL (rc 10). `test_page_update_over_an_unchanged_file` ok, as
  expected;
- against main's install.iss (SHA-256 `c1641098...`, driver `7dc582cf65b5`): the 5 ok, and the whole
  `test_install_removal.py` 50 tests OK in 89 s.

How, on a host without ISCC and clang (P64TEST_PREBUILT and P64TEST_INSTALL_ISS exclude each other, so the old
install.iss goes into the driver at compile time): `git show 9aa69eb9b:tools/installer/install.iss > <P>/src/old.iss`;
through `quietlock.py run`, with `MSYS_NO_PATHCONV=1` and Windows paths, run `ISCC.exe /Q /O<P>/old
"/DP64Include=<P>\src\old.iss" tools/installer/tests/p64test.iss` and the runbook's normal ISCC and clang lines into
`<P>/fix`, with the helper copied into both. Then submit `tb.py --host oldpc submit --file <P>/pre=pre --file
<job>=job` with a job that runs `python -I -X utf8 -m unittest discover -v -s tools/installer/tests -p
test_install_removal.py -k InstallOptionsUpdate` once with `P64TEST_PREBUILT=%TESTBED_BIN%\pre\old` and once with
`...\fix`, with `P64TEST_WORK` under `C:\c5-64bit-work`.

**Seen on a real install (2026-11-25 11:00, the MC, `[L]`):** the beta 4 hotfix 1 installer (`0.1b4h1-std-only`, built from
the beta 4 cut dc259f0b1, which has neither 0733 nor 0735) run as an update over beta 4 in the owner's game failed with
exit 20, exactly as this section describes: the owner's VP64Options.ini had been put back by hash after the beta 4
install, so the removal found it changed since the install, kept it, its backup and the record, and the install then
refused ("an install record is already in ...\VP64\install"). The removal itself had completed: the game was left on
stock Vox Populi with every Vox Populi Lua file and the 32-bit DLL restored. Recovery: the leftover `VP64\install`
folder moved aside (copied and verified by hash first), the installer run again as a fresh install (exit 0), the
owner's VP64Options.ini restored by hash. Any installer built from a cut older than 0733 and 0735 needs the same steps
when it updates an install whose options file was edited.

## 17. Vox Populi 5.5 (ticket 0767, D259, 2026-11-25)

Written by impl-0767 (account B), branch `impl-0767`. The installer for beta 5 is for Vox Populi 5.5 only.

### 17.1 What 5.5 changes for the installer [C: the fork clone, `git` reads only, 2026-11-25 14:4x EST]

- **VP's own installer**: `git diff Release-5.4.8 Release-5.5 -- VPSetupData.iss` is one line, `MyAppVersion "5.4.8"` to
  `"5.5.0"`. So section 1 holds for 5.5: the same `[InstallDelete]` (the whole `(1) Community Patch`, `(2)`, `(3a)`,
  `(3b)`, `(4a)`, `(5)` and My Games' `cache`), no version check, the modpack form untouched. The tag is `Release-5.5`
  (upstream `master` b0bce8c2f); the version the installer shows is the tag's, "5.5" (not VP's "5.5.0").
- **The 32-bit DLLs** are at the same paths (`(1) Community Patch/CvGameCore_Expansion2.dll`, the 43-civ one at
  `(3b) 43 Civs Community Patch/`): 22 majors 11,353,088 bytes, 43 majors 11,355,136 bytes (5.4.8: 16,837,632 and
  16,841,728). SHA-256 in `tools/installer/vp_releases.tsv` (regenerated, 17.3); a 5.5 release descriptor
  (`tools/release/releases/Release-5.5.json`, not this ticket's) needs them as `player_dll_sha256` and
  `player_dll_43_sha256`, since the pack's `x86_dlls` (what the installer accepts) come from there.
- **Vox Populi's own Lua fixes** (section 13 2b, ticket 0638): all three fix commits (475bd4997, 14d16fa3d, aa7a29b38;
  PRs 13463, 13467, 13539) are ancestors of `Release-5.5` (`git merge-base --is-ancestor`). `vp_lua_fixes.tsv` has rows
  for 5.4.8's DLLs only, so `build_installer.py vp_fix_rows` returns None for a 5.5 build: no box, no `payload/vpfix`,
  no Vox Populi file changed. Trap: `vp_lua_fixes.py --check` without `--release` now takes the latest tag, 5.5, and
  differs from the committed 5.4.8 table; give `--release Release-5.4.8`.
- **Saves**: Vox Populi 5.5 does not load games saved under 5.4.8 (upstream 0848d52cc, #13643, adds a field to
  `CvPlayer::Serialize` without a version guard; found by 0766) [C per lead-0763 #15167; not measured here].

### 17.2 The version check needs no code change; two texts were added [C: code]

`P64VPVersion` and `P64VPLink` come from the version table's row of the standard pack's first `x86_dlls` hash
(`vp_of`), else from the release name; the refusal of 4.3 is generic. A 5.5 build therefore says, to a 5.4.8 player:
"This patch is for Vox Populi 5.5. Your Vox Populi is 5.4.8. Install Vox Populi 5.5 from
https://github.com/LoneGazebo/Community-Patch-DLL/releases/tag/Release-5.5 and run this installer again." Added by 0767:
- `build_installer.py SAVE_NOTES` {build's VP version: (earlier version, sentence)} becomes `P64VPSaveFrom` /
  `P64VPSaveNote` in `payload.iss`; `patch64.iss` appends the sentence to that refusal only when the installed VP is
  `P64VPSaveFrom`: "Games saved under Vox Populi 5.4.8 do not load under Vox Populi 5.5 (a change in Vox Populi itself,
  not in the patch)." A 5.4.7 refusal has no such sentence (tested).
- D260 (the owner, relayed by lead-0763 #15190): the installer's links name upstream, no fork and no owner handle:
  `AppSupportURL` and the finished page's report link `https://github.com/LoneGazebo/Community-Patch-DLL/issues/new`,
  `AppPublisherURL` and both EXE-build refusals `https://github.com/LoneGazebo/Community-Patch-DLL/releases`,
  `notices.py REPORT_URL` the same issues link.
- D261 (the owner, relayed by lead-0763 #15298): the notice's "Source code:" lines name the public branch that holds
  the patch's `64bit/` folder and the DLL's 64-bit changes, with its commit: the fork's `x64` branch until the upstream
  merge, upstream after it. No address is written in `notices.py` any more (it named the patch's own repository and the
  fork, with the build's tools and DLL pin commits). `build_installer.py --source-commit` (required for a release build,
  refused with exit 4 otherwise), `--source-repo` (default: the release tools' setting `fork_url`, `.git` dropped),
  `--source-branch` (default `x64`), `--source-dll-commit` (default: `--source-commit`); recorded in
  `build-record.json` `source`. A test build without them says "this TEST build names no public source location" and
  lists the build's own commits. What 0759's take must pass: `--source-commit <the x64 branch's export commit>` (and
  `--source-dll-commit` if the DLL is to be named by the branch's first commit), with `fork_url` set as the lock stage
  already needs it. Open for lead-0763 [I]: the x64 branch's DLL commit is the defines-gated subset of the pin
  (0762), so whether it is the exact source of the built DLL is for the licence check of the take.

### 17.3 The update path from beta 4 on 5.4.8 [C: code; L: hermetic, 17.4]

What a beta tester meets (mods form, i3, the fixes box ticked as by default):
1. **The 5.5 installer before Vox Populi is updated** refuses (the kept `_x86` DLL is 5.4.8's), changes nothing, and
   beta 4 stays usable. Its **removal** works on 5.4.8: `RunChecks` checks no VP version for a removal.
2. **Vox Populi 5.5's installer over beta 4** deletes the patch's files with the mod folders, but not the record and
   backups in `My Games\VP64\install` and not the patch's D33 settings in `config.ini` (with `vp64-config-side.ini`
   beside it). A game started now runs plain VP 5.5 with D33 on: the red map of section 8. `vp64helper.exe` is gone with
   the folder, so the player has no off switch.
3. **The 5.5 installer then** finds the record stale (13.5: no added file left, and the DLL's place holds 5.5's DLL, not
   the proxy), runs the NEW payload's helper `off` first (`P64StaleHelperOff`, 12.7), so `config.ini` is the player's
   again, deletes the stale record and its backups (the 5.4.8 DLL's and the 5.4.8 Lua files' backups included; nothing
   of them is put back), and installs as an update (0733: the player's `VP64Options.ini` values kept).
So the docs (`docs/INSTALL.md` 5, `docs/BETA-TESTERS.md` 3) give the order: remove the patch first (with the new
installer), then Vox Populi 5.5, then the patch; and, for a player who already updated Vox Populi: do not start the
game, run the 5.5 installer first. A 5.4.8 modpack left beside a 5.5 in `MODS` is "Vox Populi is installed more than
once" (refused, nothing changed).

### 17.4 Tests

`tools/installer/tests/test_vp55.py` (new; runbook 1.13). It reuses `test_installer.py`'s fixtures but reads Vox
Populi's real 32-bit DLLs and the fix table's Lua files from the fork clone at test time (`git show`, read only; only
the real bytes have the real hashes; nothing of VP goes into the repository or an installer). Synthetic trees under
`C:/c5-64bit-work/0767/`, silent test-mode runs on a hidden desktop, `/P64RUNTIME=ok`; no game, no instance.

- `VersionCheckTests` (stub routines, the committed `vp_releases.tsv`): the build says 5.5, links Release-5.5, has no
  fixes box; 5.5's 22- and 43-civ DLLs install the std and m43 variants; 5.4.8's two are refused with the exact text
  and the save sentence ("5.4.8 (43 civilizations)" for the 43-civ DLL), nothing written; 5.4.7 is refused without
  the save sentence; no VP names 5.5; the table's 5.5 rows equal the clone's DLLs.
- `UpdateFromBeta4Tests` (the real `install.iss`, i3, a 5.4.8 "beta 4" and a 5.5 installer): (1) beta 4 on 5.4.8 ->
  5.5 refused, tree unchanged by SHA-256 -> VP 5.5's installer simulated -> the 5.5 install over the stale record
  (`off` first, the 5.4.8 DLL's backup gone, VP 5.5's Lua files untouched) -> removal gives exactly the tree VP 5.5's
  installer left, with the player's own `config.ini`; (2) the recommended order: the 5.5 installer removes beta 4 on
  5.4.8 (the original tree back by SHA-256, the 16 fixed Lua files back to 5.4.8's), then VP 5.5, then install and
  removal (VP 5.5's tree back); (3) both orders with a `VP64Options.ini` the player edited after beta 4's install: the
  5.5 install succeeds, the edit survives install and removal, no record left; (4) a 5.4.8 modpack beside VP 5.5 in
  MODS is refused, nothing changed.

**Results [L, hermetic, 2026-11-25, branch impl-0767, workstation, each through `quietlock.py run`]:**
`VersionCheckTests` 6/6 in 40 s (15:35); `UpdateFromBeta4Tests` 4/4 in each of three setups: the fixture installers
with the stand-in helper (15:41, 232 s; the edited-file test first failed on the test's own reading of the stand-in's
file and was corrected), the fixture installers with the real helper of beta 4 hotfix 1 (16:04, 224 s), and the
released beta 4 hotfix 1 installer (`0.1b4h1-std-only`, SHA-256 `99a406bd...`, a scratch copy) for the first install
with the 5.5 fixture shipping that real helper (16:1x, 229 s). `vp_releases.py --check` against the clone: up to
date, 365 rows (16:10-16:16). After the D261 notice change (16:28-16:39): `tools/harness/tests/test_installer.py`
61/61 (302 s; its D261 tests among them), `test_vpfix_build.py` 8/8, `test_vp55.py` 10/10 (stand-in helper),
`test_safety_static.py` 55 passed, `test_runbook_paths.py` exit 0.

**A fixture trap found on the way [L, 2026-11-25 15:5x].** `test_installer.Trees` writes `config.ini` with
`write_text('...\r\n...')` in text mode, so on Windows the file has CR CR LF line ends. The stand-in helper does not
read it; the real helper's `on` refuses: "config.ini has no [DisableSystems] section ... (start the game once so that
it writes one)", exit 1, and the install rolls back (exit 20). patch64.iss's own check (`ConfigIniOk`, a substring
search) accepts the same file. Not a player case (the EXE writes CRLF), but any test that ships the real helper must
write `config.ini` as bytes (`test_vp55.py game_case`). The installer's message "its line is in <My Games>\Logs\VP64"
is wrong for `on`: the helper writes its line only to stdout, which the installer's hidden `Exec` drops; it was
reproduced by running the helper by hand on a copy of the fixture's file.

### 17.5 Not verified

- A live run: the real 5.5 installer (from a 5.5 take, 0765) on a real game install updated from beta 4, and a game
  started after it. It rides on 0759's C5 tier (MC-B #15112).
- The modpack form of the update (only the "twice" refusal is tested).
- The save sentence's cause (upstream 0848d52cc) is lead-0763's / 0766's finding, not measured here.
