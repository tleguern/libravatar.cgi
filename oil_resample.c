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

#include "oil_resample.h"
#include <math.h>
#include <stdlib.h>
#include <limits.h>

/**
 * When shrinking a 10 million pixel wide scanline down to a single pixel, we
 * reach the limits of single-precision floats, and the xscaler will get stuck
 * in an infinite loop. Limit input dimensions to one million by one million
 * pixels to avoid this issue as well as overflow issues with 32-bit ints.
 */
#define MAX_DIMENSION 1000000

/**
 * Bicubic interpolation. 2 base taps on either side.
 */
#define TAPS 4

/**
 * Clamp a float between 0 and 1.
 */
static float clampf(float x) {
	if (x > 1.0f) {
		return 1.0f;
	} else if (x < 0.0f) {
		return 0.0f;
	}
	return x;
}

/**
 * Convert a float to 8-bit integer.
 */
static int clamp8(float x)
{
	return round(clampf(x) * 255.0f);
}

/**
 * Map from the discreet dest coordinate pos to a continuous source coordinate.
 * The resulting coordinate can range from -0.5 to the maximum of the
 * destination image dimension.
 */
static double map(int dim_in, int dim_out, int pos)
{
	return (pos + 0.5) * ((double)dim_in / dim_out) - 0.5;
}

/**
 * Returns the mapped input position and put the sub-pixel remainder in rest.
 */
static int split_map(int dim_in, int dim_out, int pos, float *rest)
{
	double smp;
	int smp_i;

	smp = map(dim_in, dim_out, pos);
	smp_i = smp < 0 ? -1 : smp;
	*rest = smp - smp_i;
	return smp_i;
}

/**
 * Given input and output dimension, calculate the total number of taps that
 * will be needed to calculate an output sample.
 *
 * When we reduce an image by a factor of two, we need to scale our resampling
 * function by two as well in order to avoid aliasing.
 */
static int calc_taps(int dim_in, int dim_out)
{
	int tmp;
	if (dim_out > dim_in) {
		return TAPS;
	}
	tmp = TAPS * dim_in / dim_out;
	return tmp - (tmp & 1);
}

/**
 * Catmull-Rom interpolator.
 */
static float catrom(float x)
{
	if (x>2) {
		return 0;
	}
	if (x<1) {
		return (1.5f*x - 2.5f)*x*x + 1;
	}
	return (((5 - x)*x - 8)*x + 4) / 2;
}

/**
 * Given an offset tx, calculate taps coefficients.
 */
static void calc_coeffs(float *coeffs, float tx, int taps)
{
	int i;
	float tmp, tap_mult, fudge;

	tap_mult = (float)taps / TAPS;
	tx = 1 - tx - taps / 2;
	fudge = 0.0f;

	for (i=0; i<taps; i++) {
		tmp = catrom(fabsf(tx) / tap_mult) / tap_mult;
		fudge += tmp;
		coeffs[i] = tmp;
		tx += 1;
	}
	fudge = 1 / fudge;
	for (i=0; i<taps; i++) {
		coeffs[i] *= fudge;
	}
}

/**
 * Holds pre-calculated table of linear float to srgb char mappings.
 * Initialized via build_l2s_rights();
 */
static float l2s_rights[256];

/**
 * Populates l2s_rights.
 */
static void build_l2s_rights(void)
{
	int i;
	double srgb_f, tmp, val;

	for (i=0; i<255; i++) {
		srgb_f = (i + 0.5)/255.0;
		if (srgb_f <= 0.0404482362771082) {
			val = srgb_f / 12.92;
		} else {
			tmp = (srgb_f + 0.055)/1.055;
			val = pow(tmp, 2.4);
		}
		l2s_rights[i] = val;
	}
	l2s_rights[i] = 256.0f;
}

/**
 * Maps the given linear RGB float to sRGB integer.
 *
 * Performs a binary search on l2s_rights.
 */
static int linear_sample_to_srgb(float in)
{
	int offs, i;
	offs = 0;
	for (i=128; i>0; i >>= 1) {
		if (in > l2s_rights[offs + i]) {
			offs += i;
		}
	}
	return in > l2s_rights[offs] ? offs + 1 : offs;
}

