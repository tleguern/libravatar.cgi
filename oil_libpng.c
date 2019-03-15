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

#include "oil_libpng.h"
#include <stdlib.h>

static unsigned char **alloc_full_image_buf(int height, int rowbytes)
{
	int i, j;
	unsigned char **imgbuf;

	imgbuf = malloc(height * sizeof(unsigned char *));
	if (!imgbuf) {
		return NULL;
	}
	for (i=0; i<height; i++) {
		imgbuf[i] = malloc(rowbytes);
		if (!imgbuf[i]) {
			for (j=0; j<i-1; j++) {
				free(imgbuf[j]);
			}
			free(imgbuf);
			return NULL;
		}
	}
	return imgbuf;
}

static void free_full_image_buf(unsigned char **imgbuf, int height)
{
	int i;
	for (i=0; i<height; i++) {
		free(imgbuf[i]);
	}
	free(imgbuf);
}

int oil_libpng_init(struct oil_libpng *ol, png_structp rpng, png_infop rinfo,
	int out_width, int out_height)
{
	int ret, in_width, in_height, buf_len;
	enum oil_colorspace cs;

	ol->rpng = rpng;
	ol->rinfo = rinfo;
	ol->in_vpos = 0;
	ol->inbuf = NULL;
	ol->inimage = NULL;

	cs = png_cs_to_oil(png_get_color_type(rpng, rinfo));
	if (cs == OIL_CS_UNKNOWN) {
		return -1;
	}

	in_width = png_get_image_width(rpng, rinfo);
	in_height = png_get_image_height(rpng, rinfo);
	ret = oil_scale_init(&ol->os, in_height, out_height, in_width,
		out_width, cs);
	if (ret!=0) {
		free(ol->inbuf);
		return ret;
	}

	buf_len = png_get_rowbytes(rpng, rinfo);
	switch (png_get_interlace_type(rpng, rinfo)) {
	case PNG_INTERLACE_NONE:
		ol->inbuf = malloc(buf_len);
		if (!ol->inbuf) {
			oil_scale_free(&ol->os);
			return -2;
		}
		break;
	case PNG_INTERLACE_ADAM7:
		ol->inimage = alloc_full_image_buf(in_height, buf_len);
		if (!ol->inimage) {
			oil_scale_free(&ol->os);
			return -2;
		}
		png_read_image(rpng, ol->inimage);
		break;
	}

	return 0;
}

void oil_libpng_free(struct oil_libpng *ol)
{
	if (ol->inbuf) {
		free(ol->inbuf);
	}
	if (ol->inimage) {
		free_full_image_buf(ol->inimage, ol->os.in_height);
	}
	oil_scale_free(&ol->os);
}

static void read_scanline_interlaced(struct oil_libpng *ol)
{
	int i;

	for (i=oil_scale_slots(&ol->os); i>0; i--) {
		oil_scale_in(&ol->os, ol->inimage[ol->in_vpos++]);
	}
}

static void read_scanline(struct oil_libpng *ol)
{
	int i;

	for (i=oil_scale_slots(&ol->os); i>0; i--) {
		png_read_row(ol->rpng, ol->inbuf, NULL);
		oil_scale_in(&ol->os, ol->inbuf);
	}
}

void oil_libpng_read_scanline(struct oil_libpng *ol, unsigned char *outbuf)
{
	switch (png_get_interlace_type(ol->rpng, ol->rinfo)) {
	case PNG_INTERLACE_NONE:
		read_scanline(ol);
		break;
	case PNG_INTERLACE_ADAM7:
		read_scanline_interlaced(ol);
		break;
	}
	oil_scale_out(&ol->os, outbuf);
}

enum oil_colorspace png_cs_to_oil(png_byte cs)
{
	switch(cs) {
	case PNG_COLOR_TYPE_GRAY:
		return OIL_CS_G;
	case PNG_COLOR_TYPE_GA:
		return OIL_CS_GA;
	case PNG_COLOR_TYPE_RGB:
		return OIL_CS_RGB;
	case PNG_COLOR_TYPE_RGBA:
		return OIL_CS_RGBA;
	default:
		return OIL_CS_UNKNOWN;
	}
}
