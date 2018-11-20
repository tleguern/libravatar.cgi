#ifndef LIBRAVATAR_H_
#define LIBRAVATAR_H_

#define _PATH_DEFAULT "/htdocs/avatars/default.png"

size_t pngscale(FILE *, unsigned char **, uint32_t);
int blank(size_t, uint8_t **, size_t *);
int mm(size_t, uint8_t **, size_t *);

#endif /* LIBRAVATAR_H_ */