/**
 * Resizes a strip of RGBX scanlines to a single scanline.
 */
static void strip_scale_rgbx(float **in, int strip_height, int len,
	unsigned char *out, float *coeffs)
{
	int i, j;
	double sum[3];

	for (i=0; i<len; i+=4) {
		sum[0] = sum[1] = sum[2] = 0;
		for (j=0; j<strip_height; j++) {
			sum[0] += coeffs[j] * in[j][i];
			sum[1] += coeffs[j] * in[j][i + 1];
			sum[2] += coeffs[j] * in[j][i + 2];
		}
		out[0] = linear_sample_to_srgb(sum[0]);
		out[1] = linear_sample_to_srgb(sum[1]);
		out[2] = linear_sample_to_srgb(sum[2]);
		out[3] = 0;
		out += 4;
	}
}

/**
 * Resizes a strip of RGB scanlines to a single scanline.
 */
static void strip_scale_rgb(float **in, int strip_height, int len,
	unsigned char *out, float *coeffs)
{
	int i, j;
	double sum[3];

	for (i=0; i<len; i+=3) {
		sum[0] = sum[1] = sum[2] = 0;
		for (j=0; j<strip_height; j++) {
			sum[0] += coeffs[j] * in[j][i];
			sum[1] += coeffs[j] * in[j][i + 1];
			sum[2] += coeffs[j] * in[j][i + 2];
		}
		out[0] = linear_sample_to_srgb(sum[0]);
		out[1] = linear_sample_to_srgb(sum[1]);
		out[2] = linear_sample_to_srgb(sum[2]);
		out += 3;
	}
}

/**
 * Resizes a strip of greyscale scanlines to a single scanline.
 */
static void strip_scale_g(float **in, int strip_height, int len,
	unsigned char *out, float *coeffs)
{
	int i, j;
	double sum;

	for (i=0; i<len; i++) {
		sum = 0;
		for (j=0; j<strip_height; j++) {
			sum += coeffs[j] * in[j][i];
		}
		out[i] = clamp8(sum);
	}
}

/**
 * Resizes a strip of greyscale-alpha scanlines to a single scanline.
 */
static void strip_scale_ga(float **in, int strip_height, int len,
	unsigned char *out, float *coeffs)
{
	int i, j;
	double sum[2], alpha;

	for (i=0; i<len; i+=2) {
		sum[0] = sum[1] = 0;
		for (j=0; j<strip_height; j++) {
			sum[0] += coeffs[j] * in[j][i];
			sum[1] += coeffs[j] * in[j][i + 1];
		}
		alpha = clampf(sum[1]);
		if (alpha != 0) {
			sum[0] /= alpha;
		}
		out[0] = clamp8(sum[0]);
		out[1] = round(alpha * 255.0f);
		out += 2;
	}
}

/**
 * Resizes a strip of RGB-alpha scanlines to a single scanline.
 */
static void strip_scale_rgba(float **in, int strip_height, int len,
	unsigned char *out, float *coeffs)
{
	int i, j;
	double sum[4], alpha;

	for (i=0; i<len; i+=4) {
		sum[0] = sum[1] = sum[2] = sum[3] = 0;
		for (j=0; j<strip_height; j++) {
			sum[0] += coeffs[j] * in[j][i];
			sum[1] += coeffs[j] * in[j][i + 1];
			sum[2] += coeffs[j] * in[j][i + 2];
			sum[3] += coeffs[j] * in[j][i + 3];
		}
		alpha = clampf(sum[3]);
		if (alpha != 0) {
			sum[0] /= alpha;
			sum[1] /= alpha;
			sum[2] /= alpha;
		}
		out[0] = linear_sample_to_srgb(sum[0]);
		out[1] = linear_sample_to_srgb(sum[1]);
		out[2] = linear_sample_to_srgb(sum[2]);
		out[3] = round(alpha * 255.0f);
		out += 4;
	}
}

/**
 * Resizes a strip of CMYK scanlines to a single scanline.
 */
