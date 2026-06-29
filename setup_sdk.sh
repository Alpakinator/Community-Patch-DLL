#!/bin/bash
# ============================================================================
# setup_sdk.sh — Extract Windows SDK 7.0 + VC9 CRT using wine msiexec /a
# ============================================================================
set -euo pipefail

WIN_SDK="${WIN_SDK:-/opt/win-sdk}"
ISO_PATH=/tmp/sdk.iso
WORK=/tmp/sdk-work

if [ ! -f "${ISO_PATH}" ]; then
    echo "ERROR: ${ISO_PATH} not found"
    exit 1
fi

# ------------------------------------------------------------------
# Step 1: Extract MSI + companion CAB files from the ISO
# ------------------------------------------------------------------
echo "=== Extracting from ISO ==="
rm -rf "${WORK}"
mkdir -p "${WORK}"
7z x -y -o"${WORK}" "${ISO_PATH}" \
    Setup/WinSDK/WinSDK_x86.msi \
    Setup/WinSDK/cab1.cab \
    Setup/WinSDKBuild/WinSDKBuild_x86.msi \
    Setup/WinSDKBuild/cab1.cab \
    Setup/WinSDKBuild/cab2.cab \
    Setup/WinSDKBuild/cab3.cab \
    Setup/WinSDKBuild/cab4.cab \
    Setup/WinSDKBuild/cab2.cab \
    Setup/WinSDKBuild/cab3.cab \
    Setup/WinSDKBuild/cab4.cab \
    Setup/WinSDKWin32Tools/WinSDKWin32Tools_x86.msi \
    Setup/WinSDKWin32Tools/cab1.cab \
    Setup/vc_stdx86/vc_stdx86.msi \
    Setup/vc_stdx86/vc_stdx86.cab > /dev/null

# ------------------------------------------------------------------
# Step 2: wine msiexec /a (administrative install = extract with real paths)
# The MSI knows the mapping from CAB internal names → real file paths.
# ------------------------------------------------------------------
echo "=== WinSDK: extract via msiextract (handles embedded + CAB files) ==="
mkdir -p "${WORK}"/winsdk_dest
msiextract "${WORK}"/Setup/WinSDK/WinSDK_x86.msi -C "${WORK}"/winsdk_dest 2>&1 | tail -3 || true
echo "  Files: $(find "${WORK}"/winsdk_dest -type f 2>/dev/null | wc -l)"

echo "=== Win32Tools: wine msiexec /a ==="
mkdir -p "${WORK}"/wintools_dest
( cd "${WORK}"/Setup/WinSDKWin32Tools 2>/dev/null && wine msiexec /a WinSDKWin32Tools_x86.msi /qn TARGETDIR="Z:${WORK}/wintools_dest" 2>&1 | tail -2 ) || true
echo "  Files: $(find "${WORK}"/wintools_dest -type f 2>/dev/null | wc -l)"

echo "=== WinSDKBuild: wine msiexec /a (likely contains headers + import libs) ==="
mkdir -p "${WORK}"/winsdkbuild_dest
( cd "${WORK}"/Setup/WinSDKBuild 2>/dev/null && wine msiexec /a WinSDKBuild_x86.msi /qn TARGETDIR="Z:${WORK}/winsdkbuild_dest" 2>&1 | tail -2 ) || true
echo "  Files: $(find "${WORK}"/winsdkbuild_dest -type f 2>/dev/null | wc -l)"

echo "=== VC9: extract via wine msiexec /a ==="
mkdir -p "${WORK}"/vc9_dest
WINE_OK=false
if ( cd "${WORK}"/Setup/vc_stdx86 2>/dev/null && wine msiexec /a vc_stdx86.msi /qn TARGETDIR="Z:${WORK}/vc9_dest" 2>&1 | tail -3 ); then
    WINE_OK=true
