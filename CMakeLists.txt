cmake_minimum_required(VERSION 3.14)
project(libsdi12
    VERSION 0.3.0
    DESCRIPTION "Portable SDI-12 v1.4 protocol library — sensor and master"
    HOMEPAGE_URL "https://github.com/phillipweinstock/libsdi12"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

include(GNUInstallDirs)

# ── Options ──────────────────────────────────────────────────────────────
option(SDI12_BUILD_TESTS  "Build libsdi12 unit tests"   OFF)
option(SDI12_BUILD_SHARED "Build shared library"         ON)
option(SDI12_BUILD_STATIC "Build static library"         ON)

# ── Sources & headers ────────────────────────────────────────────────────
set(SDI12_SOURCES
    sdi12_crc.c
    sdi12_sensor.c
    sdi12_master.c
)

set(SDI12_PUBLIC_HEADERS
    libsdi12.h
    sdi12.h
    sdi12_easy.h
    sdi12_sensor.h
    sdi12_master.h
)

# ── Shared library ──────────────────────────────────────────────────────
if(SDI12_BUILD_SHARED)
    add_library(sdi12_shared SHARED ${SDI12_SOURCES})
    set_target_properties(sdi12_shared PROPERTIES
        OUTPUT_NAME   sdi12
        VERSION       ${PROJECT_VERSION}
        SOVERSION     ${PROJECT_VERSION_MAJOR}
        PUBLIC_HEADER "${SDI12_PUBLIC_HEADERS}"
    )
    target_include_directories(sdi12_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/sdi12>
    )
    install(TARGETS sdi12_shared
        LIBRARY       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sdi12
    )
endif()

# ── Static library ──────────────────────────────────────────────────────
if(SDI12_BUILD_STATIC)
    add_library(sdi12_static STATIC ${SDI12_SOURCES})
    set_target_properties(sdi12_static PROPERTIES
        OUTPUT_NAME   sdi12
        PUBLIC_HEADER "${SDI12_PUBLIC_HEADERS}"
    )
    target_include_directories(sdi12_static PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/sdi12>
    )
    install(TARGETS sdi12_static
        ARCHIVE       DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/sdi12
    )
endif()

# Convenience alias (prefer shared when available)
if(SDI12_BUILD_SHARED)
    add_library(sdi12 ALIAS sdi12_shared)
elseif(SDI12_BUILD_STATIC)
    add_library(sdi12 ALIAS sdi12_static)
endif()

# ── pkg-config ──────────────────────────────────────────────────────────
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/sdi12.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/sdi12.pc
    @ONLY
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/sdi12.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)

# ── Tests ───────────────────────────────────────────────────────────────
if(SDI12_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
