add_library(quewi_video STATIC
    VideoEngine.h
    VideoEngine.cpp
    VideoCue.h
    VideoCue.cpp
    Compositor.h
    Compositor.cpp
    CompositorWindow.h
    CompositorWindow.cpp
    Layer.h
    VideoLayer.h
    VideoLayer.cpp
    ImageLayer.h
    ImageLayer.cpp
    TextLayer.h
    TextLayer.cpp
    TestPatternLayer.h
    TestPatternLayer.cpp
)
target_include_directories(quewi_video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_video PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    quewi_core
)
