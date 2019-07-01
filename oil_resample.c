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
#include <string.h>

/**
 * When shrinking a 10 million pixel wide scanline down to a single pixel, we
 * reach the limits of single-precision floats. Limit input dimensions to one
 * million by one million pixels to avoid this issue as well as overflow issues
 * with 32-bit ints.
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
 * Convert a float to an int. When compiling on x86 without march=native, this
 * performs much better than roundf().
 */
static int f2i(float x)
{
	return x + 0.5f;
}

/**
 * Convert a float to 8-bit integer.
 */
static int clamp8(float x)
{
	return f2i(clampf(x) * 255.0f);
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
	if (x<1) {
		return (1.5f*x - 2.5f)*x*x + 1;
	}
	return (((5 - x)*x - 8)*x + 4) / 2;
}

/**
 * Given an offset tx, calculate taps coefficients.
 */
static void calc_coeffs(float *coeffs, float tx, int taps, int ltrim, int rtrim)
{
	int i;
	float tmp, tap_mult, fudge;

	tap_mult = (float)taps / TAPS;
	tx = 1 - tx - taps / 2 + ltrim;
	fudge = 0.0f;

	for (i=ltrim; i<taps-rtrim; i++) {
		tmp = catrom(fabsf(tx) / tap_mult) / tap_mult;
		fudge += tmp;
		coeffs[i] = tmp;
		tx += 1;
	}
	fudge = 1 / fudge;
	for (i=ltrim; i<taps-rtrim; i++) {
		coeffs[i] *= fudge;
	}
}

/**
 * Pre-calculated table of linear to srgb mappings. Initialized via build_l2s().
 *
 * catmull-rom interpolation can produce values from -17/64 to 81/64.
 *
 * The total allocated space will be split into three parts:
 *   * 17/98 of padding below zero
 *   * 64/98 of mapping
 *   * 17/98 of padding above one
 */
#define L2S_ALL_LEN 32768
static unsigned char l2s_map_all[L2S_ALL_LEN];
static int l2s_len;
static unsigned char *l2s_map;

static void build_l2s(void)
{
	int i, padding;
	double srgb_f, tmp, val;

	padding = L2S_ALL_LEN * 17 / 98;
	l2s_len = L2S_ALL_LEN - 2 * padding;
	l2s_map = l2s_map_all + padding;

	for (i=0; i<l2s_len; i++) {
		srgb_f = (i + 0.5)/(l2s_len - 1);
		if (srgb_f <= 0.00313) {
			val = srgb_f * 12.92;
		} else {
			tmp = pow(srgb_f, 1/2.4);
			val = 1.055 * tmp - 0.055;
		}

		l2s_map[i] = round(val * 255);
	}

	for (i=0; i<padding; i++) {
		l2s_map[l2s_len + i] = 255;
	}
}

/**
 * Maps the given linear RGB float to sRGB integer.
 */
