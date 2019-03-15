/**
 * Copyright (c) 2014-2019 Timothy Elliott
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef OIL_LIBPNG_H
#define OIL_LIBPNG_H

#include <stdio.h>
#include <png.h>
#include "oil_resample.h"

struct oil_libpng {
	struct oil_scale os;
	png_structp rpng;
	png_infop rinfo;
	int in_vpos;
	unsigned char *inbuf;
	unsigned char **inimage;
};

/**
 * Initialize an oil_libpng struct.
 * @ol: Pointer to the struct to be initialized.
 * @dinfo: Pointer to a libjpeg decompress struct, with header already read.
 * @out_height: Desired height, in pixels, of the output image.
 * @out_width: Desired width, in pixels, of the output image.
 *
 * Returns 0 on success.
 * Returns -1 if an argument is bad.
 * Returns -2 if unable to allocate memory.
 */
int oil_libpng_init(struct oil_libpng *ol, png_structp rpng, png_infop rinfo,
	int out_width, int out_height);

void oil_libpng_free(struct oil_libpng *ol);

void oil_libpng_read_scanline(struct oil_libpng *ol, unsigned char *outbuf);

enum oil_colorspace png_cs_to_oil(png_byte cs);

#endif
