cmake_minimum_required(VERSION 3.24)
project(quewi
    VERSION 0.0.1
    DESCRIPTION "Theatre cueing software"
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
    WebSockets
    Test
)

qt_standard_project_setup()

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
