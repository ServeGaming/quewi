add_library(quewi_osc STATIC
    OscEngine.h
    OscEngine.cpp
)
target_include_directories(quewi_osc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_osc PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
    quewi_core
)
