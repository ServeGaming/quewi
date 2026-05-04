add_library(quewi_core STATIC
    Workspace.h
    Workspace.cpp
)
target_include_directories(quewi_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_core PUBLIC Qt6::Core)
