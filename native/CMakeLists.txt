cmake_minimum_required(VERSION 3.16)
project(vlcflix_studio LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

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
  src/media/walk.cpp
  src/media/probe.cpp
  src/media/subtitles.cpp
  src/media/artwork.cpp
  src/media/scan.cpp
  src/platform/proc.cpp
)
target_include_directories(vlcflix_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vlcflix_core PUBLIC pthread)

add_executable(vlcflix src/main.cpp)
target_link_libraries(vlcflix PRIVATE vlcflix_core)

# Turn 2 adds SDL2, libjpeg, libpng, FreeType and libVLC here. They are not part of
# this target yet on purpose: nothing above needs them, and a build that does not
# need a dependency should not require it.
enable_testing()
add_executable(native-tests tests/run_tests.cpp)
target_link_libraries(native-tests PRIVATE vlcflix_core)
add_test(NAME native-tests COMMAND native-tests)
