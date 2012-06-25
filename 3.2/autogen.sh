#! /bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd $srcdir

intltoolize --force || exit 1
autoreconf -v --install || exit 1
cd $ORIGDIR || exit $?

#$srcdir/configure --enable-maintainer-mode "$@" CFLAGS="-g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -DHANDLE_LIBICAL_MEMORY=1"
