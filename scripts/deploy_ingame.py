#!/usr/bin/env python3
"""
Deploy mod files to local Civ5 installation for testing.

Profiles:
    - cp-only   : (1) Community Patch only (non-EUI)
    - vp-no-eui : (1) + (2) + (4a), VPUI in DLC
    - vp-eui    : (1)+(2) without LUA + (3a)+(4a), VPUI+UI_bc1 in DLC

CvGameCore_Expansion2.dll is NOT copied -- place it in
"(1) Community Patch" manually before running this script.

Usage:
    python scripts/deploy_ingame.py
    python scripts/deploy_ingame.py --dry-run
    python scripts/deploy_ingame.py --profile cp-only --clean-unused
    python scripts/deploy_ingame.py --mods-dir /path/to/MODS --dlc-dir /path/to/DLC
"""

import argparse
import filecmp
import json
import os
import shutil
import sys
from pathlib import Path

from generate_modinfo import parse_civ5proj


SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_DIR = SCRIPT_DIR.parent.resolve()


# ---------------------------------------------------------------------------
# Defaults (load from deploy_config.local.json if it exists)
# ---------------------------------------------------------------------------
def load_config() -> tuple[Path, Path]:
    """Load paths from local config file, fallback to defaults."""
    config_file = SCRIPT_DIR / "deploy_config.local.json"
    
    if config_file.exists():
        with open(config_file) as f:
            config = json.load(f)
        return Path(config["mods_dir"]), Path(config["dlc_dir"])
    
    # Defaults (only used if config file doesn't exist)
    mods_dir = Path(
        "{insert_your_path}/Documents/My Games/Sid Meier's Civilization 5/MODS"
    )
    dlc_dir = Path(
        "{insert_your_path}/SteamLibrary/steamapps/common/Sid Meier's Civilization V/Assets/DLC"
    )
    return mods_dir, dlc_dir


DEFAULT_MODS_DIR, DEFAULT_DLC_DIR = load_config()

# ---------------------------------------------------------------------------
# Deploy profiles
# Each mod entry:
#   (source folder name relative to PROJECT_DIR,
#    destination folder name in MODS,
#    set of extra path parts to exclude for this mod)
# ---------------------------------------------------------------------------
PROFILE_CONFIG = {
    "cp-only": {
        "mods": [
            ("(1) Community Patch", "(1) Community Patch", set()),
        ],
        "dlc": [],
    },
    "vp-no-eui": {
        "mods": [
            ("(1) Community Patch", "(1) Community Patch", set()),
            ("(2) Vox Populi", "(2) Vox Populi", set()),
            ("(4a) Squads for VP", "(4a) Squads for VP", set()),
        ],
        "dlc": ["VPUI"],
    },
    "vp-eui": {
        "mods": [
            # Per EUI instructions: LUA from (1) and (2) is replaced by (3a).
            ("(1) Community Patch", "(1) Community Patch", {"LUA"}),
            ("(2) Vox Populi", "(2) Vox Populi", {"LUA"}),
            ("(3a) VP - EUI Compatibility Files", "(3a) VP - EUI Compatibility Files", set()),
            ("(4a) Squads for VP", "(4a) Squads for VP", set()),
        ],
        "dlc": ["VPUI", "UI_bc1"],
    },
}

ALL_MOD_DESTS = [
    "(1) Community Patch",
    "(2) Vox Populi",
    "(3a) VP - EUI Compatibility Files",
    "(4a) Squads for VP",
]

ALL_DLC_DESTS = ["VPUI", "UI_bc1"]

# Folders/files to exclude from every mod folder
MOD_EXCLUDES = {
    # ModBuddy project files
    ".civ5proj",
    ".civ5sln",
    # Developer / documentation
    "Kit",
    "Credits.txt",
    "MANUAL INSTALL.txt",
    "INSTRUCTIONS.txt",
    "SampleContracts.xml",
    "SampleEvents.xml",
    # Source art files
    ".xcf",
    # Never copy the DLL from the repo -- user supplies it manually
    "CvGameCore_Expansion2.dll",
}

