#!/usr/bin/env bash
# ============================================================================
# docker-build.sh - Build VP DLL via Docker
# ============================================================================
# One-command build for CvGameCore_Expansion2.dll using the containerized
# toolchain. Works on any OS with Docker installed.
#
# Usage:
#   ./docker-build.sh                     # Release build
#   ./docker-build.sh --config debug      # Debug build
#   ./docker-build.sh --build             # Rebuild Docker image, then compile
#   ./docker-build.sh --shell             # Open shell inside the container
#
# First run: docker image built automatically (~15 min, cached after).
# Subsequent: < 2 minutes (only changed code recompiles).
#
# Output: clang-output/Release/CvGameCore_Expansion2.dll
#         clang-output/Debug/CvGameCore_Expansion2.dll
# ============================================================================

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
IMAGE="vp-dll-builder"
CONFIG="release"
DO_BUILD_IMAGE=false
DO_SHELL=false
DO_43_CIVS=false

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --config) CONFIG="$2"; shift 2 ;;
        --build)  DO_BUILD_IMAGE=true; shift ;;
        --shell)  DO_SHELL=true; shift ;;
        --43-civs) DO_43_CIVS=true; shift ;;
        --help|-h)
            echo "Usage: $0 [--config release|debug] [--build] [--shell] [--43-civs]"
            echo ""
            echo "  --config release|debug   Build configuration (default: release)"
            echo "  --build                  Rebuild Docker image first"
            echo "  --shell                  Open a shell in the build container"
            echo "  --43-civs                Build 43-civ version"
            exit 0
            ;;
        *) echo "Unknown: $1"; exit 1 ;;
    esac
done

# ---------------------------------------------------------------------------
# Build image if needed
# ---------------------------------------------------------------------------
if $DO_BUILD_IMAGE || ! docker image inspect "$IMAGE" &>/dev/null 2>&1; then
    echo "=== Building Docker image: $IMAGE ==="
    docker build -t "$IMAGE" "$SCRIPT_DIR"
    echo "=== Image ready ==="
fi

# ---------------------------------------------------------------------------
# Shell mode (debugging / exploration)
# ---------------------------------------------------------------------------
if $DO_SHELL; then
    echo "=== Starting shell in $IMAGE ==="
    echo "    Source is at /workspace"
    echo "    Build with: python build_vp_clang_linux.py --config release"
    docker run --rm -it \
        --entrypoint /bin/bash \
        --user "$(id -u):$(id -g)" \
        -v "$SCRIPT_DIR:/workspace" \
        "$IMAGE"
    exit 0
fi

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
EXTRA_ARGS=""
$DO_43_CIVS && EXTRA_ARGS="--43-civs"
echo "=== Building VP DLL ($CONFIG)$($DO_43_CIVS && echo ' 43-civs') ==="
docker run --rm \
    -e PYTHONUNBUFFERED=1 \
    --user "$(id -u):$(id -g)" \
    -v "$SCRIPT_DIR:/workspace" \
    "$IMAGE" \
    --config "$CONFIG" $EXTRA_ARGS \
    || { echo ""; echo "BUILD FAILED. Is Docker running?"; exit 1; }

echo ""
echo "=== Done ==="
echo "DLL: $SCRIPT_DIR/clang-output/${CONFIG^}/CvGameCore_Expansion2.dll"
