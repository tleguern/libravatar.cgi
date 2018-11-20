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

#include <stdint.h>
#include <string.h>
#include <zlib.h>

#include "lgpng.h"

size_t
write_png_sig(uint8_t *buf)
{
	char	sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};

	(void)memcpy(buf, sig, sizeof(sig));
	return(sizeof(sig));
}

size_t
write_IHDR(uint8_t *buf, size_t width, int bitdepth, enum colourtype colour)
{
	uint32_t	crc, length;
	size_t		bufw;
	uint8_t		type[4] = "IHDR";
	struct IHDR	ihdr;

	ihdr.width = htonl(width);
	ihdr.height = htonl(width);
	ihdr.bitdepth = bitdepth;
	ihdr.colourtype = colour;
	ihdr.compression = 0;
	ihdr.filter = 0;
	ihdr.interlace = INTERLACE_METHOD_STANDARD;
	length = htonl(13);
	crc = crc32(0, Z_NULL, 0);
	crc = crc32(crc, type, sizeof(type));
	crc = crc32(crc, (Bytef *)&ihdr, sizeof(ihdr));
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

size_t
write_IEND(uint8_t *buf)
{
	int		length;
	uint32_t	crc;
	size_t		bufw;
	uint8_t		type[4] = "IEND";

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

