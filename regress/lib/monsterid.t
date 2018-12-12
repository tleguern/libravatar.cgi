#
# default=monsterid
#
test_expect_success "[monsterid] GET on a non existing user's avatar with d=monsterid" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=monsterid" 200
'
test_expect_success PNGINFO "[monsterid] Size of the fetched monsterid avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
