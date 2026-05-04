qt_add_executable(quewi
    main.cpp
    MainWindow.cpp
    MainWindow.h
)

set_target_properties(quewi PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

target_link_libraries(quewi PRIVATE
    Qt6::Widgets
    Qt6::Network
    quewi_core
    quewi_show
    quewi_ui
    quewi_audio
    quewi_video
    quewi_lighting
    quewi_osc
    quewi_midi
)
