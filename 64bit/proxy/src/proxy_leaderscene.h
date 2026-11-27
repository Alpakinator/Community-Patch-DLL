// proxy_leaderscene.h - 3D leader scenes for majors above slot 21 (ticket 0381). Hand-written.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// What and why (research/02-exe-internals/leader-scene-player-limit.md, sections 1, 2 and 7):
//   - the EXE loads an AI leader's 3D scene only when ICvEngineUtility1::NotifySpecificAILeaderInGame(player) (engine
//     utility slot 181, VA 0x68B370) is called for that player; its own NotifyAILeadersInGame (slot 180, 0x694160), run
//     once per game load, calls it for players 0-21 only. So in a 43-major DLL build majors 22-42 get no scene: the
//     diplomacy screen has no 3D leader, and the EXE's debug text "THERE IS NO SCENE LOADED FOR THIS LEADER!!" stays at
//     the top-left of later 3D leader screens;
//   - slot 181 is the EXE's own exported behaviour: the DLL already calls it (a disconnected human's slot passing to the
//     AI). The proxy, which sits in the DLL's place, calls it for the AI majors above 21 whose scene is not in the EXE's
//     scene cache. Nothing of the EXE is written; its scene cache (a std::map player -> scene in .data, VA 0xDA27A4) is
//     only read, to know which scenes are loaded and when a new game session has emptied it.
// The mode (MC's return of 0381, 2026-11-16: each scene costs the 32-bit EXE about 32 MB of address space, research 7.8):
//   vpproxy.ini leader_scenes = 0 (the default: off, the stock behaviour)
//                             | 1 (on demand: the proxy's hook on the DLL's GameplayDiplomacyAILeaderMessage asks the EXE
//                                  for that one player's scene just before it forwards the message, when the player is
//                                  22..MAX_CIV_PLAYERS-1 and has no scene in the cache; the EXE's own message checks
//                                  skip a human, a never-alive player and the active player; only leaders that open a
//                                  screen cost address space)
//                             | 2 (all at load: the Driver below asks for every AI major above 21 from the Update hook,
//                                  0381's first version, kept for comparison)
//   VPPROXY_LEADER_SCENES = 0 | 1 | 2 in the EXE's environment overrides the ini (a test switch).
// Mode 2's timing: from ProxyRtInt::BeforeUpdate (the EXE's game-core thread, the top of each ICvGame1::Update), the
// thread the DLL itself calls slot 181 from. At most one cache read a second; an evaluation of the slots when the cache's
// keys change, else at most every 10 s. Each player is asked at most twice per game session, 30 s apart; only an
// ever-alive, non-human, non-active, non-minor player (the major/minor split is read from the DLL at run time, issue
// 4253), and only when the cache holds at least one scene and the game is final-initialised.
// Gates of both modes: the DX11 EXE build 0x546CD0A8 with the cache in a writable, non-executable section (else off,
// one log line). With the 22-major DLL every slot from 22 is a city state: mode 2 asks nothing, mode 1 never sees a
// leader message for one (city states have no leader screen).
// Every action is one "leaderscene:" line in vpproxy.log.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

