#
# This is the Makefile for development of SJ Test.
# The Makefile that gets distributed is Makefile.dist.
#
# Also see build.sh for platform-specific options.
#

# Any rule referncing these variable shoud depend on version.txt
VERS=`cat version.txt`
DISTDIR=SJtest-${VERS}

CFLAGS=-g -Wall
SRCS=	sjtest.c getopt.c replgetopt.h
	
BINDIRS=Linux-glibc:2.3-x86_64 Linux-glibc:2.5-x86_64 \
	Linux-glibc:2.3-i386 Darwin-9.8.0-i386 \
	SunOS-5.10-i86pc FreeBSD-6.3-RELEASE-p12-i386

all: sjtest

sjtest: getopt.o sjtest.o
	${CC} ${LDLAGS} -o $@ -lm getopt.o sjtest.o

sjtest.o: replgetopt.h sjtest.c
	${CC} ${CFLAGS}   -c -o sjtest.o sjtest.c

version.txt: sjtest.c
	sed -n '/char.*version.*SJ Test/s/.*\([0-9]\.[0-9][0-9]*[a-z]*[0-9]*\).*/\1/p' sjtest.c > version.txt
	@echo "A failure at this point means the version string extraction is broken"
	@test -s version.txt

package: build version.txt
	tar cvzf ${DISTDIR}.tgz ${DISTDIR}

build: version.txt doxy
	-mkdir ${DISTDIR}
	cp doc/ReadMe.txt ${DISTDIR}
	cp -r doc/html ${DISTDIR}/doc
	cp    doc/sjtest.pdf ${DISTDIR}/doc
	-mkdir ${DISTDIR}/src
	cp ${SRCS} ${DISTDIR}/src
	cp Makefile.dist ${DISTDIR}/src/Makefile
	ssh Axe       'cd                      src/SJtest; ./build.sh ${DISTDIR}'
	ssh Day       'cd                      src/SJtest; ./build.sh ${DISTDIR}'
	ssh OldHat    'cd                      src/SJtest; ./build.sh ${DISTDIR}'
	ssh Ra        'cd                      src/SJtest; ./build.sh ${DISTDIR}'
	ssh Raze      'cd                      src/SJtest; ./build.sh ${DISTDIR}'
	ssh Wormwood  'cd /29W/Day/d0/home/bob/src/SJtest; ./build.sh ${DISTDIR}'
#	ssh localhost 'cd                      src/SJtest; ./build.sh ${DISTDIR}'

# Documentation
doxy: version.txt
	sed -i.bak -e "/PROJECT_NUMBER/s/=.*/= ${VERS}/" Doxyfile
	doxygen

clean:
	rm -r -f core *.o sjtest Doxyfile.bak

clobber: clean
	rm -r -f version.txt SJtest-* doc/html
