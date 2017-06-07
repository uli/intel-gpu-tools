#! /bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd $srcdir

if ! type gtkdocize > /dev/null 2>&1; then
        echo "EXTRA_DIST =" > gtk-doc.make
        echo "CLEANFILES =" >> gtk-doc.make
else
        gtkdocize || exit $?
fi

autoreconf -v --install || exit 1
cd $ORIGDIR || exit $?

git config --local --get format.subjectPrefix >/dev/null 2>&1 ||
    git config --local format.subjectPrefix "PATCH i-g-t"

if test -z "$NOCONFIGURE"; then
        $srcdir/configure "$@"
fi
