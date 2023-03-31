#!/usr/bin/env sh

set -eux

BIN="rss"

CFLAGS="-std=c11 -D_DEFAULT_SOURCE"
WARNINGS="-Wall -Wextra -Wpedantic"
LIBRARIES="-pthread -lssl -lcrypto"
LIBRARIES="$LIBRARIES `pkg-config --cflags --libs freetype2`"
LIBRARIES="$LIBRARIES `sdl2-config --cflags --static-libs` -lGL"
FLAGS="$CFLAGS $WARNINGS $LIBRARIES"

DEBUG="-DDEBUG -ggdb -O0"
RELEASE="-O2 -march=native"
if [ $# -ge 1 ] && [ "$1" = "--debug" ]; then
	FLAGS="$FLAGS $DEBUG"
	shift 1;
else
	FLAGS="$FLAGS $RELEASE"
fi

# NOTE(ariel) Pass any additional specified arguments from script to compiler.
if [ $# -ge 1 ]; then
	FLAGS="$FLAGS $@"
fi

# NOTE(ariel) Order of C source files and statically linked libraries matters.
# Statically linked libraries (.a files) must follow all C source files because
# it seems like `ld` discards routines left uncalled.
cc src/*.c src/*.a $FLAGS -o $BIN
