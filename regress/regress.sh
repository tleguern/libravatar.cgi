#!/bin/sh
# Tristan Le Guern <tleguern@bouledef.eu>
# Public domain

testhttpcode() {
	local verb="$1"; shift
	local path="$1"; shift
	local desiredcode="$1"
	local error=0
	local tmp=libravatar.test.png
	local code=

	curl -sS -X "$verb" -i "$baseurl/$path" 2>/dev/null\
	    | grep -v -e Date: -e Server: > "$tmp"
	code=$(grep -a 'HTTP/1.1' "$tmp" | tail -n 1 | cut -d' ' -f 2)
	if [ "$code" != "$desiredcode" ]; then
		error=1
	fi
	return $error
}

testhttpcodewithredirect() {
	local verb="$1"; shift
	local path="$1"; shift
	local desiredcode="$1"
	local error=0
	local tmp=libravatar.test.png
	local code=

	curl -sLS -X "$verb" -i "$baseurl/$path" 2>/dev/null\
	    | grep -v -e Date: -e Server: > "$tmp"
	code=$(grep -a HTTP "$tmp" | tail -n 1 | cut -d' ' -f 2)
	if [ "$code" != "$desiredcode" ]; then
		error=1
	fi
	return $error
}

testpngwidth() {
	local path="$1"; shift
	local desiredwidth="$1"

	local width=$(pnginfo -sc IHDR -f "$path" | grep width | cut -d' ' -f3)
	[ $width -eq $desiredwidth ]
}

downloadfile() {
	local path="$1"; shift
	local tmp=libravatar.test.png

	curl -sLS "$baseurl/$path" 2>/dev/null > "$tmp"
}

