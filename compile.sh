#!/usr/bin/env sh

set -eux

BIN="rss"

CFLAGS="-std=c11 -D_DEFAULT_SOURCE"
WARNINGS="-Wall -Wextra -Wpedantic"
LIBRARIES="-pthread `sdl2-config --cflags --libs` -lGL"
FLAGS="$CFLAGS $WARNINGS $LIBRARIES"

DEBUG="-DDEBUG -g -O0"
RELEASE="-O2"
if [ $# -eq 1 ] && [ "$1" = "--debug" ]; then
    FLAGS="$FLAGS $DEBUG"
else
    FLAGS="$FLAGS $RELEASE"
fi

cc src/*.c $FLAGS -o $BIN
