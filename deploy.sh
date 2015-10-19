#!/bin/sh
# Build and deploy plugins
#
# Copyright (c) 2015, FxArena DA <mail@fxarena.net>
# All rights reserved.
#
# OpenFX-Arena is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License version 2. You should have received a copy of the GNU General Public License version 2 along with OpenFX-Arena. If not, see http://www.gnu.org/licenses/.
# OpenFX-Arena is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#
# Need custom licensing terms or conditions? Commercial license for proprietary software? Contact us.
#

CWD=$(pwd)

MAGICK=6.9.2-5
MAGICK_URL=ftp://ftp.sunet.se/pub/multimedia/graphics/ImageMagick/ImageMagick-$MAGICK.tar.gz
if [ -z "$QUANTUM" ]; then
  Q=32
else
  Q=$QUANTUM
fi
if [ "$CL" = "1" ]; then
  CL_CONF="--with-x --enable-opencl"
  CL_FLAGS="-I${CWD}/OpenCL -L${CWD}/OpenCL"
  USE_CL=1
else
  USE_CL=0
fi
MAGICK_OPT="--disable-docs --disable-deprecated --with-magick-plus-plus=yes --with-quantum-depth=${Q} --without-dps --without-djvu --without-fftw --without-fpx --without-gslib --without-gvc --without-jbig --without-jpeg --with-lcms --without-openjp2 --without-lqr --without-lzma --without-openexr --with-pango --with-png --with-rsvg --without-tiff --without-webp --with-xml --with-zlib --without-bzlib --enable-static --disable-shared --enable-hdri --with-freetype --with-fontconfig --without-x --without-modules $CL_CONF"

PNG=1.2.53
PNG_URL=https://github.com/olear/openfx-arena/releases/download/1.9.0/libpng-1.2.53.tar.gz

if [ -z "$VERSION" ]; then
  ARENA=2.0.0
else
  ARENA=$VERSION
fi
if [ -z "$PACKAGE" ]; then
  PKGNAME=Arena
else
  PKGNAME=$PACKAGE
fi
if [ -z "$PREFIX" ]; then
  PREFIX=$CWD/tmp
fi
if [ ! -d $CWD/3rdparty ]; then
  mkdir -p $CWD/3rdparty || exit 1
fi
if [ -z "$JOBS" ]; then
  JOBS=4
fi
OS=$(uname -s)

if [ -z "$ARCH" ]; then
  case "$( uname -m )" in
    i?86) export ARCH=i686 ;;
    amd64) export ARCH=x86_64 ;;
       *) export ARCH=$( uname -m ) ;;
  esac
fi
if [ "$ARCH" = "i686" ]; then
  BF="-m32 -pipe -O3 -march=i686 -mtune=core2"
  BIT=32
elif [ "$ARCH" = "x86_64" ]; then
  BF="-m64 -pipe -O3 -fPIC -march=core2"
  BIT=64
else
  echo "CPU not supported"
  exit 1
fi

if [ "$OS" = "Linux" ]; then
  PKGOS=Linux
fi
if [ "$OS" = "FreeBSD" ]; then
  PKGOS=FreeBSD
fi
if [ "$OS" = "Msys" ]; then
  PKGOS=Windows
fi
if [ "$DEBUG" = "1" ]; then
  TAG=debug
else
  TAG=release
  BF="${BF} -fomit-frame-pointer"
fi

PKG=$PKGNAME.ofx.bundle-$ARENA-$PKGOS-x86-$TAG-$BIT
if [ "$CL" = "1" ]; then
  PKG="${PKG}-OpenCL"
fi

export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig
export LD_LIBRARY_PATH=$PREFIX/lib:$PREFIX/lib64:$LD_LIBRARY_PATH
export PATH=$PREFIX/bin:$PATH

MAKE=make

if [ "$PKGOS" = "FreeBSD" ]; then
  export CXX=clang++
  export CC=clang
  MAKE=gmake
  BSD="-std=c++11"
  USE_FREEBSD=1
fi

if [ "$CLEAN" = "1" ]; then
  rm -rf $PREFIX || exit 1
fi
if [ ! -d $PREFIX ]; then
  mkdir -p $PREFIX/{bin,lib,include} || exit 1
  (cd $PREFIX ; ln -sf lib lib64)
fi

# libpng
if [ ! -f ${PREFIX}/lib/libpng.a ] && [ "$NOPNG" != "1" ]; then
  if [ ! -f $CWD/3rdparty/libpng-$PNG.tar.gz ]; then
    wget $PNG_URL -O $CWD/3rdparty/libpng-$PNG.tar.gz || exit 1
  fi
  if [ ! -d $CWD/3rdparty/libpng-$PNG ]; then
    tar xvf $CWD/3rdparty/libpng-$PNG.tar.gz -C $CWD/3rdparty/ || exit 1
  fi
  cd $CWD/3rdparty/libpng-$PNG || exit 1
  $MAKE distclean
  CFLAGS="-m${BIT} ${BF}" CXXFLAGS="-m${BIT} ${BF} ${BSD} -I${PREFIX}/include" CPPFLAGS="-I${PREFIX}/include -L${PREFIX}/lib" ./configure --prefix=$PREFIX --enable-static --disable-shared || exit 1
  $MAKE -j$JOBS install || exit 1
  mkdir -p $PREFIX/share/doc/libpng/ || exit 1
  cp LICENSE $PREFIX/share/doc/libpng/ || exit 1
  cd .. || exit 1
  rm -rf libpng-$PNG || exit 1
