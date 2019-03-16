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
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>

#include "libravatar.h"
#include "lgpng.h"

#define PNGMM_MAX_SIZE 1500

static int
palette_init(struct PLTE *plte, size_t n)
{
	plte->entries = calloc(n, sizeof(*(plte->entries)));
	if (NULL == plte->entries) {
		plte->entriesz = 0;
		return(-1);
	}
	plte->entriesz = n;
	return(0);
}

static void
palette_assign(struct PLTE *plte, int n, uint8_t red, uint8_t green, uint8_t blue)
{
	plte->entries[n].red = red;
	plte->entries[n].green = green;
	plte->entries[n].blue = blue;
}

static void
palette_free(struct PLTE *plte)
{
	free(plte->entries);
	plte->entries = NULL;
	plte->entriesz = 0;
}

static size_t
write_PLTE(struct PLTE *plte, uint8_t *buf)
{
	uint32_t	crc, length;
	size_t		bufw;
	uint8_t		type[4] = "PLTE";

	length = htonl(plte->entriesz * sizeof(*(plte->entries)));
	crc = crc32(0, Z_NULL, 0);
	crc = crc32(crc, type, sizeof(type));
	for (size_t i = 0; i < plte->entriesz; i++) {
		crc = crc32(crc, (Bytef *)&(plte->entries[i]),
		    sizeof(plte->entries[i]));
	}
	crc = htonl(crc);
	bufw = 0;
	(void)memcpy(buf + bufw, &length, sizeof(length));
	bufw += sizeof(length);
	(void)memcpy(buf + bufw, type, sizeof(type));
	bufw += sizeof(type);
	for (size_t i = 0; i < plte->entriesz; i++) {
		(void)memcpy(buf + bufw, &(plte->entries[i]),
		    sizeof(plte->entries[i]));
		bufw += sizeof(plte->entries[i]);
	}
	(void)memcpy(buf + bufw, &crc, sizeof(crc));
	bufw += sizeof(crc);
	return(bufw);
}

static size_t
write_IDAT(uint8_t *buf, size_t width)
{
	size_t		 dataz, deflatez;
	uint32_t	 crc, length;
	size_t		 bufw;
	uint8_t		 type[4] = "IDAT";
	uint8_t		*data, *deflate;
	int		 extrabyte, scanline;
	int		 radius, cx, cy;
	int		 area, p0x, p0y, p1x, p1y, p2x, p2y;

	/* Circle attributes */
	radius = width / 4;
	cx = width / 2;
	cy = width / 3;
	/* Triangle attributes */
	p0x = 0;
	p0y = 0;
	p1x = width / 5 * 4 - cx;
	p1y = width - cy;
	p2x = width / 5 - cx;
	p2y = width - cy;
	area = (-p1y * p2x + p0y * (-p1x + p2x) + p0x * (p1y - p2y)
	    + p1x * p2y) / 2;

	/* with a bitdepth of 1 the last byte is not always entirely used */
	extrabyte = width % 8 != 0 ? 1 : 0;
	scanline = width / 8 + extrabyte;
	/* each scanline has one leading byte used to store filtering flags */
	scanline += 1;
	dataz = scanline * width;
	if (NULL == (data = calloc(dataz, 1))) {
		return(-1);
	}
	for (size_t y = 0; y < width; y++) {
		for (size_t x = 0; x < width; x++) {
			int value, byte, bit;
			long fx, fy;

			fx = x - cx;
			fy = y - cy;
			value = 0;
			if (pow(fx, 2) + pow(fy, 2) - pow(radius, 2) <= 0) {
				/* the current pixel is part of the circle */
				value = 1;
			} else {
				/* or perhaps part of the triangle */
				int s, t;
                        
				s = p2y * fx + (-p2x) * fy;
				t = -p1y * fx + (p1x) * fy;
				if (s > 0 && t > 0 && (s + t) < 2 * area) {
					value = 1;
				}
			}
			byte = y * scanline + (x / 8) + 1;
			bit = x % 8;
			data[byte] |= value << (7 - bit);
		}
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

int
mm(size_t width, uint8_t **buf, size_t *bufz)
{
	struct PLTE	 plte;

	if (NULL == ((*buf) = calloc(PNGMM_MAX_SIZE, 1))) {
		return(-1);
	}
	if (-1 == palette_init(&plte, 2)) {
		free(*buf);
		return(-1);
	}
	palette_assign(&plte, 0, 169, 169, 169);
	palette_assign(&plte, 1,  255, 255, 255);
	*bufz = 0;
	*bufz += write_png_sig(*buf);
	*bufz += write_IHDR(*buf + *bufz, width, 1, COLOUR_TYPE_INDEXED);
	*bufz += write_PLTE(&plte, *buf + *bufz);
	palette_free(&plte);
	*bufz += write_IDAT(*buf + *bufz, width);
	*bufz += write_IEND(*buf + *bufz);
	return(0);
}

