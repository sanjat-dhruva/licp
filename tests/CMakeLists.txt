#
# @file tests/CMakeLists.txt
# @brief Google Test suite (see the GoogleTest selection order below).
#
# Google Test, in order of preference:
#   1. an installed package (find_package),
#   2. sources given with -DLICP_GTEST_SOURCE_DIR=... or the Debian/Ubuntu
#      package sources in /usr/src/googletest (needed when cross-compiling),
#   3. a download of release 1.14.0 from GitHub.
set(LICP_GTEST_SOURCE_DIR "" CACHE PATH "GoogleTest source tree to build from")
if(NOT LICP_GTEST_SOURCE_DIR AND NOT CMAKE_CROSSCOMPILING)
  find_package(GTest QUIET)
endif()
if(NOT GTest_FOUND)
  if(NOT LICP_GTEST_SOURCE_DIR AND EXISTS /usr/src/googletest/CMakeLists.txt)
    set(LICP_GTEST_SOURCE_DIR /usr/src/googletest)
  endif()
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
  if(LICP_GTEST_SOURCE_DIR)
    add_subdirectory(${LICP_GTEST_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/googletest EXCLUDE_FROM_ALL)
  else()
    include(FetchContent)
    FetchContent_Declare(googletest
      GIT_REPOSITORY https://github.com/google/googletest.git
      GIT_TAG        v1.14.0)
    FetchContent_MakeAvailable(googletest)
  endif()
  if(NOT TARGET GTest::gtest)
    add_library(GTest::gtest ALIAS gtest)
    add_library(GTest::gtest_main ALIAS gtest_main)
  endif()
endif()

file(GLOB LICP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/unit/*.cpp
                                              ${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp)
add_executable(licp_tests ${LICP_TEST_SOURCES})
target_include_directories(licp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_compile_definitions(licp_tests PRIVATE LICP_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
target_link_libraries(licp_tests PRIVATE licp_core GTest::gtest GTest::gtest_main)
# Google Test headers trigger -Wconversion/-Wshadow noise; keep tests strict on errors only.
target_compile_options(licp_tests PRIVATE -Wno-conversion -Wno-shadow -Wno-error)

include(GoogleTest)
gtest_discover_tests(licp_tests DISCOVERY_TIMEOUT 60)
