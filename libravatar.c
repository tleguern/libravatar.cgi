/*
 * Copyright (c) 2015,2018 Tristan Le Guern <tleguern@bouledef.eu>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <kcgi.h>
#include <kcgihtml.h>

#include "libravatar.h"

enum page {
	PAGE_INDEX,
	PAGE_AVATAR,
	PAGE__MAX
};

enum defaultstyle {
	DEFAULT_NONE,
	DEFAULT_URL,
	DEFAULT_404,
	DEFAULT_MM,
	DEFAULT_BLANK,
	DEFAULT__MAX
};

struct avatar {
	int		 d;	/* default */
	int		 f;	/* forcedefault */
	size_t		 s;	/* size */
	char		*hash;
	char		*url;
};

static void
http_start(struct kreq *r, enum khttp http)
{
	khttp_head(r, kresps[KRESP_STATUS],
	    "%s", khttps[http]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE],
	    "%s", kmimetypes[r->mime]);
	khttp_body(r);
}

static char *
urldecode(const char *cp)
{
	char	*p;
	char	 ch;
	size_t	 nm, sz, cur;

	if (NULL == cp)
		return(NULL);

	sz = strlen(cp) + 1;
	nm = 0;
	for (cur = 0; '\0' != (ch = cp[cur]); cur++)
		if ('%' == ch)
			nm += 1;
	sz = sz - nm * 2;
	if (NULL == (p = calloc(sz, 1)))
		return(NULL);

	for (cur = 0; '\0' != (ch = *cp); cp++, cur++) {
		if ('%' == ch) {
			char e, buf[3];
			buf[0] = *(cp + 1);
			buf[1] = *(cp + 2);
			buf[2] = '\0';
			/* treat invalid triplets as regular text */
			if (1 == sscanf(buf, "%hhx", &e)) {
				(void)snprintf(p + cur, 2, "%c", e);
				cp += 2;
				continue;
			}
		} else if ('+' == ch) {
			ch = ' ';
		}
		p[cur] = ch;
	}
	return(p);
}

static void
page_index(struct kreq *r)
{
	struct khtmlreq h;

	http_start(r, KHTTP_200);
	khtml_open(&h, r, KHTML_PRETTY);
	khtml_elem(&h, KELEM_DOCTYPE);
	khtml_elem(&h, KELEM_HTML);
	khtml_elem(&h, KELEM_BODY);
	khtml_elem(&h, KELEM_P);
	khtml_puts(&h, "This web interface delivers profile picture (avatar)."
	    " The query syntax is as follow:");
	khtml_closeelem(&h, 1);
	khtml_elem(&h, KELEM_PRE);
	khtml_puts(&h, "http://");
	khtml_puts(&h, r->host);
	khtml_puts(&h, "/avatar/hash?s=size;d=default;f=y;r=g");
	khtml_closeelem(&h, 1);
	khtml_elem(&h, KELEM_UL);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "hash: md5 or sha1 hash of an email address.\n");
	khtml_closeelem(&h, 1);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "s/size: The file size in pixels, must be between 1 and 512. The default value is 80.\n");
	khtml_closeelem(&h, 1);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "d/default: Default replacement for missing images. Can be an URL or the following values:\n");
	khtml_elem(&h, KELEM_UL);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "404: Do not load any image and return an HTTP 404 response.\n");
	khtml_closeelem(&h, 1);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "mm: Load a simple and static shadow silhouette.\n");
	khtml_closeelem(&h, 1);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "blank: Load a transparent PNG image.\n");
	khtml_closeelem(&h, 2);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "r/rating: Kept for compatibility with Gravatar but ignored.\n");
	khtml_closeelem(&h, 1);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "f/forcedefault: Force the default image even if the hash has a match.\n");

	khtml_close(&h);
}

static void
page_avatar(struct kreq *r)
{
	size_t		 dataz;
	enum kmime	 mime;
	char		 filename[100];
	unsigned char	*data;
	struct avatar	*avatar;
	FILE		*s;

	dataz = 0;
	data = NULL;
	avatar = ((struct avatar *)r->arg);
	s = NULL;
	if (0 == avatar->f) {
		snprintf(filename, sizeof(filename), "/htdocs/avatars/%s.png",
		    avatar->hash);
		mime = KMIME_IMAGE_PNG;
		s = fopen(filename, "r");
	}
	if (1 == avatar->f || NULL == s) {
		switch (avatar->d) {
		case DEFAULT_404:
			http_start(r, KHTTP_404);
			return;
		case DEFAULT_BLANK:
			if (-1 == blank(avatar->s, &data, &dataz)) {
				http_start(r, KHTTP_500);
				return;
			}
			mime = KMIME_IMAGE_PNG;
			break;
		case DEFAULT_MM:
			if (-1 == mm(avatar->s, &data, &dataz)) {
				http_start(r, KHTTP_500);
				return;
			}
			mime = KMIME_IMAGE_PNG;
			break;
		case DEFAULT_URL:
			khttp_head(r, kresps[KRESP_STATUS],
			    "%s", khttps[KHTTP_307]);
			khttp_head(r, kresps[KRESP_LOCATION],
			    "%s", avatar->url);
			khttp_body(r);
			return;
		default:
			if (NULL == (s = fopen(_PATH_DEFAULT, "r"))) {
				http_start(r, KHTTP_500);
				return;
			}
			mime = KMIME_IMAGE_PNG;
			break;
		}
	}
	/* Only resize if an image is found or if the default one is served */
	if (NULL != s || DEFAULT_NONE == avatar->d) {
		if (0 == (dataz = pngscale(s, &data, avatar->s))) {
			fclose(s);
			http_start(r, KHTTP_500);
			return;
		}
	}
	khttp_head(r, kresps[KRESP_STATUS],
	    "%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE],
	    "%s", kmimetypes[mime]);
	khttp_head(r, kresps[KRESP_ACCESS_CONTROL_ALLOW_ORIGIN], "*");
	khttp_head(r, kresps[KRESP_CACHE_CONTROL], "max-age=86400");
	khttp_body(r);
	khttp_write(r, data, dataz);
	free(data);
	if (NULL != s) {
		fclose(s);
	}
}

