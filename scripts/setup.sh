#!/usr/bin/env bash
#
# setup.sh – Prepare the build environment for Lakka Installer NX
#
# Usage:  ./scripts/setup.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
echo "==> Project root: $ROOT"

# ── 1. Initialise git submodules (Borealis) ─────────────────────────────────
echo ""
echo "==> Initialising git submodules..."
cd "$ROOT"
git submodule update --init --recursive

# ── 2. Download LZMA SDK (for 7z extraction) ────────────────────────────────
LZMA_DIR="$ROOT/lib/lzma"
LZMA_VER="2301"
LZMA_URL="https://github.com/ip7z/7zip/raw/main/C"

# List of required LZMA SDK files (public domain)
LZMA_FILES=(
    7z.h 7zAlloc.c 7zAlloc.h 7zArcIn.c 7zBuf.c 7zBuf.h 7zBuf2.c
    7zCrc.c 7zCrc.h 7zCrcOpt.c 7zDec.c 7zFile.c 7zFile.h 7zStream.c
    7zTypes.h
    Bcj2.c Bcj2.h Bra.c Bra.h Bra86.c
    Compiler.h CpuArch.c CpuArch.h
    Delta.c Delta.h
    LzmaDec.c LzmaDec.h Lzma2Dec.c Lzma2Dec.h
    Ppmd.h Ppmd7.c Ppmd7.h Ppmd7Dec.c
    Precomp.h
)

if [ -f "$LZMA_DIR/7z.h" ]; then
    echo "==> LZMA SDK already present – skipping download."
else
    echo "==> Downloading LZMA SDK C sources..."
    mkdir -p "$LZMA_DIR"
    for f in "${LZMA_FILES[@]}"; do
        echo "    $f"
        curl -sSL "$LZMA_URL/$f" -o "$LZMA_DIR/$f" || {
            echo "    [WARN] Failed to download $f – trying alternative URL..."
            # Fallback: LZMA SDK on SourceForge / 7-zip.org
            ALT_URL="https://raw.githubusercontent.com/jljusten/LZMA-SDK/master/C/$f"
            curl -sSL "$ALT_URL" -o "$LZMA_DIR/$f" || echo "    [ERROR] Could not download $f"
        }
    done
    echo "==> LZMA SDK downloaded to $LZMA_DIR"
fi

# ── 3. Copy Borealis resources into romfs ────────────────────────────────────
ROMFS="$ROOT/romfs"
BRLS_RES="$ROOT/lib/borealis/library/resources"

if [ -d "$BRLS_RES" ]; then
    echo ""
    echo "==> Copying Borealis resources to romfs/..."
    mkdir -p "$ROMFS"
    cp -r "$BRLS_RES"/* "$ROMFS"/ 2>/dev/null || true
    echo "    Done."
else
    echo ""
    echo "==> [WARN] Borealis resources not found at $BRLS_RES"
    echo "    Make sure git submodules are initialised."
fi

# ── 4. Install devkitPro packages (if dkp-pacman is available) ───────────────
if command -v dkp-pacman &>/dev/null; then
    echo ""
    echo "==> Installing devkitPro packages..."
    sudo dkp-pacman -S --needed --noconfirm \
        switch-dev \
        switch-curl \
        switch-mbedtls \
        switch-zlib \
        switch-bzip2 \
        switch-freetype \
        switch-libpng \
        switch-glfw \
        switch-mesa \
        || echo "    [WARN] Some packages may have failed to install."
else
    echo ""
    echo "==> dkp-pacman not found.  Please install devkitPro and run:"
    echo "    sudo dkp-pacman -S switch-dev switch-curl switch-mbedtls \\"
    echo "        switch-zlib switch-bzip2 switch-freetype switch-libpng \\"
    echo "        switch-glfw switch-mesa"
fi

echo ""
echo "==> Setup complete.  Run 'make' to build the .nro."
