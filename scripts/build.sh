export DIR_=/omniscidb/scripts/conda/
export CXXFLAGS="`echo $CXXFLAGS | sed 's/-fPIC//'`"
export CXXFLAGS="$CXXFLAGS -Dsecure_getenv=getenv"
export CXXFLAGS="$CXXFLAGS -D__STDC_FORMAT_MACROS"
export LDFLAGS="`echo $LDFLAGS | sed 's/-Wl,--as-needed//'`"
export EXTRA_CMAKE_OPTIONS=""
export EXTRA_CMAKE_OPTIONS="$EXTRA_CMAKE_OPTIONS -DCMAKE_C_COMPILER=${CC} -DCMAKE_CXX_COMPILER=${CXX}"
export EXTRA_CMAKE_OPTIONS="$EXTRA_CMAKE_OPTIONS -DENABLE_TESTS=on"

export BUILD_TYPE="release"

export EXTRA_CMAKE_OPTIONS="$EXTRA_CMAKE_OPTIONS -DBoost_NO_BOOST_CMAKE=on"

. ${DIR_}/get_cxx_include_path.sh
export CPLUS_INCLUDE_PATH=$(get_cxx_include_path)

cmake -Wno-dev \
   -DCMAKE_PREFIX_PATH=$PREFIX \
   -DCMAKE_INSTALL_PREFIX=$PREFIX/$INSTALL_BASE \
   -DCMAKE_BUILD_TYPE=$BUILD_TYPE \
   -DMAPD_DOCS_DOWNLOAD=off \
   -DENABLE_AWS_S3=off \
   -DENABLE_FOLLY=off \
   -DENABLE_JAVA_REMOTE_DEBUG=off \
   -DENABLE_PROFILER=off \
   -DPREFER_STATIC_LIBS=off \
   -DENABLE_CUDA=off \
   -DENABLE_DBE=ON \
   -DENABLE_FSI=ON \
   -DENABLE_ITT=OFF \
   -DENABLE_JIT_DEBUG=OFF \
   -DENABLE_INTEL_JIT_LISTENER=OFF \
   $EXTRA_CMAKE_OPTIONS \
   ..

make -j ${CPU_COUNT:-`nproc`} || make -j ${CPU_COUNT:-`nproc`} || make
