#!/bin/bash

if [ -z "$1" ] || [ ! -d "$1" ]; then
  echo Usage: $0 DEB_LOCATION
  exit 1
fi

_DEB_LOCATION=$1

if [ ! -d debian ] || [ ! -d src ]; then
  echo Please run me from the top-level source directory.
  exit 2
fi

VERSION=`head -n 1 ChangeLog | sed -e 's/.*(//' -e 's/).*//'`
TEMP_DIR=$(mktemp -d)
SRC_DIR=$(pwd)

export VERSION

pushd $TEMP_DIR >& /dev/null || exit 1

mkdir s3fuse-$VERSION || exit 1
cd s3fuse-$VERSION || exit 1

cp -r $SRC_DIR/* . || exit 1
./clean.sh || exit 1
rm -rf *.sh dist/*.in || exit 1
find . -type d -name .svn | xargs rm -rf || exit 1
cp $SRC_DIR/build-config.sh . || exit 1
cp ChangeLog debian/changelog || exit 1

autoreconf --force --install || exit 1
dpkg-buildpackage || echo "Might have failed to build package, but continuing anyway."

cd .. || exit 1

rm -rf s3fuse-$VERSION

popd >& /dev/null || exit 1

cp -f $TEMP_DIR/* $_DEB_LOCATION || echo "Failed to copy package." && exit 1

rm -r $TEMP_DIR