namespace ProxyLeaderScene
{
// From ProxyRtInt::BeforeUpdate (proxy_update.cpp). Cheap: a clock test on most Updates (mode 2), nothing otherwise.
void OnUpdate();

enum Mode
{
	kModeOff = 0,
	kModeDemand = 1,
	kModeAll = 2,
};
// The mode from the environment variable's value (NULL when unset) and vpproxy.ini's text (NULL when there is none);
// why receives one sentence for the log. A value other than 0, 1, 2 is off, and why says so.
int DecideMode(const char* envValue, const char* iniText, std::string& why);
// Mode 1: true when the proxy should ask the EXE for player's scene before forwarding its leader message. readResult and
// c are the cache read (kReadOk with the keys, or a failure: then the player is asked, since the EXE's handler loads a
// scene only when it is missing).
bool DemandWants(int player, int readResult, uint64_t keys);

// ---- the parts the offline test (proxy/tools/rt_tests/leaderscene_unit.cpp) uses; the product calls them through
// OnUpdate and the leader-message hook only ---------------------------------------------------------------------------
// The scene cache (the leader-head object + 0x24, a std::map) is the build table's lsc_scene_map (ticket 4963,
// exebuild/symbols/proxy.tsv); its RVA in a build, 0 when the build does not carry it:
uint32_t SceneMapRva(int build);
const uint32_t kMapHeadOff = 4;             // MSVC 2008 _Tree: +4 head node, +8 size
const uint32_t kMapSizeOff = 8;
const int kFirstSlot = 22;     // the EXE's own loop ends at 21 (0x694160): the first slot it never asks for
const int kCivPlayers = 63;    // MAX_CIV_PLAYERS, the same in both DLL builds (the barbarians, slot 63, are not a leader)
const int kMaxKeys = 64;
const double kWalkEveryMs = 1000.0;
const double kEvalEveryMs = 10000.0;
const double kRetryAfterMs = 30000.0;
const int kMaxAttempts = 2;
const int kMaxFaults = 5;      // cache reads that faulted (SEH) before the job turns itself off for the process

enum ReadResult
{
	kReadOk = 0,
	kReadBad = 1,    // the structure did not pass the checks (size, keys, links): not used this time
	kReadFault = 2,  // an access violation while reading (caught): counted
};
struct CacheRead
{
	uint32_t size;
	uint64_t keys;  // bit k: player k has a scene in the cache
};
// Reads the std::map<int, scene*> whose object is at mapObj (x86 layout: +4 head node, +8 size; node: left +0,
// parent +4, right +8, key +0xC, value +0x10, isnil byte +0x15). Bounded, validated (head isnil 1, nodes isnil 0, keys
// 0..63 without duplicates, at most 64 nodes, the count equal to the size) and run under SEH.
int ReadCache(uintptr_t mapObj, CacheRead& out);
// NULL if the image at base is a build the patch knows (PE stamp and SizeOfImage), its table carries the cache's address
// and the cache lies in a writable, non-executable section of it; else the reason not to touch it. *sceneMapRva (may be
// NULL) gets the cache's RVA in that build when NULL is returned.
const char* CheckExe(uintptr_t base, uint32_t* sceneMapRva = 0);

struct SlotFacts
{
	bool exists;     // the game has a player object for the slot
	bool everAlive;
	bool human;
	int civ;         // the slot's civilization type (pre-game), the key of the minor cache
};

// What the driver needs from the EXE and the DLL; the product's implementation is in proxy_leaderscene.cpp, the test's
// in leaderscene_unit.cpp.
class Env
{
public:
	virtual ~Env() {}
	virtual double NowMs() = 0;
	virtual int ReadCache(CacheRead& out) = 0;   // a ReadResult
	virtual bool BeginEval() = 0;                // false: no game, or not final-initialised (EndEval is not called)
	virtual int ActivePlayer() = 0;
	virtual bool Facts(int slot, SlotFacts& f) = 0;
	virtual bool IsMinor(int slot) = 0;          // the DLL's answer (crosses to the server)
	virtual void Notify(int slot) = 0;           // ICvEngineUtility1::NotifySpecificAILeaderInGame
	virtual void EndEval() = 0;
	virtual void Log(const char* line) = 0;
};

class Driver
{
public:
	Driver();
	void Tick(Env& env);
	bool Off() const { return m_off; }
	uint32_t Generation() const { return m_generation; }
	uint32_t Notifies() const { return m_notifies; }
	uint32_t Evaluations() const { return m_evals; }

private:
	void NewGeneration(Env& env, const CacheRead& c);
	void Logf(Env& env, const char* fmt, ...);

	bool m_off;
	bool m_walked;
	bool m_evaluated;
	bool m_badLogged;
	double m_lastWalk;
	double m_lastEval;
	uint64_t m_lastKeys;
	uint32_t m_lastSize;
	uint64_t m_evalKeys;
	uint32_t m_generation;
	uint32_t m_faults;
	uint32_t m_notifies;
	uint32_t m_evals;
	int m_attempts[kMaxKeys];
	double m_lastNotify[kMaxKeys];
	bool m_gaveUp[kMaxKeys];
	bool m_minorKnown[kMaxKeys];
	bool m_minor[kMaxKeys];
	int m_minorCiv[kMaxKeys];
};
}  // namespace ProxyLeaderScene
