#!/bin/sh

WORKD=$(cd $(dirname $0) && pwd)

baseurl=http://localhost/cgi-bin/libravatar.cgi

test_description="Libravatar.cgi API compliance"
. ./lib/regress.sh
. /usr/local/share/sharness/sharness.sh

# curl is mandatory, there is no point in running without it
command -v curl > /dev/null 2>&1 && test_set_prereq CURL
if ! test_have_prereq CURL; then
	skip_all="skipping all tests as curl is not installed"
	test_done
fi

# pnginfo is nice to have but not strictly mandatory
command -v pnginfo > /dev/null 2>&1 && test_set_prereq PNGINFO

# Hash values of the string "test_avatar"
md5hash=b4becdf161eb9e311664b548cc735a9d
sha256hash=ff0d280b9c5f5a2c55588f66a1d676ac82c5be8f14f90afec5c0b525467d059a
adler32hash=1be5049f

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
test_expect_success "GET test avatar using MD5" '
	testhttpcode GET avatar/$md5hash 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "GET test avatar using SHA256" '
	testhttpcode GET avatar/$sha256hash 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "MD5 and SHA256 should produce the same avatar" '
	downloadfile "avatar/$md5hash" &&
	mv libravatar.test.png libravatar.md5.png &&
	downloadfile "avatar/$sha256hash" &&
	mv libravatar.test.png libravatar.sha256.png &&
	test_cmp libravatar.md5.png libravatar.sha256.png
'
test_expect_success "GET test avatar with a size of 10" '
	testhttpcode GET avatar/$md5hash?s=10 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 10" '
	testpngwidth libravatar.test.png 10
'
test_expect_success "GET test avatar with a size of 200" '
	testhttpcode GET avatar/$md5hash?s=200 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 200" '
	testpngwidth libravatar.test.png 200
'
# XXX: pngscale() removes the PLTE chunk
test_expect_failure "GET on a non existing user's avatar" '
	downloadfile "avatar/$(_md5 invalid$RANDOM)" && \
	test_cmp libravatar.test.png ../config/default.png
'

#
# Invalid hash, size= or default=
#
test_expect_success "GET on an invalid hash (adler32)" '
	testhttpcode GET avatar/$adler32hash 200
'
test_expect_failure "Small hash should return nobody.png" '
	downloadfile avatar/$adler32hash &&
	test_cmp libravatar.test.png ../config/default.png
'
test_expect_success "GET test avatar with an empty size" '
	testhttpcode GET avatar/$md5hash?s= 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "GET test avatar with an invalid size" '
	testhttpcode GET avatar/$md5hash?s=mille 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "GET test avatar with size 0" '
	testhttpcode GET avatar/$md5hash?s=0 200
'
test_expect_success "GET test avatar with size 1000" '
	testhttpcode GET avatar/$md5hash?s=1000 200
'
# libravatar.cgi returns a size of 80 on any invalid size
test_expect_failure PNGINFO "Size of the fetched avatar should be 512" '
	testpngwidth libravatar.test.png 512
'
test_expect_success "GET test avatar with a negative size" '
	testhttpcode GET avatar/$md5hash?s=-10 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "GET test avatar with an empty default" '
	testhttpcode GET avatar/$md5hash?d= 200
'
test_expect_success "GET on a non existing user's avatar with an empty default" '
	testhttpcode GET "avatar/$(_sha256 invalid$RANDOM)?d=" 200
'
test_expect_failure "The fetched avatar should be nobody.png" '
	downloadfile "avatar/$(_sha256 invalid$RANDOM)?d=" &&
	test_cmp libravatar.test.png ../config/default.png
'
test_expect_success "GET test avatar with an unimplemented default" '
	testhttpcode GET avatar/$md5hash?d=unicorn 200
'
#test_expect_success "The fetched avatar should not be nobody.png" '
#	downloadfile avatar/$md5hash?d=unicorn &&
#	test_must_fail test_cmp libravatar.test.png ../config/default.png
#'
test_expect_success "GET test avatar with a forced unimplemented default" '
	testhttpcode GET "avatar/$md5hash?d=unicorn&f=y" 200
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
test_expect_failure "The fetched avatar should be nobody.png" '
	test_cmp libravatar.test.png ../config/default.png
'

# Check the list of supported `default` parameter values.
defaults=$(echo $(curl -si -X OPTIONS "$baseurl"/avatar | grep Allow | cut -d':' -f2 | tr -d '\r'))
for d in $defaults; do
	. "$WORKD/lib/$d.t"
done

test_done
