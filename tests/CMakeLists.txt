# Helper: declare a Qt Test executable, register it with CTest, and
# accumulate its name so the Windows PATH fix-up below covers it
# automatically. Previously the foreach list of names was maintained
# by hand and a new test silently couldn't find Qt's DLLs on Windows
# if you forgot to append to it.
set(_quewi_test_names "")
function(quewi_add_test name)
    set(_src "test_${name}.cpp")
    qt_add_executable(test_${name} ${_src})
    target_link_libraries(test_${name} PRIVATE ${ARGN})
    add_test(NAME ${name} COMMAND test_${name})
    list(APPEND _quewi_test_names ${name})
    set(_quewi_test_names "${_quewi_test_names}" PARENT_SCOPE)
endfunction()

quewi_add_test(smoke        Qt6::Test quewi_core)
quewi_add_test(fade         Qt6::Test quewi_audio)
quewi_add_test(osc_codec    Qt6::Test Qt6::Gui quewi_osc)
quewi_add_test(osc_pattern  Qt6::Test quewi_osc)
quewi_add_test(osc_slip     Qt6::Test quewi_osc)
quewi_add_test(osc_loopback Qt6::Test Qt6::Network quewi_osc)
quewi_add_test(audio_smoke  Qt6::Test Qt6::Multimedia quewi_audio)
quewi_add_test(vbap         Qt6::Test quewi_audio)
quewi_add_test(trajectory   Qt6::Test quewi_audio)
quewi_add_test(artnet       Qt6::Test Qt6::Network quewi_lighting)
quewi_add_test(x32_value    Qt6::Test quewi_mix)
quewi_add_test(console_link Qt6::Test quewi_mix)
quewi_add_test(x32_link     Qt6::Test Qt6::Network quewi_mix)
quewi_add_test(mix_show     Qt6::Test quewi_mix)
quewi_add_test(mix_persistence Qt6::Test Qt6::Sql quewi_mix quewi_show)
quewi_add_test(dm7_value    Qt6::Test quewi_mix)
quewi_add_test(dm7_link     Qt6::Test Qt6::Network quewi_mix)
# Integration test against a real X32 emulator on 127.0.0.1:10023. Skips
# cleanly when nothing is listening (i.e. always, in CI), so it's safe to keep
# in the default set. See docs/dev/session-handoff.md for the emulator.
quewi_add_test(x32_emulator Qt6::Test Qt6::Network quewi_mix)
# MixGridModel is a plain QAbstractTableModel over the mix show, so it's
# compiled in directly rather than linked against quewi_ui. quewi_ui pulls in
# GoEngine, which lives in the app target — the app links both so it resolves
# there, but a test that only wants the grid model shouldn't have to drag a
# cue engine along behind it.
quewi_add_test(mix_grid     Qt6::Test Qt6::Widgets quewi_mix)
target_sources(test_mix_grid PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/MixGridModel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/ui/Theme.cpp
)

# On Windows, Qt's DLLs need to be reachable via PATH at test runtime.
if(WIN32)
    foreach(name ${_quewi_test_names})
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endforeach()
endif()
