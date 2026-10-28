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
  src/platform/proc.cpp
  src/render/canvas.cpp
  src/render/font.cpp
  src/render/image.cpp
  src/render/png_writer.cpp
  src/ui/theme.cpp
  src/ui/app.cpp
  src/platform/window_sdl.cpp
)
target_include_directories(vlcflix_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vlcflix_core PUBLIC pthread PkgConfig::SDL2 PkgConfig::FREETYPE
                                             PkgConfig::PNG PkgConfig::JPEG)

add_executable(vlcflix src/main.cpp)
target_link_libraries(vlcflix PRIVATE vlcflix_core)

# Turn 3 adds libVLC to this target. It is not required yet, and a build that does
# not need a dependency should not require it.
enable_testing()
add_executable(native-tests tests/run_tests.cpp tests/ui_tests.cpp)
target_link_libraries(native-tests PRIVATE vlcflix_core)
add_test(NAME native-tests COMMAND native-tests)