static void strip_scale_cmyk(float **in, int strip_height, int len,
	unsigned char *out, float *coeffs)
{
	int i, j;
	double sum[4];

	for (i=0; i<len; i+=4) {
		sum[0] = sum[1] = sum[2] = sum[3] = 0;
		for (j=0; j<strip_height; j++) {
			sum[0] += coeffs[j] * in[j][i];
			sum[1] += coeffs[j] * in[j][i + 1];
			sum[2] += coeffs[j] * in[j][i + 2];
			sum[3] += coeffs[j] * in[j][i + 3];
		}
		out[0] = clamp8(sum[0]);
		out[1] = clamp8(sum[1]);
		out[2] = clamp8(sum[2]);
		out[3] = clamp8(sum[3]);
		out += 4;
	}
}

/**
 * Scale a strip of scanlines. Branches to the correct interpolator using the
 * given colorspace.
 */
static void strip_scale(float **in, int strip_height, int len,
	unsigned char *out, float *coeffs, float ty, enum oil_colorspace cs)
{
	calc_coeffs(coeffs, ty, strip_height);

	switch(cs) {
	case OIL_CS_G:
		strip_scale_g(in, strip_height, len, out, coeffs);
		break;
	case OIL_CS_GA:
		strip_scale_ga(in, strip_height, len, out, coeffs);
		break;
	case OIL_CS_RGB:
		strip_scale_rgb(in, strip_height, len, out, coeffs);
		break;
	case OIL_CS_RGBX:
		strip_scale_rgbx(in, strip_height, len, out, coeffs);
		break;
	case OIL_CS_RGBA:
		strip_scale_rgba(in, strip_height, len, out, coeffs);
		break;
	case OIL_CS_CMYK:
		strip_scale_cmyk(in, strip_height, len, out, coeffs);
		break;
	case OIL_CS_UNKNOWN:
		break;
	}
}

/* horizontal scaling */

/**
 * Holds pre-calculated mapping of sRGB chars to linear RGB floating point
 * values.
 */
static float s2l_map_f[256];

/**
 * Populates s2l_map_f.
 */
static void build_s2l(void)
{
	int input;
	double in_f, tmp, val;

	for (input=0; input<=255; input++) {
		in_f = input / 255.0;
		if (in_f <= 0.040448236277) {
			val = in_f / 12.92;
		} else {
			tmp = ((in_f + 0.055)/1.055);
			val = pow(tmp, 2.4);
		}
		s2l_map_f[input] = val;
	}
}

/**
 * Given input & output dimensions, populate a buffer of coefficients and
 * border counters.
 *
 * This method assumes that in_width >= out_width.
 *
 * It generates 4 * in_width coefficients -- 4 for every input sample.
 *
 * It generates out_width border counters, these indicate how many input
 * samples to process before the next output sample is finished.
 */
static void xscale_calc_coeffs(int in_width, int out_width, float *coeff_buf,
	int *border_buf)
{
	struct {
		float tx;
		float fudge;
		float *center;
	} out_s[4];
	int i, j, out_pos, border, taps;
	float tap_mult_f, tx;

	out_pos = 0;
	border = 0;
	taps = calc_taps(in_width, out_width);
	tap_mult_f = (float)TAPS / taps;

	for (i=0; i<4; i++) {
		out_s[i].tx = -1 * map(in_width, out_width, i) * tap_mult_f;
		out_s[i].fudge = 1.0f;
	}

	for (i=0; i<in_width; i++) {
		for (j=0; j<4; j++) {
			tx = fabsf(out_s[j].tx);
			coeff_buf[j] = catrom(tx) * tap_mult_f;
			out_s[j].fudge -= coeff_buf[j];
			if (tx < tap_mult_f) {
				out_s[j].center = coeff_buf + j;
			}
			out_s[j].tx += tap_mult_f;
		}
		border++;
		coeff_buf += 4;
		if (out_s[0].tx >= 2.0f) {
			out_s[0].center[0] += out_s[0].fudge;

			out_s[0] = out_s[1];
			out_s[1] = out_s[2];
			out_s[2] = out_s[3];

			out_s[3].tx = (i + 1 - map(in_width, out_width, out_pos + 4)) * tap_mult_f;
			out_s[3].fudge = 1.0f;

			border_buf[out_pos] = border;
			border = 0;
			out_pos++;
		}
	}

	for (i=0; i + out_pos < out_width; i++) {
		out_s[i].center[0] += out_s[i].fudge;
		border_buf[i + out_pos] = border;
		border = 0;
	}
}

