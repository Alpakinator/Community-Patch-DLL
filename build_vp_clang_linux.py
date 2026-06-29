#!/usr/bin/env python3
# ============================================================================
# build_vp_clang_linux.py - Linux-native VP DLL build script
# ============================================================================
# Cross-compiles CvGameCore_Expansion2.dll using clang-cl + lld-link.
#
# Works on: bare Linux, Docker container, WSL
# Windows devs: use build_vp_clang_sdk.py or build_vp_clang.py instead.
#
# Prerequisites:
#   - clang + lld (Arch: pacman -S clang lld)
#   - Windows SDK 7.0 + VC9 CRT headers/libs extracted to a known directory
#     (set WIN_SDK_DIR env var or use --sdk-dir)
#
# The Dockerfile handles SDK extraction automatically.
#
# Usage:
#   python build_vp_clang_linux.py --config release
#   python build_vp_clang_linux.py --config debug
#   python build_vp_clang_linux.py --sdk-dir /path/to/sdk --config release
# ============================================================================

import os
import subprocess
import sys
from enum import Enum
import typing
import time
import tempfile
from pathlib import Path
import argparse
from queue import Queue


class Config(Enum):
    Release = 0
    Debug = 1


# ---------------------------------------------------------------------------
# Paths — shared with the Windows build scripts, adapted for Linux
# ---------------------------------------------------------------------------
CORE_DLL = 'CvGameCore_Expansion2'
PROJECT_DIR = Path(__file__).parent.resolve()

# SDK location: env var, --sdk-dir flag, or default
def _get_sdk_dir() -> Path:
    env = os.environ.get('WIN_SDK_DIR', '')
    if env:
        return Path(env)
    # Common fallbacks
    for candidate in ['/opt/win-sdk', os.path.expanduser('~/.local/share/vp-sdk')]:
        if Path(candidate).is_dir():
            return Path(candidate)
    return Path('/opt/win-sdk')

SDK_DIR = _get_sdk_dir()
SDK_INCLUDE = SDK_DIR / 'Include'
SDK_LIB = SDK_DIR / 'Lib'

# VC9 CRT headers are merged into SDK_INCLUDE by the Dockerfile.
# On bare Linux, point WIN_SDK_DIR at a directory containing both.
INCLUDE_PATHS = [SDK_INCLUDE]
# Libraries may be in architecture subdirectories (x86, x64, etc.)
LIB_PATHS = [SDK_LIB, SDK_LIB / 'x86']

BUILD_DIR = {
    Config.Release: Path('clang-build/Release'),
    Config.Debug: Path('clang-build/Debug'),
}
OUT_DIR = {
    Config.Release: Path('clang-output/Release'),
    Config.Debug: Path('clang-output/Debug'),
}

