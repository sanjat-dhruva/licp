# LICP On-board Service (`licpd`)

C11 software for the S-MIoT satellite payload (LEO IoT Communication Payload, LICP), running on Linux on the ZSOM-F01 controller (Zynq UltraScale+ XCZU3EG).

It implements the payload's part of "S-MIoT LEO Network – How It Works" (7 Oct 2026):

- **Frame checks.** Every received frame is parsed, its device looked up, its 32-bit frame counter rebuilt, and its message integrity code verified. Duplicates and replays are rejected.
- **Priority store-and-forward.** Accepted frames go to a checksummed log in flash that survives power loss. Emergency frames are stored first. Frames are kept until the ground confirms delivery.
- **Superframe timing.** Each 45.5-second superframe is a 3.5-second beacon window followed by twelve 3.5-second slots, locked to satellite navigation time.
- **Load control.** The payload counts collisions in shared slots and sets the access probability per priority class. Bulk traffic is throttled first; emergency traffic never is.
- **Beacon and downlinks.** A signed beacon is sent every superframe. It carries:
  - time and access probabilities,
  - emergency acknowledgements,
  - call-ins and broadcast notices,
  - the orbit record in rotation.

  One-to-one messages, broadcasts and the acknowledgement bitmap follow in the same window.
- **Ground upload plan.** The central system uploads a plan with:
  - devices and keys,
  - orbit records,
  - pre-built downlinks,
  - parameters.

  Corrupted plans are rejected whole.
- **On-board computer link.** A framed message protocol is used to:
  - upload plans,
  - hand stored frames to the ground,
  - release them,
  - read telemetry.

## Folder structure

```
licp/
├── CMakeLists.txt              top-level build
├── README.md                   this file
├── build.sh                    build, test and package helper (./build.sh help)
├── app/main.c                  licpd main program
├── cmake/                      cross-compilation toolchain for the ZSOM-F01
├── config/licp.conf.example    every configuration key with its default
├── docs/
│   ├── Doxyfile                API documentation (doxygen docs/Doxyfile)
│   ├── architecture.md         modules, data flow, formats
│   ├── testing.md              how to test, expected results, deviations
│   ├── deploy.md               how to get licpd onto the board
│   └── adding_features.md      how to extend the code
├── include/licp/<module>/      public headers, one folder per module
├── src/<module>/               sources, mirroring include/
├── tests/
│   ├── CMakeLists.txt          Google Test build
│   ├── support/test_util.hpp   shared test helpers
│   └── unit/test_*.cpp         112 tests
└── tools/gen_vectors.py        independent generator of test vectors
```

Modules: `common`, `crypto`, `lorawan`, `region`, `proto`, `core`, `storage`, `hal`, `obc`, `app`. See `docs/architecture.md`.

## Quick start: `build.sh`

You don't need to remember the CMake commands below; `build.sh` wraps them all. Run `./build.sh help` for the full menu.

| Command | What it does |
|---|---|
| `./build.sh test` | Build for this computer and run all 112 tests |
| `./build.sh test -f 'Store.*'` | Run only matching tests |
| `./build.sh asan` | Build with memory and undefined-behaviour checks, then run the tests |
| `./build.sh zsom` | Cross-compile `licpd` for the ZSOM-F01 (no radio cards) |
| `./build.sh zsom --hal <sx1302_hal> --build-hal` | Flight build including the SX1302 back-end |
| `./build.sh arm-test` | Run the tests as 64-bit Arm code under qemu |
| `./build.sh docs` | Generate the Doxygen documentation |
| `./build.sh sim` | Run the service on a PC with simulated radios |
| `./build.sh all` | test + asan + docs; run before committing |
| `./build.sh clean` | Delete all build folders |

## Build and test by hand (host)

