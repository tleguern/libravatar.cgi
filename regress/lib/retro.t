#
# default=retro
#
test_expect_success "[retro] GET on a non existing user's avatar with d=retro" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=retro" 200
'
test_expect_success PNGINFO "[retro] Size of the fetched retro avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
