cmake_minimum_required(VERSION 3.16)
project(openftds VERSION 0.2.1 LANGUAGES C CXX)

include(GNUInstallDirs)
include(CTest)
find_package(Threads REQUIRED)

option(OPENFTDS_BUILD_EXAMPLES "Build examples" ON)
option(OPENFTDS_BUILD_TESTS "Build tests" ${BUILD_TESTING})
option(OPENFTDS_BUILD_FTDS_COMPAT_LIBRARY "Build libftdsclient compatibility name" ON)

add_library(openftds src/openftds.c)
add_library(openftds::openftds ALIAS openftds)
function(openftds_configure_target target)
    target_compile_features(${target} PUBLIC c_std_11)
    target_include_directories(${target} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${target} PUBLIC Threads::Threads)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    endif()
    set_target_properties(${target} PROPERTIES
        C_STANDARD 11
        C_STANDARD_REQUIRED YES
        C_EXTENSIONS NO
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        POSITION_INDEPENDENT_CODE ON
    )
endfunction()
openftds_configure_target(openftds)

if(OPENFTDS_BUILD_FTDS_COMPAT_LIBRARY)
    add_library(ftdsclient src/openftds.c)
    add_library(openftds::ftdsclient ALIAS ftdsclient)
    openftds_configure_target(ftdsclient)
endif()

if(OPENFTDS_BUILD_EXAMPLES)
    add_executable(openftds-example examples/basic.c)
    target_link_libraries(openftds-example PRIVATE openftds::openftds)
    add_executable(openftds-ftds-compatible examples/ftds_compatible.c)
    if(TARGET ftdsclient)
        target_link_libraries(openftds-ftds-compatible PRIVATE openftds::ftdsclient)
    else()
        target_link_libraries(openftds-ftds-compatible PRIVATE openftds::openftds)
    endif()
endif()

if(OPENFTDS_BUILD_TESTS)
    add_executable(openftds-test tests/test_openftds.c)
    target_link_libraries(openftds-test PRIVATE openftds::openftds)
    add_test(NAME openftds-core COMMAND openftds-test)

    add_executable(openftds-cpp20-test tests/test_cpp.cpp)
    target_compile_features(openftds-cpp20-test PRIVATE cxx_std_20)
    target_link_libraries(openftds-cpp20-test PRIVATE openftds::openftds)
    add_test(NAME openftds-cpp20 COMMAND openftds-cpp20-test)

    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME openftds-report
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_report.py
                    ${CMAKE_CURRENT_SOURCE_DIR}/tools/openftds-report)
        add_test(NAME openftds-yrcache-build
            COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_yrcache_build.py)
    endif()
endif()

set(OPENFTDS_INSTALL_TARGETS openftds)
if(TARGET ftdsclient)
    list(APPEND OPENFTDS_INSTALL_TARGETS ftdsclient)
endif()
install(TARGETS ${OPENFTDS_INSTALL_TARGETS}
    EXPORT openftdsTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(PROGRAMS tools/openftds-report DESTINATION ${CMAKE_INSTALL_BINDIR})
install(EXPORT openftdsTargets
    FILE openftdsTargets.cmake
    NAMESPACE openftds::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/openftds
)
