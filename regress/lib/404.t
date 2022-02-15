#
# default=404
#
test_expect_success "[404] GET test avatar with default=404" '
	testhttpcode GET avatar/$md5hash?d=404 200
'
test_expect_success "[404] GET test avatar with default=404 and forcedefault=y" '
	testhttpcode GET "avatar/$md5hash?d=404&f=y" 404
'
test_expect_success "[404] GET on a non existing user's avatar with default=404" '
	testhttpcodewithredirect GET "avatar/$(_sha256 invalid$RANDOM)?d=404" 404
'