# Pre-built static libraries — checked into the repo as COFF .lib / .obj
LIBS = [
    'CvWorldBuilderMap/lib/CvWorldBuilderMapWin32.obj',
    'CvGameCoreDLLUtil/lib/CvGameCoreDLLUtilWin32.lib',
    'CvLocalization/lib/CvLocalizationWin32.lib',
    'CvGameDatabase/lib/CvGameDatabaseWin32.lib',
    'FirePlace/lib/FireWorksWin32.obj',
    'FirePlace/lib/FLuaWin32.lib',
    'ThirdPartyLibs/Lua51/lib/lua51_Win32.lib',
]
DEFAULT_LIBS = [
    'winmm.lib',
    'kernel32.lib',
    'user32.lib',
    'gdi32.lib',
    'winspool.lib',
    'comdlg32.lib',
    'advapi32.lib',
    'shell32.lib',
    'ole32.lib',
    'oleaut32.lib',
    'uuid.lib',
    'odbc32.lib',
    'odbccp32.lib',
    'msvcrt.lib',
    'oldnames.lib',
]
DEF_FILE = 'CvGameCoreDLL_Expansion2/CvGameCoreDLL.def'
INCLUDE_DIRS = [
    'CvGameCoreDLL_Expansion2',
    'CvWorldBuilderMap/include',
    'CvGameCoreDLLUtil/include',
    'CvLocalization/include',
    'CvGameDatabase/include',
    'FirePlace/include',
    'FirePlace/include/FireWorks',
    'ThirdPartyLibs/Lua51/include',
]
SHARED_PREDEFS = [
    'FXS_IS_DLL',
    'WIN32',
    '_WINDOWS',
    '_USRDLL',
    'EXTERNAL_PAUSING',
    'CVGAMECOREDLL_EXPORTS',
    'FINAL_RELEASE',
    '_CRT_SECURE_NO_WARNINGS',
    '_WINDLL',
]
RELEASE_PREDEFS = SHARED_PREDEFS + ['STRONG_ASSUMPTIONS', 'NDEBUG', 'VPRELEASE_ERRORMSG']
DEBUG_PREDEFS = SHARED_PREDEFS + ['VPDEBUG']
PREDEFS = {
    Config.Release: RELEASE_PREDEFS,
    Config.Debug: DEBUG_PREDEFS,
}
# Suppressed warnings (identical to Windows clang build)
CL_SUPPRESS = [
    'invalid-offsetof',
    'tautological-constant-out-of-range-compare',
    'comment',
    'c++11-narrowing',
    'enum-constexpr-conversion',  # TODO: #9786 (same as Windows build)
]
PCH_CPP = 'CvGameCoreDLL_Expansion2/_precompile.cpp'
PCH_H = 'CvGameCoreDLLPCH.h'
PCH = 'CvGameCoreDLLPCH.pch'

