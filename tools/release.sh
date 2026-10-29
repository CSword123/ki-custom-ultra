#!/bin/sh
# The release zips: dist/ki-custom-ultra-VERSION-windows-x64.zip and
# dist/ki-custom-ultra-VERSION-linux-x64.zip, each a ki-custom-ultra/ folder
# that runs where it is extracted, without ki1-decomp or ki1-port.
#
#   tools/release.sh windows|linux|all
#   tools/release.sh patch [FROM]
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
#            rpath). Python 3 and gcc are the player's. The program, SDL3
#            and ki1-port's libki1.a are built in Debian 13 (trixie), so
#            the zip needs its glibc (2.41) or newer, not this machine's:
#            Debian's official image (docker.io library/debian, the layer
#            below) unpacked into build/debian-root/ and run with bwrap as
#            root of a user namespace, with apt's build packages added once.
#            SDL3 is built there from source (into build/debian/sdl3/), the
#            program into build/debian/ and libki1.a into ki1-port's
#            build-debian/.
#
# patch makes dist/ki-custom-ultra-VERSION-patch.zip: the files of this
# version's two release folders (made first, by windows and linux) that are
# new or changed since FROM's zips (default the VERSION before, from
# CHANGELOG.md), in one ki-custom-ultra/ folder to extract over a FROM folder
# of either system. Files gone since FROM are listed in its PATCH.txt.
#
# What it downloads is kept in build/release-cache/ and checked against the
# SHA-256 below. SDL3_DIR (default ../SDL3-3.4.18/x86_64-w64-mingw32) is the
# mingw folder of SDL's SDL3-devel-*-mingw release.
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

DEB_LAYER=a1d156b0aab5fb07d822a7f4681f6d3ed1689073531c34f5668818c233633f11   # debian:trixie (13.7)
DEB_ROOT=build/debian-root
DEB_PKGS="build-essential pkg-config cmake ninja-build python3 ca-certificates libx11-dev libxext-dev
    libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev libxkbcommon-dev libwayland-dev
    wayland-protocols libdecor-0-dev libegl-dev libgl-dev libgles-dev libvulkan-dev libasound2-dev libpulse-dev
    libpipewire-0.3-dev libjack-jackd2-dev libdrm-dev libgbm-dev libudev-dev libdbus-1-dev libibus-1.0-dev
    libusb-1.0-0-dev"

SDL_VER=3.4.2
SDL_TGZ=SDL3-$SDL_VER.tar.gz
SDL_URL=https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/$SDL_TGZ
SDL_SHA=ef39a2e3f9a8a78296c40da701967dd1b0d0d6e267e483863ce70f8a03b4050c

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

# a command in the Debian root as root, the folder above this one at /work
in_debian() {
    bwrap --bind $DEB_ROOT / --dev /dev --proc /proc --tmpfs /tmp --ro-bind /etc/resolv.conf /etc/resolv.conf \
        --bind "$(cd .. && pwd)" /work --chdir /work/"$(basename "$PWD")" --unshare-user --uid 0 --gid 0 \
        --setenv PATH /usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin --setenv HOME /root \
        --setenv DEBIAN_FRONTEND noninteractive "$@"
}

debian_root() {     # build/debian-root/ with the build packages, once
    [ -f $DEB_ROOT/.ready ] && return 0
    f=debian-trixie-$DEB_LAYER.tar.gz
    if [ ! -f "$CACHE/$f" ]; then
        mkdir -p $CACHE
        tok=$(curl -fsSL "https://auth.docker.io/token?service=registry.docker.io&scope=repository:library/debian:pull" |
              sed 's/.*"token":"\([^"]*\)".*/\1/')
        curl -fL -H "Authorization: Bearer $tok" -o "$CACHE/$f.part" \
            "https://registry-1.docker.io/v2/library/debian/blobs/sha256:$DEB_LAYER"
        mv "$CACHE/$f.part" "$CACHE/$f"
    fi
    echo "$DEB_LAYER  $CACHE/$f" | sha256sum -c --quiet || { echo "release.sh: $f does not match its SHA-256" >&2; exit 1; }
    rm -rf $DEB_ROOT
    mkdir -p $DEB_ROOT
    tar -xzf "$CACHE/$f" -C $DEB_ROOT --no-same-owner --exclude='dev/*'
    echo 'APT::Sandbox::User "root";' > $DEB_ROOT/etc/apt/apt.conf.d/99sandbox
    in_debian sh -c "apt-get update -q && apt-get install -y -q --no-install-recommends $(echo $DEB_PKGS)" >/dev/null
    touch $DEB_ROOT/.ready
}

