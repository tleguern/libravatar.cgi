/**
 * Copyright (c) 2014-2016 Timothy Elliott
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

#ifndef RESAMPLE_H
#define RESAMPLE_H

#include <stdint.h>
#include <stddef.h>

enum oil_colorspace {
	OIL_CS_G       = 0x0001,
	OIL_CS_GA      = 0x0002,
	OIL_CS_RGB     = 0x0003,
	OIL_CS_RGBX    = 0x0004,
	OIL_CS_RGBA    = 0x0104,
	OIL_CS_CMYK    = 0x0204,
};
#define CS_TO_CMP(x) (x&0xFF)

struct sl_rbuf {
	uint32_t height; // number of scanlines that the ring buffer can hold
	size_t length; // width in bytes of each scanline in the buffer
	uint32_t count; // total no. of scanlines that have been fed in
	uint16_t *buf; // buffer for the ring buffer
	uint16_t **virt; // space to provide scanline pointers for scaling
};

/**
 * Struct to hold state for y-scaling.
 */
struct yscaler {
	struct sl_rbuf rb; // ring buffer holding scanlines.
	uint32_t in_height; // input image height.
	uint32_t out_height; // output image height.
	uint32_t width;
	enum oil_colorspace cs;
	uint32_t target; // where the ring buffer should be on next scaling.
	float ty; // sub-pixel offset for next scaling.
};

/**
 * Initialize a yscaler struct. Calculates how large the scanline ring buffer
 * will need to be and allocates it.
 */
int yscaler_init(struct yscaler *ys, uint32_t in_height, uint32_t out_height,
	uint32_t width, enum oil_colorspace cs);

/**
 * Free a yscaler struct, including the ring buffer.
 */
void yscaler_free(struct yscaler *ys);

/**
 * Get a pointer to the next scanline to be filled in the ring buffer. Returns
 * null if no more scanlines are needed to perform scaling.
 */
uint16_t *yscaler_next(struct yscaler *ys);

/**
 * Scale the buffered contents of the yscaler to produce the next scaled output
 * scanline.
 *
 * Scaled scanline will be written to the out parameter.
 * The width parameter is the nuber of samples in each scanline.
 * The cmp parameter is the number of components per sample (3 for RGB).
 * The pos parameter is the position of the output scanline.
 */
int yscaler_scale(struct yscaler *ys, uint8_t *out, uint32_t pos);

/**
 * Struct to hold state for x-scaling.
 */
struct xscaler {
	uint16_t *psl_buf;
	uint16_t *psl_pos0;
	size_t psl_offset;
	uint32_t width_in;
	uint32_t width_out;
	enum oil_colorspace cs;
};

struct preprocess_xscaler {
	struct xscaler xs;
	uint32_t width_in;
	enum oil_colorspace cs_in;
	uint32_t scale_factor;
};

int preprocess_xscaler_init(struct preprocess_xscaler *pxs, uint32_t width_in,
	uint32_t width_out, enum oil_colorspace cs_in);
void preprocess_xscaler_scale(struct preprocess_xscaler *pxs, uint8_t *in,
	uint16_t *out);
void preprocess_xscaler_free(struct preprocess_xscaler *pxs);

/**
 * Utility helpers.
 */
void fix_ratio(uint32_t src_width, uint32_t src_height, uint32_t *out_width,
	uint32_t *out_height);

#endif
