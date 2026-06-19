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
    python scripts/deploy_test.py
    python scripts/deploy_test.py --dry-run
    python scripts/deploy_test.py --profile cp-only --clean-unused
    python scripts/deploy_test.py --mods-dir /path/to/MODS --dlc-dir /path/to/DLC
"""

import argparse
import filecmp
import json
import os
import shutil
import sys
from pathlib import Path


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


def sync_folder(src: Path, dst: Path, dry_run: bool,
                extra_excludes: set = frozenset()) -> tuple[int, int]:
    """e.
    Mirror src into dst, skipping excluded paths.
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
        copied, skipped = sync_folder(src, dst, dry_run, extra_excludes)
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
        copied, skipped = sync_folder(src, dst, dry_run)
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


def check_dll_present() -> None:
    """Warn if the DLL in (1) Community Patch looks stale or missing."""
    dll = PROJECT_DIR / "(1) Community Patch" / "CvGameCore_Expansion2.dll"
    if not dll.exists():
        print("WARNING: CvGameCore_Expansion2.dll not found in (1) Community Patch.")
        print("         Build it and copy it there before launching the game.")
    else:
        import datetime
        mtime = datetime.datetime.fromtimestamp(dll.stat().st_mtime)
        print(f"\nDLL present in (1) Community Patch (last modified: {mtime:%Y-%m-%d %H:%M:%S})")
        print("  (NOT copied by this script -- ensure it is your latest build)")


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

    check_dll_present()

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