fi
if ! $WINE_OK || [ "$(find "${WORK}"/vc9_dest -type f 2>/dev/null | wc -l)" -eq 0 ]; then
    echo "  Wine failed, trying msiextract..."
    msiextract "${WORK}"/Setup/vc_stdx86/vc_stdx86.msi -C "${WORK}"/vc9_dest 2>&1 | tail -3 || true
fi
echo "  Files: $(find "${WORK}"/vc9_dest -type f 2>/dev/null | wc -l)"

# ------------------------------------------------------------------
# Step 3: Find Include and Lib directories (msiexec creates real paths)
# ------------------------------------------------------------------
echo "=== Directory layout ==="
echo "  WinSDK:"
find "${WORK}"/winsdk_dest -maxdepth 5 -type d 2>/dev/null | head -10 || true
echo "  WinSDK sample files (first 20):"
find "${WORK}"/winsdk_dest -type f 2>/dev/null | head -20 || true
echo "  WinSDKBuild:"
find "${WORK}"/winsdkbuild_dest -maxdepth 5 -type d 2>/dev/null | head -10 || true
echo "  WinSDKBuild sample files (first 20):"
find "${WORK}"/winsdkbuild_dest -type f 2>/dev/null | head -20 || true
echo "  Win32Tools:"
find "${WORK}"/wintools_dest -maxdepth 5 -type d 2>/dev/null | head -10 || true
echo "  VC9:"
find "${WORK}"/vc9_dest -maxdepth 5 -type d 2>/dev/null | head -20

WINSDK_INC=$(find "${WORK}"/winsdk_dest "${WORK}"/wintools_dest "${WORK}"/winsdkbuild_dest -type d -iname include 2>/dev/null | head -1)
WINSDK_LIB=$(find "${WORK}"/winsdk_dest "${WORK}"/wintools_dest "${WORK}"/winsdkbuild_dest -type d -iname lib     2>/dev/null | head -1)
VC9_INC=$(   find "${WORK}"/vc9_dest   -type d -iname include 2>/dev/null | head -1)
VC9_LIB=$(   find "${WORK}"/vc9_dest   -type d -iname lib     2>/dev/null | head -1)

echo ""
echo "  WINSDK_INC=${WINSDK_INC:-NOT FOUND}"
echo "  WINSDK_LIB=${WINSDK_LIB:-NOT FOUND}"
echo "  VC9_INC=${VC9_INC:-NOT FOUND}"
echo "  VC9_LIB=${VC9_LIB:-NOT FOUND}"

# ------------------------------------------------------------------
# Step 4: Merge into /opt/win-sdk
# ------------------------------------------------------------------
rm -rf "${WIN_SDK}"
mkdir -p "${WIN_SDK}"/Include "${WIN_SDK}"/Lib

