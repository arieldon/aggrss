#!/usr/bin/env sh

set -eux

BIN="rss"

DEBUG="-DDEBUG -g -O0"
RELEASE="-O2"
WARNINGS="-Wall -Wextra -Wpedantic"
FLAGS="$WARNINGS `sdl2-config --libs` -lGL"

if [ $# -eq 1 ] && [ "$1" = "--debug" ]; then
    FLAGS="$FLAGS $DEBUG"
else
    FLAGS="$FLAGS $RELEASE"
fi

cc src/*.c $FLAGS -o $BIN
