add_library(quewi_lighting STATIC
    Sacn.h
    Sacn.cpp
    LightingEngine.h
    LightingEngine.cpp
    LightCue.h
    LightCue.cpp
)
target_include_directories(quewi_lighting PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_lighting PUBLIC Qt6::Core Qt6::Network quewi_core)
