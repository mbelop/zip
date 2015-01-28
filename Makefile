PROG=	zip

CFLAGS+=-W -Wall -Werror
LDFLAGS=-lz

NOMAN= yes

.include <bsd.prog.mk>