# Source file list — identical to build_vp_clang_sdk.py
CPP = [
    'CvGameCoreDLL_Expansion2/Lua/CvLuaArea.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaArgsHandle.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaCity.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaDeal.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaEnums.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaFractal.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaGame.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaGameInfo.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaLeague.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaMap.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaPlayer.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaPlot.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaSupport.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaTeam.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaTeamTech.cpp',
    'CvGameCoreDLL_Expansion2/Lua/CvLuaUnit.cpp',
    'CvGameCoreDLL_Expansion2/CustomMods.cpp',
    'CvGameCoreDLL_Expansion2/CvAchievementInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvAchievementUnlocker.cpp',
    'CvGameCoreDLL_Expansion2/CvAdvisorCounsel.cpp',
    'CvGameCoreDLL_Expansion2/CvAdvisorRecommender.cpp',
    'CvGameCoreDLL_Expansion2/CvAIOperation.cpp',
    'CvGameCoreDLL_Expansion2/CvArea.cpp',
    'CvGameCoreDLL_Expansion2/CvArmyAI.cpp',
    'CvGameCoreDLL_Expansion2/CvAStar.cpp',
    'CvGameCoreDLL_Expansion2/CvAStarNode.cpp',
    'CvGameCoreDLL_Expansion2/CvBarbarians.cpp',
    'CvGameCoreDLL_Expansion2/CvBeliefClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp',
    'CvGameCoreDLL_Expansion2/CvBuildingClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvBuildingProductionAI.cpp',
    'CvGameCoreDLL_Expansion2/CvCity.cpp',
    'CvGameCoreDLL_Expansion2/CvCityAI.cpp',
    'CvGameCoreDLL_Expansion2/CvCityCitizens.cpp',
    'CvGameCoreDLL_Expansion2/CvCityConnections.cpp',
    'CvGameCoreDLL_Expansion2/CvCityManager.cpp',
    'CvGameCoreDLL_Expansion2/CvCitySpecializationAI.cpp',
    'CvGameCoreDLL_Expansion2/CvCityStrategyAI.cpp',
    'CvGameCoreDLL_Expansion2/CvContractClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvCorporationClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvCultureClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvDangerPlots.cpp',
    'CvGameCoreDLL_Expansion2/CvDatabaseUtility.cpp',
    'CvGameCoreDLL_Expansion2/CvDealAI.cpp',
    'CvGameCoreDLL_Expansion2/CvDealClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp',
    'CvGameCoreDLL_Expansion2/CvDiplomacyRequests.cpp',
    'CvGameCoreDLL_Expansion2/CvDistanceMap.cpp',
    'CvGameCoreDLL_Expansion2/CvDllBuildInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllBuildingInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllCity.cpp',
    'CvGameCoreDLL_Expansion2/CvDllCivilizationInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllColorInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllCombatInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllContext.cpp',
    'CvGameCoreDLL_Expansion2/CvDllDatabaseUtility.cpp',
    'CvGameCoreDLL_Expansion2/CvDllDeal.cpp',
    'CvGameCoreDLL_Expansion2/CvDllDealAI.cpp',
    'CvGameCoreDLL_Expansion2/CvDllDiplomacyAI.cpp',
    'CvGameCoreDLL_Expansion2/CvDllDlcPackageInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllEraInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllFeatureInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllGame.cpp',
    'CvGameCoreDLL_Expansion2/CvDllGameAsynch.cpp',
    'CvGameCoreDLL_Expansion2/CvDllGameDeals.cpp',
    'CvGameCoreDLL_Expansion2/CvDllGameOptionInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllGameSpeedInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllHandicapInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllImprovementInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllInterfaceModeInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllLeaderheadInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllMap.cpp',
    'CvGameCoreDLL_Expansion2/CvDllMinorCivInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllMissionData.cpp',
    'CvGameCoreDLL_Expansion2/CvDllMissionInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllNetInitInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllNetLoadGameInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllNetMessageExt.cpp',
    'CvGameCoreDLL_Expansion2/CvDllNetMessageHandler.cpp',
    'CvGameCoreDLL_Expansion2/CvDllNetworkSyncronization.cpp',
    'CvGameCoreDLL_Expansion2/CvDllPathFinderUpdate.cpp',
    'CvGameCoreDLL_Expansion2/CvDllPlayer.cpp',
    'CvGameCoreDLL_Expansion2/CvDllPlayerColorInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllPlayerOptionInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllPlot.cpp',
    'CvGameCoreDLL_Expansion2/CvDllPolicyInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllPreGame.cpp',
    'CvGameCoreDLL_Expansion2/CvDllPromotionInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllRandom.cpp',
    'CvGameCoreDLL_Expansion2/CvDllResourceInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllScriptSystemUtility.cpp',
    'CvGameCoreDLL_Expansion2/CvDllTeam.cpp',
    'CvGameCoreDLL_Expansion2/CvDllTechInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllTerrainInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllUnit.cpp',
    'CvGameCoreDLL_Expansion2/CvDllUnitCombatClassInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllUnitInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllVictoryInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvDllWorldBuilderMapLoader.cpp',
    'CvGameCoreDLL_Expansion2/CvDllWorldInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvEconomicAI.cpp',
    'CvGameCoreDLL_Expansion2/CvEmphasisClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvEspionageClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvEventLog.cpp',
    'CvGameCoreDLL_Expansion2/CvFlavorManager.cpp',
    'CvGameCoreDLL_Expansion2/CvFractal.cpp',
    'CvGameCoreDLL_Expansion2/CvGame.cpp',
    'CvGameCoreDLL_Expansion2/CvGameCoreDLL.cpp',
    'CvGameCoreDLL_Expansion2/CvGameCoreEnumSerialization.cpp',
    'CvGameCoreDLL_Expansion2/CvGameCoreStructs.cpp',
    'CvGameCoreDLL_Expansion2/CvGameCoreUtils.cpp',
    'CvGameCoreDLL_Expansion2/CvGameQueries.cpp',
    'CvGameCoreDLL_Expansion2/CvGameTextMgr.cpp',
    'CvGameCoreDLL_Expansion2/CvGlobals.cpp',
    'CvGameCoreDLL_Expansion2/CvGoodyHuts.cpp',
    'CvGameCoreDLL_Expansion2/CvGrandStrategyAI.cpp',
    'CvGameCoreDLL_Expansion2/CvGreatPersonInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvHomelandAI.cpp',
    'CvGameCoreDLL_Expansion2/CvImprovementClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvInfos.cpp',
    'CvGameCoreDLL_Expansion2/CvInfosSerializationHelper.cpp',
    'CvGameCoreDLL_Expansion2/CvInternalGameCoreUtils.cpp',
    'CvGameCoreDLL_Expansion2/CvLoggerCSV.cpp',
    'CvGameCoreDLL_Expansion2/CvMap.cpp',
    'CvGameCoreDLL_Expansion2/CvMapGenerator.cpp',
    'CvGameCoreDLL_Expansion2/CvMilitaryAI.cpp',
    'CvGameCoreDLL_Expansion2/CvMinorCivAI.cpp',
    'CvGameCoreDLL_Expansion2/CvNotificationClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvNotifications.cpp',
    'CvGameCoreDLL_Expansion2/CvPlayer.cpp',
    'CvGameCoreDLL_Expansion2/CvPlayerAI.cpp',
    'CvGameCoreDLL_Expansion2/CvPlayerManager.cpp',
    'CvGameCoreDLL_Expansion2/CvPlot.cpp',
    'CvGameCoreDLL_Expansion2/CvPlotInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvPlotManager.cpp',
    'CvGameCoreDLL_Expansion2/CvPolicyAI.cpp',
    'CvGameCoreDLL_Expansion2/CvPolicyClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvPopupInfoSerialization.cpp',
    'CvGameCoreDLL_Expansion2/CvPreGame.cpp',
    'CvGameCoreDLL_Expansion2/CvProcessProductionAI.cpp',
    'CvGameCoreDLL_Expansion2/CvProjectClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvProjectProductionAI.cpp',
    'CvGameCoreDLL_Expansion2/CvPromotionClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvRandom.cpp',
    'CvGameCoreDLL_Expansion2/CvReligionClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvReplayInfo.cpp',
    'CvGameCoreDLL_Expansion2/CvReplayMessage.cpp',
    'CvGameCoreDLL_Expansion2/CvSerialize.cpp',
    'CvGameCoreDLL_Expansion2/CvSiteEvaluationClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvStartPositioner.cpp',
    'CvGameCoreDLL_Expansion2/cvStopWatch.cpp',
    'CvGameCoreDLL_Expansion2/CvTacticalAI.cpp',
    'CvGameCoreDLL_Expansion2/CvTacticalAnalysisMap.cpp',
    'CvGameCoreDLL_Expansion2/CvTargeting.cpp',
    'CvGameCoreDLL_Expansion2/CvTeam.cpp',
    'CvGameCoreDLL_Expansion2/CvTechAI.cpp',
    'CvGameCoreDLL_Expansion2/CvTechClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvTradeClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvTraitClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvTreasury.cpp',
    'CvGameCoreDLL_Expansion2/CvTypes.cpp',
    'CvGameCoreDLL_Expansion2/CvUnit.cpp',
    'CvGameCoreDLL_Expansion2/CvUnitClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvUnitCombat.cpp',
    'CvGameCoreDLL_Expansion2/CvUnitCycler.cpp',
    'CvGameCoreDLL_Expansion2/CvUnitMission.cpp',
    'CvGameCoreDLL_Expansion2/CvUnitMovement.cpp',
    'CvGameCoreDLL_Expansion2/CvUnitProductionAI.cpp',
    'CvGameCoreDLL_Expansion2/CvVotingClasses.cpp',
    'CvGameCoreDLL_Expansion2/CvWonderProductionAI.cpp',
    'CvGameCoreDLL_Expansion2/CvWorldBuilderMapLoader.cpp',
]


