# Pure logic (no SDL) so it can be unit tested: board, pieces, solver, solved-date log.
add_library(puzzlecore STATIC src/puzzle.c)
target_include_directories(puzzlecore PUBLIC src)
target_compile_options(puzzlecore PUBLIC -Wall -Wextra)

add_executable(puzzle src/main.c)
target_link_libraries(puzzle PRIVATE puzzlecore tiny)

add_executable(puzzle_tests tests/puzzle_test.c)
target_link_libraries(puzzle_tests PRIVATE puzzlecore tiny)
add_test(NAME puzzle_tests COMMAND puzzle_tests)
