PROG= libreavatar
SRCS= libreavatar.c
OBJS= ${SRCS:.c=.o}

LDFLAGS+= -L /usr/local/lib
LDADD+= -lkcgi -lz
CFLAGS+= -I /usr/local/include
CFLAGS+= -Wall -Wextra -Wmissing-prototypes -Wstrict-prototypes -Wwrite-strings

INSTALL_PROGRAM?= install -m 0555
DESTDIR?=/var/www/cgi-bin

.SUFFIXES: .c .o
.PHONY: clean install

.c.o:
	${CC} ${CFLAGS} -c $<

${PROG}: ${OBJS}
	${CC} -static ${CFLAGS} ${LDFLAGS} -o $@ ${OBJS} ${LDADD}

clean:
	rm -f ${PROG} ${OBJS}

install: ${PROG}
	${INSTALL_PROGRAM} ${PROG} ${DESTDIR}	
