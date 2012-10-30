#!/bin/sh
#
# There are two different ways binaries get built
# 1) Use Makefile to build binaries in this directory
# 2) Use build.sh to build binaries in platform-appropriate sub directory
# Build.sh has the benefit that it auotmatically sets platform-specific
# compile and link options.
#

case $# in
1) break;;
*) echo "Usage: $0: VersionDir"; exit 1;;
esac

VERSION=$1

SYSTEM=`uname -s`
MACHINE=`uname -m`
RELEASE=`uname -r`
case $SYSTEM in
	Darwin)
		CFLAGS="-g -Wall"
		;;
	FreeBSD)
		CFLAGS="-g -Wall"
		LDFLAGS="-lm"
		;;
	Linux)
		RELEASE=glibc:`echo /lib/libc-*.so | sed -e 's/^.*-//' -e 's/\([0-9]\.[0-9]\).*\.so$/\1/'`
		CFLAGS="-g -Wall"
		LDFLAGS="-lm"
		;;
	SunOS)
		CFLAGS="-g"
		LDFLAGS="-lm"
		;;
	*)
		echo Building on unknown system: $SYSTEM
		;;
esac
BINDIR=$VERSION/bin/$SYSTEM-$RELEASE-$MACHINE

if [ ! -d $BINDIR ]; then
	mkdir -p $BINDIR
fi

set -x

cd $BINDIR

cc $CFLAGS $LDFLAGS -o sjtest ../../../getopt.c ../../../sjtest.c
