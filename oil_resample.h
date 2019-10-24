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

#ifndef OIL_RESAMPLE_H
#define OIL_RESAMPLE_H

/**
 * Color spaces currently supported by oil.
 */
enum oil_colorspace {
	// error
	OIL_CS_UNKNOWN = 0,

	// greyscale - no sRGB gamma space conversions
	OIL_CS_G       = 0x0001,

	// greyscale w/ alpha - uses premultiplied alpha
	OIL_CS_GA      = 0x0002,

	// sRGB - input will be converted to linear RGB during processing
	OIL_CS_RGB     = 0x0003,

	// sRGB w/ padding - same as OIL_CS_RGB, but padded with an extra byte
	OIL_CS_RGBX    = 0x0004,

	// sRGB w/ alpha - sRGB to linear conversion and premultiplied alpha
	OIL_CS_RGBA    = 0x0104,

	// no color space conversions
	OIL_CS_CMYK    = 0x0204,
};

/**
 * Macro to get the number of components from an oil color space.
 */
#define OIL_CMP(x) ((x)&0xFF)

/**
 * Struct to hold state for scaling. Changing these will produce unpredictable
 * results.
 */
struct oil_scale {
	int in_height; // input image height.
	int out_height; // output image height.
	int in_width; // input image width.
	int out_width; // output image height.
	enum oil_colorspace cs; // color space of input & output.
	int in_pos; // current row of input image.
	int out_pos; // current row of output image.

	float *coeffs_y; // buffer for holding temporary y-coefficients.
	float *coeffs_x; // buffer for holding precalculated coefficients.
	int *borders_x; // holds precalculated coefficient rotation points.
	int *borders_y; // coefficient rotation points for y-scaling.
	float *sums_y; // buffer of intermediate sums for y-scaling.
	float *rb; // ring buffer holding scanlines.
	int rows_in_rb; // number of rows currently in the ring buffer.
	float *tmp_coeffs; // temporary buffer for calculating coeffs.
};

/**
 * Initialize static, pre-calculated tables. This only needs to be called once.
 * A call to oil_scale_init() will initialize these tables if not already done,
 * so explicityly calling oil_global_init() is only needed if there are
 * concurrency concerns.
 */
void oil_global_init(void);

/**
 * Reset an already-initialized oil_scale struct. This allows you to re-use an
 * oil_scale struct when the input & output dimensions as well as the colorspace
 * will be the same.
 */
void oil_scale_reset(struct oil_scale *os);

/**
 * Initialize an oil scaler struct.
 * @os: Pointer to the scaler struct to be initialized.
 * @in_height: Height, in pixels, of the input image.
 * @out_height: Height, in pixels, of the output image.
 * @in_width: Width, in pixels, of the input image.
 * @out_width: Width, in pixels, of the output image.
 * @cs: Color space of the input/output images.
 *
 * Returns 0 on success.
 * Returns -1 if an argument is bad.
 * Returns -2 if unable to allocate memory.
 */
int oil_scale_init(struct oil_scale *os, int in_height, int out_height,
	int in_width, int out_width, enum oil_colorspace cs);

/**
 * Reset rows countersin an oil scaler struct.
 * @os: Pointer to the scaler struct to be reseted.
 */
void oil_scale_restart(struct oil_scale *);

/**
 * Free heap allocations associated with a yscaler struct.
 * @os: Pointer to the yscaler struct to be freed.
 */
void oil_scale_free(struct oil_scale *os);

/**
 * Return the number of input scanlines needed before the next output scanline
 * can be produced.
 * @os: Pointer to the oil scaler struct.
 *
 * Returns 0 if no more input lines are needed to produce the next output line.
 * Otherwise, returns the number of input lines that are needed.
 */
int oil_scale_slots(struct oil_scale *os);

/**
 * Ingest & buffer an input scanline. Input is unsigned chars.
 * @os: Pointer to the scaler struct.
 * @in: Pointer to the input buffer containing a scanline.
 */
void oil_scale_in(struct oil_scale *os, unsigned char *in);

/**
 * Scale previously ingested & buffered contents to produce the next scaled output
 * scanline.
 * @os: Pointer to the scaler struct.
 * @out: Pointer to the buffer where the output scanline will be written.
 */
void oil_scale_out(struct oil_scale *os, unsigned char *out);

/**
 * Calculate an output ratio that preserves the input aspect ratio.
 * @src_width: Width, in pixels, of the input image.
 * @src_height: Height, in pixels, of the input image.
 * @out_width: Width, in pixels, of the output bounding box.
 * @out_height: Height, in pixels, of the output bounding box.
 *
 * The out_width and out_height parameters will be modified, if necessary, to
 * maintain the input aspect ratio while staying within the given bounding box.
 *
 * Returns 0 on success.
 * Returns -1 if an argument is bad.
 * Returns -3 if an adjusted dimension would be out of range.
 */
 int oil_fix_ratio(int src_width, int src_height, int *out_width,
	int *out_height);

#endif
