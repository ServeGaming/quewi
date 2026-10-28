cmake_minimum_required(VERSION 3.24)
project(quewi
    VERSION 1.3.0
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
# Minimum is 6.8 — CI pins 6.8.3; local dev tracks the latest stable
# (6.11+ at time of writing). 6.8 is a hard floor: MediaImportDialog uses
# QAudioBufferOutput (new in 6.8), and Qt 6.8 itself needs macOS 12+
# (see CMAKE_OSX_DEPLOYMENT_TARGET in CMakePresets.json).
find_package(Qt6 6.8 REQUIRED COMPONENTS
    Core
    Concurrent
    Gui
    Widgets
    Network
    Sql
    Multimedia
    MultimediaWidgets
    WebSockets
    Pdf
    PdfWidgets
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

# Monocypher — small public-domain (CC0 / BSD-2) crypto library; quewi uses
# its Ed25519 signature check to verify trial licenses offline.
FetchContent_Declare(monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        4.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(monocypher)
enable_language(C)                       # Monocypher is C
add_library(monocypher STATIC
    ${monocypher_SOURCE_DIR}/src/monocypher.c
    ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
target_include_directories(monocypher PUBLIC
    ${monocypher_SOURCE_DIR}/src ${monocypher_SOURCE_DIR}/src/optional)
set_target_properties(monocypher PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Trial licensing (docs/dev/licensing.md). On only for the builds that go to
# theatres (the release workflow), with the license server's public key.
option(QUEWI_LICENSING "Require a trial key to start (release builds)" OFF)
set(QUEWI_LICENSE_PUBKEY "" CACHE STRING "License server's Ed25519 public key (64 hex chars)")
# Not given: the one in resources/license-pubkey.txt (public by nature; the
# private half lives only in the license server's secrets).
if(QUEWI_LICENSING AND QUEWI_LICENSE_PUBKEY STREQUAL "" AND EXISTS ${CMAKE_SOURCE_DIR}/resources/license-pubkey.txt)
    file(STRINGS ${CMAKE_SOURCE_DIR}/resources/license-pubkey.txt _quewi_pub LIMIT_COUNT 1 REGEX "^[0-9a-fA-F]+$")
    set(QUEWI_LICENSE_PUBKEY "${_quewi_pub}")
endif()
if(QUEWI_LICENSING AND NOT QUEWI_LICENSE_PUBKEY MATCHES "^[0-9a-fA-F]+$")
    message(FATAL_ERROR "QUEWI_LICENSING needs QUEWI_LICENSE_PUBKEY (64 hex characters)")
endif()

enable_testing()

add_subdirectory(src/core)
add_subdirectory(src/audio)
add_subdirectory(src/video)
add_subdirectory(src/lighting)
add_subdirectory(src/osc)
add_subdirectory(src/midi)
add_subdirectory(src/mix)
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
    # WiX rejects extensionless license files; stage a .txt copy.
    configure_file("${CMAKE_SOURCE_DIR}/LICENSE"
                   "${CMAKE_BINARY_DIR}/LICENSE.txt"
                   COPYONLY)
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_BINARY_DIR}/LICENSE.txt")

    # Stable Upgrade GUID — must NEVER change once a release is shipped,
    # or upgrades won't replace the prior installation. Generated once.
    set(CPACK_WIX_UPGRADE_GUID "B1F2C3D4-9A0E-4F8B-9A1C-7E3F6A5C2B11")
    set(CPACK_WIX_PROGRAM_MENU_FOLDER "quewi")
    # App icon shown in Add/Remove Programs and on Start Menu shortcuts.
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/icons/quewi.ico")
        set(CPACK_WIX_PRODUCT_ICON "${CMAKE_SOURCE_DIR}/resources/icons/quewi.ico")
    endif()

    # Extra WiX fragment that overrides WixUIInfoIco so the installer
    # wizard's title-bar icon is the kiwi instead of the generic
    # Windows Installer icon. Note: the icon shown on the .msi file
    # itself in Explorer is owned by the Windows shell and can't be
    # customised through WiX — that would require a Burn .exe bundle.
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/installer/branding.wxs")
        list(APPEND CPACK_WIX_EXTRA_SOURCES
            "${CMAKE_SOURCE_DIR}/resources/installer/branding.wxs")
    endif()

    # Branded installer artwork. WiX is fussy about format: both must
    # be 24-bit BMP (no transparency), exact pixel dimensions, no
    # compression. If the files aren't present, WiX falls back to its
    # plain blue gradient which we definitely want to escape.
    #
    #   banner.bmp — 493 × 58 px — runs across the top of every
    #                non-welcome dialog (license, install dir,
    #                progress).
    #   dialog.bmp — 493 × 312 px — full background of the Welcome
    #                and Finish dialogs.
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/installer/banner.bmp")
        set(CPACK_WIX_UI_BANNER
            "${CMAKE_SOURCE_DIR}/resources/installer/banner.bmp")
    endif()
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/installer/dialog.bmp")
        set(CPACK_WIX_UI_DIALOG
            "${CMAKE_SOURCE_DIR}/resources/installer/dialog.bmp")
    endif()
    # RTF license is shown verbatim in the EULA dialog. If we ship a
    # styled .rtf alongside LICENSE, prefer it over the plain text
    # the WIX generator stages from CPACK_RESOURCE_FILE_LICENSE above.
    if(EXISTS "${CMAKE_SOURCE_DIR}/resources/installer/license.rtf")
        set(CPACK_RESOURCE_FILE_LICENSE
            "${CMAKE_SOURCE_DIR}/resources/installer/license.rtf")
    endif()
    set(CPACK_PACKAGE_EXECUTABLES "quewi" "quewi")
    set(CPACK_CREATE_DESKTOP_LINKS "quewi")

    # CPack/WIX automatically adds a "Uninstall <ProductName>" shortcut
    # to the Start Menu folder (id=UNINSTALL) — see
    # CPackWIX.cmake's WixSource fragment. So an explicit patch isn't
    # needed; the MSI also registers normally with Apps & Features.

    include(CPack)
endif()

# ── CPack — macOS .dmg via DragNDrop ─────────────────────────────────
# Run `cpack -G DragNDrop` from build/macos-release after installing
# to produce quewi-<version>-macos.dmg with the .app bundle and a
# symlink to /Applications for drag-to-install. macdeployqt has
# already copied Qt frameworks/plugins into the bundle (see
# src/app/CMakeLists.txt's install CODE block), so the .dmg is fully
# self-contained.
if(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_PACKAGE_NAME              "quewi")
    set(CPACK_PACKAGE_VENDOR            "ServeGaming")
    set(CPACK_PACKAGE_VERSION           "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_HOMEPAGE_URL      "${PROJECT_HOMEPAGE_URL}")
    set(CPACK_PACKAGE_FILE_NAME         "quewi-${PROJECT_VERSION}-macos")
    set(CPACK_RESOURCE_FILE_LICENSE     "${CMAKE_SOURCE_DIR}/LICENSE")

    # DragNDrop volume label shown when the .dmg is mounted in Finder.
    set(CPACK_DMG_VOLUME_NAME           "quewi ${PROJECT_VERSION}")

    # CPack mounts the staging DMG, runs this AppleScript against it
    # (icon arrangement + /Applications symlink), then re-seals to
    # the final compressed image. Without this the .dmg opens as a
    # plain folder view with no install hint — the user reported
    # "opens as a folder" before this landed.
    set(CPACK_DMG_DS_STORE_SETUP_SCRIPT
        "${CMAKE_SOURCE_DIR}/resources/macos/dmg_setup.applescript")

    include(CPack)
endif()
