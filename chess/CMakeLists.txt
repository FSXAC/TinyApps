# Pure logic (no SDL) so it can be unit tested: rules, bots and analysis.
# Bots live in src/bots, one file each, plus the shared API and roster in bot.c.
add_library(chesscore STATIC src/board.c src/book.c src/analysis.c
    src/trainer.c
    src/bots/bot.c src/bots/student.c src/bots/tiny.c src/bots/greedy.c src/bots/mirror.c src/bots/queenie.c)
target_include_directories(chesscore PUBLIC src src/bots)
target_compile_options(chesscore PUBLIC -Wall -Wextra)

# The eval bar widget (SDL), kept apart from the app so it can be built and checked on its own.
add_library(chessevalbar STATIC src/evalbar.c)
target_link_libraries(chessevalbar PUBLIC chesscore tiny)

add_executable(chess src/main.c src/pieces.c)
target_link_libraries(chess PRIVATE chessevalbar)

add_executable(chess_tests tests/chess_test.c)
target_link_libraries(chess_tests PRIVATE chesscore tiny)
add_test(NAME chess_tests COMMAND chess_tests)

add_executable(bot_tests tests/bot_test.c)
target_link_libraries(bot_tests PRIVATE chesscore tiny)
add_test(NAME bot_tests COMMAND bot_tests)

add_executable(analysis_tests tests/analysis_test.c)
target_link_libraries(analysis_tests PRIVATE chesscore tiny)
add_test(NAME analysis_tests COMMAND analysis_tests)

# Draws eval bars for a few scores; run with TINY_SCREENSHOT=out.bmp to check visuals.
add_executable(evalbar_demo tests/evalbar_demo.c)
target_link_libraries(evalbar_demo PRIVATE chessevalbar)
