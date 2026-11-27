// test_player_limits.cpp - issue 4253: the server's run-time major count and the boot guard on the player totals, on
// a real game-database connection (CvGameDatabase_x64.dll of the pinned build).
// Copyright (C) 2026 the c5-64bit-patch authors. GPL-3.0-or-later.
//
// Built and run by server/tests/limits/run_player_limits_test.py, which extracts the block "4253 PLAYER LIMITS" of
// server/src/srv_main.cpp into srv_limits.gen.h, so the code tested here is the server's own text. The compiled totals
// are the fork's (CvDLLUtilDefines.h at the pin), as srv_main.cpp's SrvCheckPlayerLimits passes them.
//
//   test_player_limits <database file>
//
// Opens the database read only as the server opens its own (Connection::Open) and runs vplimits::Check. Prints
// "RESULT PASS majors <n> minors <n>" (exit 0) or "RESULT FAIL <why>" (exit 1); 2 when the file cannot be opened.
#define CvGameDatabaseAPI __declspec(dllimport)
#include <windows.h>
#include <limits.h>
#include <stdio.h>
#include <string>
#include "CvDLLUtilDefines.h"
#include "srv_limits.gen.h"

int main(int argc, char** argv)
{
	if(argc != 2)
	{
		printf("usage: test_player_limits <database file>\n");
		return 2;
	}
	Database::Connection db;
	if(!db.Open(argv[1], Database::OPEN_READONLY))
	{
		printf("RESULT SETUP cannot open %s\n", argv[1]);
		return 2;
	}
	const vplimits::Totals compiled = {MAX_PLAYERS, MAX_TEAMS, MAX_CIV_PLAYERS};
	vplimits::Limits got = {};
	std::string why;
	const bool ok = vplimits::Check(db, compiled, got, why);
	if(ok)
		printf("RESULT PASS majors %d minors %d players %d teams %d civ_players %d\n", got.iMajors, got.iMinors,
		       got.iPlayers, got.iTeams, got.iCivPlayers);
	else
		printf("RESULT FAIL %s\n", why.c_str());
	fflush(stdout);
	return ok ? 0 : 1;
}