/**
 * Takes an array of 4 floats and shifts them left. The rightmost element is
 * set to 0.0.
 */
static void shift_left_f(float *f)
{
	f[0] = f[1];
	f[1] = f[2];
	f[2] = f[3];
	f[3] = 0.0f;
}

/**
 * Takes a sample value, an array of 4 coefficients & 4 accumulators, and
 * adds the product of sample * coeffs[n] to each accumulator.
 */
static void add_sample_to_sum_f(float sample, float *coeffs, float *sum)
{
	int i;
	for (i=0; i<4; i++) {
		sum[i] += sample * coeffs[i];
	}
}

/**
 * Takes an array of n 4-element source arrays, writes the first element to the
 * next n positions of the output address, and shifts the source arrays.
 */
static void dump_out(float *out, float sum[][4], int n)
{
	int i;
	for (i=0; i<n; i++) {
		out[i] = sum[i][0];
		shift_left_f(sum[i]);
	}
}

static void xscale_down_rgbx(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j, k;
	float sum[3][4] = {{ 0.0f }};

	for (i=0; i<out_width; i++) {
		for (j=border_buf[0]; j>0; j--) {
			for (k=0; k<3; k++) {
				add_sample_to_sum_f(s2l_map_f[in[k]], coeff_buf, sum[k]);
			}
			in += 4;
			coeff_buf += 4;
		}
		dump_out(out, sum, 3);
		out[3] = 0;
		out += 4;
		border_buf++;
	}
}

static void xscale_down_rgb(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j, k;
	float sum[3][4] = {{ 0.0f }};

	for (i=0; i<out_width; i++) {
		for (j=border_buf[0]; j>0; j--) {
			for (k=0; k<3; k++) {
				add_sample_to_sum_f(s2l_map_f[in[k]], coeff_buf, sum[k]);
			}
			in += 3;
			coeff_buf += 4;
		}
		dump_out(out, sum, 3);
		out += 3;
		border_buf++;
	}
}

static void xscale_down_g(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j;
	float sum[4] = { 0.0f };

	for (i=0; i<out_width; i++) {
		for (j=border_buf[0]; j>0; j--) {
			add_sample_to_sum_f(in[0] / 255.0f, coeff_buf, sum);
			in += 1;
			coeff_buf += 4;
		}
		out[0] = sum[0];
		shift_left_f(sum);
		out += 1;
		border_buf++;
	}
}

static void xscale_down_cmyk(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j, k;
	float sum[4][4] = {{ 0.0f }};

	for (i=0; i<out_width; i++) {
		for (j=border_buf[0]; j>0; j--) {
			for (k=0; k<4; k++) {
				add_sample_to_sum_f(in[k] / 255.0f, coeff_buf, sum[k]);
			}
			in += 4;
			coeff_buf += 4;
		}
		dump_out(out, sum, 4);
		out += 4;
		border_buf++;
	}
}

static void xscale_down_rgba(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j, k;
	float alpha, sum[4][4] = {{ 0.0f }};

	for (i=0; i<out_width; i++) {
		for (j=border_buf[0]; j>0; j--) {
			alpha = in[3] / 255.0f;
			for (k=0; k<3; k++) {
				add_sample_to_sum_f(s2l_map_f[in[k]] * alpha, coeff_buf, sum[k]);
			}
			add_sample_to_sum_f(alpha, coeff_buf, sum[3]);
			in += 4;
			coeff_buf += 4;
		}
		dump_out(out, sum, 4);
		out += 4;
		border_buf++;
	}
}

static void xscale_down_ga(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j;
	float alpha, sum[2][4] = {{ 0.0f }};

	for (i=0; i<out_width; i++) {
		for (j=border_buf[0]; j>0; j--) {
			alpha = in[1] / 255.0f;
			add_sample_to_sum_f(in[0] * alpha, coeff_buf, sum[0]);
			add_sample_to_sum_f(alpha, coeff_buf, sum[1]);
			in += 2;
			coeff_buf += 4;
		}
		dump_out(out, sum, 2);
		out += 2;
		border_buf++;
	}
}

