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
baseurl=http://localhost/cgi-bin/libravatar
. ./regress.sh

test_description="Libravatar.cgi API compliance"
. /usr/local/share/sharness/sharness.sh

command -v pnginfo > /dev/null 2>&1 && test_set_prereq PNGINFO
command -v curl > /dev/null 2>&1 && test_set_prereq CURL

if ! test_have_prereq CURL; then
	skip_all="skipping all tests as curl is not installed"
	test_done
fi
#
# Test outside of API conformance
#
test_expect_success "OPTIONS on /" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode OPTIONS / 200
"
test_expect_success "POST on /" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode POST / 405
"
test_expect_success "GET on index" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET index 200
"
test_expect_success "GET on invalid path" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET invalidpath 404
"
test_expect_success "GET on /avatar" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET avatar 400
"
#
# Normal cases
#
test_expect_success "GET on $email's avatar" "
	testhttpcode GET avatar/$hash 200
"
test_expect_success "GET on $email's avatar with a size of 200" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET avatar/$hash?s=200 200
"
#
# Invalid size= or default=
#
test_expect_success "GET on $email's avatar with an empty size" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET avatar/$hash?s= 400
"
test_expect_success "GET on $email's avatar with an invalid size" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET avatar/$hash?s=mille 400
"
test_expect_success "GET on $email's avatar with size 0" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET avatar/$hash?s=0 400
"
test_expect_success "GET avatar for $email with size 1000" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET avatar/$hash?s=1000 400
"
test_expect_success "GET on $email's avatar with an empty default" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET avatar/$hash?d= 400
"
#
# default=404
#
test_expect_success "GET on $email's avatar with default=404" "
 	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET avatar/$hash?d=404 200
"
test_expect_success "GET on $email's avatar with default=404 and forcedefault=y" "
 	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET 'avatar/$hash?d=404&f=y' 404
"
test_expect_success "GET on a non existing user's avatar with default=404" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET avatar/'$(_md5 invalid$RANDOM)'?d=404 404
"
#
# default=http://cdn.libravatar.org/nobody/80.png
#
test_expect_success "GET on a non existing user's avatar with d=\$URL (no follow)" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET 'avatar/'$(_md5 invalid$RANDOM)'?s=80&d=http%3A%2F%2Fcdn.libravatar.org%2Fnobody.png' 307
"
test_expect_success "GET on a non existing user's avatar with d=\$URL (follow)" "
	testhttpcodewithredirect GET 'avatar/'$(_md5 invalid$RANDOM)'?s=80&d=http%3A%2F%2Fcdn.libravatar.org%2Fnobody%2F80.png' 200
"
test_expect_success PNGINFO "Size of the fetched avatar should be 80" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testpngwidth /tmp/libravatar.test.png 80
"
#
# default=mm
#
test_expect_success "GET on a non existing user's avatar with d=mm" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testhttpcode GET 'avatar/'$(_md5 invalid$RANDOM)'?s=80&d=mm' 200
"
#
# default=blank
#
test_expect_success "GET on a non existing user's avatar with d=blank" "
	testhttpcode GET 'avatar/'$(_md5 invalid$RANDOM)'?s=80&d=blank' 200
"
test_expect_success PNGINFO "Size of the fetched blank avatar should be 80" "
	test_when_finished 'rm -f -- /tmp/libravatar.test.png' && \
	testpngwidth /tmp/libravatar.test.png 80
"
test_done
