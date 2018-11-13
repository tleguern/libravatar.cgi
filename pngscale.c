/*
 * Copyright (c) 2014-2016 Timothy Elliott
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "resample.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>

#include "libravatar.h"

static void my_png_writer(png_struct *, png_byte *, size_t);
static void my_png_flusher(png_struct *);
static void user_error(png_struct *, const char *);
static void user_warning(png_struct *, const char *);

static enum oil_colorspace png_cs_to_oil(png_byte cs)
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
	}
	exit(99);
}

/** Interlaced PNGs need to be fully decompressed before we can scale the image.
  */
static void png_interlaced(png_structp rpng, png_infop rinfo, png_structp wpng,
	png_infop winfo)
{
	uint8_t **sl, *outbuf;
	uint16_t *tmp;
	uint32_t i, n, in_width, in_height, out_width, out_height;
	size_t buf_len;
	struct preprocess_xscaler pxs;
	struct yscaler ys;
	enum oil_colorspace cs;

	in_width = png_get_image_width(rpng, rinfo);
	in_height = png_get_image_height(rpng, rinfo);
	out_width = png_get_image_width(wpng, winfo);
	out_height = png_get_image_height(wpng, winfo);

	/* Allocate space for the full input image and fill it. */
	sl = malloc(in_height * sizeof(uint8_t *));
	buf_len = png_get_rowbytes(rpng, rinfo);
	for (i=0; i<in_height; i++) {
		sl[i] = malloc(buf_len);
	}
	png_read_image(rpng, sl);

	cs = png_cs_to_oil(png_get_color_type(rpng, rinfo));
	outbuf = malloc(out_width * CS_TO_CMP(cs));

	preprocess_xscaler_init(&pxs, in_width, out_width, cs);
	yscaler_init(&ys, in_height, out_height, pxs.xs.width_out, cs);

	n = 0;
	for(i=0; i<out_height; i++) {
		while ((tmp = yscaler_next(&ys))) {
			preprocess_xscaler_scale(&pxs, sl[n++], tmp);
		}
		yscaler_scale(&ys, outbuf, i);
		png_write_row(wpng, outbuf);
	}

	for (i=0; i<in_height; i++) {
		free(sl[i]);
	}
	free(sl);
	free(outbuf);
	yscaler_free(&ys);
	preprocess_xscaler_free(&pxs);
}

static void png_noninterlaced(png_structp rpng, png_infop rinfo,
	png_structp wpng, png_infop winfo)
{
	uint32_t i, in_width, in_height, out_width, out_height;
	uint16_t *tmp;
	uint8_t *inbuf, *outbuf;
	struct preprocess_xscaler pxs;
	struct yscaler ys;
	enum oil_colorspace cs;

	in_width = png_get_image_width(rpng, rinfo);
	in_height = png_get_image_height(rpng, rinfo);
	out_width = png_get_image_width(wpng, winfo);
	out_height = png_get_image_height(wpng, winfo);

	cs = png_cs_to_oil(png_get_color_type(rpng, rinfo));

	inbuf = malloc(in_width * CS_TO_CMP(cs));
	outbuf = malloc(out_width * CS_TO_CMP(cs));

	preprocess_xscaler_init(&pxs, in_width, out_width, cs);
	yscaler_init(&ys, in_height, out_height, pxs.xs.width_out, cs);

	for(i=0; i<out_height; i++) {
		while ((tmp = yscaler_next(&ys))) {
			png_read_row(rpng, inbuf, NULL);
			preprocess_xscaler_scale(&pxs, inbuf, tmp);
		}
		yscaler_scale(&ys, outbuf, i);
		png_write_row(wpng, outbuf);
	}

	free(outbuf);
	free(inbuf);
	yscaler_free(&ys);
	preprocess_xscaler_free(&pxs);
}

struct pngdata {
	unsigned char	*data;
	size_t		 dataz;
};

size_t pngscale(FILE *input, unsigned char **output, uint32_t width)
{
	png_structp rpng, wpng;
	png_infop rinfo, winfo;
	png_uint_32 in_width, in_height;
	png_byte ctype;
	uint32_t height = width;
	struct pngdata pngdata;

	pngdata.data = NULL;
	pngdata.dataz = 0;
	rpng = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL,
	    user_error, user_warning);
	if (NULL == rpng) {
		*output = NULL;
		return(0);
	}
	rinfo = png_create_info_struct(rpng);
	if (NULL == rinfo) {
		png_destroy_read_struct(&rpng, NULL, NULL);
		*output = NULL;
		return(0);
	}
	png_init_io(rpng, input);
	png_read_info(rpng, rinfo);

	png_set_packing(rpng);
	png_set_strip_16(rpng);
	png_set_expand(rpng);
	png_set_interlace_handling(rpng);
	png_read_update_info(rpng, rinfo);

	ctype = png_get_color_type(rpng, rinfo);

	in_width = png_get_image_width(rpng, rinfo);
	in_height = png_get_image_height(rpng, rinfo);
	fix_ratio(in_width, in_height, &width, &height);

	wpng = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL,
	    user_error, user_warning);
	if (NULL == wpng) {
		png_destroy_read_struct(&rpng, &rinfo, NULL);
		*output = NULL;
		return(0);
	}
	winfo = png_create_info_struct(wpng);
	if (NULL == winfo) {
		png_destroy_read_struct(&rpng, &rinfo, NULL);
		png_destroy_write_struct(&wpng, NULL);
		*output = NULL;
		return(0);
	}
	png_set_write_fn(wpng, &pngdata, my_png_writer, my_png_flusher);

	png_set_IHDR(wpng, winfo, width, height, 8, ctype, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_write_info(wpng, winfo);

	switch (png_get_interlace_type(rpng, rinfo)) {
	case PNG_INTERLACE_NONE:
		png_noninterlaced(rpng, rinfo, wpng, winfo);
		break;
	case PNG_INTERLACE_ADAM7:
		png_interlaced(rpng, rinfo, wpng, winfo);
		break;
	}

	png_write_end(wpng, winfo);
	png_destroy_write_struct(&wpng, &winfo);
	png_destroy_read_struct(&rpng, &rinfo, NULL);
	(*output) = pngdata.data;
	return(pngdata.dataz);
}

static void my_png_writer(png_struct *png, png_byte *data, size_t dataz)
{
	size_t		 newsize;
	unsigned char	*tempdata;
	struct pngdata	*pngdata;

	pngdata = (struct pngdata *)png_get_io_ptr(png);
	newsize = dataz + pngdata->dataz;
	tempdata = realloc(pngdata->data, newsize);
	if (NULL == tempdata) {
		free(pngdata->data);
		pngdata->data = NULL;
		pngdata->dataz = 0;
		return;
	}
	pngdata->data = tempdata;
	memcpy(pngdata->data + pngdata->dataz, data, dataz);
	pngdata->dataz = newsize;
}

static void my_png_flusher(png_struct *png)
{
	(void)png;
}

static void user_error(png_struct *png, const char *error)
{
	(void)png;
	fprintf(stderr, "pngscale: error: %s\n", error);
}

static void user_warning(png_struct *png, const char *warning)
{
	(void)png;
	fprintf(stderr, "pngscale: warning: %s\n", warning);
}

