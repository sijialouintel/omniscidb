#!/bin/sh -l

git clone -b cider https://github.com/Intel-bigdata/omniscidb.git
source omniscidb/scripts/omnisci-env.sh
bash omniscidb/build-omnisci-debug.sh