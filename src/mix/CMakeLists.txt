add_library(quewi_mix STATIC
    X32Value.h
    X32Value.cpp
    ConsoleLink.h
    ConsoleLink.cpp
    X32Link.h
    X32Link.cpp
    Dm7Value.h
    Dm7Value.cpp
    MixCue.h
    MixCue.cpp
)
target_include_directories(quewi_mix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_mix PUBLIC
    Qt6::Core
    Qt6::Network
    quewi_osc      # OscCodec/OscMessage only — NOT OscEngine, see the spec
    quewi_core     # cues::Cue base for MixCue; also compiles MixShow
)
