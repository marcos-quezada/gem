add_executable(test_gem_trash
    test_gem_trash.c
    ${PROJECT_SOURCE_DIR}/src/apps/desktop/gem_trash.c)

target_include_directories(test_gem_trash PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/src/apps/desktop)
target_link_libraries(test_gem_trash PRIVATE platform_linux)
set_target_properties(test_gem_trash PROPERTIES BUILD_WITH_INSTALL_RPATH FALSE
    BUILD_RPATH "${GEM_LIBRARY_DIR}")
target_compile_options(test_gem_trash PRIVATE -Wall -Wextra -pedantic -UNDEBUG
    $<$<CONFIG:Debug>:-g;-fsanitize=address,undefined>)
target_link_options(test_gem_trash PRIVATE
    $<$<CONFIG:Debug>:-fsanitize=address,undefined>)

add_test(NAME test_gem_trash COMMAND test_gem_trash)
set_tests_properties(test_gem_trash PROPERTIES LABELS unit
    ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
