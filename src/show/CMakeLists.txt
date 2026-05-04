add_library(quewi_show STATIC
    ShowFile.h
    ShowFile.cpp
)
target_include_directories(quewi_show PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_show PUBLIC Qt6::Core Qt6::Sql quewi_core quewi_osc quewi_audio quewi_lighting quewi_video)
