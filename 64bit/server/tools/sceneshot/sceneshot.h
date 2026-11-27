// sceneshot.h - the offline scene harness's player: a recorded scene bundle in, the server's own picture (colour PNG
// and raw depth) of a recorded frame out, drawn by the same render units server.exe links (epic 0301, task 3905;
// contract: research/06-tooling-and-workflows/offline-scene-harness.md sections 2, 4, 6, 7, 10 and "sceneshot").
// Written 2026-11-15 for this repository (GPL-3.0-or-later).
//
//   sceneshot.exe --bundle DIR (--frame ID | --span FIRST:LAST | --camera NAME | --dumped) --out DIR
//                 [--compare] [--diff] [--no-pictures] [--adapter bundle|hw|warp|<LUID hex>] [--install DIR] [--exe PATH]
//                 [--settle-ms MS] [--bake-rate R] [--bake-burst N] [--bake-budget-ms MS]
//                 [--tolerance PIXELS:DELTA] [--allow-other-exe] [--allow-other-assets]
//                 [--mods DIR (the decals' mods folder; default render_config's render_decal_mods: issue 0364)]
//                 [--set render_KEY=VALUE (diagnostics, not P1)] [--units-no-history (issue 0440, diagnostics, not P1)]
//                 [--option KEY=VALUE (ticket 4938: an [Extras] line of VP64Options.ini, repeatable; an extra on is not P1)]
//                 [--asset-root DIR (ticket 0614; else the environment's C5_SCENE_ASSET_ROOT): the install and the mods'
//                  folders from a frozen asset root made by assetroot.py, checked against its asset-root.txt; not with
//                  --install or --mods]
// Ticket 0614 (offline-scene-harness.md 34): a source path (the install, the EXE, the mods' Documents folder, the asset
// root) inside a pool instance whose claim file exists (<pool dir>\claims\<name>.json, the rule of tools/harness/pool.py
// holder) is refused (kInstall), before anything is read and again before the OK line; a damaged or changed asset root is
// refused too. Nothing falls back to another source.
// Issue 0440 (format version 5): a bundle with render_units=1 is replayed with the server's unit scene (server/src/units/,
// linked): its database from the units_db member (written into --out), its controller's state from units_history.bin
// (offline-scene-harness.md 22).
//
// Every output line starts with "sceneshot: " (the render units' notes with "render: "); the last line is
// "sceneshot: OK ..." or "sceneshot: FAILED <exit code> <name>: <why>". The exit codes are the enum below; a bundle the
// checker refuses exits with scenebundle::RefusalExitCode (21..42, after its "scenebundle: REFUSED ..." line), a missing
// bundle included (NoManifest, 21): it fails, it never skips.
#pragma once
#include <string>
#include <vector>

namespace sceneshot {

enum Exit
{
	kOk = 0,
	kUsage = 2,              // bad command line
	kNoCamera = 3,           // --camera NAME: no cameras/NAME.vpcam in the bundle
	kFrameOutside = 4,       // --frame / --span: not a recorded frame of the span
	kSettleTimeout = 5,      // the residency did not settle within --settle-ms: no picture
	kResidencyStopped = 6,   // the residency stopped (out of memory at radius 0): no picture
	kNoDevice = 7,           // the adapter (the bundle's, or --adapter) is not here, or the device failed
	kReplayFailed = 8,       // a member the scene refused, the feed out of order, a draw error, a frame flag the replay does not reproduce
	kInstall = 9,            // the install or the EXE is missing, or differs from the bundle's (pins.exe_sha256, asset_set); a source
	                         // in a claimed pool instance, or an asset root that is damaged or changed (ticket 0614)
	kDiffers = 10,           // --compare: a frame's depth is not byte-equal to the server's own dump, or its colour beyond --tolerance (default: equal bytes)
	kWriteFailed = 11,       // an output file could not be written
	kNoDump = 12,            // --compare on a frame the recorder did not dump
	kUnsupported = 13,       // a bundle the player cannot replay (render_tiles=tap, a render_config value refused, a load inside the span)
};
const char* ExitName(int code);

// One output line (without the newline).
typedef void (*LineFn)(void* ctx, const char* line);

// The whole tool: args as on the command line (without the program name). Returns the exit code. Not re-entrant (the
// render units' log is process-wide); one call at a time.
int Run(const std::vector<std::string>& args, LineFn line, void* ctx);

}  // namespace sceneshot
