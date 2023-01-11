#!/usr/bin/env sh

set -eux

BIN="rss"

# NOTE(ariel) I only use the GNU11 standard rather than the C11 standard for
# inline assembly asm().
CFLAGS="-std=gnu11 -D_DEFAULT_SOURCE"
WARNINGS="-Wall -Wextra -Wpedantic"
LIBRARIES="-pthread -lssl -lcrypto `sdl2-config --cflags --libs` -lGL"
FLAGS="$CFLAGS $WARNINGS $LIBRARIES"

DEBUG="-DDEBUG -g -O0"
RELEASE="-O2"
if [ $# -eq 1 ] && [ "$1" = "--debug" ]; then
    FLAGS="$FLAGS $DEBUG"
else
    FLAGS="$FLAGS $RELEASE"
fi

# NOTE(ariel) Order of C source files and statically linked libraries matters.
# Statically linked libraries (.a files) must follow all C source files because
# it seems like `ld` discards routines left uncalled.
cc src/*.c $FLAGS -o $BIN
