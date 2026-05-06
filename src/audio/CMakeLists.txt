add_library(quewi_audio STATIC
    AudioFile.h
    AudioFile.cpp
    AudioEngine.h
    AudioEngine.cpp
    AudioCue.h
    AudioCue.cpp
    Vbap.h
    Vbap.cpp
    SpeakerPatch.h
    SpeakerPatch.cpp
    AudioEffect.h
    AudioEffect.cpp
    AudioEditorModel.h
    AudioEditorModel.cpp
    AudioEditorRenderer.h
    AudioEditorRenderer.cpp
    effects/EqEffect.h
    effects/EqEffect.cpp
    effects/CompressorEffect.h
    effects/CompressorEffect.cpp
    effects/ReverbEffect.h
    effects/ReverbEffect.cpp
    effects/DelayEffect.h
    effects/DelayEffect.cpp
)
target_include_directories(quewi_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_audio PUBLIC Qt6::Core Qt6::Multimedia quewi_core)