# ============================================================================
# Task execution (parallel build support)
# ============================================================================

class TaskResult:
    def __init__(self, commands):
        self.commands = commands
        self.returncode = None


class Task:
    def __init__(self, commands, env=None, shell=False, log=None):
        self.proc = subprocess.Popen(commands, stdout=log, stderr=log,
                                     env=env, shell=shell)
        self.result = TaskResult(commands)

    def poll(self):
        if self.proc.poll() is not None:
            self.result.returncode = self.proc.returncode
            return self.result
        return None


class TaskMan:
    def __init__(self):
        self.pending = Queue()

    def spawn(self, commands, env=None, shell=False, log=None):
        self.pending.put(Task(commands, env=env, shell=shell, log=log))

    def wait(self):
        results = []
        while not self.pending.empty():
            task = self.pending.get()
            if result := task.poll():
                results.append(result)
            else:
                self.pending.put(task)
        return results


# ============================================================================
# Build functions
# ============================================================================

def build_cl_config_args(config: Config, is_43_civs: bool = False) -> list[str]:
    """clang-cl compiler flags — identical to Windows clang build."""
    args = ['-m32', '-msse3', '/c', '/MD', '/GS', '/EHsc', '/fp:precise',
            '/Zc:wchar_t', '/Zc:threadSafeInit-', '/Zi']
    # Note: /FS (file synchronization) is Windows-only, omitted on Linux

    if config == Config.Release:
        args += ['-Os', '/Ob0', '/Oy-']  # -Os: best size/perf; /Ob0: clang inlining crashes
    else:
        args += ['/Od', '/Oy-']
    
    if is_43_civs:
        args.append('/D MOD_GLOBAL_MAX_MAJOR_CIVS=43')

    for predef in PREDEFS[config]:
        args.append(f'/D{predef}')
    for include_dir in INCLUDE_DIRS:
        args.append(f'/I"{PROJECT_DIR / include_dir}"')
    for include_path in INCLUDE_PATHS:
        args.append(f'-external:I"{include_path}"')
    for suppress in CL_SUPPRESS:
        args.append(f'-Wno-{suppress}')
    return args