PROJECT_FILE_BY_MOD = {
    "(1) Community Patch": "Community Patch.civ5proj",
    "(2) Vox Populi": "Vox Populi.civ5proj",
    "(3a) VP - EUI Compatibility Files": "VP EUI Compatibility.civ5proj",
    "(4a) Squads for VP": "Squads.civ5proj",
}

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def should_exclude(rel_path: Path, extra_excludes: set = frozenset()) -> bool:
    """Return True if this relative path should be skipped."""
    # Check every component of the path
    for part in rel_path.parts:
        if part in MOD_EXCLUDES or part in extra_excludes:
            return True
        # Extension check (e.g. ".civ5proj")
        _, ext = os.path.splitext(part)
        if ext in MOD_EXCLUDES:
            return True
    return False


def get_mod_file_list(src: Path, extra_excludes: set = frozenset()) -> tuple[list[Path], int]:
    """Return files listed in the mod project file, filtered by excludes.

    Also includes any .modinfo files in the mod root so the mod can load.
    Returns (files_to_copy, files_skipped).
    """
    civ5proj_name = PROJECT_FILE_BY_MOD.get(src.name)
    if not civ5proj_name:
        raise RuntimeError(f"No .civ5proj mapping defined for: {src.name}")

    civ5proj_path = src / civ5proj_name
    if not civ5proj_path.is_file():
        raise RuntimeError(f".civ5proj file not found: {civ5proj_path}")

    data = parse_civ5proj(civ5proj_path)
    files_to_copy: list[Path] = []
    skipped = 0

    for file_info in data["files"]:
        # .civ5proj uses Windows backslash separators; normalize for Linux
        rel_file = Path(file_info["path"].replace("\\", "/"))
        if should_exclude(rel_file, extra_excludes):
            skipped += 1
            continue

        src_file = src / rel_file
        if not src_file.exists() or not src_file.is_file():
            skipped += 1
            continue

        # Skip empty files (not registered in modinfo, game ignores them)
        if src_file.stat().st_size == 0:
            skipped += 1
            continue

        files_to_copy.append(rel_file)

    for modinfo_file in src.glob("*.modinfo"):
        rel_file = Path(modinfo_file.name)
        if should_exclude(rel_file, extra_excludes):
            skipped += 1
            continue
        files_to_copy.append(rel_file)

    # Keep order stable and avoid duplicates from project/modinfo overlap.
    unique_files = list(dict.fromkeys(files_to_copy))
    return unique_files, skipped


def sync_mod_folder(src: Path, dst: Path, dry_run: bool,
                    extra_excludes: set = frozenset()) -> tuple[int, int]:
    """
    Mirror src into dst using files listed in the source .civ5proj file.
    Returns (files_copied, files_skipped).
    """
    copied = 0
    listed_files, skipped = get_mod_file_list(src, extra_excludes)

    for rel_file in listed_files:
        src_file = src / rel_file
        dst_file = dst / rel_file

        # Skip if destination is already up-to-date
        if dst_file.exists():
            src_mtime = src_file.stat().st_mtime
            dst_mtime = dst_file.stat().st_mtime
            src_size  = src_file.stat().st_size
            dst_size  = dst_file.stat().st_size

            # Fast path: if destination is newer and same size, assume unchanged
            if src_size == dst_size and src_mtime <= dst_mtime:
                skipped += 1
                continue

            # Deep content check: files are identical if bytes match
            if filecmp.cmp(src_file, dst_file, shallow=False):
                skipped += 1
                continue

        print(f"  {'[DRY RUN] ' if dry_run else ''}COPY  {rel_file}")
        if not dry_run:
            dst_file.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src_file, dst_file)
        copied += 1

    return copied, skipped


