set(QUEWI_APP_SOURCES
    main.cpp
    MainWindow.cpp
    MainWindow.h
    GoEngine.cpp
    GoEngine.h
    UpdateChecker.cpp
    UpdateChecker.h
    ${CMAKE_SOURCE_DIR}/resources/resources.qrc
)

# On Windows, compile the .rc so the EXE carries an icon in Explorer.
# AUTORCC handles the .qrc; the .rc is picked up by CMake's RC language.
if(WIN32)
    list(APPEND QUEWI_APP_SOURCES
        ${CMAKE_SOURCE_DIR}/resources/icons/quewi.rc)
endif()

qt_add_executable(quewi ${QUEWI_APP_SOURCES})

set_target_properties(quewi PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
)

target_compile_definitions(quewi PRIVATE
    QUEWI_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(quewi PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    quewi_core
    quewi_show
    quewi_ui
    quewi_audio
    quewi_video
    quewi_lighting
    quewi_osc
    quewi_midi
)

# ── Install + windeployqt + CPack/WiX ────────────────────────────────
# Drives `cmake --install` and `cpack -G WIX`. The install step copies
# quewi.exe and runs windeployqt to bundle every Qt DLL, plugin, and
# translation the build links against. CPack then wraps the staged
# directory into an MSI on Windows.
include(GNUInstallDirs)

install(TARGETS quewi
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE  DESTINATION .
)

if(WIN32)
    # Locate windeployqt next to the Qt6 target binaries.
    get_target_property(_qmake_exec Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exec}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt
        HINTS "${_qt_bin_dir}"
        REQUIRED
    )
    # Run windeployqt against the *installed* exe so its dependencies
    # land alongside it under bin/.
    install(CODE "
        message(STATUS \"Running windeployqt on \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/quewi.exe\")
        execute_process(
            COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --release
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                --compiler-runtime
                \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/quewi.exe\"
            COMMAND_ERROR_IS_FATAL ANY
        )
    ")
endif()
