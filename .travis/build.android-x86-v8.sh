#!/usr/bin/env bash

set -x
set -e

if [[ "${BUILD_ACTION}" = "android_x86" ]]
  then ANDROID_ARCH=ia32 make v8_android_multi > /dev/null 2>&1
fi
