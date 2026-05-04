add_library(quewi_ui STATIC
    Theme.h
    Theme.cpp
    CueListView.h
    CueListView.cpp
    Inspector.h
    Inspector.cpp
    TransportBar.h
    TransportBar.cpp
    PreferencesDialog.h
    PreferencesDialog.cpp
)
target_include_directories(quewi_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_ui PUBLIC Qt6::Widgets quewi_core)
