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

#include "resample.h"
#include <stdint.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * Bicubic interpolation. 2 base taps on either side.
 */
#define TAPS 4

/**
 * 64-bit type that uses 1 bit for signedness, 33 bits for the integer, and 30
 * bits for the fraction.
 *
 * 0-29: fraction, 30-62: integer, 63: sign.
 *
 * Useful for storing the product of a fix1_30 type and an unsigned char.
 */
typedef int64_t fix33_30;

/**
 * We add this to a fix33_30 value in order to bump up rounding errors.
 *
 * The best possible value was determined by comparing to a reference
 * implementation and comparing values for the minimal number of errors.
 */
#define TOPOFF 8192

/**
 * Signed type that uses 1 bit for signedness, 1 bit for the integer, and 30
 * bits for the fraction.
 *
 * 0-29: fraction, 30: integer, 31: sign.
 *
 * Useful for storing coefficients.
 */
typedef int32_t fix1_30;
#define ONE_FIX1_30 (1<<30)

/**
 * Calculate the greatest common denominator between a and b.
 */
static uint32_t gcd(uint32_t a, uint32_t b)
{
	uint32_t c;
	while (a != 0) {
		c = a;
		a = b%a;
		b = c;
	}
	return b;
}

/**
 * Round and clamp a fix33_30 value between 0 and 255. Returns an unsigned char.
 */
static uint16_t clamp(fix33_30 x)
{
	x += 1 << 29;
	x = x < 0 ? 0 : (x > (65535L << 30) ? (65535L << 30) : x);
	return x >> 30;
}

static uint8_t clamp8(fix33_30 x)
{
	return (clamp(x) + (1 << 7)) / 257;
}

/**
 * Given input and output dimensions and an output position, return the
 * corresponding input position and put the sub-pixel remainder in rest.
 *
 * Map from a discreet dest coordinate to a continuous source coordinate.
 * The resulting coordinate can range from -0.5 to the maximum of the
 * destination image dimension.
 */