static void oil_xscale_down(unsigned char *in, float *out,
	int width_out, enum oil_colorspace cs_in, float *coeff_buf,
	int *border_buf)
{
	switch(cs_in) {
	case OIL_CS_RGBX:
		xscale_down_rgbx(in, out, width_out, coeff_buf, border_buf);
		break;
	case OIL_CS_RGB:
		xscale_down_rgb(in, out, width_out, coeff_buf, border_buf);
		break;
	case OIL_CS_G:
		xscale_down_g(in, out, width_out, coeff_buf, border_buf);
		break;
	case OIL_CS_CMYK:
		xscale_down_cmyk(in, out, width_out, coeff_buf, border_buf);
		break;
	case OIL_CS_RGBA:
		xscale_down_rgba(in, out, width_out, coeff_buf, border_buf);
		break;
	case OIL_CS_GA:
		xscale_down_ga(in, out, width_out, coeff_buf, border_buf);
		break;
	case OIL_CS_UNKNOWN:
		break;
	}
}

static int dim_safe(int i, int max)
{
	if (i < 0) {
		return 0;
	}
	if (i > max) {
		return max;
	}
	return i;
}

static void xscale_up_rgbx(unsigned char *in, int width_in, float *out,
	int width_out)
{
	int i, j, k, smp_i;
	float coeffs[4], tx, sum[3];
	unsigned char *in_pos;

	for (i=0; i<width_out; i++) {
		smp_i = split_map(width_in, width_out, i, &tx) - 1;
		calc_coeffs(coeffs, tx, 4);
		sum[0] = sum[1] = sum[2] = 0.0f;
		for (j=0; j<4; j++) {
			in_pos = in + dim_safe(smp_i + j, width_in - 1) * 4;
			for (k=0; k<3; k++) {
				sum[k] += s2l_map_f[in_pos[k]] * coeffs[j];
			}
		}
		for (k=0; k<3; k++) {
			out[k] = sum[k];
		}
		out[3] = 0.0f;
		out += 4;
	}
}

static void xscale_up_rgb(unsigned char *in, int width_in, float *out,
	int width_out)
{
	int i, j, k, smp_i;
	float coeffs[4], tx, sum[3];
	unsigned char *in_pos;

	for (i=0; i<width_out; i++) {
		smp_i = split_map(width_in, width_out, i, &tx) - 1;
		calc_coeffs(coeffs, tx, 4);
		sum[0] = sum[1] = sum[2] = 0.0f;
		for (j=0; j<4; j++) {
			in_pos = in + dim_safe(smp_i + j, width_in - 1) * 3;
			for (k=0; k<3; k++) {
				sum[k] += s2l_map_f[in_pos[k]] * coeffs[j];
			}
		}
		for (k=0; k<3; k++) {
			out[k] = sum[k];
		}
		out += 3;
	}
}

static void xscale_up_cmyk(unsigned char *in, int width_in, float *out,
	int width_out)
{
	int i, j, k, smp_i;
	float coeffs[4], tx, sum[4];
	unsigned char *in_pos;

	for (i=0; i<width_out; i++) {
		smp_i = split_map(width_in, width_out, i, &tx) - 1;
		calc_coeffs(coeffs, tx, 4);
		sum[0] = sum[1] = sum[2] = sum[3] = 0.0f;
		for (j=0; j<4; j++) {
			in_pos = in + dim_safe(smp_i + j, width_in - 1) * 4;
			for (k=0; k<4; k++) {
				sum[k] += in_pos[k]/255.0f * coeffs[j];
			}
		}
		for (k=0; k<4; k++) {
			out[k] = sum[k];
		}
		out += 4;
	}
}

