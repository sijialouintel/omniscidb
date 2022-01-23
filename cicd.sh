#!/bin/sh
set -x
source /usr/local/mapd-deps/mapd-deps.sh

mkdir build-Debug
cd build-Debug
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_CUDA=OFF -DENABLE_DBE=ON ..
make -j 8

echo "********************************************************************"
cd build-Debug
ctest -C Debug