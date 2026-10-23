# Build OpenFTDS with the host C compiler, never nvcc.
get_filename_component(OPENFTDS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)

add_library(yrcache_openftds STATIC "${OPENFTDS_ROOT}/src/openftds.c")
set_target_properties(yrcache_openftds PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED YES
    POSITION_INDEPENDENT_CODE YES
)
target_include_directories(yrcache_openftds PUBLIC "${OPENFTDS_ROOT}/include")
target_link_libraries(yrcache_openftds PUBLIC Threads::Threads)

# After defining c_ops:
# target_link_libraries(c_ops PRIVATE yrcache_openftds)
