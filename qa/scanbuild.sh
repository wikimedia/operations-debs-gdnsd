#!/bin/sh
# run from top of repo
# does clang-analyzer checks
if [ ! -f $PWD/qa/gdnsd.supp ]; then
   echo "Run this from the root of the source tree!"
   exit 99
fi
set -x
set -e
CFLAGS="-std=c11" ./configure --enable-developer --without-hardening
make clean
scan-build-11 --status-bugs make
