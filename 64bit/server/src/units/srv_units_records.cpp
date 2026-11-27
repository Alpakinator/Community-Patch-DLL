// srv_units_records.cpp - the unit feed's records 3-6 of feed.bin (epic 0375, issue 4457). Written 2026-11-16 for this
// repository (GPL-3.0-or-later). Forms: srv_units_records.h; research/05-graphics-composition/unit-server-scene.md 11.
//
// The codec is defined inline in the header (see its implementation block): this unit only checks the layouts it relies on.
#include "srv_units_records.h"

static_assert(sizeof(SrvFeed::UnitRecord) == 32, "UNITS: 32 B per unit (srv_feed.h)");
static_assert(sizeof(SrvFeed::PlayerRecord) == 32, "PLAYERS: 32 B per row (srv_feed.h)");
static_assert(sizeof(SrvFeed::CombatRecord) == 64, "COMBATS: 64 B per combat (srv_feed.h)");
static_assert(sizeof(SrvFeed::CombatMember) == 28, "COMBATS: 28 B per member (srv_feed.h)");
static_assert(sizeof(SrvFeed::PathEntry) == 12, "PATHS: PathEntry (srv_feed.h)");
