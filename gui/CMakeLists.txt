# Prerequisites (Ubuntu/Debian): sudo apt install qt6-base-dev cmake
# build-essential libvterm-dev pkg-config
#
# Build: cmake -B build && cmake --build build
# Run:   ./build/claude-box-gui

cmake_minimum_required(VERSION 3.16)
project(claude-box-gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

find_package(PkgConfig REQUIRED)
pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)

add_executable(claude-box-gui
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/BoxTableModel.cpp
    src/BoxTableModel.h
    src/BoxRecord.cpp
    src/BoxRecord.h
    src/DockerBackend.cpp
    src/DockerBackend.h
    src/PtySession.cpp
    src/PtySession.h
    src/TerminalWidget.cpp
    src/TerminalWidget.h
    src/NewBoxDialog.cpp
    src/NewBoxDialog.h
)

target_link_libraries(claude-box-gui
    PRIVATE
        Qt6::Widgets
        PkgConfig::VTERM
)

# forkpty()/openpty() lived in a separate libutil before glibc 2.34 merged
# them into libc; link it opportunistically for older toolchains without
# hard-failing configure on newer ones where it's no longer a distinct lib.
find_library(UTIL_LIBRARY util)
if(UTIL_LIBRARY)
    target_link_libraries(claude-box-gui PRIVATE ${UTIL_LIBRARY})
endif()
