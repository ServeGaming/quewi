add_library(quewi_audio STATIC
    AudioFile.h
    AudioFile.cpp
    AudioEngine.h
    AudioEngine.cpp
    AudioCue.h
    AudioCue.cpp
)
target_include_directories(quewi_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_audio PUBLIC Qt6::Core Qt6::Multimedia quewi_core)
