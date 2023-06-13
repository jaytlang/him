PROG=	himd
PREFIX=	/usr/local

SRCS=	main.c
OBJS=	$(SRCS:.c=.o)
DEPS=	$(SRCS:.c=.d)

CC=		clang
CFLAGS=		-Wall -Wextra -Werror -pedantic -O2 -g -MD
LDFLAGS=	-levent -lseccomp

${PROG}: ${OBJS}
	${CC} -o $@ ${LDFLAGS} $^

-include ${DEPS}

.PHONY: install clean
install: ${PROG}
	install -o root -g root -m 755 $< ${PREFIX}/bin/$<
clean:
	rm -f ${OBJS} ${DEPS} ${PROG}

