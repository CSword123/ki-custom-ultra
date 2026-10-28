#!/bin/sh
# The release zips: dist/ki-custom-ultra-VERSION-windows-x64.zip and
# dist/ki-custom-ultra-VERSION-linux-x64.zip, each a ki-custom-ultra/ folder
# that runs where it is extracted, without ki1-decomp or ki1-port.
#
#   tools/release.sh windows|linux|all
#
# Both are `make dist` (the program, tools/, build/py/ and build/recomp/: the
# recompiler and what it needs) plus:
#
#   windows  SDL3.dll; python/: the embeddable Python from python.org, with
#            tools/ and build/py/ on its path; mingw64/: the part of a
#            winlibs.com MinGW-w64 gcc (UCRT) that compiles the game code
#            into data/game.dll (gcc, cc1, as, ld, the C headers it
#            includes, the CRT and import libraries a DLL links with).
#            The editor uses them when they are there (src/main.cpp).
#   linux    lib/libSDL3.so.0 (found through the program's $ORIGIN/lib
#            rpath). Python 3 and gcc are the player's.
#
# What it downloads is kept in build/release-cache/ and checked against the
# SHA-256 below. SDL3_DIR (default ../SDL3-3.4.18/x86_64-w64-mingw32) is the
# mingw folder of SDL's SDL3-devel-*-mingw release; SDL3_SO (default the
# system's) the libSDL3.so.0 copied for Linux.
#
# No game code or data goes in: the zips are checked for data/, gen/,
# game.so/.dll, catalog.json, .ultrap and .u98 files before they are made.
set -eu
cd "$(dirname "$0")/.."

VERSION=$(cat VERSION)
CACHE=build/release-cache
SDL3_DIR=${SDL3_DIR:-../SDL3-3.4.18/x86_64-w64-mingw32}

PY_VER=3.14.10
PY_ZIP=python-$PY_VER-embed-amd64.zip
PY_URL=https://www.python.org/ftp/python/$PY_VER/$PY_ZIP
PY_SHA=90905e64a840557d8c1bc4f5b74db3d7ee85e3ffb217611cac1c7813acea4159

GCC_VER=16.2.0
GCC_REL=16.2.0posix-23.1.3-14.0.0-ucrt-r3
GCC_ZIP=winlibs-x86_64-posix-seh-gcc-$GCC_VER-mingw-w64ucrt-14.0.0-r3.zip
GCC_URL=https://github.com/brechtsanders/winlibs_mingw/releases/download/$GCC_REL/$GCC_ZIP
GCC_SHA=3c69305081e4a647144339a936244fcf7d3d9e558319e1ad7be2cd2886d4b6a1

fetch() {   # url file sha256
    mkdir -p $CACHE
    if [ ! -f "$CACHE/$2" ]; then
        curl -fL -o "$CACHE/$2.part" "$1"
        mv "$CACHE/$2.part" "$CACHE/$2"
    fi
    echo "$3  $CACHE/$2" | sha256sum -c --quiet || { echo "release.sh: $2 does not match its SHA-256" >&2; exit 1; }
}

check_clean() {     # nothing from the game in the folder
    bad=$(find "$1" \( -name data -o -name gen -o -name 'game.so' -o -name 'game.dll' -o -name catalog.json \
                       -o -name '*.ultrap' -o -name '*.kip' -o -name '*.u98' -o -name '*.bin' \) -print)
    [ -z "$bad" ] || { echo "release.sh: game data in the release: $bad" >&2; exit 1; }
}

zip_up() {          # folder (dist/ki-custom-ultra-*) -> zip with a ki-custom-ultra/ folder in it
    check_clean "$1"
    out=dist/ki-custom-ultra-$VERSION-$2-x64.zip
    rm -rf dist/stage "$out"
    mkdir -p dist/stage
    cp -a "$1" dist/stage/ki-custom-ultra
    (cd dist/stage && zip -qr -9 -X "../$(basename "$out")" ki-custom-ultra)
    rm -rf dist/stage
    echo "$out: $(du -h "$out" | cut -f1)"
}

