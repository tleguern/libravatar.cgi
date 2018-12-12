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
baseurl=https://avatars.linux-kernel.at

defaults="404 retro identicon monsterid mp mm"

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

if ! test_have_prereq CURL; then
	skip_all="skipping all tests as curl is not installed"
	test_done
fi

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
test_expect_success NOBODY "GET on a non existing user's avatar" '
	downloadfile "avatar/$(_md5 invalid$RANDOM)?d=" && \
	test_cmp libravatar.test.png libravatar.nobody.png
'
#
# Invalid hash, size= or default=
#
# Returns the home page
test_expect_failure "GET on a small hash (adler32)" '
	testhttpcode GET avatar/$adler32hash 400
'
# Returns default size
test_expect_failure "GET on $email's avatar with an empty size" '
	testhttpcode GET avatar/$md5hash?s= 400
'
# Returns default size
test_expect_failure "GET on $email's avatar with an invalid size" '
	testhttpcode GET avatar/$md5hash?s=mille 400
'
# Returns default size
test_expect_failure "GET on $email's avatar with size 0" '
	testhttpcode GET avatar/$md5hash?s=0 400
'
# Returns size of 512
test_expect_failure "GET avatar for $email with size 1000" '
	testhttpcode GET avatar/$md5hash?s=1000 400
'
# Crash
test_expect_failure "GET avatar for $email with negative size" '
	testhttpcode GET avatar/$md5hash?s=-10 400
'
# Returns the user's avatar
test_expect_failure "GET on $email's avatar with an empty default" '
	testhttpcode GET avatar/$md5hash?d= 400
'
# Returns nobody.png
test_expect_success NOBODY "GET on a non existing user's avatar with an empty default" '
	downloadfile "avatar/$(_md5 invalid$RANDOM)?d=" && \
	test_cmp libravatar.test.png libravatar.nobody.png
'
test_expect_success "GET on $email's avatar with a wrong default" '
	testhttpcode GET avatar/$md5hash?d=unicorn 400
'
test_expect_success "GET on $email's avatar with a forced wrong default" '
	testhttpcode GET "avatar/$md5hash?d=unicorn&f=y" 400
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
