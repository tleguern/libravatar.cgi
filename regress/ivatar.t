#!/bin/sh

#
# Optionnal user-suplied email address.
#
_md5() {
	if command -v md5 > /dev/null; then
		md5 -qs "$1"
	else
		echo -n "$1" | md5sum | cut -d' ' -f1
	fi
}
if [ -z "$1" ]; then
	email=tleguern@bouledef.eu
else
	email="$1"; shift
fi
hash="$(_md5 $email)"
baseurl=https://avatars.linux-kernel.at
. ./regress.sh

test_description="ivatar API compliance"
. /usr/local/share/sharness/sharness.sh

command -v pnginfo > /dev/null 2>&1 && test_set_prereq PNGINFO
command -v curl > /dev/null 2>&1 && test_set_prereq CURL
curl -sL "$baseurl/mm/80.png" > libravatar.mm.png \
    && test_set_prereq MM
curl -sL http://cdn.libravatar.org/nobody/80.png > libravatar.nobody.png \
    &&  test_set_prereq NOBODY

if ! test_have_prereq CURL; then
	skip_all="skipping all tests as curl is not installed"
	test_done
fi

#
# Normal cases
#
test_expect_success "GET on $email's avatar" '
	testhttpcode GET avatar/$hash 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success "GET on $email's avatar with a size of 200" '
	testhttpcode GET avatar/$hash?s=200 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 200" '
	testpngwidth libravatar.test.png 200
'
#
# Invalid size= or default=
#
test_expect_success "GET on $email's avatar with an empty size" '
	testhttpcode GET avatar/$hash?s= 400
'
test_expect_success "GET on $email's avatar with an invalid size" '
	testhttpcode GET avatar/$hash?s=mille 400
'
test_expect_success "GET on $email's avatar with size 0" '
	testhttpcode GET avatar/$hash?s=0 400
'
test_expect_success "GET avatar for $email with size 1000" '
	testhttpcode GET avatar/$hash?s=1000 400
'
test_expect_success "GET on $email's avatar with an empty default" '
	testhttpcode GET avatar/$hash?d= 400
'
#
# default=404
#
test_expect_success "GET on $email's avatar with default=404" '
	testhttpcode GET avatar/$hash?d=404 200
'
test_expect_success "GET on $email's avatar with default=404 and forcedefault=y" '
	testhttpcode GET "avatar/$hash?d=404&f=y" 404
'
test_expect_success "GET on a non existing user's avatar with default=404" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?d=404" 404
'
#
# default=http://cdn.libravatar.org/nobody/80.png
#
test_expect_success "GET on a non existing user's avatar with d=\$URL (no follow)" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=http%3A%2F%2Fcdn.libravatar.org%2Fnobody.png" 307
'
test_expect_success "GET on a non existing user's avatar with d=\$URL (follow)" 
'
	testhttpcodewithredirect GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=http%3A%2F%2Fcdn.libravatar.org%2Fnobody%2F80.png" 200
'
test_expect_success PNGINFO "Size of the fetched avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success NOBODY "The fetched avatar should be nobody.png" '
	downloadfile "avatar/$(_md5 invalid$RANDOM)?s=80&d=http%3A%2F%2Fcdn.libravatar.org%2Fnobody%2F80.png" && \
	test_cmp libravatar.test.png libravatar.nobody.png
'
#
# default=mm
#
test_expect_success "GET on a non existing user's avatar with d=mm" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=mm" 200
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
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=mp" 200
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
