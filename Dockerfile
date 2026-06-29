# ============================================================================
# Dockerfile - VP DLL Build Environment
# ============================================================================
# Reproducible cross-compilation environment for CvGameCore_Expansion2.dll.
# Uses clang + lld targeting i386-pc-windows-msvc (Win32 x86).
#
# Works on any OS with Docker: Linux, Windows (Docker Desktop), macOS.
#
# Quick start:
#   docker build -t vp-dll-builder .
#   ./docker-build.sh --config release
# ============================================================================

FROM archlinux:latest

LABEL org.voxpopuli.image="vp-dll-builder"
LABEL org.voxpopuli.description="Vox Populi CvGameCoreDLL build environment"
LABEL org.voxpopuli.target="i386-pc-windows-msvc"

# ---------------------------------------------------------------------------
# Layer 1: System packages (cached until packages change)
# ---------------------------------------------------------------------------
RUN pacman -Syu --noconfirm \
    clang \
    lld \
    python \
    git \
    p7zip \
    wget \
    msitools \
    cabextract \
    && pacman -Scc --noconfirm \
    && clang-cl --version \
    && lld-link --version

# ---------------------------------------------------------------------------
# Layer 2a: Download Windows SDK 7.0 ISO (cached separately from extraction)
# ---------------------------------------------------------------------------
ARG SDK_URL="https://web.archive.org/web/20161230154527/http://download.microsoft.com/download/2/E/9/2E911956-F90F-4BFB-8231-E292A7B6F287/GRMSDK_EN_DVD.iso"
ARG WIN_SDK=/opt/win-sdk

RUN echo "=== Downloading Windows SDK 7.0 ISO (580 MB) ===" && \
    wget -q --show-progress -O /tmp/sdk.iso "${SDK_URL}" && \
    echo "65739fb0874cc17ea6962d8ce7915364c7161fa106ed1bf1c917924c18ac63ca  /tmp/sdk.iso" | sha256sum -c -

# ---------------------------------------------------------------------------
# Layer 2b: Install Wine (for reliable MSI extraction via msiexec)
# ---------------------------------------------------------------------------
RUN pacman -Syu --noconfirm wine && pacman -Scc --noconfirm

# ---------------------------------------------------------------------------
# Layer 2c: Extract SDK (can change without re-downloading the ISO)
# ---------------------------------------------------------------------------
COPY setup_sdk.sh /tmp/setup_sdk.sh
RUN WIN_SDK="${WIN_SDK}" bash /tmp/setup_sdk.sh && rm -f /tmp/setup_sdk.sh /tmp/sdk.iso

# ---------------------------------------------------------------------------
# Layer 2d: WDK header stubs — headers referenced by the Windows SDK that are
# only shipped with the Windows Driver Kit, not the base SDK.  User-mode code
# doesn't need their actual content; empty files or minimal stubs satisfy the
# #include chain.  Both uppercase and lowercase versions are created.
# ---------------------------------------------------------------------------
RUN for h in DriverSpecs.h SpecStrings.h driverspecs.h specstrings.h; do \
        touch "${WIN_SDK}/Include/$h"; \
    done && \
    echo "WDK stubs created:" && \
    ls -la "${WIN_SDK}"/Include/[Dd]river*Specs* "${WIN_SDK}"/Include/[Ss]pec[Ss]trings*

# ---------------------------------------------------------------------------
# Layer 2e: Case-insensitive lib symlinks — the Windows SDK and VC9 CRT contain
# .lib files with mixed case (GDI32.lib, Kernel32.Lib, etc.) but lld-link and
# our DEFAULT_LIBS may reference either case.  Create both uppercase and
# lowercase symlinks for every .lib file so resolution works on Linux.
# ---------------------------------------------------------------------------
COPY fix_lib_case.sh /tmp/fix_lib_case.sh
RUN bash /tmp/fix_lib_case.sh "${WIN_SDK}/Lib" && rm /tmp/fix_lib_case.sh

# ---------------------------------------------------------------------------
# Layer 3: Environment
# ---------------------------------------------------------------------------
ENV WIN_SDK_DIR=${WIN_SDK}
WORKDIR /workspace

# Default: debug build. Override with e.g. docker run ... --config release
ENTRYPOINT ["python", "build_vp_clang_linux.py"]
CMD ["--config", "debug"]