linux() {
    command -v bwrap >/dev/null || { echo "release.sh: no bwrap (bubblewrap) for the Debian build" >&2; exit 1; }
    fetch "$SDL_URL" "$SDL_TGZ" "$SDL_SHA"
    debian_root
    sdl=build/debian/sdl3
    if [ ! -f $sdl/lib/libSDL3.so.0 ]; then
        in_debian sh -c "set -e; tar -xzf $CACHE/$SDL_TGZ -C /tmp
            cmake -S /tmp/SDL3-$SDL_VER -B /tmp/sdl -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=\$PWD/$sdl \
                -DCMAKE_INSTALL_LIBDIR=lib -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF >/dev/null
            ninja -C /tmp/sdl install >/dev/null"
    fi
    # SDL's own link flags would put build/debian/sdl3/lib in the rpath
    d=dist/ki-custom-ultra-linux
    abs=/work/$(basename "$PWD")/$sdl
    in_debian env PKG_CONFIG_PATH=$abs/lib/pkgconfig make OBJDIR=build/debian/obj PROG=build/debian/ki-custom-ultra \
        PORT_BUILD=build-debian SDL_LIBS="-L$abs/lib -lSDL3" DIST=$d dist
    mkdir -p $d/lib
    cp $sdl/lib/libSDL3.so.0 $d/lib/libSDL3.so.0
    need=$(objdump -T $d/ki-custom-ultra $d/lib/libSDL3.so.0 | grep -o 'GLIBC_2\.[0-9]*' | sort -uV | tail -n1)
    echo "linux: needs $need or newer"
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

# the files of new/ that old/ lacks or has with other bytes, copied to out/;
# THIRD-PARTY.txt, which differs per system, only if more than its version did
changed() {         # old new out from
    (cd "$2" && find . -type f) | sort | while read -r f; do
        cmp -s "$1/$f" "$2/$f" && continue
        [ "$f" = ./THIRD-PARTY.txt ] && sed "1s/ $VERSION:/ $4:/" "$2/$f" | cmp -s "$1/$f" - && continue
        if [ -f "$3/$f" ] && ! cmp -s "$2/$f" "$3/$f"; then
            echo "release.sh: $f differs between the Windows and Linux releases" >&2; exit 1
        fi
        mkdir -p "$3/$(dirname "$f")"
        cp -p "$2/$f" "$3/$f"
    done
}

patch() {           # [from version]
    from=${1:-$(grep -m2 '^## ' CHANGELOG.md | tail -n1 | cut -d' ' -f2)}
    [ "$from" != "$VERSION" ] || { echo "release.sh: no version before $VERSION in CHANGELOG.md" >&2; exit 1; }
    st=dist/stage-patch out=dist/ki-custom-ultra-$VERSION-patch.zip
    rm -rf $st "$out"
    mkdir -p $st/p/ki-custom-ultra
    gone=
    for sys in windows linux; do
        z=dist/ki-custom-ultra-$from-$sys-x64.zip
        [ -f "$z" ] || { echo "release.sh: no $z to patch from" >&2; exit 1; }
        [ -d dist/ki-custom-ultra-$sys ] || { echo "release.sh: no dist/ki-custom-ultra-$sys (tools/release.sh $sys)" >&2; exit 1; }
        unzip -q "$z" -d $st/$sys
        changed $st/$sys/ki-custom-ultra dist/ki-custom-ultra-$sys $st/p/ki-custom-ultra $from
        gone="$gone$( (cd $st/$sys/ki-custom-ultra && find . -type f) | sort | while read -r f; do
            [ -e "dist/ki-custom-ultra-$sys/$f" ] || echo "  ${f#./} ($sys)"; done)"
    done
    check_clean $st/p
    {
        echo "ki-custom-ultra $from -> $VERSION patch"
        echo
        echo "Extract this zip where the $from ki-custom-ultra/ folder is (Windows or"
        echo "Linux) and let it replace the files. Your roms/, data/ and projects/ stay."
        echo "The other system's files come along and can be deleted (Windows:"
        echo "ki-custom-ultra.exe; Linux: ki-custom-ultra and lib/)."
        echo
        echo "Files in this patch:"
        (cd $st/p/ki-custom-ultra && find . -type f ! -name PATCH.txt) | sort | sed 's|^\./|  |'
        if [ -n "$gone" ]; then echo; echo "No longer used (can be deleted):"; echo "$gone"; fi
    } > $st/p/ki-custom-ultra/PATCH.txt
    (cd $st/p && zip -qr -9 -X "../../$(basename "$out")" ki-custom-ultra)
    rm -rf $st
    echo "$out: $(du -h "$out" | cut -f1)"
}

case "${1:-all}" in
windows) windows ;;
linux) linux ;;
all) windows; linux ;;
patch) patch "${2:-}" ;;
*) echo "usage: tools/release.sh windows|linux|all|patch [FROM]" >&2; exit 2 ;;
esac
