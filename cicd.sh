#!/bin/sh -l

# mkdir build
# pwd
# cd build
# pwd

export PATH_REPO=/workspace/omniscidb
export PATH_SCRIPTS=$PATH_REPO/scripts
source $PATH_SCRIPTS/omnisci-env.sh
bash  $PATH_REPO/build-omnisci-debug.sh

echo "here is the info: "
ls $PATH_REPO/build-$BUILD_TYPE
# cd $PATH_REPO/build-$BUILD_TYPE
# ctest -C $BUILD_TYPE