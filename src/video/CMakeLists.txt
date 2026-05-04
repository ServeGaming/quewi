add_library(quewi_video STATIC
    VideoEngine.h
    VideoEngine.cpp
)
target_include_directories(quewi_video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_video PUBLIC Qt6::Core Qt6::Gui quewi_core)
# FFmpeg + Qt RHI hooked up in Phase 5
