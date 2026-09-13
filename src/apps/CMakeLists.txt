# Bundled GEM applications. Independent SDK examples remain under samples/.
add_subdirectory(desktop)
add_subdirectory(terminal)
add_subdirectory(clock)
add_subdirectory(calculator)

# Clock shares the artwork loader with the independent visual samples.
foreach(target clock clock_gemd)
    target_link_libraries(${target} PRIVATE sample_resources)
endforeach()
