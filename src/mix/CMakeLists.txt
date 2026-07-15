add_library(quewi_mix STATIC
    X32Value.h
    X32Value.cpp
)
target_include_directories(quewi_mix PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_mix PUBLIC
    Qt6::Core
)
