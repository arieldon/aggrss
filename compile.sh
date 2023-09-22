#!/usr/bin/env sh

set -eux

BIN="aggrss"

CFLAGS="-std=c11 -D_DEFAULT_SOURCE"
WARNINGS="-Wall -Wextra -Wpedantic"
LIBRARIES="-pthread"
LIBRARIES="$LIBRARIES `curl-config --cflags --libs`"
LIBRARIES="$LIBRARIES `pkg-config --cflags --libs sqlite3`"
LIBRARIES="$LIBRARIES `pkg-config --cflags --libs freetype2`"
LIBRARIES="$LIBRARIES `sdl2-config --cflags --static-libs` -lGL"
REQUIRED_MACROS="-DCONFIG_DIRECTORY_PATH=\"$HOME/.config/$BIN\""
FLAGS="$CFLAGS $WARNINGS $LIBRARIES $REQUIRED_MACROS"

DEBUG="-DDEBUG -ggdb -O0"
RELEASE="-O2 -march=native"
if [ $# -ge 1 ] && [ "$1" = "--debug" ]; then
	FLAGS="$FLAGS $DEBUG"
	shift 1
else
	FLAGS="$FLAGS $RELEASE"
fi

# NOTE(ariel) Pass any additional specified arguments from script to compiler.
if [ $# -ge 1 ]; then
	FLAGS="$FLAGS $@"
fi

cc src/*.c $FLAGS -o $BIN
