cmake_minimum_required(VERSION 3.20)
project(MusicNotes CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)

# Pure logic (no SDL): note naming, chord detection, note log, synth.
add_library(musiccore STATIC
  src/theory.cpp
  src/note_log.cpp
  src/synth.cpp
)
target_include_directories(musiccore PUBLIC src)
target_compile_options(musiccore PUBLIC -Wall -Wextra)

add_executable(musicnotes src/main.cpp src/keymap.cpp src/text.cpp)
target_link_libraries(musicnotes PRIVATE musiccore SDL3::SDL3 SDL3_ttf::SDL3_ttf)

enable_testing()
add_executable(musiccore_tests tests/core_tests.cpp)
target_link_libraries(musiccore_tests PRIVATE musiccore)
add_test(NAME musiccore_tests COMMAND musiccore_tests)
