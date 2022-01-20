#!/bin/sh -l

# mkdir build
# pwd
# cd build
# pwd

export PATH_REPO=/workspace/omniscidb
export PATH_SCRIPTS=$PATH_REPO/scripts
echo "start0********************************************************"
pwd
ls
echo "end0********************************************************"
source $PATH_SCRIPTS/omnisci-env.sh
bash  $PATH_REPO/build-omnisci-debug.sh

ls $PATH_REPO
echo "start1********************************************************"
echo "here is the info: "
ls $PATH_REPO/build-$BUILD_TYPE
echo "end1********************************************************"
# cd $PATH_REPO/build-$BUILD_TYPE
# ctest -C $BUILD_TYPE