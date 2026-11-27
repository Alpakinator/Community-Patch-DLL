// srv_options.h - the server's view of the patch's options file (one-off 9042, decisions D89 and D88). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The server never looks for the file itself: it reads the file server.cfg names as options_file=<UTF-8 path> (the
// proxy writes that line at every mode=on boot; run_server.py --options PATH for direct runs). Without the line every
// option is at its default. The renderer runs inside the server and reads its options here, for example epic 3400:
//   const int radius = SrvOptions().BakedTileCameraRadius();  // -1 = keep the whole map baked (D88)
// Loader, rules and the list of options: options/include/vp64_options.h; players' description: docs/OPTIONS.md.
#pragma once
#include "vp64_options.h"

// Called once by srv_main.cpp after server.log is open, before the DLL is loaded (direct and remote mode). Logs the
// file, every warning and every value ("options: ...").
void SrvOptionsLoad();
// The values in force (the defaults until SrvOptionsLoad has run). Read-only after start-up: any thread.
const Vp64Opt::Options& SrvOptions();
