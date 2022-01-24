#!/bin/sh
set -x
source ./scripts/omnisci-env.sh
bash ./build-omnisci-debug.sh

echo "********************************************************************"
# cd build-Debug
# ctest -C Debug

cd ..
mkdir presto_cpp
cd presto_cpp
export https_proxy=http://child-prc.intel.com:913
export http_proxy=http://child-prc.intel.com:913
git clone -b Modular_SQL https://github.com/Intel-bigdata/velox.git
cd velox
source scripts/omnisci-env.sh
bash build-debug.sh

echo "********************************************************************"
cd build-Debug
ctest -C Debug