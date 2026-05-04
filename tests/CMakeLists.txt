qt_add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE Qt6::Test quewi_core)
add_test(NAME smoke COMMAND test_smoke)

# On Windows, Qt's DLLs need to be reachable via PATH at test runtime.
# Qt6's CMake exports the bin dir of the Qt install on the imported targets.
if(WIN32)
    set_tests_properties(smoke PROPERTIES
        ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()
