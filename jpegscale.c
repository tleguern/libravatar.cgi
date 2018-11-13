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
#include <math.h>
#include <jpeglib.h>

#include "libravatar.h"

static void prepare_jpeg_decompress(FILE *input,
	struct jpeg_decompress_struct *dinfo, struct jpeg_error_mgr *jerr)
{
	uint32_t i;

	dinfo->err = jpeg_std_error(jerr);
	jpeg_create_decompress(dinfo);
	jpeg_stdio_src(dinfo, input);

	/* Save custom headers for the compressor, but ignore APP0 & APP14 so
	 * libjpeg can handle them.
	 */
	jpeg_save_markers(dinfo, JPEG_COM, 0xFFFF);
	for (i=1; i<14; i++) {
		jpeg_save_markers(dinfo, JPEG_APP0+i, 0xFFFF);
	}
	jpeg_save_markers(dinfo, JPEG_APP0+15, 0xFFFF);
	jpeg_read_header(dinfo, TRUE);

#ifdef JCS_EXTENSIONS
	if (dinfo->out_color_space == JCS_RGB) {
		dinfo->out_color_space = JCS_EXT_RGBX;
		jpeg_calc_output_dimensions(dinfo);
	}
#endif

	jpeg_start_decompress(dinfo);
}

static enum oil_colorspace jpeg_cs_to_oil(J_COLOR_SPACE cs)
{
	switch(cs) {
	case JCS_GRAYSCALE:
		return OIL_CS_G;
	case JCS_RGB:
		return OIL_CS_RGB;
	case JCS_CMYK:
		return OIL_CS_CMYK;
#ifdef JCS_EXTENSIONS
	case JCS_EXT_RGBX:
		return OIL_CS_RGBX;
#endif
	default:
		fprintf(stderr, "Unknown jpeg color space: %d\n", cs);
		exit(1);
	}
}

static J_COLOR_SPACE oil_cs_to_jpeg(enum oil_colorspace cs)
{
	switch(cs) {
	case OIL_CS_G:
		return JCS_GRAYSCALE;
	case OIL_CS_RGB:
		return JCS_RGB;
	case OIL_CS_CMYK:
		return JCS_CMYK;
	case OIL_CS_RGBX:
#ifdef JCS_EXTENSIONS
		return JCS_EXT_RGBX;
#endif
	default:
		fprintf(stderr, "Unknown oil color space: %d\n", cs);
		exit(1);
	}
}

unsigned long jpegscale(FILE *input, unsigned char **output, uint32_t width_out)
{
	struct jpeg_decompress_struct dinfo;
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	uint8_t *inbuf, *outbuf;
	uint16_t *tmp;
	uint32_t i;
	struct preprocess_xscaler pxs;
	struct yscaler ys;
	jpeg_saved_marker_ptr marker;
	enum oil_colorspace cs;
	uint32_t height_out = width_out;
	unsigned long outputz = 0;

	prepare_jpeg_decompress(input, &dinfo, &jerr);

	/* Use the image dimensions read from the header to calculate our final
	 * output dimensions.
	 */
	fix_ratio(dinfo.image_width, dinfo.image_height, &width_out, &height_out);

	/* Allocate jpeg decoder output buffer */
	inbuf = malloc(dinfo.image_width * dinfo.output_components);

	/* Map jpeg to oil color space. */
	cs = jpeg_cs_to_oil(dinfo.out_color_space);

	/* Set up xscaler */
	preprocess_xscaler_init(&pxs, dinfo.image_width, width_out, cs);

	/* set up yscaler */
	yscaler_init(&ys, dinfo.image_height, height_out, pxs.xs.width_out, cs);

	/* Allocate linear converter output buffer */
	outbuf = malloc(ys.width * CS_TO_CMP(ys.cs) * sizeof(uint16_t));

	/* Jpeg compressor. */
	cinfo.err = &jerr;
	jpeg_create_compress(&cinfo);
	jpeg_mem_dest(&cinfo, output, &outputz);
	cinfo.image_width = ys.width;
	cinfo.image_height = ys.out_height;
	cinfo.input_components = CS_TO_CMP(ys.cs);
	cinfo.in_color_space = oil_cs_to_jpeg(ys.cs);
	jpeg_set_defaults(&cinfo);
	jpeg_set_quality(&cinfo, 95, FALSE);
	jpeg_start_compress(&cinfo, TRUE);

	/* Copy custom headers from source jpeg to dest jpeg. */
	for (marker=dinfo.marker_list; marker; marker=marker->next) {
		jpeg_write_marker(&cinfo, marker->marker, marker->data,
			marker->data_length);
	}

	/* Read scanlines, process image, and write scanlines to the jpeg
	 * encoder.
	 */
	for(i=0; i<height_out; i++) {
		while ((tmp = yscaler_next(&ys))) {
			jpeg_read_scanlines(&dinfo, &inbuf, 1);
			preprocess_xscaler_scale(&pxs, inbuf, tmp);
		}
		yscaler_scale(&ys, outbuf, i);
		jpeg_write_scanlines(&cinfo, (JSAMPARRAY)&outbuf, 1);
	}

	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);

	jpeg_finish_decompress(&dinfo);
	jpeg_destroy_decompress(&dinfo);
	free(outbuf);
	free(inbuf);
	yscaler_free(&ys);
	preprocess_xscaler_free(&pxs);
	return(outputz);
}

