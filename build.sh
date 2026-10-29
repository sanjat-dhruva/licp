#!/usr/bin/env bash
#
# @file build.sh
# @brief One entry point to build, test and package the LICP on-board service.
#
# Run "./build.sh help" for the menu. Works with bash 3.2 or newer (macOS and Linux).
#
set -euo pipefail

# ---------------------------------------------------------------------------
# Defaults (override with options or environment variables)
# ---------------------------------------------------------------------------
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_TYPE="${BUILD_TYPE:-}"
JOBS="${JOBS:-}"
HAL_DIR="${HAL_DIR:-}"
BUILD_HAL=0
SYSROOT="${SYSROOT:-}"
CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
GTEST_SRC="${GTEST_SRC:-}"
GTEST_FILTER=""
CONFIG_FILE=""
VERBOSE=0

# Build directories (one per kind of build, so they never mix)
DIR_HOST="$ROOT/build"
DIR_ASAN="$ROOT/build-asan"
DIR_ZSOM="$ROOT/build-zsom"
DIR_ARM_TEST="$ROOT/build-arm-test"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
if [ -t 1 ]; then
    C_OK=$'\033[32m'; C_ERR=$'\033[31m'; C_HEAD=$'\033[1m'; C_OFF=$'\033[0m'
else
    C_OK=""; C_ERR=""; C_HEAD=""; C_OFF=""
fi

say()  { printf '%s==> %s%s\n' "$C_HEAD" "$*" "$C_OFF"; }
ok()   { printf '%s%s%s\n' "$C_OK" "$*" "$C_OFF"; }
die()  { printf '%serror: %s%s\n' "$C_ERR" "$*" "$C_OFF" >&2; exit 1; }
need() { command -v "$1" >/dev/null 2>&1 || die "'$1' not found. $2"; }

# Run a command, printing it first when --verbose is given.
run() {
    if [ "$VERBOSE" -eq 1 ]; then printf '  $ %s\n' "$*"; fi
    "$@"
}

cpu_count() {
    if [ -n "$JOBS" ]; then echo "$JOBS"; return; fi
    if command -v nproc >/dev/null 2>&1; then nproc
    elif command -v sysctl >/dev/null 2>&1; then sysctl -n hw.ncpu
    else echo 4
    fi
}

gtest_args() {
    if [ -n "$GTEST_SRC" ]; then echo "-DLICP_GTEST_SOURCE_DIR=$GTEST_SRC"; fi
}

hal_args() {
    if [ -n "$HAL_DIR" ]; then
        echo "-DLICP_WITH_LIBLORAGW=ON"
        echo "-DCMAKE_INCLUDE_PATH=$HAL_DIR/libloragw/inc"
        echo "-DCMAKE_LIBRARY_PATH=$HAL_DIR/libloragw;$HAL_DIR/libtools"
    fi
}

# Print the highest GLIBC symbol version the binary needs. The target's glibc
# must be at least this new, or the binary will not load. Needs GNU sort; a
# silent no-op elsewhere.
glibc_check() {
    local bin="$1"
    local objdump="${CROSS_PREFIX}objdump"
    command -v "$objdump" >/dev/null 2>&1 || return 0
    local max
    max="$("$objdump" -T "$bin" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -Vu 2>/dev/null | tail -1)" || return 0
    [ -n "$max" ] && printf '  requires at most %s (target glibc must be this version or newer)\n' "$max"
    return 0
}

# Build Semtech's libloragw and libtools; $1 = cross prefix ("" for this computer).
build_hal() {
    local prefix="$1"
    [ -n "$HAL_DIR" ] || die "--build-hal needs --hal <path to sx1302_hal>"
    need make "Install make."
    say "Building sx1302_hal in $HAL_DIR (CROSS_COMPILE='$prefix')"
    run make -C "$HAL_DIR/libtools" clean >/dev/null
    run make -C "$HAL_DIR/libloragw" clean >/dev/null
    run make -C "$HAL_DIR/libtools" CROSS_COMPILE="$prefix" -j"$(cpu_count)"
    run make -C "$HAL_DIR/libloragw" CROSS_COMPILE="$prefix" libloragw.a -j"$(cpu_count)"
}

