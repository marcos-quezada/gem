add_executable(test_terminal_vt100
    test_terminal_vt100.c
    ${PROJECT_SOURCE_DIR}/src/apps/terminal/screen.c
    ${PROJECT_SOURCE_DIR}/src/apps/terminal/vt100.c)

target_include_directories(test_terminal_vt100 PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/apps/terminal)
target_link_libraries(test_terminal_vt100 PRIVATE platform_linux)
set_target_properties(test_terminal_vt100 PROPERTIES
    BUILD_WITH_INSTALL_RPATH FALSE BUILD_RPATH "${GEM_LIBRARY_DIR}")
target_compile_options(test_terminal_vt100 PRIVATE -Wall -Wextra -pedantic
    -UNDEBUG $<$<CONFIG:Debug>:-g;-fsanitize=address,undefined>)
target_link_options(test_terminal_vt100 PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>)

add_test(NAME test_terminal_vt100 COMMAND test_terminal_vt100)
set_tests_properties(test_terminal_vt100 PROPERTIES LABELS unit
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