# the part of winlibs' mingw64/ that `gcc -c` and `gcc -shared` use for the
# game code: the driver, cc1, collect2/lto-wrapper, as and ld with the DLLs
# they load, gcc's own headers and libgcc, and of mingw-w64 the C runtime
# headers (closure of the standard C headers) and the libraries a DLL links
# with (gcc's link spec, -static-libgcc)
trim_mingw() {      # winlibs mingw64/ -> out/
    src=$1 out=$2
    lx=libexec/gcc/x86_64-w64-mingw32/$GCC_VER
    lg=lib/gcc/x86_64-w64-mingw32/$GCC_VER
    t=x86_64-w64-mingw32
    mkdir -p "$out/bin" "$out/$lx" "$out/$lg" "$out/$t/bin" "$out/$t/lib" "$out/$t/include"
    cp "$src/bin/gcc.exe" "$src/bin/libiconv-2.dll" "$src/bin/libwinpthread-1.dll" "$out/bin/"
    cp "$src/$lx/cc1.exe" "$src/$lx/collect2.exe" "$src/$lx/lto-wrapper.exe" \
       "$src/$lx/"*.dll "$out/$lx/"
    cp "$src/$t/bin/as.exe" "$src/$t/bin/ld.exe" "$src/$t/bin/"*.dll "$out/$t/bin/"
    cp -r "$src/$lg/include" "$src/$lg/include-fixed" "$out/$lg/"
    cp "$src/$lg/crtbegin.o" "$src/$lg/crtend.o" "$src/$lg/libgcc.a" "$src/$lg/libgcc_eh.a" "$out/$lg/"
    for f in crt2.o dllcrt2.o libmingw32.a libmingwex.a libmsvcrt.a libucrt.a libucrtbase.a libmoldname.a \
             libm.a libkernel32.a libpthread.a libwinpthread.a libadvapi32.a libshell32.a libuser32.a; do
        cp "$src/$t/lib/$f" "$out/$t/lib/"
    done
    probe=$CACHE/probe.c
    for h in assert ctype errno float inttypes limits locale math setjmp signal stdarg stdbool stddef \
             stdint stdio stdlib string time wchar wctype fenv malloc memory; do
        echo "#include <$h.h>"
    done > "$probe"
    x86_64-w64-mingw32-gcc -M -nostdinc -D_UCRT -std=gnu11 -isystem "$src/$lg/include" \
        -isystem "$src/$lg/include-fixed" -isystem "$src/$t/include" "$probe" |
        tr ' \\' '\n\n' | grep "^$src/$t/include/" | sort -u | while read -r h; do
            rel=${h#"$src/$t/include/"}
            mkdir -p "$out/$t/include/$(dirname "$rel")"
            cp "$h" "$out/$t/include/$rel"
        done
    cp "$src/version_info.txt" "$out/"
}

windows() {
    fetch "$PY_URL" "$PY_ZIP" "$PY_SHA"
    fetch "$GCC_URL" "$GCC_ZIP" "$GCC_SHA"
    [ -f "$SDL3_DIR/bin/SDL3.dll" ] || { echo "release.sh: no $SDL3_DIR/bin/SDL3.dll (set SDL3_DIR)" >&2; exit 1; }
    d=dist/ki-custom-ultra-windows
    make TARGET=windows SDL3_DIR="$SDL3_DIR" DIST=$d dist
    unzip -q -o "$CACHE/$PY_ZIP" -d $d/python
    # the embeddable Python adds no script folder to sys.path: tools/ and
    # build/py/ go in its ._pth (relative to python/)
    printf 'python%s.zip\n.\n..\\tools\n..\\build\\py\n' "$(echo $PY_VER | cut -d. -f1,2 | tr -d .)" \
        > $d/python/python$(echo $PY_VER | cut -d. -f1,2 | tr -d .)._pth
    [ -d $CACHE/winlibs/mingw64 ] || unzip -q -o "$CACHE/$GCC_ZIP" -d $CACHE/winlibs
    trim_mingw $CACHE/winlibs/mingw64 $d/mingw64
    third_party_notes windows > $d/THIRD-PARTY.txt
    zip_up $d windows
}

linux() {
    so=${SDL3_SO:-$(readlink -f "$(pkg-config --variable=libdir sdl3)/libSDL3.so.0")}
    d=dist/ki-custom-ultra-linux
    make DIST=$d dist
    mkdir -p $d/lib
    cp "$so" $d/lib/libSDL3.so.0
    third_party_notes linux > $d/THIRD-PARTY.txt
    zip_up $d linux
}

third_party_notes() {   # windows|linux
    cat <<EOF
ki-custom-ultra $VERSION: what else is in this folder

SDL 3 (SDL3.dll, lib/libSDL3.so.0), zlib license: https://libsdl.org/
Dear ImGui (in the program), MIT license: https://github.com/ocornut/imgui
nlohmann/json (in the program), MIT license: https://github.com/nlohmann/json
EOF
    [ "$1" = windows ] || return 0
    cat <<EOF

python/: the embeddable Python $PY_VER for Windows from python.org
($PY_URL), unchanged but for python*._pth. PSF license: python/LICENSE.txt.

mingw64/: part of the winlibs.com build of GCC $GCC_VER, GNU Binutils and
MinGW-w64 (UCRT) ($GCC_URL; mingw64/version_info.txt), unchanged, only the
files that compile the game code. GCC and Binutils are GPL v3 (libgcc and
gcc's headers with the GCC Runtime Library Exception), GMP, MPFR and MPC
LGPL v3, ISL MIT, libiconv and gettext's libintl LGPL, zlib and zstd their
own permissive licenses; the MinGW-w64 headers and runtime are ZPL / public
domain. Sources: https://gcc.gnu.org/, https://www.gnu.org/software/binutils/,
https://www.mingw-w64.org/, and the build scripts at
https://github.com/brechtsanders/winlibs_mingw.
EOF
}

case "${1:-all}" in
windows) windows ;;
linux) linux ;;
all) windows; linux ;;
*) echo "usage: tools/release.sh windows|linux|all" >&2; exit 2 ;;
esac
