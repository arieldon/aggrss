#!/usr/bin/env sh

set -eu

Download()
{
	Link="$1"
	Hostname=`echo "$Link" | cut -d'/' -f3`
	FilePath="./tests/inputs/$Hostname"
	echo "started downloading $Link"
	curl -sS --max-time 100 -L -o "$FilePath" "$Link"
	echo "finished downloading $Link"
}
export -f Download

mkdir -p ./tests/inputs
sqlite3 .feeds.db 'select link from feeds;' | xargs -P`nproc` -I {} sh -c 'Download "{}"'
