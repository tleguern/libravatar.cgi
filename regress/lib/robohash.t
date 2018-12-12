#
# default=robohash
#
test_expect_success "[robohash] GET on a non existing user's avatar with d=robohash" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=robohash" 200
'
test_expect_success PNGINFO "[robohash] Size of the fetched robohash avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