def build_link_config_args(config: Config) -> list[str]:
    """lld-link flags — identical to Windows clang build."""
    args = ['/MACHINE:x86', '/DLL', '/DEBUG', '/DYNAMICBASE',
            '/NXCOMPAT', '/SUBSYSTEM:WINDOWS', '/MANIFEST:EMBED',
            '/FORCE:MULTIPLE', '/NODEFAULTLIB:MSVCRT', '/NODEFAULTLIB:OLDNAMES',
            '/NODEFAULTLIB:VERSION',
            f'/DEF:"{PROJECT_DIR / DEF_FILE}"']
    if config == Config.Release:
        args += ['/OPT:REF', '/OPT:ICF']
    return args


def prepare_dirs(build_dir: Path, out_dir: Path):
    build_dir.mkdir(parents=True, exist_ok=True)
    out_dir.mkdir(parents=True, exist_ok=True)
    for cpp in CPP:
        (build_dir / Path(cpp).parent).mkdir(parents=True, exist_ok=True)


def build_clang_cpp(cl: str, cl_args: list[str], build_dir: Path, log):
    print('building clang.cpp...')
    start_time = time.time()
    src = Path('clang.cpp')
    out = build_dir / 'clang.obj'
    # clang-cl on Linux cannot handle absolute paths (confuses / with MSVC flags)
    args_str = ' '.join(cl_args)
    command = f'{cl} "{src}" /Fo"{out}" {args_str}'
    cp = subprocess.run(command, capture_output=True, shell=True)
    log.write(f'==== {src} ====\n'.encode())
    log.write(cp.stdout)
    log.write(cp.stderr)
    log.flush()
    if cp.returncode != 0:
        print('FAILED to build clang.cpp - see build log')
        sys.exit(1)
    print(f'clang.cpp built in {time.time() - start_time:.1f}s')