def sync_tree_folder(src: Path, dst: Path, dry_run: bool,
                     extra_excludes: set = frozenset()) -> tuple[int, int]:
    """
    Mirror src into dst by recursively walking files, skipping excluded paths.
    Used for DLC folders that don't have .civ5proj files.
    Returns (files_copied, files_skipped).
    """
    copied = 0
    skipped = 0

    for root, dirs, files in os.walk(src):
        root_path = Path(root)
        rel_root = root_path.relative_to(src)

        # Prune excluded directories in-place so os.walk skips them
        dirs[:] = [
            d for d in dirs
            if not should_exclude(rel_root / d, extra_excludes)
        ]

        for filename in files:
            rel_file = rel_root / filename
            if should_exclude(rel_file, extra_excludes):
                skipped += 1
                continue

            src_file = root_path / filename

            # Skip empty files (not registered in modinfo, game ignores them)
            if src_file.stat().st_size == 0:
                skipped += 1
                continue

            dst_file = dst / rel_file

            # Skip if destination is already up-to-date
            if dst_file.exists():
                src_mtime = src_file.stat().st_mtime
                dst_mtime = dst_file.stat().st_mtime
                src_size  = src_file.stat().st_size
                dst_size  = dst_file.stat().st_size
                
                # Fast path: if destination is newer and same size, assume unchanged
                if src_size == dst_size and src_mtime <= dst_mtime:
                    skipped += 1
                    continue
                
                # Deep content check: files are identical if bytes match
                if filecmp.cmp(src_file, dst_file, shallow=False):
                    skipped += 1
                    continue

            print(f"  {'[DRY RUN] ' if dry_run else ''}COPY  {rel_file}")
            if not dry_run:
                dst_file.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src_file, dst_file)
            copied += 1

    return copied, skipped


def deploy_mods(mods_dir: Path, dry_run: bool, mod_folders: list[tuple[str, str, set]]) -> None:
    print(f"\n=== MODS -> {mods_dir} ===")
    total_copied = 0
    for src_name, dst_name, extra_excludes in mod_folders:
        src = PROJECT_DIR / src_name
        dst = mods_dir / dst_name
        if not src.is_dir():
            print(f"  WARNING: source not found: {src}")
            continue
        print(f"\n[{src_name}]")
        if extra_excludes:
            print(f"  (also excluding: {', '.join(sorted(extra_excludes))})")

        # If LUA is excluded (EUI profile), delete stale LUA folder at destination
        if "LUA" in extra_excludes:
            lua_dst = dst / "LUA"
            if lua_dst.exists():
                print(f"  {'[DRY RUN] ' if dry_run else ''}REMOVE LUA/  {lua_dst}")
                if not dry_run:
                    shutil.rmtree(lua_dst)

        copied, skipped = sync_mod_folder(src, dst, dry_run, extra_excludes)
        print(f"  {copied} file(s) updated, {skipped} unchanged/skipped")
        total_copied += copied
    print(f"\nMODS total: {total_copied} file(s) updated")


def deploy_dlc(dlc_dir: Path, dry_run: bool, dlc_folders: list[str]) -> None:
    print(f"\n=== DLC -> {dlc_dir} ===")
    total_copied = 0
    for folder_name in dlc_folders:
        src = PROJECT_DIR / folder_name
        dst = dlc_dir / folder_name
        if not src.is_dir():
            print(f"  WARNING: source not found: {src}")
            continue
        print(f"\n[{folder_name}]")
        copied, skipped = sync_tree_folder(src, dst, dry_run)
        print(f"  {copied} file(s) updated, {skipped} unchanged/skipped")
        total_copied += copied
    print(f"\nDLC total: {total_copied} file(s) updated")


def clean_unused_targets(mods_dir: Path, dlc_dir: Path | None,
                         active_mod_dests: set[str], active_dlc_dests: set[str],
                         dry_run: bool) -> None:
    print("\n=== Cleaning Unused Targets ===")

    for mod_name in ALL_MOD_DESTS:
        if mod_name in active_mod_dests:
            continue
        target = mods_dir / mod_name
        if target.exists():
            print(f"  {'[DRY RUN] ' if dry_run else ''}REMOVE {target}")
            if not dry_run:
                shutil.rmtree(target)

    if dlc_dir is not None:
        for dlc_name in ALL_DLC_DESTS:
            if dlc_name in active_dlc_dests:
                continue
            target = dlc_dir / dlc_name
            if target.exists():
                print(f"  {'[DRY RUN] ' if dry_run else ''}REMOVE {target}")
                if not dry_run:
                    shutil.rmtree(target)


