#
# default=mp (Gravatar decided to add a synonym without explanation)
#
test_expect_success "[mp] GET on a non existing user's avatar with d=mp" '
	testhttpcodewithredirect GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=mp" 200
'
test_expect_success PNGINFO "[mp] Size of the fetched mp avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
test_expect_success MM "[mp] The fetched mp avatar should be mm.png" '
	downloadfile "avatar/$(_md5 invalid$RANDOM)?s=80&d=mp" && \
	test_cmp libravatar.test.png libravatar.mm.png
'
