#!/usr/bin/env bash

set -x
set -e

if [[ "${BUILD_ACTION}" = "test" ]]
  then make -j3 v8 > /dev/null 2>&1
fi

if [[ "${BUILD_ACTION}" = "android_x86" ]]
  then ANDROID_ARCH=ia32 make V=1 -j3 v8_android_multi > /dev/null 2>&1
fi

if [[ "${BUILD_ACTION}" = "android_arm" ]]
  then ANDROID_ARCH=arm make V=1 -j3 v8_android_multi
# > /dev/null 2>&1
fi
