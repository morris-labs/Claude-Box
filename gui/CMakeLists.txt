# Prerequisites (Ubuntu/Debian): sudo apt install qt6-base-dev cmake
# build-essential libvterm-dev pkg-config
#
# Build: cmake -B build && cmake --build build
# Run:   ./build/claude-box-gui

cmake_minimum_required(VERSION 3.16)
project(claude-box-gui VERSION 1.8.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent)

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
    src/ConversationCatalog.cpp
    src/ConversationCatalog.h
    src/DockerBackend.cpp
    src/DockerBackend.h
    src/PtySession.cpp
    src/PtySession.h
    src/TerminalWidget.cpp
    src/TerminalWidget.h
    src/NewBoxDialog.cpp
    src/NewBoxDialog.h
    src/BoxDetailsPanel.cpp
    src/BoxDetailsPanel.h
    src/Theme.cpp
    src/Theme.h
    src/Icons.cpp
    src/Icons.h
)

# --- desktop entry -------------------------------------------------------
#
# Generated rather than committed ready-made because Exec= and Icon= have
# to be absolute paths, and those depend on where this checkout lives.
# `cmake --build build --target desktop-install` drops the result into the
# per-user XDG directories; nothing is installed automatically.
set(CLAUDE_BOX_EXEC "$<TARGET_FILE:claude-box-gui>")
# The name the entry and QIcon::fromTheme() both look up; the file below
# is installed under exactly this name, so swapping the artwork never
# means touching either lookup.
set(CLAUDE_BOX_ICON "claude-box")
set(CLAUDE_BOX_ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/claude-box-jail.svg"
    CACHE FILEPATH "SVG installed as the application icon")
set(CLAUDE_BOX_DESKTOP "${CMAKE_CURRENT_BINARY_DIR}/claude-box-gui.desktop")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/claude-box-gui.desktop.in"
    "${CMAKE_CURRENT_BINARY_DIR}/claude-box-gui.desktop.pre"
    @ONLY
)
# A second pass is needed for the $<TARGET_FILE:...> generator expression,
# which configure_file() cannot expand on its own.
file(GENERATE OUTPUT "${CLAUDE_BOX_DESKTOP}"
     INPUT "${CMAKE_CURRENT_BINARY_DIR}/claude-box-gui.desktop.pre")

# A .desktop file without the executable bit is a document, not a
# launcher: clicking one hands it to the default text handler (or, in a
# file manager that tries to run it, to a terminal that flashes and dies)
# instead of starting the app. The generated copy gets marked executable
# for the same reason -- it sits in the build tree where it's easy to
# click by mistake.
add_custom_command(TARGET claude-box-gui POST_BUILD
    COMMAND chmod +x "${CLAUDE_BOX_DESKTOP}"
    COMMENT "Marking the generated desktop entry executable"
    VERBATIM
)

find_program(UPDATE_DESKTOP_DATABASE update-desktop-database)
find_program(GIO_TOOL gio)
find_program(GTK_UPDATE_ICON_CACHE gtk-update-icon-cache)

set(desktop_install_commands
    COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.local/share/applications"
    COMMAND ${CMAKE_COMMAND} -E make_directory "$ENV{HOME}/.local/share/icons/hicolor/scalable/apps"
    COMMAND ${CMAKE_COMMAND} -E copy "${CLAUDE_BOX_ICON_SOURCE}"
            "$ENV{HOME}/.local/share/icons/hicolor/scalable/apps/${CLAUDE_BOX_ICON}.svg"
    COMMAND ${CMAKE_COMMAND} -E copy "${CLAUDE_BOX_DESKTOP}"
            "$ENV{HOME}/.local/share/applications/claude-box-gui.desktop"
    COMMAND chmod +x "$ENV{HOME}/.local/share/applications/claude-box-gui.desktop"
)
if(GIO_TOOL)
    # GNOME's "Allow Launching": without the trusted bit, Files opens the
    # entry rather than running it, however set the executable bit is.
    list(APPEND desktop_install_commands
        COMMAND ${GIO_TOOL} set "$ENV{HOME}/.local/share/applications/claude-box-gui.desktop"
                metadata::trusted true)
endif()
if(UPDATE_DESKTOP_DATABASE)
    list(APPEND desktop_install_commands
        COMMAND ${UPDATE_DESKTOP_DATABASE} "$ENV{HOME}/.local/share/applications")
endif()
if(GTK_UPDATE_ICON_CACHE)
    # ~/.local/share/icons/hicolor already carries an icon-theme.cache
    # from other apps, and a stale one hides a newly dropped icon from
    # GTK entirely. --ignore-theme-index because this per-user hicolor
    # tree has no index.theme of its own.
    list(APPEND desktop_install_commands
        COMMAND ${GTK_UPDATE_ICON_CACHE} -f -t --ignore-theme-index
                "$ENV{HOME}/.local/share/icons/hicolor")
endif()

add_custom_target(desktop-install
    ${desktop_install_commands}
    DEPENDS claude-box-gui
    COMMENT "Installing claude-box-gui.desktop and its icon into ~/.local/share"
    VERBATIM
)

target_compile_definitions(claude-box-gui
    PRIVATE
        CLAUDE_BOX_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(claude-box-gui
    PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        PkgConfig::VTERM
)

# forkpty()/openpty() lived in a separate libutil before glibc 2.34 merged
# them into libc; link it opportunistically for older toolchains without
# hard-failing configure on newer ones where it's no longer a distinct lib.
find_library(UTIL_LIBRARY util)
if(UTIL_LIBRARY)
    target_link_libraries(claude-box-gui PRIVATE ${UTIL_LIBRARY})
endif()
