add_library(quewi_midi STATIC
    MidiEngine.h
    MidiEngine.cpp
    MidiInputEngine.h
    MidiInputEngine.cpp
    MidiCue.h
    MidiCue.cpp
)
target_include_directories(quewi_midi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_midi PUBLIC Qt6::Core quewi_core rtmidi)
target_include_directories(quewi_midi PRIVATE ${rtmidi_SOURCE_DIR})

# On Linux RtMidi only talks to MIDI devices through ALSA, and quietly builds a
# do-nothing "dummy" API when the ALSA headers are missing — which is how the
# 1.1.0 AppImage shipped with MIDI that never saw a device. Fail the build
# instead. (apt: libasound2-dev; dnf: alsa-lib-devel.)
if(UNIX AND NOT APPLE)
    find_package(ALSA)
    if(NOT ALSA_FOUND)
        message(FATAL_ERROR
            "ALSA development headers not found: MIDI wouldn't work on Linux. "
            "Install libasound2-dev (Debian/Ubuntu) or alsa-lib-devel (Fedora).")
    endif()
endif()
