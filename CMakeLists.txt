cmake_minimum_required(VERSION 3.16)
project(vlcflix_studio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(FREETYPE REQUIRED IMPORTED_TARGET freetype2)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
pkg_check_modules(JPEG REQUIRED IMPORTED_TARGET libjpeg)
pkg_check_modules(VLC REQUIRED IMPORTED_TARGET libvlc)

# The logic that has no pixels in it. Turn 1 is entirely this library plus the CLI.
add_library(vlcflix_core STATIC
  src/core/json.cpp
  src/core/log.cpp
  src/core/paths.cpp
  src/core/naming.cpp
  src/core/sha1.cpp
  src/core/store.cpp
  src/core/sync.cpp
  src/core/catalog.cpp
  src/library/walk.cpp
  src/library/probe.cpp
  src/library/subtitles.cpp
  src/library/artwork.cpp
  src/library/scan.cpp
  src/library/fs_browse.cpp
  src/platform/proc.cpp
  src/render/canvas.cpp
  src/render/font.cpp
  src/render/image.cpp
  src/render/png_writer.cpp
  src/ui/theme.cpp
  src/ui/app.cpp
  src/platform/window_sdl.cpp
  src/playback/engine.cpp
  src/playback/presenter.cpp
  src/ui/player.cpp
)
target_include_directories(vlcflix_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vlcflix_core PUBLIC pthread PkgConfig::SDL2 PkgConfig::FREETYPE
                                             PkgConfig::PNG PkgConfig::JPEG PkgConfig::VLC)

add_executable(vlcflix src/main.cpp)
target_link_libraries(vlcflix PRIVATE vlcflix_core)

# libVLC is required for playback (turn 3): the picture is delivered to our own
# callbacks, so the engine runs in this process - no vlc.exe, no HTTP bridge.
enable_testing()
add_executable(native-tests tests/run_tests.cpp tests/ui_tests.cpp tests/player_tests.cpp tests/golden_tests.cpp tests/fault_tests.cpp)
target_link_libraries(native-tests PRIVATE vlcflix_core)
# The tests play the bundled samples, so they run from the repository root where
# `samples/` lives. A build directory outside the tree would otherwise have no way to
# find them, and the playback tests would fail for the wrong reason.
add_test(NAME native-tests COMMAND native-tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# The journey drives the built binary, so it takes the binary path as an argument
# instead of guessing where a `make` build would have put it.
add_test(NAME journey COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/journey.sh --binary $<TARGET_FILE:vlcflix>
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
