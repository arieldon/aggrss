#!/usr/bin/env sh

set -eu

clang -g3 -DPRINT_TREE_SUPPORT -Isrc/ tests/test_rss.c -o tests/test_rss && ./tests/test_rss && diff -qrEZwB ./tests/actual_outputs ./tests/desired_outputs
