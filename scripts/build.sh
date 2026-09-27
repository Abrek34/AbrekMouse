#!/bin/bash
# Quick build script (no cmake required) && cmake build wrapper
# Supports PGO (Profile-Guided Optimization): RAWACCEL_PGO=1
# Supports LTO (Link Time Optimization): RAWACCEL_LTO=1
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BUILD="$ROOT/build-manual"

# PGO/LTO support
USE_PGO="${RAWACCEL_PGO:-0}"
USE_LTO="${RAWACCEL_LTO:-0}"
PGO_GEN="${RAWACCEL_PGO_GEN:-0}"  # Generate profile data
PGO_USE="${RAWACCEL_PGO_USE:-0}"  # Use existing profile data

# Detect whether to use cmake or direct compilation
USE_CMAKE=0
if [ "${RAWACCEL_USE_CMAKE:-0}" = "1" ]; then
    USE_CMAKE=1
fi

# Environment detection
if ! command -v pkg-config >/dev/null 2>&1; then
    echo "pkg-config not found. Install pkgconf or pkg-config." >&2
    exit 1
fi
EVDEV_CFLAGS="$(pkg-config --cflags libevdev)"
EVDEV_LIBS="$(pkg-config --libs libevdev)"
HAVE_GTK4=0
GTK4_CFLAGS=""
GTK4_LIBS=""
if pkg-config --exists gtk4 2>/dev/null; then
    HAVE_GTK4=1
    GTK4_CFLAGS="$(pkg-config --cflags gtk4)"
    GTK4_LIBS="$(pkg-config --libs gtk4)"
fi

CXX="${CXX:-g++}"

# Architecture flags: -march=native optimises for this machine's CPU but breaks portability.
# Set RAWACCEL_PORTABLE=1 to build a portable binary (runs on other CPU architectures).
if [ "${RAWACCEL_PORTABLE:-0}" = "1" ]; then
    MARCH=""
    echo "[INFO] Portable build enabled ( -march=native disabled )"
else
    MARCH="-march=native"
    echo "[INFO] Native architecture build ( -march=native )"
fi

# PGO/LTO flags
PGO_FLAGS=""
LTO_FLAGS=""
if [ "$USE_PGO" = "1" ] && [ "$PGO_GEN" = "1" ]; then
    PGO_FLAGS="-fprofile-generate=$BUILD/pgo-data"
    echo "[INFO] PGO profile generation enabled"
elif [ "$USE_PGO" = "1" ] && [ "$PGO_USE" = "1" ]; then
    PGO_FLAGS="-fprofile-use=$BUILD/pgo-data -fprofile-correction"
    echo "[INFO] PGO profile use enabled"
fi

if [ "$USE_LTO" = "1" ]; then
    LTO_FLAGS="-flto=auto -fwhole-program-vtables -fdevirtualize-at-ltrans"
    echo "[INFO] LTO (Link Time Optimization) enabled"
fi

# Security hardening for a daemon that runs with root + uinput access:
#   -fstack-protector-strong   : stack canaries on functions with arrays/refs
#   -fstack-clash-protection   : probe pages on stack growth -> defeats stack clash attacks
#   -fcf-protection=full       : Intel CET -- indirect-call/branch + return target
#                                validation (no-op on CPUs without CET hardware)
#   -D_FORTIFY_SOURCE=2        : compile-time bounds checking on libc string ops
#   -D_GLIBCXX_ASSERTIONS      : runtime bounds checking on libstdc++ containers
#                                (vector::operator[], std::string ops, ...)
#   -fPIE -pie                 : full ASLR for the binary
#   -Wformat -Wformat-security : catch printf-style format-string mistakes
#   -fomit-frame-pointer       : omit frame pointer for better register allocation (x86_64)

# -fcf-protection is x86-specific (mirrors CMakeLists.txt) so non-x86
# portable builds (-march=native disabled) still compile.
case "$(uname -m)" in
    x86_64|amd64|i[3-6]86) FCF="-fcf-protection=full" ;;
    *) FCF="" ;;
esac

# Base C++ flags with aggressive optimizations
BASE_CXXFLAGS="-std=c++20 -O3 $MARCH -Wall -Wextra -Wpedantic -Wno-unused-parameter"

