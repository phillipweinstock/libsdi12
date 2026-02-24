cmake_minimum_required(VERSION 3.12)
project(libsdi12 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Library sources
set(SDI12_SOURCES
    sdi12_crc.c
    sdi12_sensor.c
    sdi12_master.c
)

set(SDI12_HEADERS
    sdi12.h
    sdi12_sensor.h
    sdi12_master.h
)

# Static library
add_library(sdi12 STATIC ${SDI12_SOURCES})
target_include_directories(sdi12 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Install
install(TARGETS sdi12
    ARCHIVE DESTINATION lib
)
install(FILES ${SDI12_HEADERS}
    DESTINATION include/sdi12
)

# Option to build tests (standalone only)
option(SDI12_BUILD_TESTS "Build libsdi12 unit tests" OFF)
if(SDI12_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
