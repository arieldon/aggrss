#!/usr/bin/env sh

set -eux

BIN="rss"

# NOTE(ariel) I only use the GNU11 standard rather than the C11 standard for
# inline assembly asm().
CFLAGS="-std=gnu11 -D_DEFAULT_SOURCE"
WARNINGS="-Wall -Wextra -Wpedantic"
LIBRARIES="-pthread -lssl -lcrypto `sdl2-config --cflags --libs` -lGL"
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
cc src/*.c $FLAGS -o $BIN
