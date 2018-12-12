#
# default=identicon
#
test_expect_success "[identicon] GET on a non existing user's avatar with d=identicon" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=identicon" 200
'
test_expect_success PNGINFO "[identicon] Size of the fetched identicon avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
