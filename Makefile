targets=throughput

all: ${targets}

LDLIBS+=-lgsl -lgslcblas -lm -lpthread

CFLAGS+=-g -I. -Wall

install:
	mkdir -p ${DESTDIR}/usr/bin
	cp ${targets} ${DESTDIR}/usr/bin

clean:
	rm -f ${targets}
