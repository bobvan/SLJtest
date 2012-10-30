#
# There are two different ways binaries get built
# 1) Use Makefile to build binaries in this directory
# 2) Use build.sh to build binaries in platform-appropriate sub directory
# Build.sh has the benefit that it auotmatically sets platform-specific
# compile and link options.
#
VERS=`sed -n '/char.*version.*SJ Test/s/.*\([0-9]\.[0-9][0-9]*[a-z]*[0-9]*\).*/\1/p' sjtest.c`
DISTDIR=SJtest-${VERS}

CFLAGS=-g -Wall
SRCS=	sjtest.c getopt.c replgetopt.h \
	Makefile build.sh
BINDIRS=Linux-glibc:2.3-x86_64 Linux-glibc:2.5-x86_64 \
	Linux-glibc:2.3-i386 Darwin-9.8.0-i386 \
	SunOS-5.10-i86pc FreeBSD-6.3-RELEASE-p12-i386
SCP_HOST=Ra

all: sjtest

sjtest: getopt.o sjtest.o
	${CC} ${LDLAGS} -o $@ -lm getopt.o sjtest.o

sjtest.o: replgetopt.h sjtest.c
	${CC} ${CFLAGS}   -c -o sjtest.o sjtest.c

scp:
	scp ${SRCS} ${SCP_HOST}:tmp/sjtest

package: build
	@echo "A failure at this point means the version string extraction is broken"
	@test -n "${VERS}"
	tar cvzf ${DISTDIR}.tgz ${DISTDIR}

build:
	cp doc/ReadMe.txt ${DISTDIR}
	mkdir -p ${DISTDIR}/src
	cp ${SRCS} ${DISTDIR}/src
	ssh Axe      'cd                      tmp/SJtest; ./build.sh ${VERS}'
	ssh Day      'cd                      tmp/SJtest; ./build.sh ${VERS}'
	ssh OldHat   'cd                      tmp/SJtest; ./build.sh ${VERS}'
	ssh Ra       'cd                      tmp/SJtest; ./build.sh ${VERS}'
	ssh Raze     'cd                      tmp/SJtest; ./build.sh ${VERS}'
	ssh Wormwood 'cd /29W/Day/d0/home/bob/tmp/SJtest; ./build.sh ${VERS}'
#	ssh localhost 'cd uni/INFA/SJtest; ./build.sh ${VERS}'

# Documentation
doxy:
	@echo "A failure at this point means the version string extraction is broken"
	@test -n "${VERS}"
	sed -i '' -e "/PROJECT_NUMBER/s/=.*/= ${VERS}/" Doxyfile
	doxygen

clean:
	rm -r -f core *.o sjtest

clobber: clean
	rm -r -f SJtest-* doc/html
