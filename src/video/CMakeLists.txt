add_library(quewi_video STATIC
    VideoEngine.h
    VideoEngine.cpp
    VideoOutputWindow.h
    VideoOutputWindow.cpp
    VideoCue.h
    VideoCue.cpp
)
target_include_directories(quewi_video PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_video PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    quewi_core
)
