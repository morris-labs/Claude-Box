# Prerequisites (Ubuntu/Debian): sudo apt install qt6-base-dev cmake
# build-essential libvterm-dev pkg-config
#
# Build: cmake -B build && cmake --build build
# Run:   ./build/claude-box-gui

cmake_minimum_required(VERSION 3.16)
project(claude-box-gui VERSION 3.1.0 LANGUAGES CXX)

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

# WIN32: build as a Windows GUI subsystem executable (no console window on
# launch). Qt6::Widgets links Qt6::EntryPoint, which provides the WinMain
# shim that calls our regular int main(). On non-Windows platforms this flag
# is ignored by CMake.
add_executable(claude-box-gui WIN32
    src/main.cpp
    src/SingleInstanceGuard.cpp
    src/SingleInstanceGuard.h
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
    src/PortAllocator.cpp
    src/PortAllocator.h
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
    src/UsageView.cpp
    src/UsageView.h
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

# --- macOS .app bundle ---------------------------------------------------
#
# Sets up the .app bundle structure, embeds the Info.plist, and adds an
# optional `generate-icns` target that creates the dock icon from the SVG
# source.  The icon generation requires rsvg-convert (librsvg) and the
# Xcode-supplied iconutil to be on PATH.
#
# Build:               cmake -B build && cmake --build build
# Run:                 open build/claude-box-gui.app
# Generate .icns:      cmake --build build --target generate-icns
# Self-contained .app: cmake --build build --target app-bundle
if(APPLE)

set_target_properties(claude-box-gui PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
)

set(CLAUDE_BOX_ICNS_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/claude-box-jail.svg"
    CACHE FILEPATH "SVG to convert into the .app bundle icon (.icns)")
set(ICNS_DIR  "${CMAKE_CURRENT_BINARY_DIR}/claude-box.iconset")
set(ICNS_FILE "${CMAKE_CURRENT_BINARY_DIR}/claude-box.icns")

# rsvg-convert and iconutil are needed only when the developer explicitly
# runs `generate-icns`; a plain build skips this step so the prerequisite
# tools are not required for everyone who just wants to build and run.
find_program(RSVG_CONVERT rsvg-convert)
find_program(ICONUTIL      iconutil)

if(RSVG_CONVERT AND ICONUTIL)
    # macOS requires these exact filenames inside the .iconset directory.
    # iconutil accepts exactly: 16, 32, 128, 256, 512 (plus @2x variants).
    # 64 is not a valid slot and causes iconutil to fail or silently drop it.
    set(ICNS_SIZES 16 32 128 256 512)
    set(icns_pngs)
    foreach(sz IN LISTS ICNS_SIZES)
        math(EXPR sz2 "${sz} * 2")
        list(APPEND icns_pngs
            "${ICNS_DIR}/icon_${sz}x${sz}.png"
            "${ICNS_DIR}/icon_${sz}x${sz}@2x.png"
        )
        add_custom_command(
            OUTPUT "${ICNS_DIR}/icon_${sz}x${sz}.png"
                   "${ICNS_DIR}/icon_${sz}x${sz}@2x.png"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${ICNS_DIR}"
            COMMAND ${RSVG_CONVERT} -w ${sz}  -h ${sz}  -o "${ICNS_DIR}/icon_${sz}x${sz}.png"   "${CLAUDE_BOX_ICNS_SOURCE}"
            COMMAND ${RSVG_CONVERT} -w ${sz2} -h ${sz2} -o "${ICNS_DIR}/icon_${sz}x${sz}@2x.png" "${CLAUDE_BOX_ICNS_SOURCE}"
            DEPENDS "${CLAUDE_BOX_ICNS_SOURCE}"
            VERBATIM
        )
    endforeach()

    add_custom_command(
        OUTPUT  "${ICNS_FILE}"
        COMMAND ${ICONUTIL} -c icns -o "${ICNS_FILE}" "${ICNS_DIR}"
        DEPENDS ${icns_pngs}
        VERBATIM
    )
    # generate-icns: build the .icns from the iconset PNGs, then copy it
    # into the app bundle's Resources directory.  The bundle must exist
    # first (i.e. `cmake --build build` must have already run), so the
    # correct workflow is:
    #   cmake --build build
    #   cmake --build build --target generate-icns
    # The copy is here rather than as a POST_BUILD on claude-box-gui so
    # that a plain build never fails when rsvg-convert / iconutil are absent.
    add_custom_target(generate-icns
        COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:claude-box-gui>/Contents/Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${ICNS_FILE}"
                "$<TARGET_BUNDLE_DIR:claude-box-gui>/Contents/Resources/claude-box.icns"
        DEPENDS "${ICNS_FILE}"
        COMMENT "Generating icon and installing it into the app bundle"
        VERBATIM
    )
else()
    message(STATUS "rsvg-convert or iconutil not found -- skipping generate-icns target. "
                   "Run `brew install librsvg` and ensure Xcode command-line tools are "
                   "installed, then reconfigure to enable icon generation.")
endif()

# macdeployqt bundles the Qt frameworks into the .app so it is
# self-contained.  Requires macdeployqt to be on PATH (it ships with Qt).
find_program(MACDEPLOYQT macdeployqt
    HINTS "${Qt6_DIR}/../../../bin"
          "${Qt6_DIR}/../../../../bin"
)
if(MACDEPLOYQT)
    # app-bundle: run macdeployqt, then ad-hoc-sign the result so Gatekeeper
    # allows it to run on the local machine without a Developer ID.
    # For distribution to other machines, replace `-s -` with your Developer ID:
    #   codesign --deep --force --verify --sign "Developer ID Application: ..."
    add_custom_target(app-bundle
        COMMAND ${MACDEPLOYQT} "$<TARGET_BUNDLE_DIR:claude-box-gui>"
        # Sign first, then verify as a separate invocation. Combining
        # --sign and --verify in one codesign call evaluates the existing
        # signature state before signing on some Xcode versions, immediately
        # failing with "code object is not signed at all" on a fresh bundle.
        COMMAND codesign --deep --force --sign -
                "$<TARGET_BUNDLE_DIR:claude-box-gui>"
        COMMAND codesign --verify --deep
                "$<TARGET_BUNDLE_DIR:claude-box-gui>"
        DEPENDS claude-box-gui
        COMMENT "Running macdeployqt and ad-hoc signing the .app bundle"
        VERBATIM
    )

    # dmg: create a distributable disk image from the signed .app.
    # Notarization (notarytool) is not automated here -- it requires a
    # Developer ID certificate and Apple ID credentials, so it is a
    # manual step documented in the README once those are in place.
    #
    # hdiutil -srcfolder DIR copies the *contents* of DIR into the DMG
    # volume root, so the .app must be inside a staging directory, not
    # passed directly -- otherwise the DMG root holds Contents/MacOS/…
    # instead of the bundle itself.
    set(DMG_STAGE "${CMAKE_BINARY_DIR}/dmg-stage")
    set(DMG_PATH  "${CMAKE_BINARY_DIR}/Claude Box.dmg")
    add_custom_target(dmg
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${DMG_STAGE}"
        COMMAND ${CMAKE_COMMAND} -E make_directory   "${DMG_STAGE}"
        COMMAND cp -R "$<TARGET_BUNDLE_DIR:claude-box-gui>" "${DMG_STAGE}/"
        COMMAND hdiutil create -volname "Claude Box"
                -srcfolder "${DMG_STAGE}"
                -ov -format UDZO
                "${DMG_PATH}"
        DEPENDS app-bundle
        COMMENT "Creating Claude Box.dmg"
        VERBATIM
    )
else()
    message(STATUS "macdeployqt not found -- app-bundle and dmg targets unavailable. "
                   "Ensure the Qt bin directory is on PATH.")
endif()

endif() # APPLE

target_compile_definitions(claude-box-gui
    PRIVATE
        CLAUDE_BOX_VERSION="${PROJECT_VERSION}"
        # Lets SetupWizard's "Build Image" button find the Dockerfile
        # without guessing -- meaningful only when this binary is still
        # running from the checkout it was built in (the only way it's
        # distributed today); SetupWizard checks the Dockerfile is
        # actually there before trusting this rather than assuming so.
        CLAUDE_BOX_REPO_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
        # The Windows SDK's <windows.h> (pulled in via Qt) #defines min
        # and max as macros, which corrupts std::min({...}) and
        # std::max({...}) initializer-list calls into parse errors.
        # NOMINMAX suppresses those macros for the whole target.
        $<$<PLATFORM_ID:Windows>:NOMINMAX>
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