static enum khttp
sanitize(struct kreq *r)
{
	size_t		 i;
	const char	*err;
	struct avatar	*avatar;

	avatar = ((struct avatar *)r->arg);
	avatar->hash = r->path;

	for (i = 0; i < r->fieldsz; i++) {
		if (strcmp(r->fields[i].key, "s") == 0
		    || strcmp(r->fields[i].key, "size") == 0) {
			avatar->s = strtonum(r->fields[i].val, 1, 512, &err);
			if (err != NULL)
				avatar->s = 80;
		} else if (strcmp(r->fields[i].key, "d") == 0
		    || strcmp(r->fields[i].key, "default") == 0) {
			if (strcmp(r->fields[i].val, "404") == 0) {
				avatar->d = DEFAULT_404;
			} else if (strcmp(r->fields[i].val, "mm") == 0) {
				avatar->d = DEFAULT_MM;
			} else if (strcmp(r->fields[i].val, "mp") == 0) {
				avatar->d = DEFAULT_MM;
			} else if (strcmp(r->fields[i].val, "blank") == 0) {
				avatar->d = DEFAULT_BLANK;
			} else if (strncmp(r->fields[i].val, "http", 4) == 0) {
				avatar->d = DEFAULT_URL;
				if (NULL ==
				    (avatar->url = urldecode(r->fields[i].val)))
					avatar->d = DEFAULT__MAX;
			}
		} else if (strcmp(r->fields[i].key, "f") == 0
		    || strcmp(r->fields[i].key, "forcedefault") == 0) {
			if (strcmp(r->fields[i].val, "y") == 0)
				avatar->f = 1;
		} else if (strcmp(r->fields[i].key, "r") == 0
		    || strcmp(r->fields[i].key, "rating") == 0) {
			continue;
		} else {
			return(KHTTP_400);
		}
	}
	return(KHTTP_200);
}

int
main(void)
{
	struct kreq r;
	enum kcgi_err err;
	const char *pages[PAGE__MAX] = {"index", "avatar"};
	struct avatar avatar;

	avatar.d = DEFAULT_NONE;
	avatar.f = 0;
	avatar.s = 80;
	avatar.hash = NULL;

#if HAVE_PLEDGE
	if (-1 == pledge("stdio proc rpath unveil", NULL))
		return 0;
#endif
	err = khttp_parsex(&r, ksuffixmap, kmimetypes, KMIME__MAX, NULL, 0,
	    pages, PAGE__MAX, KMIME_TEXT_HTML, PAGE_INDEX, &avatar,
	    NULL, 0, NULL);
	if (KCGI_OK != err)
		return(EXIT_FAILURE);
#if HAVE_PLEDGE
	if (-1 == unveil("/htdocs/avatars/", "r"))
		return 0;
	if (-1 == unveil(NULL, NULL))
		return 0;
	if (-1 == pledge("stdio rpath", NULL))
		return 0;
#endif

	if (KMETHOD_OPTIONS == r.method) {
		if (PAGE_AVATAR == r.page) {
			khttp_head(&r, kresps[KRESP_ALLOW], "404 blank mm mp");
		} else {
			khttp_head(&r, kresps[KRESP_ALLOW], "OPTIONS GET");
		}
		http_start(&r, KHTTP_200);
	} else if (KMETHOD_GET != r.method) {
		http_start(&r, KHTTP_405);
	} else if (PAGE__MAX == r.page) {
		http_start(&r, KHTTP_404);
	} else if (PAGE_INDEX == r.page) {
		if (KMIME_TEXT_HTML == r.mime)
			page_index(&r);
		else
			http_start(&r, KHTTP_415);
	} else if (PAGE_AVATAR == r.page) {
		enum khttp san;

		if (NULL == r.path || strlen(r.path) == 0)
			http_start(&r, KHTTP_400);
		else if (KHTTP_200 != (san = sanitize(&r)))
			http_start(&r, san);
		else
			page_avatar(&r);
	}
	free(avatar.url);
	khttp_free(&r);
	return(EXIT_SUCCESS);
}

