#!/usr/bin/env sh

set -eux

BIN="aggrss"

COMPILER="clang"
CFLAGS="-std=c11 -D_DEFAULT_SOURCE"
WARNINGS="-Wall -Wextra -Wshadow -Wformat -Wdouble-promotion -Wno-sign-conversion -Wno-unused-function -Wno-unused-parameter"
if [ "$COMPILER" = "clang" ]
then
	WARNINGS="$WARNINGS -Wreturn-stack-address"
elif [ "$COMPILER" = "gcc" ]
then
	WARNINGS="$WARNINGS -Wconversion -Wreturn-local-addr -Wrestrict -Wnull-dereference -Wjump-misses-init"
fi
LIBRARIES="-pthread -lm"
LIBRARIES="$LIBRARIES `curl-config --cflags --libs`"
LIBRARIES="$LIBRARIES `pkg-config --cflags --libs sqlite3`"
LIBRARIES="$LIBRARIES `pkg-config --cflags --libs freetype2`"
LIBRARIES="$LIBRARIES `sdl2-config --cflags --static-libs` -lGL"
REQUIRED_MACROS="-DCONFIG_DIRECTORY_PATH=\"$HOME/.config/$BIN\""
FLAGS="$CFLAGS $WARNINGS $LIBRARIES $REQUIRED_MACROS"

if [ $# -ge 1 ] && [ "$1" = "--release" ]
then
	RELEASE="-O2"
	FLAGS="$FLAGS $RELEASE"
	shift 1
else
	DEBUG="-DDEBUG -g3 -O0 -fno-omit-frame-pointer -fsanitize=undefined -fsanitize-undefined-trap-on-error"
	FLAGS="$FLAGS $DEBUG"
fi

if [ $# -ge 1 ]
then
	FLAGS="$FLAGS $@"
fi

$COMPILER src/main.c $FLAGS -o $BIN
