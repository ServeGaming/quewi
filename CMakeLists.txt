cmake_minimum_required(VERSION 3.24)
project(quewi
    VERSION 0.1.0
    DESCRIPTION "Theatre cueing software"
    HOMEPAGE_URL "https://github.com/ServeGaming/quewi"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Warnings
if(MSVC)
    add_compile_options(/W4 /permissive- /Zc:__cplusplus)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Qt
find_package(Qt6 6.11 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Sql
    Multimedia
    MultimediaWidgets
    WebSockets
    Test
)

qt_standard_project_setup()

# RtMidi — single-file cross-platform MIDI device I/O. Header + impl, MIT.
include(FetchContent)
set(RTMIDI_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(RTMIDI_BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF)
FetchContent_Declare(rtmidi
    GIT_REPOSITORY https://github.com/thestk/rtmidi.git
    GIT_TAG        6.0.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(rtmidi)

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/audio)
add_subdirectory(src/video)
add_subdirectory(src/lighting)
add_subdirectory(src/osc)
add_subdirectory(src/midi)
add_subdirectory(src/show)
add_subdirectory(src/ui)
add_subdirectory(src/app)

add_subdirectory(tests)

# ── CPack — Windows MSI via WiX ───────────────────────────────────────
# Run `cpack -G WIX` from build/windows-release after installing to
# produce quewi-<version>-win64.msi. The WiX upgrade GUID is fixed for
# the lifetime of the product so successive installers replace the
# previous version cleanly; bumping it would orphan existing installs.
if(WIN32)
    set(CPACK_GENERATOR "WIX")
    set(CPACK_PACKAGE_NAME              "quewi")
    set(CPACK_PACKAGE_VENDOR            "ServeGaming")
    set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_HOMEPAGE_URL      "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "quewi")
    set(CPACK_PACKAGE_FILE_NAME "quewi-${PROJECT_VERSION}-win64")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

    # Stable Upgrade GUID — must NEVER change once a release is shipped,
    # or upgrades won't replace the prior installation. Generated once.
    set(CPACK_WIX_UPGRADE_GUID "B1F2C3D4-9A0E-4F8B-9A1C-7E3F6A5C2B11")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "quewi")
    set(CPACK_PACKAGE_EXECUTABLES "quewi" "quewi")
    set(CPACK_CREATE_DESKTOP_LINKS "quewi")

    include(CPack)
endif()
