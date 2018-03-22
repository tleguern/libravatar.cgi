#!/bin/sh
# Tristan Le Guern <tleguern@bouledef.eu>
# Public domain

cd $(dirname $0)
n=0

testcode() {
	verb="$1"; shift
	path="$1"; shift
	desiredcode="$1"; shift

	title="$verb on $path"
	n=$((n + 1))

	tmp=$(mktemp -t libravatar.XXXXXXXX)
	curl -X "$verb" -i "http://localhost/cgi-bin/$path" 2>/dev/null\
	    | grep -v -e Date -e Server > "$tmp"
	code=$(head -n 1 "$tmp" | cut -d' ' -f 2)
	if [ "$code" != "$desiredcode" ]; then
		message="not ok $n - $title got HTTP $code but expected $desiredcode"
	fi
	rm $tmp
	echo ${message:-"ok $n - $title"}
}

testfile() {	
	path="$1"; shift
	desiredfile="$1"; shift

	title="check for $desiredfile"
	n=$((n + 1))

	tmp=$(mktemp -t libravatar.XXXXXXXX)
	ftp -iMV -o "$tmp" "http://localhost/cgi-bin/$path" 2>/dev/null 
	if ! diff -a "$desiredfile" "$tmp" > /dev/null; then
		message="not ok $n - $title wrong file served"
	fi
	rm $tmp
	echo ${message:-"ok $n - $title"}
}

echo "TAP version 13"
echo "1..17"

testcode OPTIONS libravatar 200
testcode POST libravatar 405
testcode GET libravatar/index 200
testcode GET libravatar/invalidpath 404
testcode GET libravatar/avatar 400
testcode GET "libravatar/avatar/4751ed9aae86881d2b45dd0512c3e14a?s=" 400
testcode GET "libravatar/avatar/4751ed9aae86881d2b45dd0512c3e14a?s=0" 400
testcode GET "libravatar/avatar/4751ed9aae86881d2b45dd0512c3e14a?s=1000" 400
testcode GET "libravatar/avatar/4751ed9aae86881d2b45dd0512c3e14a?s=mille" 400
testcode GET "libravatar/avatar/4751ed9aae86881d2b45dd0512c3e14a?s=200" 200
testcode GET "libravatar/avatar/4751ed9aae86881d2b45dd0512c3e14a?d=" 400
testcode GET "libravatar/avatar/4751ed9aae86881d2b45dd0512c3e14a?d=404" 200
testcode GET "libravatar/avatar/4751ed9aae86881d2b45dd0512c3e14a?d=404&f=y" 404
testcode GET "libravatar/avatar/invalidinvalidinvalidinvalidinva?d=404" 404
testcode GET "libravatar/avatar/invalidinvalidinvalidinvalidinva?d=http%3A%2F%2Flocalhost%2Favatars%2Fmm.jpeg" 307
testfile "libravatar/avatar/invalidinvalidinvalidinvalidinva?d=mm" ../img/mm.jpeg
testfile "libravatar/avatar/invalidinvalidinvalidinvalidinva?d=blank" ../img/blank.png

