# Pure logic (no SDL) so it can be unit tested.
add_library(speakcore STATIC src/speak.c)
target_include_directories(speakcore PUBLIC src)
target_compile_options(speakcore PUBLIC -Wall -Wextra)

# The worker runs under the Python that has mlx-audio. Resolve the real
# interpreter now (pyenv shims are shell scripts); SPEAK_PYTHON overrides at runtime.
if(NOT SPEAK_PYTHON)
  find_program(SPEAK_PYTHON_SHIM python3)
  execute_process(COMMAND ${SPEAK_PYTHON_SHIM} -c "import sys; print(sys.executable)"
                  OUTPUT_VARIABLE _py OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(SPEAK_PYTHON "${_py}" CACHE FILEPATH "Python interpreter with mlx-audio installed")
endif()

add_executable(speak src/main.c)
target_link_libraries(speak PRIVATE speakcore tiny)
target_compile_definitions(speak PRIVATE
  SPEAK_PYTHON_DEFAULT="${SPEAK_PYTHON}"
  SPEAK_WORKER="${CMAKE_CURRENT_SOURCE_DIR}/worker/speak_worker.py")

add_executable(speak_tests tests/speak_test.c)
target_link_libraries(speak_tests PRIVATE speakcore tiny)
add_test(NAME speak_tests COMMAND speak_tests)
