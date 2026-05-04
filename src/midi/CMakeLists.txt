add_library(quewi_midi STATIC
    MidiEngine.h
    MidiEngine.cpp
)
target_include_directories(quewi_midi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_midi PUBLIC Qt6::Core quewi_core)
# RtMidi vendored in third_party/rtmidi/ (added in Phase 6)
