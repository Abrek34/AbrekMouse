cmake_minimum_required(VERSION 3.16)
project(rawaccel-linux VERSION 0.6.7 LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CheckCXXSourceCompiles)
include(GNUInstallDirs)

# ── Build options ─────────────────────────────────────────────────────────────
option(BUILD_GUI    "Build GTK4 GUI"    ON)
option(BUILD_DAEMON "Build daemon"      ON)
option(BUILD_CLI    "Build CLI tool"    ON)
option(BUILD_TESTS  "Build unit tests" ON)
option(RAWACCEL_PORTABLE "Portable build (-march=native disabled)" OFF)

# ── Compiler warnings ─────────────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)

# ── Security hardening (mirrors scripts/build.sh) ─────────────────────────────
# The daemon runs as root with uinput access, so every binary gets the same
# hardening the direct-compilation path applies:
#   -fstack-protector-strong   : stack canaries on functions with arrays/refs
#   -fstack-clash-protection   : defeat stack clash attacks
#   -D_FORTIFY_SOURCE=2        : compile-time bounds checks on libc string ops
#   -D_GLIBCXX_ASSERTIONS      : runtime bounds checks on libstdc++ containers
#   -fPIE + -pie               : full ASLR for the binary
#   -pie -z relro,now,noexecstack,separate-code : hardened linking
# -fcf-protection=full (Intel CET) is x86-specific; guarded so other arches still build.
# _FORTIFY_SOURCE: if already defined by the environment (e.g. makepkg.conf
# -D_FORTIFY_SOURCE=3), keep it and avoid a "redefined" warning; otherwise add
# the hardened -D_FORTIFY_SOURCE=2 default.
set(_rawaccel_old_req "${CMAKE_REQUIRED_FLAGS}")
set(CMAKE_REQUIRED_FLAGS "${CMAKE_CXX_FLAGS}")
check_cxx_source_compiles("
#ifndef _FORTIFY_SOURCE
#error not externally defined
#endif
int main() { return 0; }" RAWACCEL_FORTIFY_PREDEFINED)
set(CMAKE_REQUIRED_FLAGS "${_rawaccel_old_req}")

add_compile_options(
    -fstack-protector-strong
    -fstack-clash-protection
    -D_GLIBCXX_ASSERTIONS
    -fPIE
    -Wformat
    -Wformat-security
)
if(NOT RAWACCEL_FORTIFY_PREDEFINED)
    add_compile_options(-D_FORTIFY_SOURCE=2)
endif()
if(NOT RAWACCEL_PORTABLE)
    add_compile_options(-march=native)
endif()
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64|i[3-6]86)$")
    add_compile_options(-fcf-protection=full)
endif()
add_link_options(-pie -Wl,-z,relro,-z,now,-z,noexecstack,-z,separate-code)

# ── Dependencies ─────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(EVDEV REQUIRED libevdev)

if(BUILD_GUI)
    pkg_check_modules(GTK4 REQUIRED gtk4)
endif()

# ── Shared config library ─────────────────────────────────────────────────────
add_library(rawaccel-config STATIC
    ${CMAKE_SOURCE_DIR}/src/config.cpp
    ${CMAKE_SOURCE_DIR}/src/logitech_receiver.cpp
    ${CMAKE_SOURCE_DIR}/src/logitech_hidpp.cpp
)
target_include_directories(rawaccel-config PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_options(rawaccel-config PRIVATE -Wall -Wextra)
target_compile_options(rawaccel-config PRIVATE $<$<CONFIG:Release>:-O3> $<$<CONFIG:Debug>:-O0 -g>)

# ── Daemon ────────────────────────────────────────────────────────────────────
if(BUILD_DAEMON)
    add_executable(rawaccel-daemon
        daemon/main.cpp
        daemon/daemon.cpp
    )
    target_include_directories(rawaccel-daemon PRIVATE
        ${EVDEV_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/daemon
    )
    target_link_libraries(rawaccel-daemon
        rawaccel-config
        ${EVDEV_LIBRARIES}
        Threads::Threads
        pthread
    )

    install(TARGETS rawaccel-daemon DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ── CLI ───────────────────────────────────────────────────────────────────────
if(BUILD_CLI)
    add_executable(rawaccel-cli
        cli/main.cpp
    )
    target_include_directories(rawaccel-cli PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(rawaccel-cli
        rawaccel-config
        Threads::Threads
    )

    install(TARGETS rawaccel-cli DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ── GUI ───────────────────────────────────────────────────────────────────────
if(BUILD_GUI)
    add_executable(rawaccel-gui
        gui/main.cpp
    )
    target_include_directories(rawaccel-gui PRIVATE
        ${GTK4_INCLUDE_DIRS}
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(rawaccel-gui
        rawaccel-config
        ${GTK4_LIBRARIES}
        Threads::Threads
        dl
    )

    install(TARGETS rawaccel-gui DESTINATION ${CMAKE_INSTALL_BINDIR})

    # Desktop file
    install(FILES scripts/rawaccel.desktop
            DESTINATION /usr/share/applications)
endif()

# ── Systemd service ───────────────────────────────────────────────────────────
install(FILES scripts/rawaccel.service
        DESTINATION /usr/lib/systemd/system)

# ── Config dir ────────────────────────────────────────────────────────────────
install(FILES config/default.json
        DESTINATION /etc/rawaccel
        RENAME settings.json)

# ── libinput quirks ───────────────────────────────────────────────────────────
install(FILES scripts/rawaccel.quirks
        DESTINATION /usr/share/libinput
        RENAME 50-rawaccel.quirks)

# ── Tests ─────────────────────────────────────────────────────────────────────
if(BUILD_TESTS)
    enable_testing()
    add_executable(rawaccel-tests
        tests/test_accel.cpp
    )
    target_include_directories(rawaccel-tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    target_link_libraries(rawaccel-tests
        rawaccel-config
        Threads::Threads
        dl
    )
    add_test(NAME rawaccel-unit-tests COMMAND rawaccel-tests)
endif()

message(STATUS "")
message(STATUS "RawAccel Linux build configuration:")
message(STATUS "  Build daemon : ${BUILD_DAEMON}")
message(STATUS "  Build CLI    : ${BUILD_CLI}")
message(STATUS "  Build GUI    : ${BUILD_GUI}")
message(STATUS "  Build tests  : ${BUILD_TESTS}")
message(STATUS "")
