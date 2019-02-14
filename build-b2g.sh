#!/bin/bash

set -e

export MOZCONFIG=mozconfig-b2g
export GONK_PATH=$HOME/work/B2G
export GONK_PRODUCT_NAME=generic

# Check that the GONK_PATH environment variable is set.
if [ -z ${GONK_PATH+x} ];
then
    echo "Please set GONK_PATH to the root of your Gonk directory first.";
    exit 1;
else
    echo "Using '$GONK_PATH'";
fi

# Check that the GONK_PRODUCT_NAME environment variable is set.
if [ -z ${GONK_PRODUCT_NAME+x} ];
then
    echo "Please set GONK_PRODUCT_NAME to the name of the device (look at $GONK_PATH/out/target/product).";
    exit 1;
else
    echo "Product is '$GONK_PRODUCT_NAME'";
fi

export MOZCONFIG=`pwd`/mozconfig-b2g

export NDK_DIR=/home/walac/work/android-ndk-r17b

export PATH=$NDK_DIR/toolchains/llvm/prebuilt/linux-x86_64/bin:$GONK_PATH/prebuilts/linux-x86_64/bin/:$PATH

SYSROOT=$NDK_DIR/platforms/android-23/arch-arm/
GONK_LIBS=$GONK_PATH/out/target/product/$GONK_PRODUCT_NAME/obj/lib/

ARCH_DIR="arch-arm"

export GONK_PRODUCT=$GONK_PRODUCT_NAME

# ld --version

export CFLAGS="-DANDROID -DTARGET_OS_GONK \
-DJE_FORCE_SYNC_COMPARE_AND_SWAP_4=1 \
-D__SOFTFP__ \
-D_USING_LIBCXX \
-Wno-nullability-completeness \
-DGR_GL_USE_NEW_SHADER_SOURCE_SIGNATURE=1 \
-isystem $GONK_PATH/bionic \
-isystem $GONK_PATH/bionic/libc/$ARCH_DIR/include \
-isystem $NDK_DIR/platforms/android-23/arch-arm/usr/include \
-isystem $GONK_PATH/bionic/libc/include/ \
-isystem $GONK_PATH/bionic/libc/kernel/common \
-isystem $GONK_PATH/bionic/libc/kernel/$ARCH_DIR \
-isystem $GONK_PATH/bionic/libc/kernel/uapi/ \
-isystem $GONK_PATH/bionic/libc/kernel/uapi/asm-arm/ \
-isystem $GONK_PATH/bionic/libm/include \
-I$GONK_PATH/system/core/libpixelflinger/include/ \
-I$GONK_PATH/frameworks/native/include \
-I$GONK_PATH/system \
-I$(pwd)/modules/freetype2/include \
-I$GONK_PATH/system/core/include \
-I$GONK_PATH/external/zlib"

export CPPFLAGS="-O2 -fPIC \
-isystem $GONK_PATH/api/cpp/include \
$CFLAGS"

export CXXFLAGS="$CPPFLAGS -std=c++14"

GCC_LIB="-L$GONK_PATH/prebuilts/gcc/darwin-x86/arm/arm-linux-androideabi-4.9/lib/gcc/arm-linux-androideabi/4.9.x-google/"

export ANDROID_NDK=$NDK_DIR
export ANDROID_PLATFORM=android-23

export LDFLAGS="-L$GONK_PATH/out/target/product/$GONK_PRODUCT_NAME/obj/lib \
-Wl,-rpath-link=$GONK_PATH/out/target/product/$GONK_PRODUCT_NAME/obj/lib \
--sysroot=$NDK_DIR/sysroot $GCC_LIB -ldl -lstdc++ -Wl,--no-as-needed \
-lbinder -lutils -lhardware -lhardware_legacy -lpower"

./mach build $@
