cmake_minimum_required(VERSION 3.20)
project(tinyapps C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
# All app binaries land in build/bin; the ~/.local/bin launchers point there.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

find_package(SDL3 REQUIRED CONFIG)
find_package(SDL3_ttf REQUIRED CONFIG)

enable_testing()

add_subdirectory(common)
add_subdirectory(tinypainter)
add_subdirectory(tempo)
add_subdirectory(musicnotes)

# `cmake --build build --target links` (re)creates the launchers in ~/.local/bin.
set(LINK_DIR $ENV{HOME}/.local/bin)
add_custom_target(links
  COMMAND ${CMAKE_COMMAND} -E make_directory ${LINK_DIR}
  COMMAND ln -sfn $<TARGET_FILE:tinypainter> ${LINK_DIR}/tpt
  COMMAND ln -sfn $<TARGET_FILE:tempo> ${LINK_DIR}/tempo
  COMMAND ln -sfn $<TARGET_FILE:musicnotes> ${LINK_DIR}/nts
  DEPENDS tinypainter tempo musicnotes
  COMMENT "Linking tpt, tempo, nts into ${LINK_DIR}"
  VERBATIM)