def validate_paths(mods_dir: Path, dlc_dir: Path | None) -> None:
    """Validate that paths are sensible and point to the right directories.
    
    Fails immediately if paths are clearly wrong to prevent accidental corruption.
    """
    # Check that mods_dir and dlc_dir are absolute paths (not relative)
    if not mods_dir.is_absolute():
        print(f"ERROR: MODS path must be absolute: {mods_dir}", file=sys.stderr)
        sys.exit(1)
    if dlc_dir and not dlc_dir.is_absolute():
        print(f"ERROR: DLC path must be absolute: {dlc_dir}", file=sys.stderr)
        sys.exit(1)
    
    # Check that paths have reasonable depth (not root or single-level directories)
    if len(mods_dir.parts) < 4:
        print(f"ERROR: MODS path seems too shallow (might be wrong): {mods_dir}", file=sys.stderr)
        sys.exit(1)
    if dlc_dir and len(dlc_dir.parts) < 4:
        print(f"ERROR: DLC path seems too shallow (might be wrong): {dlc_dir}", file=sys.stderr)
        sys.exit(1)
    
    # Check that paths contain expected keywords to catch obvious mistakes
    mods_path_lower = str(mods_dir).lower()
    dlc_path_lower = str(dlc_dir).lower() if dlc_dir else ""
    
    if "mods" not in mods_path_lower:
        print(f"WARNING: MODS path doesn't contain 'mods': {mods_dir}", file=sys.stderr)
        response = input("Continue anyway? (y/N): ").strip().lower()
        if response != "y":
            sys.exit(1)
    
    if dlc_dir and "dlc" not in dlc_path_lower:
        print(f"WARNING: DLC path doesn't contain 'dlc': {dlc_dir}", file=sys.stderr)
        response = input("Continue anyway? (y/N): ").strip().lower()
        if response != "y":
            sys.exit(1)
    
    # If MODS dir is not empty, check it looks like a Civ5 MODS folder
    if mods_dir.exists() and any(mods_dir.iterdir()):
        # Check for at least one known mod folder or .civ5mod file
        has_mod_content = any(
            (mods_dir / name).exists() for name in ALL_MOD_DESTS
        ) or any(f.suffix == ".civ5mod" for f in mods_dir.glob("*"))
        
        if not has_mod_content:
            print(f"WARNING: MODS directory exists but looks empty (no known mods): {mods_dir}", file=sys.stderr)
            response = input("Continue anyway? (y/N): ").strip().lower()
            if response != "y":
                sys.exit(1)
    
    print(f"✓ Paths validated")
    print(f"  MODS: {mods_dir}")
    if dlc_dir:
        print(f"  DLC:  {dlc_dir}")


def get_source_hash(is_43_civs: bool = False) -> str:
    """Return a hash of all C++ source files that affect the DLL build."""
    import hashlib
    
    source_dirs = [
        "CvGameCoreDLL_Expansion2",
        "CvGameCoreDLLUtil",
        "CvGameDatabase",
        "CvLocalization",
        "CvWorldBuilderMap",
        "FirePlace",
        "ThirdPartyLibs",
    ]
    top_extensions = {".cpp", ".h"}
    
    hasher = hashlib.sha256()
    
    # Include variant in hash so switching 43-civs always triggers rebuild
    hasher.update(b"43civs" if is_43_civs else b"standard")
    
    for d in source_dirs:
        path = PROJECT_DIR / d
        if not path.is_dir():
            continue
        for f in sorted(path.rglob("*")):
            if f.is_file() and f.suffix in top_extensions:
                rel = f.relative_to(PROJECT_DIR)
                hasher.update(str(rel).encode())
                hasher.update(f.read_bytes())
    
    for script in ["build_vp_clang_linux.py", "clang.cpp", "Dockerfile", 
                   "fix_lib_case.sh", "setup_sdk.sh", "docker-build.sh"]:
        script_path = PROJECT_DIR / script
        if script_path.is_file():
            hasher.update(script_path.name.encode())
            hasher.update(script_path.read_bytes())
    
    return hasher.hexdigest()


