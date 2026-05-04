add_library(quewi_audio STATIC
    AudioEngine.h
    AudioEngine.cpp
)
target_include_directories(quewi_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_audio PUBLIC Qt6::Core Qt6::Multimedia quewi_core)
