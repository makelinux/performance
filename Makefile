targets=throughput

all: ${targets}

LDLIBS+=-lm

CFLAGS+=-g -I.

install:
	mkdir -p ${DESTDIR}/usr/bin
	cp ${targets} ${DESTDIR}/usr/bin

clean:
	rm -f ${targets}
