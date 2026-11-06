#!/bin/bash
# Quick build script (no cmake required) && cmake build wrapper
# Supports PGO (Profile-Guided Optimization): RAWACCEL_PGO=1
# Supports LTO (Link Time Optimization): RAWACCEL_LTO=1
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BUILD="$ROOT/build-manual"
# T51-01: the CMake path uses its OWN directory — sharing build-manual let
# CMake's CMakeCache/Makefile/CMakeFiles mix with the direct-path binaries.
BUILD_CMAKE="$ROOT/build-cmake"

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

# Compiler family: a few optimisation flags are GCC-only / Clang-only, and the
# documented `CXX=clang++ bash scripts/build.sh` path used to fail before it
# compiled a single file (`-fvect-cost-model=very-cheap`), while
# `RAWACCEL_LTO=1` failed on BOTH compilers (the old LTO set mixed
# `-fwhole-program-vtables` (Clang) with `-fdevirtualize-at-ltrans` (GCC)).
# Detect once here and keep the two flag sets disjoint.
IS_CLANG=0
CXX_PREDEFINED="$($CXX -dM -E -x c++ /dev/null 2>/dev/null || true)"
case "$CXX_PREDEFINED" in
    *"#define __clang__"*) IS_CLANG=1 ;;
esac

# RAWACCEL_PORTABLE=1 means: DO NOT USE -march=native.  It exists for
# reproducible/CI builds (GitHub runners do not guarantee a CPU level, and
# -march=native bakes whatever the runner happens to have into the binary).
# It is NOT a promise that the result runs anywhere -- see below.
#
# THIS FLAG WAS TWO SWITCHES, NOT ONE, AND THE TWO DISAGREED.  MARCH controls
# -march=native, but SIMD_FLAGS below was appended unconditionally, so this
# branch only dropped -march=native and still compiled in -mavx2 -mfma.  Two
# consequences, both measured:
#   1) The flag did not do what its own comment claimed.  Backend selection is
#      compile-time only (simd_math.hpp:48-60 keys off __AVX2__ / __SSE2__;
#      measured: zero uses of __builtin_cpu_supports/cpuid/xgetbv in the tree),
#      so there is no runtime dispatch to fall back on and an AVX2 instruction
#      on a CPU without AVX2 is #UD -> SIGILL.  A binary whose whole purpose is
#      to be CPU-independent died at startup on any pre-Haswell x86-64.
#   2) The same flag meant DIFFERENT THINGS in the two build systems, which is
#      the real defect.  CMakeLists.txt:68 and scripts/bench_hotpath.sh:20-25
#      add no ISA flags at all, so there "portable" honestly meant the
#      x86-64 baseline; only build.sh added -mavx2 -mfma.  setup.sh and CI
#      both go through build.sh, so the misleading path was also the common
#      one.  All three now mean the same thing: no -march=native, no optional
#      ISA level beyond the baseline.
#
# ARM/aarch64 was never affected: the uname case below adds nothing there,
# which is why this survived.  SSE2 is the x86-64 baseline ABI floor, so naming
# it in SIMD_FLAGS is a no-op on this target and is kept only to document intent
# -- the load-bearing part of the fix is DROPPING -mavx2 -mfma.
if [ "${RAWACCEL_PORTABLE:-0}" = "1" ]; then
    MARCH=""
    echo "[INFO] Baseline build ( -march=native and AVX2/AVX/FMA disabled )"
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
    PGO_FLAGS="-fprofile-use=$BUILD/pgo-data"
    # -fprofile-correction is GCC-only; Clang warns and ignores it.
    if [ "$IS_CLANG" = "0" ]; then
        PGO_FLAGS="$PGO_FLAGS -fprofile-correction"
    fi
    echo "[INFO] PGO profile use enabled"
fi

if [ "$USE_LTO" = "1" ]; then
    # GCC and Clang do not share either of the two "extra" LTO flags:
    #   GCC   : -fdevirtualize-at-ltrans
    #   Clang : -fwhole-program-vtables
    # `-flto=auto` is accepted by both (Clang treats `auto` as the default).
    if [ "$IS_CLANG" = "1" ]; then
        LTO_FLAGS="-flto=auto -fwhole-program-vtables"
    else
        LTO_FLAGS="-flto=auto -fdevirtualize-at-ltrans"
    fi
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
# baseline builds (RAWACCEL_PORTABLE=1) still compile.
case "$(uname -m)" in
    x86_64|amd64|i[3-6]86) FCF="-fcf-protection=full" ;;
    *) FCF="" ;;
esac

# Base C++ flags with aggressive optimizations
BASE_CXXFLAGS="-std=c++20 -O3 $MARCH -Wall -Wextra -Wpedantic -Wno-unused-parameter"

# Additional performance flags.  -fvect-cost-model is GCC-only; Clang rejects
# the whole command line with "unknown argument" (the documented clang build
# path failed here first).
PERF_FLAGS="-fomit-frame-pointer -funroll-loops -ftree-vectorize"
if [ "$IS_CLANG" = "0" ]; then
    PERF_FLAGS="$PERF_FLAGS -fvect-cost-model=very-cheap"
