qt_add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE Qt6::Test quewi_core)
add_test(NAME smoke COMMAND test_smoke)

qt_add_executable(test_osc_codec test_osc_codec.cpp)
target_link_libraries(test_osc_codec PRIVATE Qt6::Test Qt6::Gui quewi_osc)
add_test(NAME osc_codec COMMAND test_osc_codec)

qt_add_executable(test_osc_pattern test_osc_pattern.cpp)
target_link_libraries(test_osc_pattern PRIVATE Qt6::Test quewi_osc)
add_test(NAME osc_pattern COMMAND test_osc_pattern)

qt_add_executable(test_osc_slip test_osc_slip.cpp)
target_link_libraries(test_osc_slip PRIVATE Qt6::Test quewi_osc)
add_test(NAME osc_slip COMMAND test_osc_slip)

qt_add_executable(test_osc_loopback test_osc_loopback.cpp)
target_link_libraries(test_osc_loopback PRIVATE Qt6::Test Qt6::Network quewi_osc)
add_test(NAME osc_loopback COMMAND test_osc_loopback)

qt_add_executable(test_audio_smoke test_audio_smoke.cpp)
target_link_libraries(test_audio_smoke PRIVATE Qt6::Test Qt6::Multimedia quewi_audio)
add_test(NAME audio_smoke COMMAND test_audio_smoke)

qt_add_executable(test_vbap test_vbap.cpp)
target_link_libraries(test_vbap PRIVATE Qt6::Test quewi_audio)
add_test(NAME vbap COMMAND test_vbap)

# On Windows, Qt's DLLs need to be reachable via PATH at test runtime.
if(WIN32)
    foreach(name smoke osc_codec osc_pattern osc_slip osc_loopback audio_smoke vbap)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endforeach()
endif()
