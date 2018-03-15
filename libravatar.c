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

#include <sys/types.h>

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

enum page {
	PAGE_INDEX,
	PAGE_AVATAR,
	PAGE__MAX
};

struct avatar {
	int		 d;	/* default flag */
	size_t		 s;	/* file size in pixel */
	char		*hash;
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
	khtml_puts(&h, "/avatar/hash?s=size;d=retro");
	khtml_closeelem(&h, 1);
	khtml_elem(&h, KELEM_UL);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "hash: md5 or sha1 hash of an email address.\n");
	khtml_closeelem(&h, 1);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "s/size: The file size in pixels, must be between 1 and 512. The default value is 80.\n");
	khtml_closeelem(&h, 1);
	khtml_elem(&h, KELEM_LI);
	khtml_puts(&h, "d/default: Default replacement for missing images. Only accepts 'retro'. Optionnal.\n");
	khtml_close(&h);
}

static void
page_avatar(struct kreq *r)
{
	int		 fd;
	size_t		 linez;
	char		 filename[100];
	char		 line[1500];
	struct avatar	*avatar;

	avatar = ((struct avatar *)r->arg);
	snprintf(filename, sizeof(filename), "/htdocs/avatars/%s.jpeg",
	    avatar->hash);
	if (-1 == (fd = open(filename, O_RDONLY))) {
		if (avatar->d == 0) {
			http_start(r, KHTTP_404);
			return;
		} else {
			/* To be implemented */
			http_start(r, KHTTP_404);
			return;
		}
	}
	khttp_head(r, kresps[KRESP_STATUS],
	    "%s", khttps[KHTTP_200]);
	khttp_head(r, kresps[KRESP_CONTENT_TYPE],
	    "%s", kmimetypes[KMIME_IMAGE_JPEG]);
	khttp_head(r, kresps[KRESP_ACCESS_CONTROL_ALLOW_ORIGIN], "*");
	khttp_head(r, kresps[KRESP_CACHE_CONTROL], "max-age=300");
	khttp_body(r);
	while ((linez = read(fd, line, 1000)) > 0) {
		khttp_write(r, line, linez);
	}
	close(fd);
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
				return(KHTTP_404);
		} else if (strcmp(r->fields[i].key, "d") == 0
		    || strcmp(r->fields[i].key, "default") == 0) {
			if (strcmp(r->fields[i].val, "retro") == 0) {
				avatar->d = 1;
			} else {
				return(KHTTP_404);
			}
		} else {
			return(KHTTP_404);
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

	avatar.s = 80;
	avatar.d = 0;
	avatar.hash = NULL;

	err = khttp_parsex(&r, ksuffixmap, kmimetypes, KMIME__MAX, NULL, 0,
	    pages, PAGE__MAX, KMIME_TEXT_HTML, PAGE_INDEX, &avatar,
	    NULL, 0, NULL);
	if (KCGI_OK != err)
		return(EXIT_FAILURE);

	if (KMETHOD_OPTIONS == r.method) {
		khttp_head(&r, kresps[KRESP_ALLOW], "OPTIONS GET");
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
	khttp_free(&r);
	return(EXIT_SUCCESS);
}