# Additional performance flags
PERF_FLAGS="-fomit-frame-pointer -funroll-loops -ftree-vectorize -fvect-cost-model=very-cheap"
# SIMD math optimizations
SIMD_FLAGS="-mfpmath=sse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mavx -mavx2 -mfma"
case "$(uname -m)" in
    x86_64|amd64) BASE_CXXFLAGS="$BASE_CXXFLAGS $SIMD_FLAGS" ;;
esac

# If the environment already defines _FORTIFY_SOURCE (e.g. makepkg.conf
# -D_FORTIFY_SOURCE=3), DON'T re-define =2 -- that trips a "redefined" warning.
FORTIFY="-D_FORTIFY_SOURCE=2"
if printf 'int main(){return 0;}' | $CXX $BASE_CXXFLAGS \
        -dM -E -x c++ - 2>/dev/null | grep -q '^#define _FORTIFY_SOURCE'; then
    FORTIFY=""
fi

HARDENING="-fstack-protector-strong -fstack-clash-protection $FCF \
$FORTIFY -D_GLIBCXX_ASSERTIONS -fPIE -Wformat -Wformat-security"
LDFLAGS_HARDEN="-pie -Wl,-z,relro,-z,now,-z,noexecstack,-z,separate-code"

# Combine all flags
ALL_CXXFLAGS="$BASE_CXXFLAGS $PERF_FLAGS $HARDENING $PGO_FLAGS $LTO_FLAGS"
ALL_LDFLAGS="$LDFLAGS_HARDEN $LTO_FLAGS"

if [ "${USE_CMAKE:-0}" = "1" ]; then
    # CMake build path
    mkdir -p "$BUILD"
    cd "$BUILD"
    cmake .. -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_DAEMON=ON -DBUILD_CLI=ON -DBUILD_GUI=ON -DBUILD_TESTS=ON \
        2>&1
    make -j$(nproc) 2>&1
    cd -
else
    # Direct compilation path (original behavior)
    mkdir -p "$BUILD"

echo "[1/3] Building rawaccel-daemon..."
    # EVDEV_CFLAGS (-I/usr/include/libevdev-1.0) is required for
    # "libevdev/libevdev-uinput.h" -- EVDEV_LIBS alone only covers linking.
    "$CXX" $ALL_CXXFLAGS $EVDEV_CFLAGS -I"$ROOT/include" \
        "$ROOT/src/config.cpp" \
        "$ROOT/src/logitech_receiver.cpp" \
        "$ROOT/src/logitech_hidpp.cpp" \
        "$ROOT/daemon/daemon.cpp" \
        "$ROOT/daemon/main.cpp" \
        $EVDEV_LIBS -lpthread $ALL_LDFLAGS \
        -o "$BUILD/rawaccel-daemon"

    echo "[2/3] Building rawaccel-cli..."
    "$CXX" $ALL_CXXFLAGS $EVDEV_CFLAGS -I"$ROOT/include" \
        "$ROOT/src/config.cpp" \
        "$ROOT/src/logitech_receiver.cpp" \
        "$ROOT/src/logitech_hidpp.cpp" \
        "$ROOT/cli/main.cpp" \
        -lpthread $ALL_LDFLAGS \
        -o "$BUILD/rawaccel-cli"

    if [ "$HAVE_GTK4" = "1" ]; then
        echo "[3/3] Building rawaccel-gui..."
        "$CXX" $ALL_CXXFLAGS $EVDEV_CFLAGS $GTK4_CFLAGS -I"$ROOT/include" \
            "$ROOT/src/config.cpp" \
            "$ROOT/src/logitech_receiver.cpp" \
            "$ROOT/src/logitech_hidpp.cpp" \
            "$ROOT/gui/main.cpp" \
                -lpthread $GTK4_LIBS -ldl $ALL_LDFLAGS \
                -o "$BUILD/rawaccel-gui"
    else
        echo "[3/3] Skipping rawaccel-gui (GTK4 development files not found)."
        rm -f "$BUILD/rawaccel-gui"
    fi

    echo ""
    echo "Build complete! Binaries in $BUILD/"
    echo ""
    echo "  $BUILD/rawaccel-daemon   -- run as root or with input group"
    echo "  $BUILD/rawaccel-cli      -- CLI config tool"
    if [ "$HAVE_GTK4" = "1" ]; then
        echo "  $BUILD/rawaccel-gui      -- GUI"
    fi
fi