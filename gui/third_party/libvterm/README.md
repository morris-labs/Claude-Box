# Vendored libvterm 0.3.3

Source: <https://www.leonerd.org.uk/code/libvterm/libvterm-0.3.3.tar.gz>
(Paul "LeoNerd" Evans, MIT-licensed — see `LICENSE`.)

## Why vendored

libvterm has no vcpkg port and no CMake build of its own — upstream ships a
plain `Makefile` (libtool + a Perl Unicode-table code-generation step). On
Linux/macOS the build uses the distro `libvterm-dev` via `pkg-config`; Windows
has neither, so the source is vendored here and built by
`third_party/libvterm/CMakeLists.txt` as a small static library, linked in
place of `PkgConfig::VTERM`.

## What was copied

From the **release tarball** (not the git/bzr repo):

- `include/vterm.h`, `include/vterm_keycodes.h` — the public API
- `src/*.c` (9 translation units), `src/*.h`
- `src/fullwidth.inc`, `src/encoding/*.inc` — **pre-generated** Unicode tables

The tarball ships these `.inc` files already generated and contains **no
`.tbl` files and no `tbl2inc_c.pl`**, so there is no Perl dependency — the
code-generation only exists in upstream's VCS checkout, not in a `make dist`
tarball. The tarball's `Makefile`, `bin/`, `t/`, and `.pc.in` are not copied.

## Local patches

One, marked `claude-box local patch` in the source:

- `include/vterm.h` — `#undef small` after the includes. The Windows SDK
  (`<rpcndr.h>` via `<windows.h>`, hence via Qt) defines `small` as `char`,
  which breaks the `unsigned int small : 1;` bitfield wherever a translation
  unit includes both that header and this one. libvterm's own `.c` files
  don't hit it (they include no Windows headers); `TerminalWidget` and the
  moc output do.

The library sources compile otherwise unmodified with MSVC `/std:c11`.

## Updating

Download a newer release tarball, replace `include/` and `src/` (keep this
README and `LICENSE`), bump the version in `CMakeLists.txt`'s comment, and
rebuild. If a future release drops the pre-generated `.inc` files, the Perl
step would have to be run once and its output committed here.
