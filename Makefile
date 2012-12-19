#
# This is the Makefile for development of SLJ Test.
# The Makefile that gets distributed is Makefile.dist.
#
# Also see build.sh for platform-specific options.
#

# Any rule referncing these variable shoud depend on version.txt
VERS=`cat version.txt`
DISTDIR=SLJtest-${VERS}

CFLAGS=-g -Wall
SRCS=	sljtest.c getopt.c replgetopt.h
	
BINDIRS=Linux-glibc:2.3-x86_64 Linux-glibc:2.5-x86_64 \
	Linux-glibc:2.3-i386 Darwin-9.8.0-i386 \
	SunOS-5.10-i86pc FreeBSD-6.3-RELEASE-p12-i386

all: sljtest

sljtest: getopt.o sljtest.o
	${CC} ${LDLAGS} -o $@ -lm getopt.o sljtest.o

sljtest.o: replgetopt.h sljtest.c
	${CC} ${CFLAGS}   -c -o sljtest.o sljtest.c

sljtest.exe: sljtest.o
	${CC} ${LDLAGS} -o $@ -lm sljtest.o

version.txt: sljtest.c
	sed -n '/char.*version.*SLJ Test/s/.*\([0-9]\.[0-9][0-9]*[a-z]*[0-9]*\).*/\1/p' sljtest.c > version.txt
	@echo "A failure at this point means the version string extraction is broken"
	@test -s version.txt

package: version.txt
	tar cvzf ${DISTDIR}.tgz ${DISTDIR}
	-rm -f ${DISTDIR}.zip
	zip -r ${DISTDIR}.zip ${DISTDIR}

build: version.txt
	-mkdir ${DISTDIR}
	cp doc/ReadMe.txt ${DISTDIR}
	cp -r doc/html ${DISTDIR}/doc
	cp    doc/sljtest.pdf ${DISTDIR}/doc
	-mkdir ${DISTDIR}/src
	cp ${SRCS} ${DISTDIR}/src
	cp Makefile.dist ${DISTDIR}/src/Makefile
	ssh Axe       'cd                      src/SLJtest; ./build.sh ${DISTDIR}'
	ssh Day       'cd                      src/SLJtest; ./build.sh ${DISTDIR}'
	ssh OldHat    'cd                      src/SLJtest; ./build.sh ${DISTDIR}'
	ssh Ra        'cd                      src/SLJtest; ./build.sh ${DISTDIR}'
	ssh Raze      'cd                      src/SLJtest; ./build.sh ${DISTDIR}'
	ssh Wormwood  'cd /29W/Day/d0/home/bob/src/SLJtest; ./build.sh ${DISTDIR}'
	mkdir ${DISTDIR}bin/Windows # Placeholder for sljtest.exe to be copied in
#	ssh localhost 'cd                      src/SLJtest; ./build.sh ${DISTDIR}'

# Documentation
doxy: version.txt
	sed -i.bak -e "/PROJECT_NUMBER/s/=.*/= ${VERS}/" Doxyfile
	doxygen

clean:
	rm -r -f core *.o sljtest Doxyfile.bak

clobber: clean
	rm -r -f version.txt SLJtest-* doc/html
