# Shared SDL3 helpers and the 5x7 bitmap font (C, usable from C++).
add_library(tiny STATIC tiny.c font5x7.c)
target_include_directories(tiny PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(tiny PRIVATE -Wall -Wextra)
target_link_libraries(tiny PUBLIC SDL3::SDL3)
