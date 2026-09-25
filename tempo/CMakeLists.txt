# Pure logic (no SDL) so it can be unit tested.
add_library(tempocore STATIC src/tempo.c)
target_include_directories(tempocore PUBLIC src)
target_compile_options(tempocore PUBLIC -Wall -Wextra)

add_executable(tempo src/main.c)
target_link_libraries(tempo PRIVATE tempocore tiny)

add_executable(tempo_tests tests/tempo_test.c)
target_link_libraries(tempo_tests PRIVATE tempocore tiny)
add_test(NAME tempo_tests COMMAND tempo_tests)