if [ -n "${WINSDK_INC}" ]; then cp -r "${WINSDK_INC}"/* "${WIN_SDK}"/Include/; fi
if [ -n "${VC9_INC}" ];   then cp -rn "${VC9_INC}/"*   "${WIN_SDK}"/Include/ 2>/dev/null || true; fi
if [ -n "${WINSDK_LIB}" ]; then cp -r "${WINSDK_LIB}"/* "${WIN_SDK}"/Lib/; fi
if [ -n "${VC9_LIB}" ];   then cp -rn "${VC9_LIB}/"*   "${WIN_SDK}"/Lib/ 2>/dev/null || true; fi

# ------------------------------------------------------------------
# Step 5: Rename to lowercase, symlink old→new
# ------------------------------------------------------------------
echo "=== Normalizing to lowercase with backward symlinks ==="
find "${WIN_SDK}"/Include -depth -name '*[A-Z]*' > /tmp/rename_list
while IFS= read -r f; do
    dir=$(dirname "$f")
    base=$(basename "$f")
    lower=$(echo "$base" | tr '[:upper:]' '[:lower:]')
    if [ "$base" = "$lower" ]; then continue; fi
    if [ -e "$dir/$lower" ]; then
        rm -f "$f"
    else
        mv -- "$f" "$dir/$lower"
    fi
    ln -sf "$lower" "$dir/$base"
done < /tmp/rename_list
rm -f /tmp/rename_list

# ------------------------------------------------------------------
# Step 5b: Fix #include directives that reference files in wrong case
# Scan all headers' #include "X" directives; if X doesn't exist but
# a case-variant does, create a symlink.
# ------------------------------------------------------------------
echo "=== Fixing case-mismatched #include references ==="
grep -roh '#include "[^"]*"' "${WIN_SDK}"/Include | \
    sed 's/#include "\(.*\)"/\1/' | sort -u | \
while IFS= read -r inc; do
    # Only handle simple filenames (no path separators)
    case "$inc" in */*) continue ;; esac
    target="${WIN_SDK}/Include/${inc}"
    if [ ! -e "$target" ]; then
        # Find case-insensitive match
        found=$(find "${WIN_SDK}"/Include -maxdepth 1 -iname "$inc" ! -name "$inc" 2>/dev/null | head -1)
        if [ -n "$found" ]; then
            echo "  ${inc} -> $(basename "$found")"
            ln -sf "$(basename "$found")" "$target"
        fi
    fi
done

# The build script expects Include/ and Lib/ with capital letters.
# If they were lowercased, create symlinks.
echo "=== Creating Include/Lib symlinks if needed ==="
for d in Include Lib; do
    lower=$(echo "$d" | tr '[:upper:]' '[:lower:]')
    if [ -d "${WIN_SDK}/${lower}" ] && [ ! -d "${WIN_SDK}/${d}" ]; then
        ln -s "${lower}" "${WIN_SDK}/${d}"
        echo "  ${d} -> ${lower}"
    fi
done

# Windows SDK headers use \ as path separator in #include "a\b.h"
# Linux treats \ literally, so normalize to /
echo "=== Normalizing backslash paths in #include directives ==="
find "${WIN_SDK}"/Include -name '*.h' -exec sed -i '/^#include/s|\\|/|g' {} \;

# ------------------------------------------------------------------
# Step 5c: Create stubs for WDK headers referenced by the SDK but not shipped
# DriverSpecs.h is included by kernelspecs.h but belongs to the Windows Driver Kit,
# not the base SDK. An empty stub satisfies the #include chain for user-mode code.
# ------------------------------------------------------------------
echo "=== Creating stubs for missing WDK headers ==="
for missing_header in DriverSpecs.h; do
    # Respect the lowercase convention: create lowercase file + uppercase symlink
    lower=$(echo "$missing_header" | tr '[:upper:]' '[:lower:]')
    if [ ! -f "${WIN_SDK}/Include/${lower}" ] && [ ! -f "${WIN_SDK}/Include/${missing_header}" ]; then
        echo "  Creating empty stub: ${lower}"
        touch "${WIN_SDK}/Include/${lower}"
        if [ "$lower" != "$missing_header" ]; then
            ln -sf "$lower" "${WIN_SDK}/Include/${missing_header}"
        fi
    fi
done

# ------------------------------------------------------------------
# Step 6: Verify
# ------------------------------------------------------------------
echo "=== Verification ==="
for f in windows.h stdio.h iostream kernel32.lib msvcrt.lib DriverSpecs.h; do
    found=$(find "${WIN_SDK}" -name "$f" 2>/dev/null | head -1)
    if [ -n "$found" ]; then echo "  OK: $f"; else echo "  MISSING: $f"; fi
done
echo "Headers: $(find ${WIN_SDK}/Include -name '*.h' 2>/dev/null | wc -l)"
echo "Libs:    $(find ${WIN_SDK}/Lib     -name '*.lib' 2>/dev/null | wc -l)"

rm -rf "${WORK}"
echo "=== SDK setup complete ==="