static void xscale_up_rgba(unsigned char *in, int width_in, float *out,
	int width_out)
{
	int i, j, k, smp_i;
	float alpha, coeffs[4], tx, sum[4];
	unsigned char *in_pos;

	for (i=0; i<width_out; i++) {
		smp_i = split_map(width_in, width_out, i, &tx) - 1;
		calc_coeffs(coeffs, tx, 4);
		sum[0] = sum[1] = sum[2] = sum[3] = 0.0f;
		for (j=0; j<4; j++) {
			in_pos = in + dim_safe(smp_i + j, width_in - 1) * 4;
			alpha = in_pos[3] / 255.0f;
			for (k=0; k<3; k++) {
				sum[k] += alpha * s2l_map_f[in_pos[k]] * coeffs[j];
			}
			sum[3] += alpha * coeffs[j];
		}
		for (k=0; k<4; k++) {
			out[k] = sum[k];
		}
		out += 4;
	}
}

static void xscale_up_ga(unsigned char *in, int width_in, float *out,
	int width_out)
{
	int i, j, k, smp_i;
	float alpha, coeffs[4], tx, sum[2];
	unsigned char *in_pos;

	for (i=0; i<width_out; i++) {
		smp_i = split_map(width_in, width_out, i, &tx) - 1;
		calc_coeffs(coeffs, tx, 4);
		sum[0] = sum[1] = 0.0f;
		for (j=0; j<4; j++) {
			in_pos = in + dim_safe(smp_i + j, width_in - 1) * 2;
			alpha = in_pos[1] / 255.0f;
			sum[0] += alpha * in_pos[0]/255.0f * coeffs[j];
			sum[1] += alpha * coeffs[j];
		}
		for (k=0; k<2; k++) {
			out[k] = sum[k];
		}
		out += 2;
	}
}

static void xscale_up_g(unsigned char *in, int width_in, float *out,
	int width_out)
{
	int i, j, smp_i;
	float coeffs[4], tx, sum;
	unsigned char *in_pos;

	for (i=0; i<width_out; i++) {
		smp_i = split_map(width_in, width_out, i, &tx) - 1;
		calc_coeffs(coeffs, tx, 4);
		sum = 0.0f;
		for (j=0; j<4; j++) {
			in_pos = in + dim_safe(smp_i + j, width_in - 1);
			sum += in_pos[0]/255.0f * coeffs[j];
		}
		out[0] = sum;
		out += 1;
	}
}

static void oil_xscale_up(unsigned char *in, int width_in, float *out,
	int width_out, enum oil_colorspace cs_in)
{
	switch(cs_in) {
	case OIL_CS_RGBX:
		xscale_up_rgbx(in, width_in, out, width_out);
		break;
	case OIL_CS_RGB:
		xscale_up_rgb(in, width_in, out, width_out);
		break;
	case OIL_CS_G:
		xscale_up_g(in, width_in, out, width_out);
		break;
	case OIL_CS_CMYK:
		xscale_up_cmyk(in, width_in, out, width_out);
		break;
	case OIL_CS_RGBA:
		xscale_up_rgba(in, width_in, out, width_out);
		break;
	case OIL_CS_GA:
		xscale_up_ga(in, width_in, out, width_out);
		break;
	case OIL_CS_UNKNOWN:
		break;
	}
}

/* Global function helpers */

/**
 * Given an oil_scale struct, map the next output scanline to a position &
 * offset in the input image.
 */
static int yscaler_map_pos(struct oil_scale *ys, float *ty)
{
	int target;
	target = split_map(ys->in_height, ys->out_height, ys->out_pos, ty);
	return target + ys->taps / 2;
}

/**
 * Return the index of the buffered scanline to use for the tap at position
 * pos.
 */
static int oil_yscaler_safe_idx(struct oil_scale *ys, int pos)
{
	int ret, max_height;

	max_height = ys->in_height - 1;
	ret = ys->target - ys->taps + 1 + pos;
	if (ret < 0) {
		return 0;
	} else if (ret > max_height) {
		return max_height;
	}
	return ret;
}

/* Global functions */
void oil_global_init()
{
	build_s2l();
	build_l2s_rights();
}

