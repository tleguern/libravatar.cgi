#!/bin/sh

#
# Optionnal user-suplied email address.
#
. ./regress.sh
if [ -z "$1" ]; then
	email=tleguern@bouledef.eu
else
	email="$1"; shift
fi
baseurl=https://avatars.linux-kernel.at

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
test_expect_failure "GET on $email's avatar with a wrong default" '
	testhttpcode GET avatar/$md5hash?d=unicorn 400
'
test_expect_failure "GET on $email's avatar with a forced wrong default" '
	testhttpcode GET "avatar/$md5hash?d=unicorn&f=y" 400
'
#
# default=404
#
test_expect_success "GET on $email's avatar with default=404" '
	testhttpcode GET avatar/$md5hash?d=404 200
'
test_expect_success "GET on $email's avatar with default=404 and forcedefault=y" '
	testhttpcode GET "avatar/$md5hash?d=404&f=y" 404
'
test_expect_success "GET on a non existing user's avatar with default=404" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?d=404" 404
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
#
# default=mm
#
test_expect_success "GET on a non existing user's avatar with d=mm" '
	testhttpcodewithredirect GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=mm" 200
'
test_expect_success PNGINFO "Size of the fetched mm avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success MM "The fetched avatar should be mm.png" '
	downloadfile "avatar/$(_md5 invalid$RANDOM)?s=80&d=mm" && \
	test_cmp libravatar.test.png libravatar.mm.png
'
#
# default=mp (Gravatar decided to add a synonym without explanation)
#
test_expect_success "GET on a non existing user's avatar with d=mp" '
	testhttpcodewithredirect GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=mp" 200
'
test_expect_success PNGINFO "Size of the fetched mp avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success MM "The fetched mp avatar should be mm.png" '
	downloadfile "avatar/$(_md5 invalid$RANDOM)?s=80&d=mp" && \
	test_cmp libravatar.test.png libravatar.mm.png
'
#
# default=monsterid
#
test_expect_success "GET on a non existing user's avatar with d=monsterid" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=monsterid" 200
'
test_expect_success PNGINFO "Size of the fetched monsterid avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
#
# default=identicon
#
test_expect_success "GET on a non existing user's avatar with d=identicon" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=identicon" 200
'
test_expect_success PNGINFO "Size of the fetched identicon avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
#
# default=retro
#
test_expect_success "GET on a non existing user's avatar with d=retro" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=retro" 200
'
test_expect_success PNGINFO "Size of the fetched retro avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_done
