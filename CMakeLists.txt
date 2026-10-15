#
# @file CMakeLists.txt
# @brief Top-level build for the LICP on-board service.
#
# Targets:
#   - licp_core   : static library with every module (no hardware dependencies)
#   - licpd       : the on-board service executable (app/main.c)
#   - licp_tests  : Google Test suite (when LICP_BUILD_TESTS=ON)
#
# Options:
#   LICP_BUILD_TESTS      Build the Google Test suite (default ON)
#   LICP_WITH_LIBLORAGW   Build the SX1302 radio back-end against Semtech libloragw (default OFF)
#   LICP_SANITIZE         Build with AddressSanitizer + UndefinedBehaviorSanitizer (default OFF)
#   LICP_WERROR           Treat warnings as errors (default ON)
#
cmake_minimum_required(VERSION 3.16)
project(licp VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LICP_BUILD_TESTS    "Build the Google Test suite"                         ON)
option(LICP_WITH_LIBLORAGW "Build the SX1302 back-end against Semtech libloragw" OFF)
option(LICP_SANITIZE       "Enable AddressSanitizer and UBSan"                   OFF)
option(LICP_WERROR         "Treat warnings as errors"                            ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# Compiler warnings and options shared by every target
# ---------------------------------------------------------------------------
add_library(licp_options INTERFACE)
target_compile_options(licp_options INTERFACE
  -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion
  $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes -Wmissing-prototypes -pedantic>)
target_compile_definitions(licp_options INTERFACE _POSIX_C_SOURCE=200809L)
if(LICP_WERROR)
  target_compile_options(licp_options INTERFACE -Werror)
endif()
if(LICP_SANITIZE)
  target_compile_options(licp_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(licp_options INTERFACE -fsanitize=address,undefined)
endif()

# ---------------------------------------------------------------------------
# Core library: every module under src/ except hardware back-ends that need
# external libraries. New modules are picked up automatically: add a folder
# under src/<module>/ and include/licp/<module>/ and re-run CMake.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE LICP_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.c)
list(FILTER LICP_CORE_SOURCES EXCLUDE REGEX ".*/src/hal/licp_radio_sx1302\\.c$")

add_library(licp_core STATIC ${LICP_CORE_SOURCES})
target_include_directories(licp_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(licp_core PUBLIC licp_options m)

if(LICP_WITH_LIBLORAGW)
  find_path(LIBLORAGW_INCLUDE_DIR loragw_hal.h PATH_SUFFIXES libloragw REQUIRED)
  find_library(LIBLORAGW_LIBRARY loragw REQUIRED)
  target_sources(licp_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/hal/licp_radio_sx1302.c)
  target_include_directories(licp_core SYSTEM PRIVATE ${LIBLORAGW_INCLUDE_DIR})
  # libloragw depends on the libtools helpers shipped with sx1302_hal.
  find_library(LIBLORAGW_TINYMT tinymt32)
  find_library(LIBLORAGW_PARSON parson)
  find_library(LIBLORAGW_BASE64 base64)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
  target_link_libraries(licp_core PUBLIC ${LIBLORAGW_LIBRARY}
    $<$<BOOL:${LIBLORAGW_TINYMT}>:${LIBLORAGW_TINYMT}>
    $<$<BOOL:${LIBLORAGW_PARSON}>:${LIBLORAGW_PARSON}>
    $<$<BOOL:${LIBLORAGW_BASE64}>:${LIBLORAGW_BASE64}>
    Threads::Threads rt m)
  target_compile_definitions(licp_core PUBLIC LICP_WITH_LIBLORAGW=1)
endif()

# ---------------------------------------------------------------------------
# On-board service executable
# ---------------------------------------------------------------------------
add_executable(licpd ${CMAKE_CURRENT_SOURCE_DIR}/app/main.c)
target_link_libraries(licpd PRIVATE licp_core)
install(TARGETS licpd RUNTIME DESTINATION bin)
install(FILES config/licp.conf.example DESTINATION etc/licp)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(LICP_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Documentation: `cmake --build build --target docs`
# ---------------------------------------------------------------------------
find_package(Doxygen QUIET)
if(DOXYGEN_FOUND)
  add_custom_target(docs
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/docs/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM)
endif()