configure_and_build() {
    # $1 = build dir, rest = extra cmake arguments
    local dir="$1"; shift
    need cmake "Install CMake 3.16 or newer (for example: pip install cmake)."
    say "Configuring $(basename "$dir")"
    # shellcheck disable=SC2046
    run cmake -S "$ROOT" -B "$dir" "$@"
    say "Compiling $(basename "$dir") with $(cpu_count) jobs"
    run cmake --build "$dir" -j "$(cpu_count)"
}

run_tests() {
    # $1 = path to licp_tests, $2.. = optional emulator prefix
    local bin="$1"; shift
    [ -x "$bin" ] || die "test binary $bin not found"
    say "Running tests"
    local args="--gtest_brief=1"
    if [ -n "$GTEST_FILTER" ]; then args="$args --gtest_filter=$GTEST_FILTER"; fi
    # shellcheck disable=SC2086
    if "$@" "$bin" $args; then ok "All selected tests passed."; else die "tests failed"; fi
}

# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------
cmd_host() {
    local args=""
    args="$(gtest_args) $(hal_args)"
    if [ "$BUILD_HAL" -eq 1 ]; then build_hal ""; fi
    # shellcheck disable=SC2086
    configure_and_build "$DIR_HOST" -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Debug}" $args
    ok "Built: $DIR_HOST/licpd and $DIR_HOST/tests/licp_tests"
}

cmd_test() {
    cmd_host
    run_tests "$DIR_HOST/tests/licp_tests"
}

cmd_asan() {
    local args=""
    args="$(gtest_args)"
    # shellcheck disable=SC2086
    configure_and_build "$DIR_ASAN" -DCMAKE_BUILD_TYPE=Debug -DLICP_SANITIZE=ON $args
    run_tests "$DIR_ASAN/tests/licp_tests"
}

cmd_zsom() {
    need "${CROSS_PREFIX}gcc" "Install a cross compiler whose glibc is not newer than the target's.
  ZSOM-F01 with PetaLinux 2020.1 (glibc 2.30): Arm GNU Toolchain 9.2-2019.12,
  prefix aarch64-none-linux-gnu- (see README).
  The Debian/Ubuntu package gcc-aarch64-linux-gnu is glibc 2.35 and will NOT run there."
    if [ "$BUILD_HAL" -eq 1 ]; then build_hal "$CROSS_PREFIX"; fi
    local args=""
    args="$(hal_args)"
    if [ -n "$SYSROOT" ]; then args="$args -DCMAKE_SYSROOT=$SYSROOT"; fi
    # shellcheck disable=SC2086
    configure_and_build "$DIR_ZSOM" \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-aarch64-linux-gnu.cmake" \
        -DLICP_CROSS_PREFIX="$CROSS_PREFIX" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" -DLICP_BUILD_TESTS=OFF $args
    if [ -z "$HAL_DIR" ]; then
        printf '%snote:%s built without the SX1302 back-end (no transmitter). Add --hal <sx1302_hal> --build-hal for flight builds.\n' "$C_ERR" "$C_OFF"
    fi
    glibc_check "$DIR_ZSOM/licpd"
    ok "Built: $DIR_ZSOM/licpd (copy it and config/licp.conf.example to the ZSOM-F01)"
}

