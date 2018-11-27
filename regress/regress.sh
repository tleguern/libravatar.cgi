#!/bin/sh
# Tristan Le Guern <tleguern@bouledef.eu>
# Public domain

_md5() {
	if command -v md5 > /dev/null; then
		md5 -qs "$1"
	else
		echo -n "$1" | md5sum | cut -d' ' -f1
	fi
}

_sha256() {
	if command -v sha256 > /dev/null; then
		sha256 -qs "$1"
	else
		echo -n "$1" | sha256sum | cut -d' ' -f1
	fi
}

_adler32() {
	local _value="$*"

	local _s1=1
	local _s2=0
	local _i=0
	for _i in $(printf "$_value" | sed 's/./& /g'); do
		local _b=0

		_b=$(printf "%d" "'$_i")
		_s1=$(( (_s1 + _b) % 65521 ))
		_s2=$(( (_s2 + _s1) % 65521 ))
	done
	printf "%08x\n" $(( (_s2 << 16) + _s1))
}

testhttpcode() {
	local verb="$1"; shift
	local path="$1"; shift
	local desiredcode="$1"
	local error=0
	local tmp=libravatar.test.png
	local code=

	curl -sS -X "$verb" -i "$baseurl/$path" 2>/dev/null\
	    | grep -a -v -e Date: -e Server: > "$tmp"
	code=$(grep -a HTTP "$tmp" | head -n 1 | cut -d' ' -f 2)
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
	    | grep -a -v -e Date: -e Server: > "$tmp"
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

