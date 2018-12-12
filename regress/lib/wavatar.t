#
# default=wavatar
#
test_expect_success "[wavatar] GET on a non existing user's avatar with d=wavatar" '
	testhttpcode GET "avatar/$(_md5 invalid$RANDOM)?s=80&d=wavatar" 200
'
test_expect_success PNGINFO "[wavatar] Size of the fetched wavatar avatar should be 80" '
	testpngwidth libravatar.test.png 80
'