cmd_arm_test() {
    need "${CROSS_PREFIX}gcc" "Install the cross compiler (apt install gcc-aarch64-linux-gnu g++-aarch64-linux-gnu)."
    need qemu-aarch64 "Install the emulator (apt install qemu-user)."
    local sysroot_lib="${SYSROOT:-/usr/aarch64-linux-gnu}"
    local args=""
    args="$(gtest_args)"
    # shellcheck disable=SC2086
    configure_and_build "$DIR_ARM_TEST" \
        -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/toolchain-aarch64-linux-gnu.cmake" \
        -DLICP_CROSS_PREFIX="$CROSS_PREFIX" \
        -DCMAKE_BUILD_TYPE="${BUILD_TYPE:-Release}" \
        "-DCMAKE_CROSSCOMPILING_EMULATOR=qemu-aarch64;-L;$sysroot_lib" $args
    run_tests "$DIR_ARM_TEST/tests/licp_tests" qemu-aarch64 -L "$sysroot_lib"
}

cmd_docs() {
    need doxygen "Install Doxygen (apt install doxygen / brew install doxygen)."
    say "Generating API documentation"
    (cd "$ROOT" && run doxygen docs/Doxyfile)
    local w="$ROOT/docs/api/doxygen_warnings.txt"
    if [ -s "$w" ]; then
        printf '%sDoxygen warnings:%s\n' "$C_ERR" "$C_OFF"; cat "$w"
    else
        ok "No Doxygen warnings."
    fi
    ok "Open: $ROOT/docs/api/html/index.html"
}

cmd_sim() {
    cmd_host
    local cfg="$CONFIG_FILE"
    if [ -z "$cfg" ]; then
        local tmp="${TMPDIR:-/tmp}/licp-sim"
        mkdir -p "$tmp"
        cfg="$tmp/licp-sim.conf"
        cat > "$cfg" <<EOF
store_path = $tmp/frames.log
device_table_path = $tmp/devices.bin
store_fsync_each = false
rx_backend = sim
tx_backend = sim
obc_tcp_port = 5600
EOF
        say "Using generated configuration $cfg (data in $tmp)"
    fi
    say "Starting licpd with simulated radios (Ctrl+C to stop; on-board computer link on TCP port 5600)"
    run "$DIR_HOST/licpd" -c "$cfg" --sim -v
}

cmd_clean() {
    say "Removing build directories and generated documentation"
    run rm -rf "$DIR_HOST" "$DIR_ASAN" "$DIR_ZSOM" "$DIR_ARM_TEST" "$ROOT/docs/api"
    ok "Clean."
}

cmd_all() {
    cmd_test
    cmd_asan
    if command -v doxygen >/dev/null 2>&1; then cmd_docs; else say "Skipping docs (doxygen not installed)"; fi
    ok "Host build, tests, sanitizer tests and docs completed."
}

