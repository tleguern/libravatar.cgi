#ifndef LIBRAVATAR_H_
#define LIBRAVATAR_H_

#define _PATH_MM "/htdocs/avatars/mm.png"
#define _PATH_DEFAULT "/htdocs/avatars/default.png"

#define PNGBLANK_MAX_SIZE 255

enum interlace_method {
	INTERLACE_METHOD_STANDARD,
	INTERLACE_METHOD_ADAM7,
	INTERLACE_METHOD__MAX,
};

enum colour_type {
	COLOUR_TYPE_GREYSCALE,
	COLOUR_TYPE_FILLER1,
	COLOUR_TYPE_TRUECOLOUR,
	COLOUR_TYPE_INDEXED,
	COLOUR_TYPE_GREYSCALE_ALPHA,
	COLOUR_TYPE_FILLER5,
	COLOUR_TYPE_TRUECOLOUR_ALPHA,
	COLOUR_TYPE__MAX,
};

struct IHDR {
	uint32_t	width;
	uint32_t	height;
	int8_t		bitdepth;
	int8_t		colourtype;
	int8_t		compression;
	int8_t		filter;
	int8_t		interlace;
} __attribute__((packed));

size_t pngscale(FILE *, unsigned char **, uint32_t);
int pngblank(size_t, uint8_t **, size_t *);

#endif /* LIBRAVATAR_H_ */