def build_dll_if_needed(dry_run: bool, is_43_civs: bool = False) -> bool:
    hash_file = PROJECT_DIR / ".dll_source_hash"
    current_hash = get_source_hash(is_43_civs)
    dll_path = PROJECT_DIR / "clang-output" / "Release" / "CvGameCore_Expansion2.dll"
    
    need_build = False
    if not dll_path.exists():
        print(f"\nDLL not found — building{' (43 Civs)' if is_43_civs else ''}...")
        need_build = True
    elif hash_file.exists():
        stored_hash = hash_file.read_text().strip()
        if stored_hash != current_hash:
            print(f"\nC++ source changed — rebuilding{' (43 Civs)' if is_43_civs else ''}...")
            need_build = True
        else:
            import datetime
            mtime = datetime.datetime.fromtimestamp(dll_path.stat().st_mtime)
            print(f"\nDLL up-to-date{' (43 Civs)' if is_43_civs else ''} (built {mtime:%Y-%m-%d %H:%M:%S})")
    else:
        print(f"\nNo source hash record — rebuilding{' (43 Civs)' if is_43_civs else ''}...")
        need_build = True
    
    if not need_build:
        return True
    
    if dry_run:
        script_name = "build.bat" if sys.platform == "win32" else "./docker-build.sh"
        print(f"[DRY RUN] Would run: {script_name} --config release" + (" --43-civs" if is_43_civs else ""))
        return False
    
    import subprocess as subproc

    if sys.platform == "win32":
        build_script = PROJECT_DIR / "build.bat"
        if not build_script.is_file():
            print("ERROR: build.bat not found", file=sys.stderr)
            return False
        cmd = [str(build_script), "--config", "release"]
        if is_43_civs:
            cmd.append("--43-civs")
    else:
        build_script = PROJECT_DIR / "docker-build.sh"
        if not build_script.is_file():
            print("ERROR: docker-build.sh not found", file=sys.stderr)
            return False
        cmd = [str(build_script), "--config", "release"]
        if is_43_civs:
            cmd.append("--43-civs")
    print(f"Running: {' '.join(cmd)}")
    result = subproc.run(cmd, cwd=str(PROJECT_DIR))
    
    # On Linux/macOS, retry with sudo if permission denied (Docker socket)
    if result.returncode != 0 and sys.platform != "win32":
        output = (result.stderr or b"").decode(errors="replace")
        if "permission denied" in output.lower():
            print("  (retrying with sudo for Docker access)")
            sudo_cmd = ["sudo", str(build_script), "--config", "release"]
            if is_43_civs:
                sudo_cmd.append("--43-civs")
            result = subproc.run(sudo_cmd, cwd=str(PROJECT_DIR))
            if result.returncode == 0:
                subproc.run(
                    ["sudo", "chown", "-R", f"{os.getuid()}:{os.getgid()}",
                     str(PROJECT_DIR / "clang-build"),
                     str(PROJECT_DIR / "clang-output")],
                    cwd=str(PROJECT_DIR)
                )
    
    if result.returncode != 0:
        print("ERROR: DLL build failed", file=sys.stderr)
        return False
    
    hash_file.write_text(current_hash + "\n")
    print(f"Build successful: {dll_path}")
    return True


def copy_dll_to_mod(src_dll: Path, mods_dir: Path, dry_run: bool) -> None:
    dst = mods_dir / "(1) Community Patch" / "CvGameCore_Expansion2.dll"
    
    if dst.exists():
        if filecmp.cmp(src_dll, dst, shallow=False):
            print(f"DLL already up-to-date in MODS")
            return
        print(f"Updating DLL in MODS...")
    else:
        print(f"Copying DLL to MODS...")
    
    if dry_run:
        print(f"  [DRY RUN] COPY {src_dll} -> {dst}")
    else:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src_dll, dst)
        print(f"  {src_dll} -> {dst}")


