# test/CMakeLists.txt — build & register the test suite with CTest

set(TEST_SOURCES
    test_main.c
    test_crc.c
    test_address.c
    test_sensor.c
    test_master.c
    test_metamorphic.c
)

add_executable(test_sdi12 ${TEST_SOURCES})

# Link against whichever library variant is available
if(TARGET sdi12_static)
    target_link_libraries(test_sdi12 PRIVATE sdi12_static m)
elseif(TARGET sdi12_shared)
    target_link_libraries(test_sdi12 PRIVATE sdi12_shared m)
endif()

add_test(NAME sdi12_tests COMMAND test_sdi12)
