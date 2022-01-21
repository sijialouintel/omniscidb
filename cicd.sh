#!/bin/sh
set -x
source ./scripts/omnisci-env.sh
bash ./build-omnisci-debug.sh

echo "inside********************************************************************"
ctest -C Debug