cmake_minimum_required(VERSION 3.23)
project(tvguide VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Older third-party CMake files (doctest) declare pre-3.5 minimums that CMake 4 rejects.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

FetchContent_Declare(pugixml
  GIT_REPOSITORY https://github.com/zeux/pugixml.git
  GIT_TAG        v1.14
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(pugixml)

add_library(xmltv
  src/parse.cpp
  src/timestamp.cpp
  src/timezone.cpp
  src/html.cpp)
target_include_directories(xmltv PUBLIC include)
target_link_libraries(xmltv PUBLIC pugixml)

# Downloading and gunzip live apart from xmltv so the parser library stays free of I/O.
find_package(CURL REQUIRED)
find_package(ZLIB REQUIRED)

add_library(xmltv_fetch src/fetch.cpp)
target_link_libraries(xmltv_fetch PUBLIC xmltv PRIVATE CURL::libcurl ZLIB::ZLIB)

add_executable(tvguide apps/tvguide.cpp)
target_link_libraries(tvguide PRIVATE xmltv_fetch)

option(TVGUIDE_BUILD_TESTS "Build the test suite" ON)
if(TVGUIDE_BUILD_TESTS)
  enable_testing()

  FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(doctest)

  # Header-only mocking, used with its doctest adapter (<doctest/trompeloeil.hpp>).
  FetchContent_Declare(trompeloeil
    GIT_REPOSITORY https://github.com/rollbear/trompeloeil.git
    GIT_TAG        v49
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(trompeloeil)

  add_executable(tests tests/test_xmltv.cpp)
  target_link_libraries(tests PRIVATE xmltv_fetch doctest::doctest trompeloeil::trompeloeil)
  target_compile_definitions(tests PRIVATE
    XMLTV_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  # One CTest entry per doctest TEST_CASE, so ctest -R / --rerun-failed work per case.
  include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
  doctest_discover_tests(tests TEST_PREFIX "xmltv.")
endif()
