#!/usr/bin/env sh

set -eux

BIN="rss"

CFLAGS="-std=c11 -D_XOPEN_SOURCE -D_DEFAULT_SOURCE"
WARNINGS="-Wall -Wextra -Wpedantic"
LIBRARIES="-pthread -lssl -lcrypto"
LIBRARIES="$LIBRARIES `pkg-config --cflags --libs sqlite3`"
LIBRARIES="$LIBRARIES `pkg-config --cflags --libs freetype2`"
LIBRARIES="$LIBRARIES `sdl2-config --cflags --static-libs` -lGL"
FLAGS="$CFLAGS $WARNINGS $LIBRARIES"

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
