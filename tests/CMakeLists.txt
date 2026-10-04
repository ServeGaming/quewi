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
# Reading an Eos desk's state back over TCP 3032 (a fake desk on localhost).
quewi_add_test(eos_feedback Qt6::Test Qt6::Network quewi_osc)
quewi_add_test(audio_smoke  Qt6::Test Qt6::Multimedia quewi_audio)
# The real-time mixer rendered offline (audio::OfflineRenderer): fades,
# loops, stops and pause checked sample by sample.
quewi_add_test(mixer        Qt6::Test Qt6::Multimedia quewi_audio)
# Video cue soundtracks and video <-> audio cue conversion.
quewi_add_test(video_sound  Qt6::Test Qt6::Multimedia quewi_video quewi_audio)
# The effects rack: every effect type, the presets, and effect-edit saving.
quewi_add_test(effects      Qt6::Test Qt6::Multimedia quewi_audio)
# Lighting triggers: model, saving, and when a playing song sends each one.
quewi_add_test(light_triggers Qt6::Test Qt6::Multimedia quewi_audio)
# Manual RAM benchmark for long files; skips unless QUEWI_MEM_PROBE_FILE is set.
quewi_add_test(long_file_memory Qt6::Test Qt6::Multimedia quewi_audio)
if(WIN32)
    target_link_libraries(test_long_file_memory PRIVATE psapi)
endif()
quewi_add_test(vbap         Qt6::Test quewi_audio)
quewi_add_test(trajectory   Qt6::Test quewi_audio)
quewi_add_test(artnet       Qt6::Test Qt6::Network quewi_lighting)
quewi_add_test(x32_value    Qt6::Test quewi_mix)
quewi_add_test(console_link Qt6::Test quewi_mix)
quewi_add_test(x32_link     Qt6::Test Qt6::Network quewi_mix)
quewi_add_test(mix_show     Qt6::Test quewi_mix)
quewi_add_test(mix_persistence Qt6::Test Qt6::Sql quewi_mix quewi_show)
# Crash-journal locks: a running instance's journal is never recovered or
# deleted by another; a crashed one's is.
quewi_add_test(journal_lock Qt6::Test quewi_show)
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

# The soundboard view. CartView reads the GoEngine (for the playing glow),
# and GoEngine lives in the app target, so compile it in — same trick as the
# mix grid above.
quewi_add_test(cart_view    Qt6::Test Qt6::Widgets quewi_ui)
target_sources(test_cart_view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/GoEngine.cpp
)

# The audio editor's track/effect lifetimes (audit A7). Same GoEngine trick.
quewi_add_test(audio_editor Qt6::Test Qt6::Widgets quewi_ui)
target_sources(test_audio_editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/GoEngine.cpp
)

# The lighting-trigger UI: the editor's Lighting tab, the timeline's marker
# lane, and the Inspector's way in. Same GoEngine trick.
quewi_add_test(light_triggers_ui Qt6::Test Qt6::Widgets quewi_ui)
target_sources(test_light_triggers_ui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/GoEngine.cpp
)

# The video editor: In/Out + picture fades on the cue, the timeline's
# handles, the editor window, and a playing picture honouring them (the last
# few need QUEWI_TEST_VIDEO_FILE; they skip in CI). Same GoEngine trick.
quewi_add_test(video_editor Qt6::Test Qt6::Widgets quewi_ui)
target_sources(test_video_editor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/GoEngine.cpp
)

# The Lighting Desk setup dialog (Tools → Lighting Desk…). Same GoEngine trick.
quewi_add_test(lighting_desk_dialog Qt6::Test Qt6::Widgets quewi_ui)
target_sources(test_lighting_desk_dialog PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/GoEngine.cpp
)

# The stage-manager screen (Show Mode) and the dockable Lighting panel, fed
# ShowSnapshots directly. Same GoEngine trick.
quewi_add_test(show_mode_view Qt6::Test Qt6::Widgets quewi_ui)
target_sources(test_show_mode_view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/app/GoEngine.cpp
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