def check_dll_present(mods_dir: Path | None = None, dry_run: bool = False,
                     skip_build: bool = False, is_43_civs: bool = False) -> None:
    """Build DLL if source changed, then copy to (1) Community Patch."""
    if skip_build:
        src_dll = PROJECT_DIR / "clang-output" / "Release" / "CvGameCore_Expansion2.dll"
        if not src_dll.exists():
            print(f"ERROR: DLL not found at {src_dll} (build with --skip-build omitted)", file=sys.stderr)
            sys.exit(1)
        print("\nSkipping DLL build (--skip-build)" + (" (43 Civs)" if is_43_civs else ""))
    elif not build_dll_if_needed(dry_run, is_43_civs):
        sys.exit(1)
    
    src_dll = PROJECT_DIR / "clang-output" / "Release" / "CvGameCore_Expansion2.dll"
    if not src_dll.exists():
        print("ERROR: DLL not found after build", file=sys.stderr)
        sys.exit(1)
    
    if mods_dir and mods_dir.is_dir():
        copy_dll_to_mod(src_dll, mods_dir, dry_run)
    else:
        print(f"\nDLL ready: {src_dll}")
        print("  (MODS path not configured — copy manually)")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Deploy VP mod files to local Civ5 installation for testing."
    )
    parser.add_argument(
        "--profile",
        choices=sorted(PROFILE_CONFIG.keys()),
        default="vp-eui",
        help="Deployment profile (default: vp-eui)",
    )
    parser.add_argument(
        "--mods-dir",
        type=Path,
        default=DEFAULT_MODS_DIR,
        help="Path to Civ5 MODS folder",
    )
    parser.add_argument(
        "--dlc-dir",
        type=Path,
        default=DEFAULT_DLC_DIR,
        help="Path to Civ5 DLC folder",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print what would be copied without actually copying",
    )
    parser.add_argument(
        "--clean-unused",
        action="store_true",
        help="Delete known MODS/DLC folders not used by the selected profile",
    )
    parser.add_argument(
        "--skip-build",
        action="store_true",
        help="Skip DLL build; use existing dll in clang-output/Release/",
    )
    parser.add_argument(
        "--43-civs",
        action="store_true",
        help="Build and deploy the 43-civ version of the DLL",
    )
    args = parser.parse_args()

    profile = PROFILE_CONFIG[args.profile]
    mod_folders = profile["mods"]
    dlc_folders = profile["dlc"]

    if not args.mods_dir.is_dir():
        print(f"ERROR: MODS directory not found: {args.mods_dir}", file=sys.stderr)
        sys.exit(1)
    needs_dlc_dir = bool(dlc_folders) or args.clean_unused
    if needs_dlc_dir and not args.dlc_dir.is_dir():
        print(f"ERROR: DLC directory not found: {args.dlc_dir}", file=sys.stderr)
        sys.exit(1)

    # Validate paths before proceeding
    validate_paths(args.mods_dir, args.dlc_dir if needs_dlc_dir else None)

    if args.dry_run:
        print("*** DRY RUN -- no files will be written ***")

    print(f"*** Profile: {args.profile} ***")
    if args.profile == "cp-only":
        print("  CP-only selected: only '(1) Community Patch' is deployed (LUA kept).")

    check_dll_present(args.mods_dir, args.dry_run, args.skip_build, args.__dict__.get('43_civs', False))

    active_mod_dests = {dst for _, dst, _ in mod_folders}
    active_dlc_dests = set(dlc_folders)

    if args.clean_unused:
        clean_unused_targets(
            args.mods_dir,
            args.dlc_dir if needs_dlc_dir else None,
            active_mod_dests,
            active_dlc_dests,
            args.dry_run,
        )

    deploy_mods(args.mods_dir, args.dry_run, mod_folders)
    if dlc_folders:
        deploy_dlc(args.dlc_dir, args.dry_run, dlc_folders)
    else:
        print("\n=== DLC ===")
        print("  No DLC folders needed for this profile.")

    print("\nDone.")


if __name__ == "__main__":
    main()