fi

# magick
if [ "$CLEAN" = "1" ]; then
  rm -rf $CWD/3rdparty/ImageMagick-$MAGICK
fi
if [ ! -f ${PREFIX}/lib/libMagick++-6.Q${Q}HDRI.a ]; then
  if [ "$MAGICK_GIT" = "1" ]; then
    rm -rf $CWD/3rdparty/ImageMagick-6
    cd $CWD/3rdparty || exit 1
    git clone https://github.com/ImageMagick/ImageMagick ImageMagick-6 || exit 1
    cd ImageMagick-6 || exit 1
    git checkout ImageMagick-6 || exit 1
    MAGICK=6
  else
    if [ ! -f $CWD/3rdparty/ImageMagick-$MAGICK.tar.gz ]; then
      wget $MAGICK_URL -O $CWD/3rdparty/ImageMagick-$MAGICK.tar.gz || exit 1
    fi
    if [ ! -d $CWD/3rdparty/ImageMagick-$MAGICK ]; then
      tar xvf $CWD/3rdparty/ImageMagick-$MAGICK.tar.gz -C $CWD/3rdparty/ || exit 1
    fi
    cd $CWD/3rdparty/ImageMagick-$MAGICK || exit 1
  fi
  $MAKE distclean
  CFLAGS="-m${BIT} ${BF}" CXXFLAGS="-m${BIT} ${BF} ${BSD} -I${PREFIX}/include" CPPFLAGS="-I${PREFIX}/include -L${PREFIX}/lib $CL_FLAGS" ./configure --libdir=${PREFIX}/lib --prefix=${PREFIX} $MAGICK_OPT || exit 1
  $MAKE -j$JOBS install || exit 1
  mkdir -p $PREFIX/share/doc/ImageMagick/ || exit 1
  cp LICENSE $PREFIX/share/doc/ImageMagick/ || exit 1
  cd .. || exit 1
  if [ "$MAGICK_GIT" != "1" ]; then
    rm -rf ImageMagick-$MAGICK || exit 1
  fi
fi

cd $CWD || exit 1

if [ "$PKGNAME" != "Arena" ]; then
  cd $PKGNAME || exit 1
fi

if [ "$PKGOS" != "Windows" ]; then
  $MAKE OPENCL=$USE_CL STATIC=1 FREEBSD=$USE_FREEBSD BITS=$BIT CONFIG=$TAG clean
  $MAKE OPENCL=$USE_CL STATIC=1 FREEBSD=$USE_FREEBSD BITS=$BIT CONFIG=$TAG || exit 1
else
  make STATIC=1 OPENCL=$USE_CL MINGW=1 BIT=$BIT CONFIG=$TAG clean
  make STATIC=1 OPENCL=$USE_CL MINGW=1 BIT=$BIT CONFIG=$TAG || exit 1
fi

cd $CWD || exit 1
if [ -d $PKG ]; then
  rm -rf $PKG || exit 1
fi
mkdir $CWD/$PKG || exit 1
cp LICENSE COPYING README.md $CWD/$PKG/ || exit 1
cp $PREFIX/share/doc/ImageMagick/LICENSE $CWD/$PKG/LICENSE.ImageMagick || exit 1
cp OpenFX/Support/LICENSE $CWD/$PKG/LICENSE.OpenFX || exit 1
cp OpenFX-IO/LICENSE $CWD/$PKG/LICENSE.OpenFX-IO || exit 1
if [ "$NOPNG" != "1" ]; then 
  cp $PREFIX/share/doc/libpng/LICENSE $CWD/$PKG/LICENSE.libpng || exit 1
fi
if [ "$CL" = "1" ]; then
  cp $CWD/OpenCL/LICENSE $CWD/$PKG/LICENSE.OpenCL || exit 1
fi

# Strip and copy
if [ "$PKGNAME" != "Arena" ]; then
  PKGSRC=$PKGNAME
else
  PKGSRC=Bundle
fi
if [ "$BIT" == "64" ]; then
  PKGBIT=x86-$BIT
else
  PKGBIT=x86
fi
if [ "$PKGOS" = "Windows" ]; then
  if [ "$TAG" = "release" ]; then
    strip -s $PKGSRC/$(uname -s)-$BIT-$TAG/$PKGNAME.ofx.bundle/Contents/Win$BIT/*
  fi
  mv $PKGSRC/$(uname -s)-$BIT-$TAG/$PKGNAME.ofx.bundle $CWD/$PKG/ || exit 1
else
  if [ "$TAG" = "release" ]; then
    strip -s $PKGSRC/$(uname -s)-$BIT-$TAG/$PKGNAME.ofx.bundle/Contents/$PKGOS-$PKGBIT/$PKGNAME.ofx || exit 1
  fi
  mv $PKGSRC/$(uname -s)-$BIT-$TAG/$PKGNAME.ofx.bundle $CWD/$PKG/ || exit 1
fi

# Package
if [ "$PKGOS" = "Windows" ]; then
  rm -f $PKG.zip
  zip -9 -r $PKG.zip $PKG || exit 1
else
  rm -f $PKG.tgz
  tar cvvzf $PKG.tgz $PKG || exit 1
fi

echo "DONE!!!"
