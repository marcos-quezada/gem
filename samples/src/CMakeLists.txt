add_subdirectory(gemscape)
add_subdirectory(maestro)
add_subdirectory(msa)
add_subdirectory(stout)

foreach(target gemscape gemscape_hosted maestro maestro_hosted)
    target_link_libraries(${target} PRIVATE sample_resources)
endforeach()
