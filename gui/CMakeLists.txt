# Prerequisites (Ubuntu/Debian): sudo apt install qt6-base-dev cmake
# build-essential libvterm-dev pkg-config
#
# Build: cmake -B build && cmake --build build
# Run:   ./build/claude-box-gui

cmake_minimum_required(VERSION 3.16)
project(claude-box-gui VERSION 1.11.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Network)

# libvterm: on Linux/macOS the distro ships it and pkg-config is the native
# way to find it. Windows has neither a package nor a vcpkg port, and libvterm
# upstream has no CMake build of its own (a plain Makefile with libtool and a
# Perl Unicode-table code-generation step). The 0.3.3 *release tarball* ships
# those tables pre-generated and drops the Perl step entirely, and its sources
# compile as-is with MSVC (verified: cl /std:c11, clean), so the source is
# vendored under third_party/libvterm/ and built there. Either way the rest of
# this file links ${VTERM_LINK_LIB}.
if(WIN32)
    # The vendored libvterm is C; this project is otherwise CXX-only, so turn
    # the C compiler on here rather than in project() where it would force C
    # detection on the Linux/macOS builds that never compile a .c file.
    enable_language(C)
    add_subdirectory(third_party/libvterm)
    set(VTERM_LINK_LIB claude-box::vterm)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VTERM REQUIRED IMPORTED_TARGET vterm)
    set(VTERM_LINK_LIB PkgConfig::VTERM)
endif()

# PtySession.h declares one interface; exactly one of these backs it --
# forkpty on Unix, ConPTY on Windows. Picked here rather than compiling
# both and #ifdef-ing inside one file because the two mechanisms don't
# share so much as a helper function, only the class they implement.
if(WIN32)
    set(PTY_SESSION_SOURCE src/PtySessionWin.cpp)
else()
    set(PTY_SESSION_SOURCE src/PtySessionUnix.cpp)
endif()

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
    src/ContainerPaths.cpp
    src/ContainerPaths.h
    src/DockerApi.cpp
    src/DockerApi.h
    src/DockerBackend.cpp
    src/DockerBackend.h
    src/SshTunnelSession.cpp
    src/SshTunnelSession.h
    src/SshRemoteCatalog.cpp
    src/SshRemoteCatalog.h
    ${PTY_SESSION_SOURCE}
    src/PtySession.h
    src/TerminalWidget.cpp
    src/TerminalWidget.h
    src/NewBoxDialog.cpp
    src/NewBoxDialog.h
    src/SshForwardsDialog.cpp
    src/SshForwardsDialog.h
    src/ManageSshRemotesDialog.cpp
    src/ManageSshRemotesDialog.h
    src/SetupWizard.cpp
    src/SetupWizard.h
    src/CommandTerminalDialog.cpp
    src/CommandTerminalDialog.h
    src/CollapsibleSection.cpp
    src/CollapsibleSection.h
    src/BoxDetailsPanel.cpp
    src/BoxDetailsPanel.h
    src/Theme.cpp
    src/Theme.h
    src/Icons.cpp
    src/Icons.h
)

# --- desktop entry -------------------------------------------------------
#
# XDG/freedesktop-specific end to end (a .desktop file, the hicolor icon
# theme, gio/update-desktop-database/gtk-update-icon-cache) -- there is no
# equivalent attempted here for Windows (a Start Menu shortcut or an
# installer is a separate decision, deferred rather than guessed at) or
# macOS (an .app bundle), so this whole block is a no-op there.
#
# Generated rather than committed ready-made because Exec= and Icon= have
# to be absolute paths, and those depend on where this checkout lives.
# `cmake --build build --target desktop-install` drops the result into the
# per-user XDG directories; nothing is installed automatically.
if(UNIX AND NOT APPLE)

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

endif() # UNIX AND NOT APPLE

target_compile_definitions(claude-box-gui
    PRIVATE
        CLAUDE_BOX_VERSION="${PROJECT_VERSION}"
        # Lets SetupWizard's "Build Image" button find the Dockerfile
        # without guessing -- meaningful only when this binary is still
        # running from the checkout it was built in (the only way it's
        # distributed today); SetupWizard checks the Dockerfile is
        # actually there before trusting this rather than assuming so.
        CLAUDE_BOX_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(claude-box-gui
    PRIVATE
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
        ${VTERM_LINK_LIB}
)

# forkpty()/openpty() lived in a separate libutil before glibc 2.34 merged
# them into libc; link it opportunistically for older toolchains without
# hard-failing configure on newer ones where it's no longer a distinct lib.
# Meaningless on Windows, which uses ConPTY instead (PtySessionWin.cpp) --
# guarded so find_library() doesn't even run there.
if(UNIX)
    find_library(UTIL_LIBRARY util)
    if(UTIL_LIBRARY)
        target_link_libraries(claude-box-gui PRIVATE ${UTIL_LIBRARY})
    endif()
endif()
