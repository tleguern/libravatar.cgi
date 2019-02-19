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
baseurl=https://www.libravatar.org

defaults="404 retro identicon monsterid mp mm robohash"

test_description="ivatar API compliance"
. /usr/local/share/sharness/sharness.sh

md5hash="$(_md5 $email)"
sha256hash="$(_sha256 $email)"
adler32hash="$(_adler32 $email)"

command -v pnginfo > /dev/null 2>&1 && test_set_prereq PNGINFO
command -v curl > /dev/null 2>&1 && test_set_prereq CURL
curl -sL "$baseurl/static/img/mm/80.png" > libravatar.mm.png \
    && test_set_prereq MM
curl -sL "$baseurl/static/img/nobody/80.png" > libravatar.nobody.png \
    &&  test_set_prereq NOBODY
test_set_prereq FORCEDEFAULT

if ! test_have_prereq CURL; then
	skip_all="skipping all tests as curl is not installed"
	test_done
fi

#
# Normal cases
#
test_expect_success "GET on $email's avatar using MD5" '
	testhttpcode GET avatar/$md5hash 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "GET on $email's avatar using SHA256" '
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
test_expect_success "GET on $email's avatar with a size of 10" '
	testhttpcode GET avatar/$md5hash?s=10 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 10" '
	testpngwidth libravatar.test.png 10
'
test_expect_success "GET on $email's avatar with a size of 200" '
	testhttpcode GET avatar/$md5hash?s=200 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 200" '
	testpngwidth libravatar.test.png 200
'
test_expect_success NOBODY "GET on a non existing user's avatar" '
	downloadfile "avatar/$(_md5 invalid$RANDOM)?d=" && \
	test_cmp libravatar.test.png libravatar.nobody.png
'
#
# Invalid hash, size= or default=
#
# Returns an HTML error page with code 200, by design. issue #43
test_expect_failure "GET on a small hash (adler32)" '
	testhttpcode GET avatar/$adler32hash 200
'
test_expect_failure NOBODY "Small hash should return nobody.png" '
	downloadfile avatar/$adler32hash &&
	test_cmp libravatar.test.png libravatar.nobody.png
'
test_expect_success "GET on $email's avatar with an empty size" '
	testhttpcode GET avatar/$md5hash?s= 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "GET on $email's avatar with an invalid size" '
	testhttpcode GET avatar/$md5hash?s=mille 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "GET on $email's avatar with size 0" '
	testhttpcode GET avatar/$md5hash?s=0 200
'
test_expect_success "GET avatar for $email with size 1000" '
	testhttpcode GET avatar/$md5hash?s=1000 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 512" '
	testpngwidth libravatar.test.png 512
'
test_expect_success "GET avatar for $email with negative size" '
	testhttpcode GET avatar/$md5hash?s=-10 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "GET on $email's avatar with an empty default" '
	testhttpcode GET avatar/$md5hash?d= 200
'
test_expect_success "GET on a non existing user's avatar with an empty default" '
	testhttpcodewithredirect GET "avatar/$(_sha256 invalid$RANDOM)?d=" 200
'
test_expect_success NOBODY "The fetched avatar should be nobody.png" '
	downloadfile "avatar/$(_sha256 invalid$RANDOM)?d=" &&
	test_cmp libravatar.test.png libravatar.nobody.png
'
test_expect_success "GET on $email's avatar with a wrong default" '
	testhttpcode GET avatar/$md5hash?d=unicorn 200
'
# issue #44
test_expect_failure "GET on $email's avatar with a forced wrong default" '
	testhttpcode GET "avatar/$md5hash?d=unicorn&f=y" 200
'
#
# default=https://avatars.linux-kernel.at/static/img/nobody/80.png
#
test_expect_success "GET on a non existing user's avatar with d=\$URL (no follow)" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=https%3A%2F%2favatars.linux-kernel.at%2Fstatic%2Fimg%2Fnobody%2F80.png" 302
#'
test_expect_success "GET on a non existing user's avatar with d=\$URL (follow)" '
	testhttpcodewithredirect GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=https%3A%2F%2favatars.linux-kernel.at%2Fstatic%2Fimg%2Fnobody%2F80.png" 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success NOBODY "The fetched avatar should be nobody.png" '
	downloadfile "avatar/$(_md5 invalid$RANDOM)?s=80&d=https%3A%2F%2favatars.linux-kernel.at%2Fstatic%2Fimg%2Fnobody%2F80.png" && \
	test_cmp libravatar.test.png libravatar.nobody.png
'

for d in $defaults; do
	. "$WORKD/lib/$d.t"
done

test_done
