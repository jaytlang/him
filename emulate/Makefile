PROG=	emulate
SRCS=	main.c
OBJS=	$(SRCS:.c=.o)
DEPS=	$(SRCS:.c=.d)

CC=		clang
CFLAGS=		$(shell sdl2-config --cflags)
CFLAGS+=	-O2 -g -Wall -Wextra -Werror -MD -pedantic -fsanitize=address
LDFLAGS= 	$(shell sdl2-config --libs) -fsanitize=address

.PHONY: all clean
all: $(PROG)

$(PROG): $(OBJS)
	$(CC) -o $@ $(LDFLAGS) $^

-include $(DEPS)

clean:
	rm -f $(PROG) $(OBJS) $(DEPS)
