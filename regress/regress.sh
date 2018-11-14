#!/bin/sh
# Tristan Le Guern <tleguern@bouledef.eu>
# Public domain

testhttpcode() {
	verb="$1"; shift
	path="$1"; shift
	desiredcode="$1"
	error=0

	tmp=libravatar.test.png
	curl -sS -X "$verb" -i "$baseurl/$path" 2>/dev/null\
	    | grep -v -e Date: -e Server: > "$tmp"
	code=$(grep -a 'HTTP/1.1' "$tmp" | tail -n 1 | cut -d' ' -f 2)
	if [ "$code" != "$desiredcode" ]; then
		error=1
	fi
	return $error
}

testhttpcodewithredirect() {
	verb="$1"; shift
	path="$1"; shift
	desiredcode="$1"
	error=0

	tmp=libravatar.test.png
	curl -sLS -X "$verb" -i "$baseurl/$path" 2>/dev/null\
	    | grep -v -e Date: -e Server: > "$tmp"
	code=$(grep -a HTTP "$tmp" | tail -n 1 | cut -d' ' -f 2)
	if [ "$code" != "$desiredcode" ]; then
		error=1
	fi
	return $error
}

testpngwidth() {
	path="$1"; shift
	desiredwidth="$1"

	width=$(pnginfo -s -c IHDR -f "$path" | grep width | cut -d' ' -f3)
	[ $width -eq $desiredwidth ]
}

downloadfile() {
	path="$1"; shift

	tmp=libravatar.test.png
	curl -sLS -X "$verb" "$baseurl/$path" 2>/dev/null > "$tmp"
}