int oil_scale_init(struct oil_scale *os, int in_height, int out_height,
	int in_width, int out_width, enum oil_colorspace cs)
{
	if (!os || in_height > MAX_DIMENSION || out_height > MAX_DIMENSION ||
		in_height < 1 || out_height < 1 ||
		in_width > MAX_DIMENSION || out_width > MAX_DIMENSION ||
		in_width < 1 || out_width < 1) {
		return -1;
	}

	/* Lazy perform global init */
	if (!s2l_map_f[128]) {
		oil_global_init();
	}

	os->in_height = in_height;
	os->out_height = out_height;
	os->in_width = in_width;
	os->out_width = out_width;
	os->cs = cs;
	os->in_pos = 0;
	os->out_pos = 0;
	os->taps = calc_taps(in_height, out_height);
	os->target = yscaler_map_pos(os, &os->ty);
	os->sl_len = out_width * OIL_CMP(cs);
	os->coeffs_y = NULL;
	os->coeffs_x = NULL;
	os->borders = NULL;
	os->rb = NULL;
	os->virt = NULL;

	/**
	 * If we are horizontally shrinking, then allocate & pre-calculate
	 * coefficients.
	 */
	if (out_width <= in_width) {
		os->coeffs_x = malloc(128 * in_width);
		os->borders = malloc(sizeof(int) * out_width);
		if (!os->coeffs_x || !os->borders) {
			oil_scale_free(os);
			return -2;
		}
		xscale_calc_coeffs(in_width, out_width, os->coeffs_x,
			os->borders);
	}

	os->rb = malloc((long)os->sl_len * os->taps * sizeof(float));
	os->virt = malloc(os->taps * sizeof(float*));
	os->coeffs_y = malloc(os->taps * sizeof(float));
	if (!os->rb || !os->virt || !os->coeffs_y) {
		oil_scale_free(os);
		return -2;
	}

	return 0;
}

void oil_scale_free(struct oil_scale *os)
{
	if (!os) {
		return;
	}
	if (os->virt) {
		free(os->virt);
		os->virt = NULL;
	}
	if (os->rb) {
		free(os->rb);
		os->rb = NULL;
	}
	if (os->coeffs_y) {
		free(os->coeffs_y);
		os->coeffs_y = NULL;
	}

	if (os->coeffs_x) {
		free(os->coeffs_x);
		os->coeffs_x = NULL;
	}
	if (os->borders) {
		free(os->borders);
		os->borders = NULL;
	}
}

int oil_scale_slots(struct oil_scale *ys)
{
	int tmp, safe_target;
	tmp = ys->target + 1;
	safe_target = tmp > ys->in_height ? ys->in_height : tmp;
	return safe_target - ys->in_pos;
}

void oil_scale_in(struct oil_scale *os, unsigned char *in)
{
	float *tmp;

	tmp = os->rb + (os->in_pos % os->taps) * os->sl_len;
	os->in_pos++;
	if (os->coeffs_x) {
		oil_xscale_down(in, tmp, os->out_width, os->cs, os->coeffs_x,
			os->borders);
	} else {
		oil_xscale_up(in, os->in_width, tmp, os->out_width, os->cs);
	}
}

void oil_scale_out(struct oil_scale *ys, unsigned char *out)
{
	int i, idx;

	if (!ys || !out) {
		return;
	}

	for (i=0; i<ys->taps; i++) {
		idx = oil_yscaler_safe_idx(ys, i);
		ys->virt[i] = ys->rb + (idx % ys->taps) * ys->sl_len;
	}
	strip_scale(ys->virt, ys->taps, ys->sl_len, out, ys->coeffs_y, ys->ty,
		ys->cs);
	ys->out_pos++;
	ys->target = yscaler_map_pos(ys, &ys->ty);
}

int oil_fix_ratio(int src_width, int src_height, int *out_width,
	int *out_height)
{
	double width_ratio, height_ratio, tmp;
	int *adjust_dim;

	if (src_width < 1 || src_height < 1 || *out_width < 1 || *out_height < 1) {
		return -1; // bad argument
	}

	width_ratio = *out_width / (double)src_width;
	height_ratio = *out_height / (double)src_height;
	if (width_ratio < height_ratio) {
		tmp = round(width_ratio * src_height);
		adjust_dim = out_height;
	} else {
		tmp = round(height_ratio * src_width);
		adjust_dim = out_width;
	}
	if (tmp > INT_MAX) {
		return -2; // adjusted dimension out of range
	}
	*adjust_dim = tmp ? tmp : 1;
	return 0;
}
