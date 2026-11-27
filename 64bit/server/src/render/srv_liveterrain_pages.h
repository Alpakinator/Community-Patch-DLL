// srv_liveterrain_pages.h - an EXE height page of the field with live windows written over it, built without writing the
// field (epic 0465 follow-up, ticket 0587). Written 2026-11-19 for this repository (GPL-3.0-or-later).
// Findings and proof: research/05-graphics-composition/live-terrain.md 8.14.8.
//
// Why: ticket 4909's live apply built the pages a change reaches on a worker thread from the scene's field, so it wrote
// the windows into that field (and into the EXE's field) in its first render-thread step and drew the old pages until its
// second step, one or two polls later: for those frames the EXE and the server's grid held the new heights while the
// server drew the old ones (live-terrain.md 8.14.6). With this unit the worker builds each page from the field before the
// windows plus the windows themselves, so the EXE write, the field's ApplyWindow and the region update all run in the
// second step, in one poll: no frame is drawn with the field and the pages disagreeing.
//
// How: page p of bake::buildHeightPage reads the samples (p.px * 896 - 64 + u) mod samplesX by (p.py * 896 - 64 + r) mod
// samplesY, u, r = 0..1023 (c5r/bake/heightpage.h). Those 1024 x 1024 samples, with every window's rectangles written
// over them in order (the order ApplyWindow writes them), are copied into a 16 x 16 plot (1024 x 1024 sample) scratch
// grid laid out so that its page (0, 0) reads them in the same places; buildHeightPage of that page is then, level by
// level, the page buildHeightPage builds from the edited field (the mips read only level 0). The page keeps the index p.
// Checked against buildHeightPage of the edited field for every page of wrapped and unwrapped fields, fields narrower than
// a page, windows on the seams and overlapping windows (server/tests/render/liveterrain/pages/test_pages.cpp).
#pragma once

#include <stdint.h>
#include <vector>

#include "c5r/bake/heightpage.h"
#include "c5r/heightfield/heightfield.h"
#include "c5r/status.h"
#include "srv_liveterrain.h"

namespace srvrender { namespace liveterrain {

// Page p (mips 1 or 11, as buildHeightPage) of g with every window of `windows` written over g's heights in order, g
// itself unchanged. Errors: those of buildHeightPage for g and p; BadLayout for a rectangle outside g or whose heights
// are not w x h (ApplyWindow's checks; nothing is built). On error `out` is empty.
c5r::Status BuildPageWithWindows(const c5r::heightfield::HeightGrid& g, c5r::bake::HeightPageIndex p, uint32_t mips,
                                 const std::vector<const std::vector<Rect>*>& windows, c5r::bake::HeightPage& out);

}}  // namespace srvrender::liveterrain
