#!/usr/bin/env sh

set -eux

BIN="aggrss"

COMPILER="gcc"
CFLAGS="-std=c11 -D_DEFAULT_SOURCE"
WARNINGS="-Wall -Wextra -Wshadow -Wconversion -Wdouble-promotion -Wno-unused-function -Wno-sign-conversion -Wno-string-conversion"
LIBRARIES="-pthread"
LIBRARIES="$LIBRARIES `curl-config --cflags --libs`"
LIBRARIES="$LIBRARIES `pkg-config --cflags --libs sqlite3`"
LIBRARIES="$LIBRARIES `pkg-config --cflags --libs freetype2`"
LIBRARIES="$LIBRARIES `sdl2-config --cflags --static-libs` -lGL"
REQUIRED_MACROS="-DCONFIG_DIRECTORY_PATH=\"$HOME/.config/$BIN\""
FLAGS="$CFLAGS $WARNINGS $LIBRARIES $REQUIRED_MACROS"

if [ $# -ge 1 ] && [ "$1" = "--release" ]; then
	RELEASE="-O2"
	FLAGS="$FLAGS $RELEASE"
	shift 1
else
	DEBUG="-DDEBUG -g3 -O0 -fno-omit-frame-pointer -fsanitize=undefined -fsanitize-undefined-trap-on-error"
	FLAGS="$FLAGS $DEBUG"
fi

if [ $# -ge 1 ]; then
	FLAGS="$FLAGS $@"
fi

$COMPILER src/*.c $FLAGS -o $BIN
