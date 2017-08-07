targets=throughput
gnulib?=gl/gllib

all: ${targets}

LDLIBS+=-lgsl -lgslcblas -lm -lpthread -L${gnulib} -lgnu

CFLAGS+=-g -I. -Wall -I${gnulib} -I${gnulib}/..

install:
	mkdir -p ${DESTDIR}/usr/bin
	cp ${targets} ${DESTDIR}/usr/bin

clean:
	rm -rf ${targets} gl

throughput: ${gnulib}/libgnu.a

gl/gllib/libgnu.a:
	@gnulib-tool --help > /dev/null || (echo Please install gnulib; false)
	test -f gl/configure || gnulib-tool --create-testdir --dir gl human xstrtol &> gl.log
	cd gl; test -f Makefile || ./configure -q
	$(MAKE) --quiet -C gl
