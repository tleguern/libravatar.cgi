#
# default=blank
#
test_expect_success "[blank] GET on a non existing user's avatar with d=blank" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=blank" 200
'
test_expect_success PNGINFO "[blank] Size of the fetched blank avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