static int32_t split_map(uint32_t dim_in, uint32_t dim_out, uint32_t pos, float *rest)
{
	double smp;
	int32_t smp_i;

	smp = (pos + 0.5) * ((double)dim_in / dim_out) - 0.5;
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
static uint64_t calc_taps(uint32_t dim_in, uint32_t dim_out)
{
	uint64_t tmp;
	if (dim_out > dim_in) {
		return TAPS;
	}
	tmp = (uint64_t)TAPS * dim_in / dim_out;
	return tmp - (tmp & 1);
}

/**
 * Catmull-Rom interpolator.
 */
static float catrom(float x)
{
	if (x<1) {
		return ((3*x - 5)*x*x + 2) / 2;
	}
	return (((5 - x)*x - 8)*x + 4) / 2;
}

/**
 * Convert a single-precision float to a fix1_30 fixed point int. x must be
 * between 0 and 1.
 */
static fix1_30 f_to_fix1_30(float x)
{
	return x * ONE_FIX1_30;
}

/**
 * Given an offset tx, calculate TAPS * tap_mult coefficients.
 *
 * The coefficients are stored as fix1_30 fixed point ints in coeffs.
 */
static void calc_coeffs(fix1_30 *coeffs, float tx, uint32_t taps)
{
	uint32_t i;
	float tmp, tap_mult, fudge;
	fix1_30 tmp_fixed;

	tap_mult = (float)taps / TAPS;
	tx = 1 - tx - taps / 2;
	fudge = 1.0;

	for (i=0; i<taps; i++) {
		tmp = catrom(fabsf(tx) / tap_mult) / tap_mult;
		fudge -= tmp;
		tmp_fixed = f_to_fix1_30(tmp);
		coeffs[i] = tmp_fixed;
		tx += 1;
	}
	coeffs[taps / 2] += f_to_fix1_30(fudge);
}

/* bicubic y-scaler */

static uint8_t linear_sample_to_srgb(uint16_t in)
{
	double in_f, s1, s2, s3;
	if (in <= 248) {
		return (in * 3295 + 32768) >> 16;
	}
	in_f = in / 65535.0;
	s1 = sqrt(in_f);
	s2 = sqrt(s1);
	s3 = sqrt(s2);
	return (0.0427447 + 0.547242 * s1 + 0.928361 * s2 - 0.518123 * s3) * 255 + 0.5;
}

static void strip_scale_rgbx(uint16_t **in, uint32_t strip_height, size_t len,
	uint8_t *out, fix1_30 *coeffs)
{
	size_t i;
	uint32_t j;
	fix33_30 coeff, sum[3];

	for (i=0; i<len; i+=4) {
		sum[0] = sum[1] = sum[2] = 0;
		for (j=0; j<strip_height; j++) {
			coeff = coeffs[j];
			sum[0] += coeff * in[j][i];
			sum[1] += coeff * in[j][i + 1];
			sum[2] += coeff * in[j][i + 2];
		}
		out[0] = linear_sample_to_srgb(clamp(sum[0]));
		out[1] = linear_sample_to_srgb(clamp(sum[1]));
		out[2] = linear_sample_to_srgb(clamp(sum[2]));
		out[3] = 0;
		out += 4;
	}
}

static void strip_scale_rgb(uint16_t **in, uint32_t strip_height, size_t len,
	uint8_t *out, fix1_30 *coeffs)
{
	size_t i;
	uint32_t j;
	fix33_30 coeff, sum[3];

	for (i=0; i<len; i+=4) {
		sum[0] = sum[1] = sum[2] = 0;
		for (j=0; j<strip_height; j++) {
			coeff = coeffs[j];
			sum[0] += coeff * in[j][i];
			sum[1] += coeff * in[j][i + 1];
			sum[2] += coeff * in[j][i + 2];
		}
		out[0] = linear_sample_to_srgb(clamp(sum[0]));
		out[1] = linear_sample_to_srgb(clamp(sum[1]));
		out[2] = linear_sample_to_srgb(clamp(sum[2]));
		out += 3;
	}
}

static void strip_scale_g(uint16_t **in, uint32_t strip_height, size_t len,
	uint8_t *out, fix1_30 *coeffs)
{
	size_t i;
	uint32_t j;
	fix33_30 sum;

	for (i=0; i<len; i++) {
		sum = 0;
		for (j=0; j<strip_height; j++) {
			sum += (fix33_30)coeffs[j] * in[j][i];
		}
		out[i] = clamp8(sum);
	}
}

static void strip_scale_rgba(uint16_t **in, uint32_t strip_height, size_t len,
	uint8_t *out, fix1_30 *coeffs)
{
	size_t i;
	uint32_t j;
	fix33_30 coeff, sum[4];

	for (i=0; i<len; i+=4) {
		sum[0] = sum[1] = sum[2] = sum[3] = 0;
		for (j=0; j<strip_height; j++) {
			coeff = coeffs[j];
			sum[0] += coeff * in[j][i];
			sum[1] += coeff * in[j][i + 1];
			sum[2] += coeff * in[j][i + 2];
			sum[3] += coeff * in[j][i + 3];
		}
		out[0] = linear_sample_to_srgb(clamp(sum[0]));
		out[1] = linear_sample_to_srgb(clamp(sum[1]));
		out[2] = linear_sample_to_srgb(clamp(sum[2]));
		out[3] = clamp8(sum[3]);
		out += 4;
	}
}

static void strip_scale_cmyk(uint16_t **in, uint32_t strip_height, size_t len,
	uint8_t *out, fix1_30 *coeffs)
{
	size_t i;
	uint32_t j;
	fix33_30 coeff, sum[4];

	for (i=0; i<len; i+=4) {
		sum[0] = sum[1] = sum[2] = sum[3] = 0;
		for (j=0; j<strip_height; j++) {
			coeff = coeffs[j];
			sum[0] += coeff * in[j][i];
			sum[1] += coeff * in[j][i + 1];
			sum[2] += coeff * in[j][i + 2];
			sum[3] += coeff * in[j][i + 3];
		}
		out[0] = clamp8(sum[0]);
		out[1] = clamp8(sum[1]);
		out[2] = clamp8(sum[2]);
		out[3] = clamp8(sum[3]);
		out += 4;
	}
}

static int strip_scale(uint16_t **in, uint32_t strip_height, size_t len, uint8_t *out,
	float ty, enum oil_colorspace cs)
{
	fix1_30 *coeffs;

	coeffs = malloc(strip_height * sizeof(fix1_30));
	if (!coeffs) {
		return -2; // unable to allocate
	}
	calc_coeffs(coeffs, ty, strip_height);

	switch(cs) {
	case OIL_CS_G:
	case OIL_CS_GA:
		strip_scale_g(in, strip_height, len, out, coeffs);
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
	}

	free(coeffs);
	return 0;
}

/* Bicubic x scaler */

static void sample_generic(uint32_t taps, fix1_30 *coeffs, uint16_t *in,
	uint16_t *out, uint8_t cmp)
{
	uint8_t i;
	uint32_t j;
	fix33_30 total, coeff;

	for (i=0; i<cmp; i++) {
		total = 0;
		for (j=0; j<taps; j++){
			coeff = coeffs[j];
			total += coeff * in[j * cmp + i];
		}
		out[i] = clamp(total);
	}
}

static void sample_rgba(uint32_t taps, fix1_30 *coeffs, uint16_t *in,
	uint16_t *out)
{
	uint32_t i;
	fix33_30 sum[4], coeff;

	sum[0] = sum[1] = sum[2] = sum[3] = 0;
	for (i=0; i<taps; i++) {
		coeff = coeffs[i];
		sum[0] += coeff * in[0];
		sum[1] += coeff * in[1];
		sum[2] += coeff * in[2];
		sum[3] += coeff * in[3];
		in += 4;
	}
	out[0] = clamp(sum[0]);
	out[1] = clamp(sum[1]);
	out[2] = clamp(sum[2]);
	out[3] = clamp(sum[3]);
}

static void sample_rgbx(uint32_t taps, fix1_30 *coeffs, uint16_t *in,
	uint16_t *out)
{
	uint32_t i;
	fix33_30 sum[3], coeff;

	sum[0] = sum[1] = sum[2] = 0;
	for (i=0; i<taps; i++) {
		coeff = coeffs[i];
		sum[0] += coeff * in[0];
		sum[1] += coeff * in[1];
		sum[2] += coeff * in[2];
		in += 4;
	}
	out[0] = clamp(sum[0]);
	out[1] = clamp(sum[1]);
	out[2] = clamp(sum[2]);
	out[3] = 0;
}

static void xscale_set_sample(uint32_t taps, fix1_30 *coeffs, uint16_t *in,
	uint16_t *out, enum oil_colorspace cs)
{
	switch(cs) {
	case OIL_CS_G:
	case OIL_CS_GA:
	case OIL_CS_RGB:
		sample_generic(taps, coeffs, in, out, CS_TO_CMP(cs));
		break;
	case OIL_CS_RGBX:
		sample_rgbx(taps, coeffs, in, out);
		break;
	case OIL_CS_RGBA:
	case OIL_CS_CMYK:
		sample_rgba(taps, coeffs, in, out);
		break;
	}
}

static void padded_sl_extend_edges(uint16_t *buf, uint32_t width, size_t pad_len,
	uint8_t cmp)
{
	uint16_t *pad_right;
	size_t i;
	pad_right = buf + pad_len + (size_t)width * cmp;
	for (i=0; i<pad_len; i++) {
		buf[i] = (buf + pad_len)[i % cmp];
		pad_right[i] = (pad_right - cmp)[i % cmp];
	}
}

static size_t padded_sl_len_offset(uint32_t in_width, uint32_t out_width,
	uint8_t cmp, size_t *offset)
{
	uint64_t taps;
	taps = calc_taps(in_width, out_width);
	*offset = (taps / 2 + 1) * cmp;
	return ((size_t)in_width * cmp + *offset * 2) * sizeof(uint16_t);
}

static int xscale_padded(uint16_t *in, uint32_t in_width, uint16_t *out,
	uint32_t out_width, enum oil_colorspace cs)
{
	float tx;
	fix1_30 *coeffs;
	uint32_t i, j, in_chunk, out_chunk, scale_gcd;
	int32_t xsmp_i;
	uint64_t taps;
	uint16_t *out_pos, *tmp;
	uint8_t cmp;

	if (!in_width || !out_width) {
		return -1; // bad input parameter
	}

	cmp = CS_TO_CMP(cs);
	taps = calc_taps(in_width, out_width);
	coeffs = malloc(taps * sizeof(fix1_30));
	if (!coeffs) {
		return -2; // unable to allocate space for coefficients
	}

	scale_gcd = gcd(in_width, out_width);
	in_chunk = in_width / scale_gcd;
	out_chunk = out_width / scale_gcd;

	for (i=0; i<out_chunk; i++) {
		xsmp_i = split_map(in_width, out_width, i, &tx);
		calc_coeffs(coeffs, tx, taps);

		xsmp_i += 1 - taps / 2;
		out_pos = out + i * cmp;
		for (j=0; j<scale_gcd; j++) {
			tmp = in + xsmp_i * cmp;
			xscale_set_sample(taps, coeffs, tmp, out_pos, cs);
			out_pos += out_chunk * cmp;
			xsmp_i += in_chunk;
		}
	}

	free(coeffs);
	return 0;
}

/* scanline ring buffer */

static int sl_rbuf_init(struct sl_rbuf *rb, uint32_t height, size_t sl_len)
{
	rb->height = height;
	rb->count = 0;
	rb->length = sl_len;
	rb->buf = malloc(sl_len * height * sizeof(uint16_t));
	if (!rb->buf) {
		return -2;
	}
	rb->virt = malloc(sizeof(uint8_t *) * height);
	if (!rb->virt) {
		free(rb->buf);
		return -2;
	}
	return 0;
}

static void sl_rbuf_free(struct sl_rbuf *rb)
{
	free(rb->buf);
	free(rb->virt);
}

static uint16_t *sl_rbuf_next(struct sl_rbuf *rb)
{
	return rb->buf + (rb->count++ % rb->height) * rb->length;
}

static uint16_t **sl_rbuf_virt(struct sl_rbuf *rb, uint32_t last_target)
{
	uint32_t i, safe, height, last_idx;
	height = rb->height;
	last_idx = rb->count - 1;

	// Make sure we have the 1st scanline if extending upwards
	if (last_target < last_idx && last_idx > height - 1) {
		return 0;
	}

	for (i=0; i<height; i++) {
		safe = last_target < i ? 0 : last_target - i;
		safe = safe > last_idx ? last_idx : safe;
		rb->virt[height - i - 1] = rb->buf + (safe % height) * rb->length;
	}
	return rb->virt;
}

/* xscaler */

static int xscaler_init(struct xscaler *xs, uint32_t width_in, uint32_t width_out,
	enum oil_colorspace cs)
{
	size_t psl_len, psl_offset;
	uint16_t *psl_buf;

	psl_len = padded_sl_len_offset(width_in, width_out, CS_TO_CMP(cs), &psl_offset);
	psl_buf = malloc(psl_len);
	if (!psl_buf) {
		return -2;
	}

	xs->psl_buf = psl_buf;
	xs->psl_offset = psl_offset;
	xs->psl_pos0 = psl_buf + psl_offset;
	xs->width_in = width_in;
	xs->width_out = width_out;
	xs->cs = cs;

	return 0;
}

static void xscaler_free(struct xscaler *xs)
{
	free(xs->psl_buf);
}

static void xscaler_scale(struct xscaler *xs, uint16_t *out_buf)
{
	padded_sl_extend_edges(xs->psl_buf, xs->width_in, xs->psl_offset, CS_TO_CMP(xs->cs));
	xscale_padded(xs->psl_pos0, xs->width_in, out_buf, xs->width_out, xs->cs);
}

/* yscaler */

static void yscaler_map_pos(struct yscaler *ys, uint32_t pos)
{
	long target;
	target = split_map(ys->in_height, ys->out_height, pos, &ys->ty);
	ys->target = target + ys->rb.height / 2;
}

int yscaler_init(struct yscaler *ys, uint32_t in_height, uint32_t out_height,
	uint32_t width, enum oil_colorspace cs)
{
	uint8_t cmp;
	int ret;
	uint32_t taps;
	taps = calc_taps(in_height, out_height);
	ys->in_height = in_height;
	ys->out_height = out_height;
	ys->width = width;
	ys->cs = cs;
	cmp = CS_TO_CMP(cs);
	if (cs == OIL_CS_RGB) {
		cmp = 4;
	}
	ret = sl_rbuf_init(&ys->rb, taps, width * cmp);
	yscaler_map_pos(ys, 0);
	return ret;
}

void yscaler_free(struct yscaler *ys)
{
	sl_rbuf_free(&ys->rb);
}

uint16_t *yscaler_next(struct yscaler *ys)
{
	if (ys->rb.count == ys->in_height || ys->rb.count > ys->target) {
		return 0;
	}
	return sl_rbuf_next(&ys->rb);
}

int yscaler_scale(struct yscaler *ys, uint8_t *out, uint32_t pos)
{
	int ret;
	uint16_t **virt;
	virt = sl_rbuf_virt(&ys->rb, ys->target);
	ret = strip_scale(virt, ys->rb.height, ys->rb.length, out, ys->ty, ys->cs);
	yscaler_map_pos(ys, pos + 1);
	return ret;
}

/* Color Space Helpers */

#define EXPAND8(X) ((X<<8) + X)

static uint16_t srgb_sample_to_linear(uint8_t x)
{
	static const uint16_t s2l_map[256] = {
		0x0000, 0x0014, 0x0028, 0x003c, 0x0050, 0x0063, 0x0077, 0x008b,
		0x009f, 0x00b3, 0x00c7, 0x00db, 0x00f1, 0x0108, 0x0120, 0x0139,
		0x0154, 0x016f, 0x018c, 0x01ab, 0x01ca, 0x01eb, 0x020e, 0x0232,
		0x0257, 0x027d, 0x02a5, 0x02ce, 0x02f9, 0x0325, 0x0353, 0x0382,
		0x03b3, 0x03e5, 0x0418, 0x044d, 0x0484, 0x04bc, 0x04f6, 0x0532,
		0x056f, 0x05ad, 0x05ed, 0x062f, 0x0673, 0x06b8, 0x06fe, 0x0747,
		0x0791, 0x07dd, 0x082a, 0x087a, 0x08ca, 0x091d, 0x0972, 0x09c8,
		0x0a20, 0x0a79, 0x0ad5, 0x0b32, 0x0b91, 0x0bf2, 0x0c55, 0x0cba,
		0x0d20, 0x0d88, 0x0df2, 0x0e5e, 0x0ecc, 0x0f3c, 0x0fae, 0x1021,
		0x1097, 0x110e, 0x1188, 0x1203, 0x1280, 0x1300, 0x1381, 0x1404,
		0x1489, 0x1510, 0x159a, 0x1625, 0x16b2, 0x1741, 0x17d3, 0x1866,
		0x18fb, 0x1993, 0x1a2c, 0x1ac8, 0x1b66, 0x1c06, 0x1ca7, 0x1d4c,
		0x1df2, 0x1e9a, 0x1f44, 0x1ff1, 0x20a0, 0x2150, 0x2204, 0x22b9,
		0x2370, 0x242a, 0x24e5, 0x25a3, 0x2664, 0x2726, 0x27eb, 0x28b1,
		0x297b, 0x2a46, 0x2b14, 0x2be3, 0x2cb6, 0x2d8a, 0x2e61, 0x2f3a,
		0x3015, 0x30f2, 0x31d2, 0x32b4, 0x3399, 0x3480, 0x3569, 0x3655,
		0x3742, 0x3833, 0x3925, 0x3a1a, 0x3b12, 0x3c0b, 0x3d07, 0x3e06,
		0x3f07, 0x400a, 0x4110, 0x4218, 0x4323, 0x4430, 0x453f, 0x4651,
		0x4765, 0x487c, 0x4995, 0x4ab1, 0x4bcf, 0x4cf0, 0x4e13, 0x4f39,
		0x5061, 0x518c, 0x52b9, 0x53e9, 0x551b, 0x5650, 0x5787, 0x58c1,
		0x59fe, 0x5b3d, 0x5c7e, 0x5dc2, 0x5f09, 0x6052, 0x619e, 0x62ed,
		0x643e, 0x6591, 0x66e8, 0x6840, 0x699c, 0x6afa, 0x6c5b, 0x6dbe,
		0x6f24, 0x708d, 0x71f8, 0x7366, 0x74d7, 0x764a, 0x77c0, 0x7939,
		0x7ab4, 0x7c32, 0x7db3, 0x7f37, 0x80bd, 0x8246, 0x83d1, 0x855f,
		0x86f0, 0x8884, 0x8a1b, 0x8bb4, 0x8d50, 0x8eef, 0x9090, 0x9235,
		0x93dc, 0x9586, 0x9732, 0x98e2, 0x9a94, 0x9c49, 0x9e01, 0x9fbb,
		0xa179, 0xa339, 0xa4fc, 0xa6c2, 0xa88b, 0xaa56, 0xac25, 0xadf6,
		0xafca, 0xb1a1, 0xb37b, 0xb557, 0xb737, 0xb919, 0xbaff, 0xbce7,
		0xbed2, 0xc0c0, 0xc2b1, 0xc4a5, 0xc69c, 0xc895, 0xca92, 0xcc91,
		0xce94, 0xd099, 0xd2a1, 0xd4ad, 0xd6bb, 0xd8cc, 0xdae0, 0xdcf7,
		0xdf11, 0xe12e, 0xe34e, 0xe571, 0xe797, 0xe9c0, 0xebec, 0xee1b,
		0xf04d, 0xf282, 0xf4ba, 0xf6f5, 0xf933, 0xfb74, 0xfdb8, 0xffff,
	};
	return s2l_map[x];
}

static void srgbx_preprocess_nx(uint8_t *in, uint16_t *out, uint32_t in_width, uint32_t n)
{
	uint32_t i, j;
	uint32_t sums[3];
	for (i=0; i<in_width/n; i++) {
		sums[0] = sums[1] = sums[2] = 0;
		for (j=0; j<n; j++) {
			sums[0] += srgb_sample_to_linear(in[0]);
			sums[1] += srgb_sample_to_linear(in[1]);
			sums[2] += srgb_sample_to_linear(in[2]);
			in += 4;
		}
		out[0] = sums[0] / n;
		out[1] = sums[1] / n;
		out[2] = sums[2] / n;
		out[3] = 0;
		out += 4;
	}
}

static void srgba_preprocess_nx(uint8_t *in, uint16_t *out, uint32_t in_width, uint32_t n)
{
	uint32_t i, j;
	uint32_t sums[4];
	for (i=0; i<in_width/n; i++) {
		sums[0] = sums[1] = sums[2] = sums[3] = 0;
		for (j=0; j<n; j++) {
			sums[0] += srgb_sample_to_linear(in[0]);
			sums[1] += srgb_sample_to_linear(in[1]);
			sums[2] += srgb_sample_to_linear(in[2]);
			sums[3] += EXPAND8(in[3]);
			in += 4;
		}
		out[0] = sums[0] / n;
		out[1] = sums[1] / n;
		out[2] = sums[2] / n;
		out[3] = sums[3] / n;
		out += 4;
	}
}

static void srgb_preprocess_nx(uint8_t *in, uint16_t *out, uint32_t in_width, uint32_t n)
{
	uint32_t i, j;
	uint32_t sums[3];
	for (i=0; i<in_width/n; i++) {
		sums[0] = sums[1] = sums[2] = 0;
		for (j=0; j<n; j++) {
			sums[0] += srgb_sample_to_linear(in[0]);
			sums[1] += srgb_sample_to_linear(in[1]);
			sums[2] += srgb_sample_to_linear(in[2]);
			in += 3;
		}
		out[0] = sums[0] / n;
		out[1] = sums[1] / n;
		out[2] = sums[2] / n;
		out[3] = 0;
		out += 4;
	}
}

static void g_preprocess_nx(uint8_t *in, uint16_t *out, uint32_t in_width, uint32_t n)
{
	uint32_t i, j;
	uint32_t sum;
	for (i=0; i<in_width/n; i++) {
		sum = 0;
		for (j=0; j<n; j++) {
			sum += EXPAND8(in[0]);
			in++;
		}
		out[0] = sum / n;
		out++;
	}
}

static void ga_preprocess_nx(uint8_t *in, uint16_t *out, uint32_t in_width, uint32_t n)
{
	uint32_t i, j;
	uint32_t sums[2];
	for (i=0; i<in_width/n; i++) {
		sums[0] = sums[1] = 0;
		for (j=0; j<n; j++) {
			sums[0] += EXPAND8(in[0]);
			sums[1] += EXPAND8(in[1]);
			in += 2;
		}
		out[0] = sums[0] / n;
		out[1] = sums[1] / n;
		out += 2;
	}
}

static void cmyk_preprocess_nx(uint8_t *in, uint16_t *out, uint32_t in_width, uint32_t n)
{
	uint32_t i, j;
	uint32_t sums[4];
	for (i=0; i<in_width/n; i++) {
		sums[0] = sums[1] = sums[2] = sums[3] = 0;
		for (j=0; j<n; j++) {
			sums[0] += EXPAND8(in[0]);
			sums[1] += EXPAND8(in[1]);
			sums[2] += EXPAND8(in[2]);
			sums[3] += EXPAND8(in[3]);
			in += 4;
		}
		out[0] = sums[0] / n;
		out[1] = sums[1] / n;
		out[2] = sums[2] / n;
		out[3] = sums[3] / n;
		out += 4;
	}
}

static uint32_t calc_pre_shrink(uint32_t dim_in, uint32_t dim_out)
{
	uint32_t max;
	max = (2 * dim_in) / dim_out / 3;
	if (max >= 4 && (dim_in % 4) == 0) {
		return 4;
	}
	if (max >= 2 && (dim_in % 2) == 0) {
		return 2;
	}
	return 1;
}

int preprocess_xscaler_init(struct preprocess_xscaler *pxs, uint32_t width_in,
	uint32_t width_out, enum oil_colorspace cs_in)
{
	enum oil_colorspace cs_out;

	pxs->width_in = width_in;
	pxs->cs_in = cs_in;
	pxs->scale_factor = calc_pre_shrink(width_in, width_out);

	/* Auto promote rgb components to rgbx for performance */
	cs_out = cs_in == OIL_CS_RGB ? OIL_CS_RGBX : cs_in;
	return xscaler_init(&pxs->xs, width_in / pxs->scale_factor, width_out, cs_out);
}

void preprocess_xscaler_free(struct preprocess_xscaler *pxs)
{
	xscaler_free(&pxs->xs);
}

static void pre_convert_g(uint8_t *in, uint16_t *out,
	uint32_t width_in, uint32_t scale_factor)
{
	switch(scale_factor) {
	case 1:
		g_preprocess_nx(in, out, width_in, 1);
		break;
	case 2:
		g_preprocess_nx(in, out, width_in, 2);
		break;
	case 4:
		g_preprocess_nx(in, out, width_in, 4);
		break;
	}
}

static void pre_convert_ga(uint8_t *in, uint16_t *out,
	uint32_t width_in, uint32_t scale_factor)
{
	switch(scale_factor) {
	case 1:
		ga_preprocess_nx(in, out, width_in, 1);
		break;
	case 2:
		ga_preprocess_nx(in, out, width_in, 2);
		break;
	case 4:
		ga_preprocess_nx(in, out, width_in, 4);
		break;
	}
}

static void pre_convert_cmyk(uint8_t *in, uint16_t *out,
	uint32_t width_in, uint32_t scale_factor)
{
	switch(scale_factor) {
	case 1:
		cmyk_preprocess_nx(in, out, width_in, 1);
		break;
	case 2:
		cmyk_preprocess_nx(in, out, width_in, 2);
		break;
	case 4:
		cmyk_preprocess_nx(in, out, width_in, 4);
		break;
	}
}

static void pre_convert_rgbx(uint8_t *in, uint16_t *out,
	uint32_t width_in, uint32_t scale_factor)
{
	switch(scale_factor) {
	case 1:
		srgbx_preprocess_nx(in, out, width_in, 1);
		break;
	case 2:
		srgbx_preprocess_nx(in, out, width_in, 2);
		break;
	case 4:
		srgbx_preprocess_nx(in, out, width_in, 4);
		break;
	}
}

static void pre_convert_rgba(uint8_t *in, uint16_t *out,
	uint32_t width_in, uint32_t scale_factor)
{
	switch(scale_factor) {
	case 1:
		srgba_preprocess_nx(in, out, width_in, 1);
		break;
	case 2:
		srgba_preprocess_nx(in, out, width_in, 2);
		break;
	case 4:
		srgba_preprocess_nx(in, out, width_in, 4);
		break;
	}
}

static void pre_convert_rgb(uint8_t *in, uint16_t *out,
	uint32_t width_in, uint32_t scale_factor)
{
	switch(scale_factor) {
	case 1:
		srgb_preprocess_nx(in, out, width_in, 1);
		break;
	case 2:
		srgb_preprocess_nx(in, out, width_in, 2);
		break;
	case 4:
		srgb_preprocess_nx(in, out, width_in, 4);
		break;
	}
}

void preprocess_xscaler_scale(struct preprocess_xscaler *pxs, uint8_t *in,
	uint16_t *out)
{
	switch(pxs->cs_in) {
	case OIL_CS_G:
		pre_convert_g(in, pxs->xs.psl_pos0, pxs->width_in, pxs->scale_factor);
		break;
	case OIL_CS_GA:
		pre_convert_ga(in, pxs->xs.psl_pos0, pxs->width_in, pxs->scale_factor);
		break;
	case OIL_CS_RGB:
		pre_convert_rgb(in, pxs->xs.psl_pos0, pxs->width_in, pxs->scale_factor);
		break;
	case OIL_CS_RGBX:
		pre_convert_rgbx(in, pxs->xs.psl_pos0, pxs->width_in, pxs->scale_factor);
		break;
	case OIL_CS_RGBA:
		pre_convert_rgba(in, pxs->xs.psl_pos0, pxs->width_in, pxs->scale_factor);
		break;
	case OIL_CS_CMYK:
		pre_convert_cmyk(in, pxs->xs.psl_pos0, pxs->width_in, pxs->scale_factor);
		break;
	}
	xscaler_scale(&pxs->xs, out);
}


/* Utility helpers */
void fix_ratio(uint32_t src_width, uint32_t src_height, uint32_t *out_width,
	uint32_t *out_height)
{
	double width_ratio, height_ratio;

	width_ratio = *out_width / (double)src_width;
	height_ratio = *out_height / (double)src_height;
	if (width_ratio < height_ratio) {
		*out_height = round(width_ratio * src_height);
		*out_height = *out_height ? *out_height : 1;
	} else {
		*out_width = round(height_ratio * src_width);
		*out_width = *out_width ? *out_width : 1;
	}
}
