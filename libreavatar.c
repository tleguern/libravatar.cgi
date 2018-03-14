#include <sys/types.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include <kcgi.h>

int main(void) {
	struct kreq r;
	const char *page = "index";

	if (KCGI_OK != khttp_parse(&r, NULL, 0, &page, 1, 0))
		return(EXIT_FAILURE);

	khttp_head(&r, kresps[KRESP_STATUS],
	    "%s", khttps[KHTTP_200]);
	khttp_head(&r, kresps[KRESP_CONTENT_TYPE],
	    "%s", kmimetypes[r.mime]);
	khttp_body(&r);
	khttp_puts(&r, "Hello, world!");
	//khttp_puts(&r, r.fullpath);
	khttp_free(&r);

	return(EXIT_SUCCESS);
}