fi
# SIMD math optimizations.
# These are what the ORACLE/GATE flags are checked against conceptually, so
# they must follow the same portability switch as MARCH above -- see the note
# at :41.  Kept in one place on purpose: tests/run_simd_parity.sh has its own
# BACKENDS list because it must select each backend EXPLICITLY (including the
# scalar fallback), which is a different question from "what does shipping
# compile with".
if [ "${RAWACCEL_PORTABLE:-0}" = "1" ]; then
    SIMD_FLAGS="-mfpmath=sse -msse2"          # baseline x86-64 only
else
    SIMD_FLAGS="-mfpmath=sse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mavx -mavx2 -mfma"
fi
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
    # CMake build path (T51-01: separate dir; T51-02: graceful GTK4-less skip)
    # T51-02: mirror the direct path — if GTK4 dev files are absent, configure
    # with BUILD_GUI=OFF instead of hard-failing inside pkg_check_modules(GTK4
    # REQUIRED).
    GUI_FLAG="-DBUILD_GUI=ON"
    if [ "$HAVE_GTK4" != "1" ]; then
        echo "[3/3] Skipping rawaccel-gui (GTK4 development files not found)."
        GUI_FLAG="-DBUILD_GUI=OFF"
    fi
    mkdir -p "$BUILD_CMAKE"
    cmake -S "$ROOT" -B "$BUILD_CMAKE" -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_DAEMON=ON -DBUILD_CLI=ON $GUI_FLAG -DBUILD_TESTS=ON \
        2>&1
    make -C "$BUILD_CMAKE" -j$(nproc) 2>&1
    echo ""
    echo "Build complete! Binaries in $BUILD_CMAKE/"
else
    # Direct compilation path (original behavior)
    mkdir -p "$BUILD"

    TOTAL_STEPS=3
    if [ "$HAVE_GTK4" = "1" ]; then
        TOTAL_STEPS=4
    fi

echo "[1/$TOTAL_STEPS] Building rawaccel-daemon..."
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

    echo "[2/$TOTAL_STEPS] Building rawaccel-cli..."
    "$CXX" $ALL_CXXFLAGS $EVDEV_CFLAGS -I"$ROOT/include" \
        "$ROOT/src/config.cpp" \
        "$ROOT/src/logitech_receiver.cpp" \
        "$ROOT/src/logitech_hidpp.cpp" \
        "$ROOT/cli/main.cpp" \
        -lpthread $ALL_LDFLAGS \
        -o "$BUILD/rawaccel-cli"

    if [ "$HAVE_GTK4" = "1" ]; then
        echo "[3/$TOTAL_STEPS] Building rawaccel-gui..."
        # Same ETXTBSY hazard as the installer below: a GUI launched directly
        # from build-manual/ may still be running when setup.sh rebuilds.
        gui_tmp="$BUILD/rawaccel-gui.tmp.$$"
        "$CXX" $ALL_CXXFLAGS $EVDEV_CFLAGS $GTK4_CFLAGS -I"$ROOT/include" \
            "$ROOT/src/config.cpp" \
            "$ROOT/src/logitech_receiver.cpp" \
            "$ROOT/src/logitech_hidpp.cpp" \
            "$ROOT/gui/main.cpp" \
                -lpthread $GTK4_LIBS -ldl $ALL_LDFLAGS \
                -o "$gui_tmp"
        mv -f "$gui_tmp" "$BUILD/rawaccel-gui"

        echo "[4/$TOTAL_STEPS] Building rawaccel-installer..."
        # The GUI installer is running while setup.sh rebuilds the tree.  A
        # direct `g++ -o build-manual/rawaccel-installer` would hit ETXTBSY on
        # the live executable; compile to a fresh inode and rename over it.
        installer_tmp="$BUILD/rawaccel-installer.tmp.$$"
        "$CXX" $ALL_CXXFLAGS $GTK4_CFLAGS -I"$ROOT/include" \
            "$ROOT/installer/main.cpp" \
                -lpthread $GTK4_LIBS -ldl $ALL_LDFLAGS \
                -o "$installer_tmp"
        mv -f "$installer_tmp" "$BUILD/rawaccel-installer"
    else
        echo "[3/$TOTAL_STEPS] Skipping rawaccel-gui (GTK4 development files not found)."
        rm -f "$BUILD/rawaccel-gui"
        rm -f "$BUILD/rawaccel-installer"
    fi

    echo ""
    echo "Build complete! Binaries in $BUILD/"
    echo ""
    echo "  $BUILD/rawaccel-daemon   -- run as root or with input group"
    echo "  $BUILD/rawaccel-cli      -- CLI config tool"
    if [ "$HAVE_GTK4" = "1" ]; then
        echo "  $BUILD/rawaccel-gui      -- GUI"
        echo "  $BUILD/rawaccel-installer -- kolay kurulum GUI"
    fi
fi