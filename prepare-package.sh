#!/bin/bash

if [ -z "$1" ]; then
    echo "No series given."
    exit
fi

export SERIES=$1
export VERSION=`dpkg-parsechangelog | sed -n 's/^Version: //p'`
export TMPFILE=`mktemp`

cp debian/changelog ${TMPFILE}
sed -i s/${VERSION}/${VERSION}~${SERIES}1/ debian/changelog
debuild -S -sa
cp ${TMPFILE} debian/changelog
rm -f ${TMPFILE}