usage() {
    cat <<EOF
${C_HEAD}LICP build helper${C_OFF}

Usage: ./build.sh <command> [options]

Commands:
  host        Build licpd and the tests for this computer          -> build/
  test        Build for this computer and run all Google Tests     (expect 112 passed)
  asan        Build with AddressSanitizer + UBSan and run tests    -> build-asan/
  zsom        Cross-compile licpd for the ZSOM-F01 (64-bit Arm)    -> build-zsom/
  arm-test    Cross-compile the tests and run them under qemu      -> build-arm-test/
  docs        Generate the Doxygen API documentation               -> docs/api/html/
  sim         Build and run licpd with simulated radios (bench)
  clean       Remove all build directories and generated docs
  all         test + asan + docs (run this before committing)
  help        Show this menu

Options:
  -t, --type <type>        Debug | Release | RelWithDebInfo
                           (default: Debug for host/test, Release for zsom/arm-test)
  -j, --jobs <n>           Parallel compile jobs (default: number of CPUs)
  -f, --filter <pattern>   Run only matching tests, e.g. 'Store.*' or 'Pipeline.*:Beacon.*'
      --hal <path>         sx1302_hal source tree: enables the SX1302 radio back-end
      --build-hal          Build libloragw from --hal first (cross-compiled for zsom)
      --sysroot <path>     Target sysroot for zsom / arm-test (Yocto or PetaLinux SDK)
      --prefix <prefix>    Cross toolchain prefix (default: aarch64-linux-gnu-;
                           aarch64-none-linux-gnu- for the Arm 9.2-2019.12 toolchain,
                           which is the one that matches PetaLinux 2020.1's glibc 2.30)
      --gtest-src <path>   Build GoogleTest from this source tree (offline machines)
  -c, --config <file>      Configuration file for 'sim' (default: generated)
  -v, --verbose            Print every command before running it
  -h, --help               Show this menu

Examples:
  ./build.sh test                                   # everyday: build and run all tests
  ./build.sh test -f 'Store.*'                      # only the storage tests
  ./build.sh asan                                   # memory and undefined-behaviour checks
  ./build.sh zsom                                   # Arm binary, no radio cards (bench)
  ./build.sh zsom --prefix aarch64-none-linux-gnu-  # glibc 2.30 target (PetaLinux 2020.1)
  ./build.sh zsom --hal ~/sx1302_hal --build-hal    # flight build with the SX1302 back-end
  ./build.sh zsom --sysroot /opt/petalinux/sysroots/cortexa72-cortexa53-xilinx-linux
  ./build.sh arm-test                               # run the tests as Arm code on a PC
  ./build.sh sim                                    # run the service on a PC with simulated radios

Environment variables BUILD_TYPE, JOBS, HAL_DIR, SYSROOT, CROSS_PREFIX and
GTEST_SRC can be used instead of the options.
EOF
}

# ---------------------------------------------------------------------------
# Argument parsing
# ---------------------------------------------------------------------------
[ $# -gt 0 ] || { usage; exit 0; }
COMMAND="$1"; shift

while [ $# -gt 0 ]; do
    case "$1" in
        -t|--type)      [ $# -ge 2 ] || die "$1 needs a value"; BUILD_TYPE="$2"; shift 2 ;;
        -j|--jobs)      [ $# -ge 2 ] || die "$1 needs a value"; JOBS="$2"; shift 2 ;;
        -f|--filter)    [ $# -ge 2 ] || die "$1 needs a value"; GTEST_FILTER="$2"; shift 2 ;;
        --hal)          [ $# -ge 2 ] || die "$1 needs a value"; HAL_DIR="$(cd "$2" && pwd)"; shift 2 ;;
        --build-hal)    BUILD_HAL=1; shift ;;
        --sysroot)      [ $# -ge 2 ] || die "$1 needs a value"; SYSROOT="$2"; shift 2 ;;
        --prefix)       [ $# -ge 2 ] || die "$1 needs a value"; CROSS_PREFIX="$2"; shift 2 ;;
        --gtest-src)    [ $# -ge 2 ] || die "$1 needs a value"; GTEST_SRC="$(cd "$2" && pwd)"; shift 2 ;;
        -c|--config)    [ $# -ge 2 ] || die "$1 needs a value"; CONFIG_FILE="$2"; shift 2 ;;
        -v|--verbose)   VERBOSE=1; shift ;;
        -h|--help)      usage; exit 0 ;;
        *)              die "unknown option '$1' (see ./build.sh help)" ;;
    esac
done

if [ -n "$HAL_DIR" ] && [ ! -f "$HAL_DIR/libloragw/inc/loragw_hal.h" ]; then
    die "--hal $HAL_DIR does not look like sx1302_hal (libloragw/inc/loragw_hal.h missing)"
fi

case "$BUILD_TYPE" in
    ""|Debug|Release|RelWithDebInfo|MinSizeRel) ;;
    *) die "unknown build type '$BUILD_TYPE' (Debug, Release, RelWithDebInfo, MinSizeRel)" ;;
esac

case "$COMMAND" in
    host)       cmd_host ;;
    test)       cmd_test ;;
    asan)       cmd_asan ;;
    zsom)       cmd_zsom ;;
    arm-test)   cmd_arm_test ;;
    docs)       cmd_docs ;;
    sim)        cmd_sim ;;
    clean)      cmd_clean ;;
    all)        cmd_all ;;
    help|-h|--help) usage ;;
    *)          usage; die "unknown command '$COMMAND'" ;;
esac
