add_executable(gemd
    main.c
    session.c
    dispatch_vdi.c
    dispatch_aes.c
    transport.c
    drawing.c
    aes_dispatch.c
    ../gem/aes_validate.c
    ../gem/tree_decode.c
    vdi_dispatch.c
    vdi_bitmap.c
    ../gem/bitmap_size.c
    ../gem/vdi_validate.c
    ../gem/protocol.c
)

target_include_directories(gemd
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
        ${PROJECT_SOURCE_DIR}/src/aes
        ${PROJECT_SOURCE_DIR}/src/gem
)

target_link_libraries(gemd PRIVATE aes)

if(GEM_PLATFORM STREQUAL "freebsd")
    # gemd is the sole libseat client (gem-freebsd-libseat design.md) --
    # needs the same compile define vdi already uses, so main.c/session.c
    # know to call gem_freebsd_seat_init/shutdown/fd/dispatch.
    target_compile_definitions(gemd PRIVATE GEM_PLATFORM_FREEBSD=1)
endif()

set_target_properties(gemd PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${GEM_CORE_DIR}
)

target_compile_features(gemd PRIVATE c_std_11)

target_compile_options(gemd
    PRIVATE
        -Wall
        -Wextra
        -pedantic
)

target_compile_options(gemd
    PRIVATE
        $<$<CONFIG:Debug>:-g>
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)

target_link_options(gemd
    PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address,undefined>
)
