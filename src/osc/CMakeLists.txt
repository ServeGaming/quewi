add_library(quewi_osc STATIC
    OscMessage.h
    OscCodec.h
    OscCodec.cpp
    OscPattern.h
    OscPattern.cpp
    OscEngine.h
    OscEngine.cpp
    OscCue.h
    OscCue.cpp
)
target_include_directories(quewi_osc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_osc PUBLIC
    Qt6::Core
    Qt6::Gui          # QColor in OscMessage
    Qt6::Network
    Qt6::WebSockets
    quewi_core
)
