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

#include "oil_resample.h"
#include "oil_libpng.h"
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
	unsigned char *outbuf;
	struct oil_libpng ol;
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

	in_width = png_get_image_width(rpng, rinfo);
	in_height = png_get_image_height(rpng, rinfo);
	oil_fix_ratio(in_width, in_height, &width, &height);

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
	if (0 != oil_libpng_init(&ol, rpng, rinfo, width, height)) {
		fprintf(stderr, "Unable to allocate buffers.\n");
		png_destroy_read_struct(&rpng, &rinfo, NULL);
		png_destroy_write_struct(&wpng, &winfo);
		*output = NULL;
		return(0);
	}

	ctype = png_get_color_type(rpng, rinfo);
	png_set_IHDR(wpng, winfo, width, height, 8, ctype, PNG_INTERLACE_NONE,
		PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	png_write_info(wpng, winfo);

	if (NULL == (outbuf = malloc(width * OIL_CMP(ol.os.cs)))) {
		fprintf(stderr, "Unable to allocate buffers.\n");
		png_destroy_read_struct(&rpng, &rinfo, NULL);
		png_destroy_write_struct(&wpng, &winfo);
		*output = NULL;
		return(0);
	}

	for (uint32_t i = 0; i < height; i++) {
		oil_libpng_read_scanline(&ol, outbuf);
		png_write_row(wpng, outbuf);
	}
        png_write_end(wpng, winfo);
	png_destroy_write_struct(&wpng, &winfo);
	png_destroy_read_struct(&rpng, &rinfo, NULL);
	free(outbuf);
	oil_libpng_free(&ol);
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

