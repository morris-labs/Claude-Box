# Vendored libvterm 0.3.3 (see README.md). Built only where the platform has
# no system libvterm / vcpkg port -- currently Windows. Upstream has no CMake
# build of its own; this is the whole of it: compile the nine src/*.c files
# with include/ and src/ on the header path. The src/*.inc files are #included
# C fragments (pre-generated Unicode tables), not separately compiled.

add_library(vterm_vendored STATIC
    src/encoding.c
    src/keyboard.c
    src/mouse.c
    src/parser.c
    src/pen.c
    src/screen.c
    src/state.c
    src/unicode.c
    src/vterm.c
)

# include/ is the public API (vterm.h, vterm_keycodes.h); src/ is where the
# .c files find their own internal headers and the .inc table fragments.
target_include_directories(vterm_vendored
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# The parent turns CMAKE_AUTOMOC on globally; there is no Qt (and no Q_OBJECT)
# in this C library, so keep moc away from it.
set_target_properties(vterm_vendored PROPERTIES AUTOMOC OFF)

# This is third-party code built verbatim -- its warnings aren't ours to fix.
# Suppress the specific ones 0.3.3 emits under MSVC rather than passing /w,
# which collides with the generator's default /W1 (compiler warning D9025):
#   C4996  "unsafe" CRT calls (snprintf/strncpy) -- also _CRT_SECURE_NO_WARNINGS
#   C4244/C4267/C4018  benign narrowing / signed-unsigned in pen.c, state.c,
#                      unicode.c (present under gcc -Wconversion too)
if(MSVC)
    target_compile_definitions(vterm_vendored PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(vterm_vendored PRIVATE
        /wd4996 /wd4244 /wd4267 /wd4018)
endif()

# Match the interface name the rest of gui/ links against, so the consuming
# target does not care whether libvterm came from here or from pkg-config.
add_library(claude-box::vterm ALIAS vterm_vendored)
