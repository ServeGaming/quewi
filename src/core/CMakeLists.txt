add_library(quewi_core STATIC
    Workspace.h
    Workspace.cpp
    CueList.h
    CueList.cpp
    CueListModel.h
    CueListModel.cpp
    UndoCommands.h
    UndoCommands.cpp
    ../cues/Cue.h
    ../cues/Cue.cpp
    ../cues/MemoCue.h
    ../cues/MemoCue.cpp
    ../cues/FadeCue.h
    ../cues/FadeCue.cpp
    ../cues/WaitCue.h
    ../cues/WaitCue.cpp
    ../cues/TargetingCue.h
    ../cues/TargetingCue.cpp
    ../cues/GroupCue.h
    ../cues/GroupCue.cpp
)
target_include_directories(quewi_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(quewi_core PUBLIC Qt6::Core Qt6::Gui)
# Qt6::Gui needed for QUndoCommand (moved from Qt5::Widgets)
