#!/usr/bin/env sh

set -eux

test ! -x ./aggrss && ./compile.sh

mkdir -p ~/.config/aggrss/
cp -r ./assets/ ~/.config/aggrss

mkdir -p ~/.local/bin
cp -r ./aggrss ~/.local/bin
