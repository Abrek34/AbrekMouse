#!/bin/bash
# Quick build script (no cmake required) && cmake build wrapper
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$SCRIPT_DIR/.."
BUILD="$ROOT/build-manual"

# Detect whether to use cmake or direct compilation
USE_CMAKE=0
if [ "${RAWACCEL_USE_CMAKE:-0}" = "1" ]; then
    USE_CMAKE=1
fi

# Environment detection
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

# Security hardening for a daemon that runs with root + uinput access:
#   -fstack-protector-strong   : stack canaries on functions with arrays/refs
#   -fstack-clash-protection   : probe pages on stack growth → defeats stack clash attacks
#   -fcf-protection=full       : Intel CET — indirect-call/branch + return target
#                                validation (no-op on CPUs without CET hardware)
#   -D_FORTIFY_SOURCE=2        : compile-time bounds checking on libc string ops
#   -D_GLIBCXX_ASSERTIONS      : runtime bounds checking on libstdc++ containers
#                                (vector::operator[], std::string ops, ...)
#   -fPIE -pie                 : full ASLR for the binary
#   -Wformat -Wformat-security : catch printf-style format-string mistakes

# -fcf-protection is x86-specific (mirrors CMakeLists.txt) so non-x86
# portable builds (-march=native disabled) still compile.
case "$(uname -m)" in
    x86_64|amd64|i[3-6]86) FCF="-fcf-protection=full" ;;
    *) FCF="" ;;
esac

# If the environment already defines _FORTIFY_SOURCE (e.g. makepkg.conf
# -D_FORTIFY_SOURCE=3), DON'T re-define =2 — that trips a "redefined"
# warning. Mirrors the CMakeLists.txt check.
FORTIFY="-D_FORTIFY_SOURCE=2"
if echo 'int main(){return 0;}' | $CXX -dM -E -x c++ - 2>/dev/null \
        | grep -q '^#define _FORTIFY_SOURCE'; then
    FORTIFY=""
fi

HARDENING="-fstack-protector-strong -fstack-clash-protection $FCF \
$FORTIFY -D_GLIBCXX_ASSERTIONS -fPIE -Wformat -Wformat-security"
# -z,noexecstack : kernel refuses to execute the stack page (defence in depth
#                  beyond the GNU_STACK PT_LOAD permissions).
# -z,separate-code: keep .text and .rodata in separate PT_LOAD segments so
#                  read-only data isn't mappable as executable.
LDFLAGS_HARDEN="-pie -Wl,-z,relro,-z,now,-z,noexecstack,-z,separate-code"

# Base C++ flags
BASE_CXXFLAGS="-std=c++20 -O3 $MARCH -Wall -Wextra -Wno-unused-parameter"

if [ "${USE_CMAKE:-0}" = "1" ]; then
    # CMake build path
    mkdir -p "$BUILD"
    cd "$BUILD"
    cmake .. -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_DAEMON=ON -DBUILD_CLI=ON -DBUILD_GUI=ON -DBUILD_TESTS=ON \
        2>&1 | tail -5
    make -j$(nproc) 2>&1
    cd -
else
    # Direct compilation path (original behavior)
    mkdir -p "$BUILD"

echo "[1/3] Building rawaccel-daemon..."
    # EVDEV_CFLAGS (-I/usr/include/libevdev-1.0) is required for
    # "libevdev/libevdev-uinput.h" — EVDEV_LIBS alone only covers linking.
    $CXX $BASE_CXXFLAGS $HARDENING $EVDEV_CFLAGS -I$ROOT/include \
        "$ROOT/src/config.cpp" \
        "$ROOT/src/logitech_receiver.cpp" \
        "$ROOT/src/logitech_hidpp.cpp" \
        "$ROOT/daemon/daemon.cpp" \
        "$ROOT/daemon/main.cpp" \
        $EVDEV_LIBS -lpthread $LDFLAGS_HARDEN \
        -o "$BUILD/rawaccel-daemon"

    echo "[2/3] Building rawaccel-cli..."
    $CXX $BASE_CXXFLAGS $EVDEV_CFLAGS $HARDENING -I$ROOT/include \
        "$ROOT/src/config.cpp" \
        "$ROOT/src/logitech_receiver.cpp" \
        "$ROOT/src/logitech_hidpp.cpp" \
        "$ROOT/cli/main.cpp" \
        $LDFLAGS_HARDEN \
        -o "$BUILD/rawaccel-cli"

    if [ "$HAVE_GTK4" = "1" ]; then
        echo "[3/3] Building rawaccel-gui..."
        $CXX $BASE_CXXFLAGS $EVDEV_CFLAGS $GTK4_CFLAGS -I$ROOT/include \
        "$ROOT/src/config.cpp" \
        "$ROOT/src/logitech_receiver.cpp" \
        "$ROOT/src/logitech_hidpp.cpp" \
        "$ROOT/gui/main.cpp" \
            $GTK4_LIBS -ldl $LDFLAGS_HARDEN \
            -o "$BUILD/rawaccel-gui"
    else
        echo "[3/3] Skipping rawaccel-gui (GTK4 development files not found)."
        rm -f "$BUILD/rawaccel-gui"
    fi

    echo ""
    echo "Build complete! Binaries in $BUILD/"
    echo ""
    echo "  $BUILD/rawaccel-daemon   — run as root or with input group"
    echo "  $BUILD/rawaccel-cli      — CLI config tool"
    if [ "$HAVE_GTK4" = "1" ]; then
        echo "  $BUILD/rawaccel-gui      — GUI"
    fi
fi
