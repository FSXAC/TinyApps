# Pure logic (no SDL) so it can be unit tested: the graph core plus each game's rules.
add_library(svcore STATIC src/sv.c games/ttt.c games/yearpuzzle.c)
target_include_directories(svcore PUBLIC src games)
target_compile_options(svcore PUBLIC -Wall -Wextra)
target_link_libraries(svcore PUBLIC puzzlecore)

add_executable(statespace src/main.c games/view_util.c games/ttt_view.c games/yearpuzzle_view.c)
target_link_libraries(statespace PRIVATE svcore tiny)

add_executable(sv_tests tests/sv_test.c)
target_link_libraries(sv_tests PRIVATE svcore tiny)
add_test(NAME sv_tests COMMAND sv_tests)

# Not part of the app: each tools/*.c measures puzzle strategies in the state space, as sv_<name>.
file(GLOB SV_TOOLS CONFIGURE_DEPENDS tools/*.c)
foreach(src ${SV_TOOLS})
    get_filename_component(tool ${src} NAME_WE)
    add_executable(sv_${tool} ${src})
    target_link_libraries(sv_${tool} PRIVATE svcore)
endforeach()