static unsigned char linear_sample_to_srgb(float in)
{
	return l2s_map[(int)(in * (l2s_len - 1))];
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
 * Takes an array of 4 floats and shifts them left. The rightmost element is
 * set to the given value.
 */
static void push_f(float *f, float val)
{
	f[0] = f[1];
	f[1] = f[2];
	f[2] = f[3];
	f[3] = val;
}

/**
 * Takes an array of 4 floats and shifts them left. The rightmost element is
 * set to 0.0.
 */
static void shift_left_f(float *f)
{
	push_f(f, 0.0f);
}

static void reduce_strip(float *in, int strip_height, int len, float *coeffs,
	float *sums, int n)
{
	int i, j;

	for (i=0; i<strip_height; i++) {
		for (j=0; j<n; j++) {
			add_sample_to_sum_f(in[i * len + j], coeffs + i * 4, sums + j * 4);
		}
	}
}

/**
 * Resizes a strip of RGBX scanlines to a single scanline.
 */
static void yscale_down_rgbx(float *in, int strip_height, int len,
	unsigned char *out, float *coeffs, float *sums)
{
	int i, j;

	for (i=0; i<len; i+=4) {
		reduce_strip(in, strip_height, len, coeffs, sums, 3);
		for (j=0; j<3; j++) {
			out[j] = linear_sample_to_srgb(sums[j * 4]);
			shift_left_f(sums + j * 4);
		}
		out[3] = 0;
		sums += 12;
		out += 4;
		in += 4;
	}
}

/**
 * Resizes a strip of RGB scanlines to a single scanline.
 */
static void yscale_down_rgb(float *in, int strip_height, int len,
	unsigned char *out, float *coeffs, float *sums)
{
	int i, j;

	for (i=0; i<len; i+=3) {
		reduce_strip(in, strip_height, len, coeffs, sums, 3);
		for (j=0; j<3; j++) {
			out[j] = linear_sample_to_srgb(sums[j * 4]);
			shift_left_f(sums + j * 4);
		}
		sums += 12;
		out += 3;
		in += 3;
	}
}

/**
 * Resizes a strip of greyscale scanlines to a single scanline.
 */
static void yscale_down_g(float *in, int strip_height, int len,
	unsigned char *out, float *coeffs, float *sums)
{
	int i;

	for (i=0; i<len; i++) {
		reduce_strip(in + i, strip_height, len, coeffs, sums, 1);
		out[i] = clamp8(sums[0]);
		shift_left_f(sums);
		sums += 4;
	}
}

/**
 * Resizes a strip of greyscale-alpha scanlines to a single scanline.
 */
static void yscale_down_ga(float *in, int strip_height, int len,
	unsigned char *out, float *coeffs, float *sums)
{
	int i;
	float alpha;

	for (i=0; i<len; i+=2) {
		reduce_strip(in, strip_height, len, coeffs, sums, 2);
		alpha = clampf(sums[4]);
		if (alpha != 0) {
			sums[0] /= alpha;
		}
		out[0] = clamp8(sums[0]);
		shift_left_f(sums);
		out[1] = f2i(alpha * 255.0f);
		shift_left_f(sums + 4);
		sums += 8;
		out += 2;
		in += 2;
	}
}

/**
 * Resizes a strip of RGB-alpha scanlines to a single scanline.
 */
static void yscale_down_rgba(float *in, int strip_height, int len,
	unsigned char *out, float *coeffs, float *sums)
{
	int i, j;
	float alpha;

	for (i=0; i<len; i+=4) {
		reduce_strip(in, strip_height, len, coeffs, sums, 4);
		alpha = clampf(sums[12]);
		if (alpha != 0) {
			for (j=0; j<3; j++) {
				sums[j * 4] /= alpha;
			}
		}
		for (j=0; j<3; j++) {
			out[j] = linear_sample_to_srgb(clampf(sums[j * 4]));
			shift_left_f(sums + j * 4);
		}
		out[3] = round(alpha * 255.0f);
		shift_left_f(sums + 12);
		sums += 16;
		out += 4;
		in += 4;
	}
}

/**
 * Downscale a strip of scanlines. Branches to the correct interpolator using
 * the given colorspace.
 */
static void yscale_down(float *in, int strip_height, int len,
	unsigned char *out, float *coeffs, float *sums, enum oil_colorspace cs)
{
	switch(cs) {
	case OIL_CS_G:
	case OIL_CS_CMYK:
		yscale_down_g(in, strip_height, len, out, coeffs, sums);
		break;
	case OIL_CS_GA:
		yscale_down_ga(in, strip_height, len, out, coeffs, sums);
		break;
	case OIL_CS_RGB:
		yscale_down_rgb(in, strip_height, len, out, coeffs, sums);
		break;
	case OIL_CS_RGBX:
		yscale_down_rgbx(in, strip_height, len, out, coeffs, sums);
		break;
	case OIL_CS_RGBA:
		yscale_down_rgba(in, strip_height, len, out, coeffs, sums);
		break;
	case OIL_CS_UNKNOWN:
		break;
	}
}

static void yscale_up_g_cmyk(float **in, int len, float *coeffs,
	unsigned char *out)
{
	int i;
	float sum;

	for (i=0; i<len; i++) {
		sum = coeffs[0] * in[0][i] +
			coeffs[1] * in[1][i] +
			coeffs[2] * in[2][i] +
			coeffs[3] * in[3][i];
		out[i] = clamp8(sum);
	}
}

static void yscale_up_ga(float **in, int len, float *coeffs,
	unsigned char *out)
{
	int i, j;
	float alpha, sums[2];

	for (i=0; i<len; i+=2) {
		for (j=0; j<2; j++) {
			sums[j] = coeffs[0] * in[0][i + j] +
				coeffs[1] * in[1][i + j] +
				coeffs[2] * in[2][i + j] +
				coeffs[3] * in[3][i + j];
		}
		alpha = clampf(sums[1]);
		if (alpha != 0) {
			sums[0] /= alpha;
		}
		out[i] = clamp8(sums[0]);
		out[i + 1] = f2i(alpha * 255.0f);
	}
}

static void yscale_up_rgb(float **in, int len, float *coeffs,
	unsigned char *out)
{
	int i;
	float sum;

	for (i=0; i<len; i++) {
		sum = coeffs[0] * in[0][i] +
			coeffs[1] * in[1][i] +
			coeffs[2] * in[2][i] +
			coeffs[3] * in[3][i];
		out[i] = linear_sample_to_srgb(sum);
	}
}

static void yscale_up_rgbx(float **in, int len, float coeffs[4],
	unsigned char *out)
{
	int i, j;
	float sum;

	for (i=0; i<len; i+=4) {
		for (j=0; j<3; j++) {
			sum = coeffs[0] * in[0][i + j] +
				coeffs[1] * in[1][i + j] +
				coeffs[2] * in[2][i + j] +
				coeffs[3] * in[3][i + j];
			out[i + j] = linear_sample_to_srgb(sum);
		}
		out[i + 3] = 0;
	}
}

static void yscale_up_rgba(float **in, int len, float *coeffs,
	unsigned char *out)
{
	int i, j;
	float alpha, sums[4];

	for (i=0; i<len; i+=4) {
		for (j=0; j<4; j++) {
			sums[j] = coeffs[0] * in[0][i + j] +
				coeffs[1] * in[1][i + j] +
				coeffs[2] * in[2][i + j] +
				coeffs[3] * in[3][i + j];
		}
		alpha = clampf(sums[3]);
		for (j=0; j<3; j++) {
			if (alpha != 0 && alpha != 1.0f) {
				sums[j] /= alpha;
				sums[j] = clampf(sums[j]);
			}
			out[i + j] = linear_sample_to_srgb(sums[j]);
		}
		out[i + 3] = f2i(alpha * 255.0f);
	}
}

/**
 * Upscale a strip of scanlines. Branches to the correct interpolator using
 * the given colorspace.
 */
static void yscale_up(float **in, int len, float *coeffs, unsigned char *out,
	enum oil_colorspace cs)
{
	switch(cs) {
	case OIL_CS_G:
	case OIL_CS_CMYK:
		yscale_up_g_cmyk(in, len, coeffs, out);
		break;
	case OIL_CS_GA:
		yscale_up_ga(in, len, coeffs, out);
		break;
	case OIL_CS_RGB:
		yscale_up_rgb(in, len, coeffs, out);
		break;
	case OIL_CS_RGBX:
		yscale_up_rgbx(in, len, coeffs, out);
		break;
	case OIL_CS_RGBA:
		yscale_up_rgba(in, len, coeffs, out);
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
static float s2l_map[256];

/**
 * Populates s2l_map.
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
		s2l_map[input] = val;
	}
}

static float i2f_map[256];

static void build_i2f(void)
{
	int i;

	for (i=0; i<=255; i++) {
		i2f_map[i] = i / 255.0f;
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
	int *border_buf, float *tmp_coeffs)
{
	int smp_i, i, j, taps, offset, pos, ltrim, rtrim, smp_end, smp_start,
		ends[4];
	float tx;

	taps = calc_taps(in_width, out_width);
	for (i=0; i<4; i++) {
		ends[i] = -1;
	}

	for (i=0; i<out_width; i++) {
		smp_i = split_map(in_width, out_width, i, &tx);

		smp_start = smp_i - (taps/2 - 1);
		smp_end = smp_i + taps/2;
		if (smp_end >= in_width) {
			smp_end = in_width - 1;
		}
		ends[i%4] = smp_end;
		border_buf[i] = smp_end - ends[(i+3)%4];

		ltrim = 0;
		if (smp_start < 0) {
			ltrim = -1 * smp_start;
		}
		rtrim = smp_start + (taps - 1) - smp_end;
		calc_coeffs(tmp_coeffs, tx, taps, ltrim, rtrim);

		for (j=ltrim; j<taps - rtrim; j++) {
			pos = smp_start + j;

			offset = 3;
			if (pos > ends[(i+3)%4]) {
				offset = 0;
			} else if (pos > ends[(i+2)%4]) {
				offset = 1;
			} else if (pos > ends[(i+1)%4]) {
				offset = 2;
			}

			coeff_buf[pos * 4 + offset] = tmp_coeffs[j];
		}
	}
}

/**
 * Precalculate coefficients and borders for an upscale.
 *
 * coeff_buf will be populated with 4 input coefficients for every output
 * sample.
 *
 * border_buf will be populated with the number of output samples to produce
 * for every input sample.
 *
 * users of coeff_buf & border_buf are expected to keep a buffer of the last 4
 * input samples, and multiply them with each output sample's coefficients.
 */
static void scale_up_coeffs(int in_width, int out_width, float *coeff_buf,
	int *border_buf)
{
	int i, smp_i, start, end, ltrim, rtrim, safe_end, max_pos;
	float tx;

	max_pos = in_width - 1;
	for (i=0; i<out_width; i++) {
		smp_i = split_map(in_width, out_width, i, &tx);
		start = smp_i - 1;
		end = smp_i + 2;

		// This is the border position at which we will tell the
		// interpolator to calculate the output sample.
		safe_end = end > max_pos ? max_pos : end;

		ltrim = 0;
		rtrim = 0;
		if (start < 0) {
			ltrim = -1 * start;
		}
		if (end > max_pos) {
			rtrim = end - max_pos;
		}

		border_buf[safe_end] += 1;

		// we offset coeff_buf by rtrim because the interpolator won't
		// be pushing any more samples into its sample buffer.
		calc_coeffs(coeff_buf + rtrim, tx, 4, ltrim, rtrim);

		coeff_buf += 4;
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
		for (j=0; j<border_buf[i]; j++) {
			for (k=0; k<3; k++) {
				add_sample_to_sum_f(s2l_map[in[k]], coeff_buf, sum[k]);
			}
			in += 4;
			coeff_buf += 4;
		}
		dump_out(out, sum, 3);
		out += 4;
	}
}

static void xscale_down_rgb(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j, k;
	float sum[3][4] = {{ 0.0f }};

	for (i=0; i<out_width; i++) {
		for (j=0; j<border_buf[i]; j++) {
			for (k=0; k<3; k++) {
				add_sample_to_sum_f(s2l_map[in[k]], coeff_buf, sum[k]);
			}
			in += 3;
			coeff_buf += 4;
		}
		dump_out(out, sum, 3);
		out += 3;
	}
}

static void xscale_down_g(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j;
	float sum[4] = { 0.0f };

	for (i=0; i<out_width; i++) {
		for (j=0; j<border_buf[i]; j++) {
			add_sample_to_sum_f(i2f_map[in[0]], coeff_buf, sum);
			in += 1;
			coeff_buf += 4;
		}
		out[0] = sum[0];
		shift_left_f(sum);
		out += 1;
	}
}

static void xscale_down_cmyk(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j, k;
	float sum[4][4] = {{ 0.0f }};

	for (i=0; i<out_width; i++) {
		for (j=0; j<border_buf[i]; j++) {
			for (k=0; k<4; k++) {
				add_sample_to_sum_f(i2f_map[in[k]], coeff_buf, sum[k]);
			}
			in += 4;
			coeff_buf += 4;
		}
		dump_out(out, sum, 4);
		out += 4;
	}
}

static void xscale_down_rgba(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j, k;
	float alpha, sum[4][4] = {{ 0.0f }};

	for (i=0; i<out_width; i++) {
		for (j=0; j<border_buf[i]; j++) {
			alpha = i2f_map[in[3]];
			for (k=0; k<3; k++) {
				add_sample_to_sum_f(s2l_map[in[k]] * alpha, coeff_buf, sum[k]);
			}
			add_sample_to_sum_f(alpha, coeff_buf, sum[3]);
			in += 4;
			coeff_buf += 4;
		}
		dump_out(out, sum, 4);
		out += 4;
	}
}

static void xscale_down_ga(unsigned char *in, float *out,
	int out_width, float *coeff_buf, int *border_buf)
{
	int i, j;
	float alpha, sum[2][4] = {{ 0.0f }};

	for (i=0; i<out_width; i++) {
		for (j=0; j<border_buf[i]; j++) {
			alpha = i2f_map[in[1]];
			add_sample_to_sum_f(i2f_map[in[0]] * alpha, coeff_buf, sum[0]);
			add_sample_to_sum_f(alpha, coeff_buf, sum[1]);
			in += 2;
			coeff_buf += 4;
		}
		dump_out(out, sum, 2);
		out += 2;
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

static void xscale_up_reduce_n(float in[][4], float *out, float *coeffs,
	int cmp)
{
	int i;

	for (i=0; i<cmp; i++) {
		out[i] = in[i][0] * coeffs[0] +
			in[i][1] * coeffs[1] +
			in[i][2] * coeffs[2] +
			in[i][3] * coeffs[3];
	}
}

static void xscale_up_rgbx(unsigned char *in, int width_in, float *out,
	float *coeff_buf, int *border_buf)
{
	int i, j;
	float smp[3][4] = {{0}};

	for (i=0; i<width_in; i++) {
		for (j=0; j<3; j++) {
			push_f(smp[j], s2l_map[in[j]]);
		}
		for (j=0; j<border_buf[i]; j++) {
			xscale_up_reduce_n(smp, out, coeff_buf, 3);
			out[3] = 0;
			out += 4;
			coeff_buf += 4;
		}
		in += 4;
	}
}

static void xscale_up_rgb(unsigned char *in, int width_in, float *out,
	float *coeff_buf, int *border_buf)
{
	int i, j;
	float smp[3][4] = {{0}};

	for (i=0; i<width_in; i++) {
		for (j=0; j<3; j++) {
			push_f(smp[j], s2l_map[in[j]]);
		}
		for (j=0; j<border_buf[i]; j++) {
			xscale_up_reduce_n(smp, out, coeff_buf, 3);
			out += 3;
			coeff_buf += 4;
		}
		in += 3;
	}
}

static void xscale_up_cmyk(unsigned char *in, int width_in, float *out,
	float *coeff_buf, int *border_buf)
{
	int i, j;
	float smp[4][4] = {{0}};

	for (i=0; i<width_in; i++) {
		for (j=0; j<4; j++) {
			push_f(smp[j], in[j] / 255.0f);
		}
		for (j=0; j<border_buf[i]; j++) {
			xscale_up_reduce_n(smp, out, coeff_buf, 4);
			out += 4;
			coeff_buf += 4;
		}
		in += 4;
	}
}

static void xscale_up_rgba(unsigned char *in, int width_in, float *out,
	float *coeff_buf, int *border_buf)
{
	int i, j;
	float smp[4][4] = {{0}};

	for (i=0; i<width_in; i++) {
		push_f(smp[3], in[3] / 255.0f);
		for (j=0; j<3; j++) {
			push_f(smp[j], smp[3][3] * s2l_map[in[j]]);
		}
		for (j=0; j<border_buf[i]; j++) {
			xscale_up_reduce_n(smp, out, coeff_buf, 4);
			out += 4;
			coeff_buf += 4;
		}
		in += 4;
	}
}

static void xscale_up_ga(unsigned char *in, int width_in, float *out,
	float *coeff_buf, int *border_buf)
{
	int i, j;
	float smp[2][4] = {{0}};

	for (i=0; i<width_in; i++) {
		push_f(smp[1], in[1] / 255.0f);
		push_f(smp[0], smp[1][3] * i2f_map[in[0]]);
		for (j=0; j<border_buf[i]; j++) {
			xscale_up_reduce_n(smp, out, coeff_buf, 2);
			out += 2;
			coeff_buf += 4;
		}
		in += 2;
	}
}

static void xscale_up_g(unsigned char *in, int width_in, float *out,
	float *coeff_buf, int *border_buf)
{
	int i, j;
	float smp[4] = {0};

	for (i=0; i<width_in; i++) {
		push_f(smp, in[i] / 255.0f);
		for (j=0; j<border_buf[i]; j++) {
			out[0] = smp[0] * coeff_buf[0] +
				smp[1] * coeff_buf[1] +
				smp[2] * coeff_buf[2] +
				smp[3] * coeff_buf[3];
			out += 1;
			coeff_buf += 4;
		}
	}
}

static void oil_xscale_up(unsigned char *in, int width_in, float *out,
	enum oil_colorspace cs_in, float *coeff_buf, int *border_buf)
{
	switch(cs_in) {
	case OIL_CS_RGBX:
		xscale_up_rgbx(in, width_in, out, coeff_buf, border_buf);
		break;
	case OIL_CS_RGB:
		xscale_up_rgb(in, width_in, out, coeff_buf, border_buf);
		break;
	case OIL_CS_G:
		xscale_up_g(in, width_in, out, coeff_buf, border_buf);
		break;
	case OIL_CS_CMYK:
		xscale_up_cmyk(in, width_in, out, coeff_buf, border_buf);
		break;
	case OIL_CS_RGBA:
		xscale_up_rgba(in, width_in, out, coeff_buf, border_buf);
		break;
	case OIL_CS_GA:
		xscale_up_ga(in, width_in, out, coeff_buf, border_buf);
		break;
	case OIL_CS_UNKNOWN:
		break;
	}
}

/* Global functions */
void oil_global_init()
{
	build_s2l();
	build_l2s();
	build_i2f();
}

static int calc_coeffs_len(int in_dim, int out_dim)
{
	if (out_dim <= in_dim) {
		return 4 * in_dim * sizeof(float);
	}
	return 4 * out_dim * sizeof(float);
}

static int calc_borders_len(int in_dim, int out_dim)
{
	return (out_dim <= in_dim ? out_dim : in_dim) * sizeof(int);
}

static void set_coeffs(int in_dim, int out_dim, float *coeffs, int *borders,
	float *tmp)
{
	if (out_dim <= in_dim) {
		xscale_calc_coeffs(in_dim, out_dim, coeffs, borders, tmp);
	} else {
		scale_up_coeffs(in_dim, out_dim, coeffs, borders);
	}
}

int oil_scale_init(struct oil_scale *os, int in_height, int out_height,
	int in_width, int out_width, enum oil_colorspace cs)
{
	int taps_x, taps_y, coeffs_x_len, coeffs_y_len, borders_x_len,
		borders_y_len, rb_len, sums_len, tmp_len;

	if (!os || in_height > MAX_DIMENSION || out_height > MAX_DIMENSION ||
		in_height < 1 || out_height < 1 ||
		in_width > MAX_DIMENSION || out_width > MAX_DIMENSION ||
		in_width < 1 || out_width < 1) {
		return -1;
	}

	// Lazy perform global init, in case oil_global_ini() hasn't been
	// called yet.
	if (!s2l_map[128]) {
		oil_global_init();
	}

	taps_x = calc_taps(in_width, out_width);
	taps_y = calc_taps(in_height, out_height);

	coeffs_x_len = calc_coeffs_len(in_width, out_width);
	borders_x_len = calc_borders_len(in_width, out_width);
	coeffs_y_len = calc_coeffs_len(in_height, out_height);
	borders_y_len = calc_borders_len(in_height, out_height);
	rb_len = out_width * OIL_CMP(cs) * taps_y * sizeof(float);
	tmp_len = (taps_x > taps_y ? taps_x : taps_y) * sizeof(float);
	sums_len = 0;
	if (out_height <= in_height) {
		sums_len = out_width * OIL_CMP(cs) * 4 * sizeof(float);
	}

	memset(os, 0, sizeof(struct oil_scale));
	os->in_height = in_height;
	os->out_height = out_height;
	os->in_width = in_width;
	os->out_width = out_width;
	os->cs = cs;
	os->coeffs_x = calloc(1, coeffs_x_len);
	os->borders_x = calloc(1, borders_x_len);
	os->coeffs_y = calloc(1, coeffs_y_len);
	os->borders_y = calloc(1, borders_y_len);
	os->rb = calloc(1, rb_len);
	os->sums_y = calloc(1, sums_len);
	os->tmp_coeffs = malloc(tmp_len);

	if (!os->coeffs_x || !os->borders_x || !os->coeffs_y ||
		!os->borders_y || !os->rb || !os->tmp_coeffs ||
		(sums_len && !os->sums_y)) {
		oil_scale_free(os);
		return -2;
	}

	set_coeffs(in_width, out_width, os->coeffs_x, os->borders_x, os->tmp_coeffs);
	set_coeffs(in_height, out_height, os->coeffs_y, os->borders_y, os->tmp_coeffs);

	return 0;
}

void oil_scale_restart(struct oil_scale *os)
{
	os->in_pos = os->out_pos = os->rows_in_rb = 0;
}

void oil_scale_free(struct oil_scale *os)
{
	if (!os) {
		return;
	}

	free(os->rb);
	os->rb = NULL;
	free(os->coeffs_y);
	os->coeffs_y = NULL;
	free(os->coeffs_x);
	os->coeffs_x = NULL;
	free(os->borders_x);
	os->borders_x = NULL;
	free(os->borders_y);
	os->borders_y = NULL;
	free(os->sums_y);
	os->sums_y = NULL;
	free(os->tmp_coeffs);
	os->tmp_coeffs = NULL;
}

int oil_scale_slots(struct oil_scale *ys)
{
	int i;

	if (ys->out_height <= ys->in_height) {
		return ys->borders_y[ys->out_pos];
	} else {
		if (ys->in_pos == 0) {
			for (i=1; ys->borders_y[i - 1] == 0; i++);
			return i;
		}
		if (ys->borders_y[ys->in_pos - 1] > 0) {
			return 0;
		}
		for (i=1; ys->borders_y[ys->in_pos + i - 1] == 0; i++);
		return i;
	}
}

static float *get_rb_line(struct oil_scale *os, int line)
{
	int sl_len;
	sl_len = OIL_CMP(os->cs) * os->out_width;
	return os->rb + line * sl_len;
}

void oil_scale_in(struct oil_scale *os, unsigned char *in)
{
	float *tmp;

	if (os->out_height <= os->in_height) {
		tmp = get_rb_line(os, os->rows_in_rb);
	} else {
		tmp = get_rb_line(os, os->in_pos % 4);
	}
	if (os->out_width <= os->in_width) {
		oil_xscale_down(in, tmp, os->out_width, os->cs,
			os->coeffs_x, os->borders_x);
	} else {
		oil_xscale_up(in, os->in_width, tmp, os->cs,
			os->coeffs_x, os->borders_x);
	}
	os->rows_in_rb++;
	os->in_pos++;
}

void oil_scale_out(struct oil_scale *os, unsigned char *out)
{
	int i, sl_len;
	float *coeffs, *in[4];

	sl_len = OIL_CMP(os->cs) * os->out_width;
	if (os->out_height <= os->in_height) {
		coeffs = os->coeffs_y + (os->in_pos - os->rows_in_rb) * 4;
		yscale_down(os->rb, os->rows_in_rb, sl_len, out, coeffs,
			os->sums_y, os->cs);
		os->rows_in_rb = 0;
	} else {
		for (i=0; i<4; i++) {
			in[i] = get_rb_line(os, (os->in_pos + i) % 4);
		}
		yscale_up(in, sl_len, os->coeffs_y + os->out_pos * 4, out,
			os->cs);
		os->borders_y[os->in_pos - 1] -= 1;
	}

	os->out_pos++;
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
