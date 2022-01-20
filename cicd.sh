#!/bin/sh
set -x
bash ./scripts/omnisci-env.sh
bash ./build-omnisci-debug.sh
ctest -C Debug