You need CMake 3.16 or newer, GCC or Clang, and GoogleTest. GoogleTest can come from the system package, from `/usr/src/googletest`, or be downloaded automatically.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
cd build && ctest --output-on-failure      # expect: 100% tests passed, 0 tests failed out of 112
```

Sanitizer build (AddressSanitizer and UndefinedBehaviorSanitizer):

```sh
cmake -S . -B build-asan -DLICP_SANITIZE=ON && cmake --build build-asan -j && ./build-asan/tests/licp_tests
```

## Build for the ZSOM-F01

### Toolchain: match the target's glibc

The toolchain's glibc must be **no newer** than the board's. glibc is backward
compatible one way only: a binary built against an older glibc runs on a newer
one, never the reverse. A binary built with Ubuntu's `gcc-aarch64-linux-gnu`
(glibc 2.35) fails to start on the board with `version 'GLIBC_2.34' not found`.

PetaLinux 2020.1 on the ZSOM-F01 ships **glibc 2.30**. Check your own image with
`ldd --version` on the board.

The matching toolchain is Arm's GNU Toolchain for the A-profile Architecture
**9.2-2019.12** (GCC 9.2.1, glibc 2.30, binutils 2.33.1):

```sh
wget https://developer.arm.com/-/media/Files/downloads/gnu-a/9.2-2019.12/binrel/gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu.tar.xz
sudo mkdir -p /opt/toolchains
sudo tar -xJf gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu.tar.xz -C /opt/toolchains
export PATH=/opt/toolchains/gcc-arm-9.2-2019.12-x86_64-aarch64-none-linux-gnu/bin:$PATH
```

Its prefix is `aarch64-none-linux-gnu-` (Arm's naming carries a `none` vendor
field), which is not `build.sh`'s default:

```sh
./build.sh zsom --prefix aarch64-none-linux-gnu-
```

The build prints the binary's maximum GLIBC requirement when it finishes.

### Verify before deploying

```sh
file build-zsom/licpd
aarch64-none-linux-gnu-objdump -T build-zsom/licpd | grep -o 'GLIBC_[0-9.]*' | sort -Vu
```

The highest version printed must be at or below the board's glibc. `GLIBC_2.34`
here means the wrong compiler was used. See `docs/deploy.md` for getting the
binary onto the board.

### By hand

```sh
cmake -S . -B build-zsom -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-linux-gnu.cmake \
      -DCMAKE_BUILD_TYPE=Release -DLICP_BUILD_TESTS=OFF \
      -DLICP_WITH_LIBLORAGW=ON \
      -DCMAKE_INCLUDE_PATH=<sx1302_hal>/libloragw/inc \
      -DCMAKE_LIBRARY_PATH="<sx1302_hal>/libloragw;<sx1302_hal>/libtools"
cmake --build build-zsom -j
```

`LICP_WITH_LIBLORAGW=ON` adds the SX1302 radio back-end, which needs Semtech's [sx1302_hal](https://github.com/Lora-net/sx1302_hal) (tested with version 2.1.0) cross-built for the target. Without it, the service builds and runs, but has no hardware transmitter.

## Run

```sh
licpd -c /etc/licp/licp.conf          # hardware radios
licpd -c licp.conf --sim -v           # simulated radios, debug logs (bench and ground tests)
```

The on-board computer connects over TCP (default port 5600). See `include/licp/obc/licp_obc.h` for the message set.

## Documentation

```sh
doxygen docs/Doxyfile                 # or: cmake --build build --target docs
open docs/api/html/index.html
```

Every public function, type and module is documented in its header with Doxygen comments.

## Status

The software modules are complete and pass their tests on x86-64 and on 64-bit Arm (natively and under emulation). `licpd` has been cross-compiled and run on the ZSOM-F01 (PetaLinux 2020.1) with simulated radios; see `docs/testing.md` section 3.1. Hardware integration remains:

- the SX1302 cards,
- the frequency-hopping receiver driver,
- the on-board computer interface.

These parts are open; see the deviations in `docs/testing.md`.