def update_commit_id(log):
    print('updating commit ID...')
    start_time = time.time()
    # Use the shell script (Linux/Mac compatible)
    update_script = PROJECT_DIR / 'update_commit_id.sh'
    if update_script.exists():
        cp = subprocess.run(['bash', str(update_script)], capture_output=True)
    else:
        # Fallback: run git describe directly
        cp = subprocess.run(
            ['git', 'describe', '--tags', '--long'],
            capture_output=True
        )
        if cp.returncode == 0:
            full = cp.stdout.decode().strip()
            version = full.split('-', 1)[-1] if '-' in full else full
            inc = PROJECT_DIR / 'commit_id.inc'
            inc.write_text(
                f'const char CURRENT_GAMECORE_VERSION[] = "{version}"; //autogenerated\n'
            )
            cp = subprocess.CompletedProcess([], 0, b'', b'')
    log.write(b'==== commit_id ====\n')
    log.write(cp.stdout)
    log.write(cp.stderr)
    log.flush()
    if cp.returncode != 0:
        print('WARNING: failed to update commit ID (non-fatal)')
    else:
        print(f'commit ID updated in {time.time() - start_time:.1f}s')


def build_pch(cl: str, cl_args: list[str], pch_path: Path, build_dir: Path, log):
    print('building precompiled header...')
    start_time = time.time()
    pch_src = Path(PCH_CPP)
    out = build_dir / Path(PCH_CPP).with_suffix('.obj')
    args_str = ' '.join(cl_args)
    command = f'{cl} "{pch_src}" /Fo"{out}" /Yc"{PCH_H}" /Fp"{pch_path}" {args_str}'
    cp = subprocess.run(command, capture_output=True, shell=True)
    log.write(f'==== {pch_src} ====\n'.encode())
    log.write(cp.stdout)
    log.write(cp.stderr)
    log.flush()
    if cp.returncode != 0:
        print('FAILED to build precompiled header - see build log')
        sys.exit(1)
    print(f'PCH built in {time.time() - start_time:.1f}s')


def build_cpps(cl: str, cl_args: list[str], pch_path: Path, build_dir: Path, log):
    print(f'building {len(CPP)} cpps...')
    start_time = time.time()
    build_tasks = TaskMan()
    logs = {}
    try:
        for cpp in CPP:
            cpp_src = Path(cpp)
            cpp_log = tempfile.TemporaryFile()
            logs[cpp_src] = cpp_log
            out = build_dir / Path(cpp).with_suffix('.obj')
            args_str = ' '.join(cl_args)
            command = f'{cl} "{cpp_src}" /Fo"{out}" /Yu"{PCH_H}" /Fp"{pch_path}" {args_str}'
            build_tasks.spawn(command, shell=True, log=cpp_log)
        build_results = build_tasks.wait()
        for cpp_src, cpp_log in logs.items():
            cpp_log.seek(0)
            log.write(f'==== {cpp_src} ====\n'.encode())
            log.write(cpp_log.read())
            del cpp_log
        log.flush()
        failed = sum(1 for r in build_results if r.returncode != 0)
        if failed:
            print(f'{failed} cpp(s) FAILED - see build log')
            sys.exit(1)
        print(f'{len(CPP)} cpps built in {time.time() - start_time:.1f}s')
    finally:
        del logs


