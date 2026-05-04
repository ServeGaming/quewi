add_library(quewi_cues STATIC
    Cue.h
    Cue.cpp
)
target_include_directories(quewi_cues PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_cues PUBLIC Qt6::Core quewi_core)
