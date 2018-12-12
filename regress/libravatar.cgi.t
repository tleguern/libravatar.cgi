#!/bin/sh

WORKD=$(cd $(dirname $0) && pwd)

#
# Optionnal user-suplied email address.
#
. ./lib/regress.sh
if [ -z "$1" ]; then
	email=tleguern@bouledef.eu
else
	email="$1"; shift
fi
baseurl=http://localhost/cgi-bin/libravatar
mmpath="$(pwd)/mm.png"

defaults=$(echo $(curl -si -X OPTIONS "$baseurl"/avatar | grep Allow | cut -d':' -f2 | tr -d '\r'))

test_description="Libravatar.cgi API compliance"
. /usr/local/share/sharness/sharness.sh

md5hash="$(_md5 $email)"
sha256hash="$(_sha256 $email)"
adler32hash="$(_adler32 $email)"

command -v pnginfo > /dev/null 2>&1 && test_set_prereq PNGINFO
command -v curl > /dev/null 2>&1 && test_set_prereq CURL
cp "$mmpath" libravatar.mm.png && test_set_prereq MM
curl -sL "http://localhost/avatars/default.png" > libravatar.nobody.png \
    &&  test_set_prereq NOBODY
test_set_prereq FORCEDEFAULT

if ! test_have_prereq CURL; then
	skip_all="skipping all tests as curl is not installed"
	test_done
fi
#
# Test outside of API conformance
#
test_expect_success "OPTIONS on /" '
	testhttpcode OPTIONS / 200
'
test_expect_success "POST on /" '
	testhttpcode POST / 405
'
test_expect_success "GET on index" '
	testhttpcode GET /index 200
'
test_expect_success "GET on invalid path" '
	testhttpcode GET invalidpath 404
'
test_expect_success "GET on /avatar" '
	testhttpcode GET avatar 400
'
#
# Normal cases
#
test_expect_success "GET on $email's avatar" '
	testhttpcode GET avatar/$md5hash 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "GET on $email's avatar with a size of 200" '
	testhttpcode GET avatar/$md5hash?s=200 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 200" '
	testpngwidth libravatar.test.png 200
'
# pngscale() removes the PLTE chunk
test_expect_failure NOBODY "GET on a non existing user's avatar" '
	downloadfile "avatar/$(_md5 invalid$RANDOM)" && \
	test_cmp libravatar.test.png libravatar.nobody.png
'
#
# Invalid hash, size= or default=
#
# Returns nobody.png
test_expect_failure "GET on a small hash (adler32)" '
	testhttpcode GET avatar/$adler32hash 400
'
test_expect_success "GET on $email's avatar with an empty size" '
	testhttpcode GET avatar/$md5hash?s= 400
'
test_expect_success "GET on $email's avatar with an invalid size" '
	testhttpcode GET avatar/$md5hash?s=mille 400
'
test_expect_success "GET on $email's avatar with size 0" '
	testhttpcode GET avatar/$md5hash?s=0 400
'
test_expect_success "GET avatar for $email with size 1000" '
	testhttpcode GET avatar/$md5hash?s=1000 400
'
test_expect_success "GET avatar for $email with negative size" '
	testhttpcode GET avatar/$md5hash?s=-10 400
'
test_expect_success "GET on $email's avatar with an empty default" '
	testhttpcode GET avatar/$md5hash?d= 400
'
test_expect_success "GET on $email's avatar with a wrong default" '
	testhttpcode GET avatar/$md5hash?d=unicorn 400
'
test_expect_success "GET on $email's avatar with a forced wrong default" '
	testhttpcode GET "avatar/$md5hash?d=unicorn&f=y" 400
'
#
# default=http://cdn.libravatar.org/nobody/80.png
#
test_expect_success "GET on a non existing user's avatar with d=\$URL (no follow)" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=http%3A%2F%2Fcdn.libravatar.org%2Fnobody.png" 307
'
test_expect_success "GET on a non existing user's avatar with d=\$URL (follow)" '
	testhttpcodewithredirect GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=http%3A%2F%2Fcdn.libravatar.org%2Fnobody%2F80.png" 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_failure NOBODY "The fetched avatar should be nobody.png" '
	test_cmp libravatar.test.png libravatar.nobody.png
'

for d in $defaults; do
	. "$WORKD/lib/$d.t"
done

test_done
