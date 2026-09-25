set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

add_executable(musiccore_tests tests/core_tests.cpp)
target_link_libraries(musiccore_tests PRIVATE musiccore)
add_test(NAME musiccore_tests COMMAND musiccore_tests)