def link_dll(link: str, link_args: list[str], build_dir: Path,
             out_dir: Path, log):
    print('linking DLL...')
    start_time = time.time()
    out_dir.mkdir(parents=True, exist_ok=True)
    rsp_file = build_dir / 'link.rsp'
    with open(rsp_file, 'w') as f:
        out_dll = out_dir / f'{CORE_DLL}.dll'
        out_pdb = out_dir / f'{CORE_DLL}.pdb'
        f.write(f'/OUT:"{out_dll}"\n/PDB:"{out_pdb}"\n')
        f.write('\n'.join(link_args) + '\n')
        for lib_path in LIB_PATHS:
            f.write(f'/LIBPATH:"{lib_path}"\n')
        for lib in LIBS:
            lib_path = PROJECT_DIR / lib
            if not lib_path.exists():
                print(f'WARNING: library not found: {lib_path}')
            f.write(f'"{lib_path}"\n')
        for default_lib in DEFAULT_LIBS:
            f.write(f'{default_lib}\n')
        clang_obj = build_dir / 'clang.obj'
        pch_obj = build_dir / Path(PCH_CPP).with_suffix('.obj')
        f.write(f'"{clang_obj}"\n"{pch_obj}"\n')
        for cpp in CPP:
            obj = build_dir / Path(cpp).with_suffix('.obj')
            f.write(f'"{obj}"\n')

    command = f'{link} @"{rsp_file}"'
    log.write(f'Linking: {command}\n'.encode())
    cp = subprocess.run(command, capture_output=True, shell=True)
    log.write(f'==== {CORE_DLL}.dll ====\n'.encode())
    log.write(cp.stdout)
    log.write(cp.stderr)
    log.flush()
    if cp.returncode != 0:
        print('FAILED to link DLL - see build log')
        sys.exit(1)
    print(f'DLL linked in {time.time() - start_time:.1f}s')


# ============================================================================
# Main
# ============================================================================

def main():
    global SDK_DIR, SDK_INCLUDE, SDK_LIB, INCLUDE_PATHS, LIB_PATHS

    arg_parser = argparse.ArgumentParser(description='Build VP DLL (Linux/clang).')
    arg_parser.add_argument('--config', type=str, default='debug',
                            choices=['release', 'debug'])
    arg_parser.add_argument('--sdk-dir', type=str, default=None,
                            help='Path to extracted Windows SDK + VC9 headers/libs')
    arg_parser.add_argument('--43-civs', action='store_true',
                            help='Build 43-civ version (MOD_GLOBAL_MAX_MAJOR_CIVS=43)')
    args = arg_parser.parse_args()

    # Override SDK dir from command line
    if args.sdk_dir:
        SDK_DIR = Path(args.sdk_dir)
    else:
        SDK_DIR = _get_sdk_dir()
    SDK_INCLUDE = SDK_DIR / 'Include'
    SDK_LIB = SDK_DIR / 'Lib'
    INCLUDE_PATHS = [SDK_INCLUDE]
    LIB_PATHS = [SDK_LIB]
    # Also search architecture subdirectories (some MSI extractions put libs there)
    x86_dir = SDK_LIB / 'x86'
    if x86_dir.is_dir():
        LIB_PATHS.append(x86_dir)

    config = Config.Release if args.config == 'release' else Config.Debug

    # Validate SDK
    if not (SDK_INCLUDE / 'windows.h').exists():
        print(f"ERROR: windows.h not found at {SDK_INCLUDE / 'windows.h'}")
        print("Set WIN_SDK_DIR or use --sdk-dir to point at extracted SDK.")
        print("The SDK must contain both Win32 API headers and VC9 CRT headers.")
        sys.exit(1)

    cl = 'clang-cl'
    link = 'lld-link'
    build_dir = PROJECT_DIR / BUILD_DIR[config]
    out_dir = PROJECT_DIR / OUT_DIR[config]
    cl_args = build_cl_config_args(config, is_43_civs=args.__dict__.get('43_civs', False))
    link_args = build_link_config_args(config)
    pch_path = build_dir / PCH

    print(f'Configuration: {args.config}{" (43 Civs)" if args.__dict__.get("43_civs") else ""}')
    print(f'SDK directory: {SDK_DIR}')
    print(f'Output:        {out_dir}')
    print()

    prepare_dirs(build_dir, out_dir)

    with open(out_dir / 'build.log', 'w+b') as log:
        update_commit_id(log)
        build_clang_cpp(cl, cl_args, build_dir, log)
        build_pch(cl, cl_args, pch_path, build_dir, log)
        build_cpps(cl, cl_args, pch_path, build_dir, log)
        link_dll(link, link_args, build_dir, out_dir, log)

    print()
    print(f'BUILD SUCCESSFUL: {out_dir / f"{CORE_DLL}.dll"}')


if __name__ == '__main__':
    main()
