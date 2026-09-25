add_executable(tinypainter src/main.c)
target_compile_options(tinypainter PRIVATE -Wall -Wextra)
target_link_libraries(tinypainter PRIVATE tiny)
