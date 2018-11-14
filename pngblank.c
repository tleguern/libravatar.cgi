/*
 * Copyright (c) 2018 Tristan Le Guern <tleguern@bouledef.eu>
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

#include <arpa/inet.h>

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "libravatar.h"

static size_t
write_IHDR(char *buf, int width)
{
	uint32_t	crc, length;
	size_t		bufw;
	char		type[4] = "IHDR";
	struct IHDR	ihdr;

	ihdr.width = htonl(width);
	ihdr.height = htonl(width);
	ihdr.bitdepth = 1;
	ihdr.colourtype = COLOUR_TYPE_GREYSCALE;
	ihdr.compression = 0;
	ihdr.filter = 0;
	ihdr.interlace = INTERLACE_METHOD_STANDARD;
	length = htonl(13);
	crc = crc32(0, Z_NULL, 0);
	crc = crc32(crc, type, sizeof(type));
	crc = crc32(crc, (Bytef*)&ihdr, sizeof(ihdr));
	crc = htonl(crc);
	bufw = 0;
	(void)memcpy(buf + bufw, &length, sizeof(length));
	bufw += sizeof(length);
	(void)memcpy(buf + bufw, type, sizeof(type));
	bufw += sizeof(type);
	(void)memcpy(buf + bufw, &ihdr, sizeof(ihdr));
	bufw += sizeof(ihdr);
	(void)memcpy(buf + bufw, &crc, sizeof(crc));
	bufw += sizeof(crc);
	return(bufw);
}

static size_t
write_tRNS(char *buf)
{
	uint32_t	crc, length;
	size_t		bufw;
	char		type[4] = "tRNS";
	uint8_t		trns[2];

	(void)memset(trns, 0, sizeof(trns));
	length = htonl(sizeof(trns));
	crc = crc32(0, Z_NULL, 0);
	crc = crc32(crc, type, sizeof(type));
	crc = crc32(crc, (Bytef *)trns, sizeof(trns));
	crc = htonl(crc);
	bufw = 0;
	(void)memcpy(buf + bufw, &length, sizeof(length));
	bufw += sizeof(length);
	(void)memcpy(buf + bufw, type, sizeof(type));
	bufw += sizeof(type);
	(void)memcpy(buf + bufw, &trns, sizeof(trns));
	bufw += sizeof(trns);
	(void)memcpy(buf + bufw, &crc, sizeof(crc));
	bufw += sizeof(crc);
	return(bufw);
}

static size_t
write_IDAT(char *buf, int width)
{
	size_t		 dataz, deflatez;
	uint32_t	 crc, length;
	size_t		 bufw;
	char		 type[4] = "IDAT";
	uint8_t		*data, *deflate;

	/* TODO: Directly write a compressed stream to avoid allocations */

	dataz = (width / (8 / 1) + \
	    (width % (8 / 1) != 0 ? 1 : 0) + 1) * width;

	if (NULL == (data = calloc(dataz, 1))) {
		return(-1);
	}
	deflatez = compressBound(dataz);
	if (NULL == (deflate = calloc(deflatez, 1))) {
		free(data);
		return(-1);
	}
	if (Z_OK != compress(deflate, &deflatez, data, dataz)) {
		return(-1);
	}
	free(data);
	length = htonl(deflatez);
	crc = crc32(0, Z_NULL, 0);
	crc = crc32(crc, type, sizeof(type));
	crc = crc32(crc, deflate, deflatez);
	crc = htonl(crc);
	bufw = 0;
	(void)memcpy(buf + bufw, &length, sizeof(length));
	bufw += sizeof(length);
	(void)memcpy(buf + bufw, type, sizeof(type));
	bufw += sizeof(type);
	(void)memcpy(buf + bufw, deflate, deflatez);
	bufw += deflatez;
	(void)memcpy(buf + bufw, &crc, sizeof(crc));
	bufw += sizeof(crc);
	free(deflate);
	return(bufw);
}

static size_t
write_IEND(char *buf)
{
	int		length;
	uint32_t	crc;
	size_t		 bufw;
	char		type[4] = "IEND";

	length = 0;
	crc = htonl(2923585666);
	bufw = 0;
	(void)memcpy(buf + bufw, &length, sizeof(length));
	bufw += sizeof(length);
	(void)memcpy(buf + bufw, type, sizeof(type));
	bufw += sizeof(type);
	(void)memcpy(buf + bufw, &crc, sizeof(crc));
	bufw += sizeof(crc);
	return(bufw);
}

int
pngblank(size_t width, uint8_t **buf, size_t *bufz)
{
	char	 sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

	if (NULL == ((*buf) = calloc(PNGBLANK_MAX_SIZE, 1)))
		return(-1);
	(void)memcpy(*buf, sig, sizeof(sig));
	*bufz = sizeof(sig);
	*bufz += write_IHDR(*buf + *bufz, width);
	*bufz += write_tRNS(*buf + *bufz);
	*bufz += write_IDAT(*buf + *bufz, width);
	*bufz += write_IEND(*buf + *bufz);
	return(0);
}

