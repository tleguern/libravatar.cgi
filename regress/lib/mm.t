#
# default=mm
#
test_expect_success "[mm] GET on a non existing user's avatar with d=mm" '
	testhttpcode GET "avatar/$(_sha256 invalid$RANDOM)?s=80&d=mm" 200
'
test_expect_success PNGINFO "[mm] Size of the fetched mm avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success MM "[mm] The fetched avatar should be mm.png" '
	downloadfile "avatar/$(_sha256 invalid$RANDOM)?s=80&d=mm" && \
	test_cmp libravatar.test.png libravatar.mm.png
'
