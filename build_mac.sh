# export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
LIB_NAME=sr-vulkan
$TAG_NAME='v2.0.0'
PACKAGE_PREFIX=${LIB_NAME}-${TAG_NAME}
PACKAGENAME=${PACKAGE_PREFIX}-macos

oldPath=`pwd`
# OpemMP
if [ ! -d "openmp-11.0.0.src" ];then
      wget 'https://github.com/llvm/llvm-project/releases/download/llvmorg-11.0.0/openmp-11.0.0.src.tar.xz'
      tar -xf openmp-11.0.0.src.tar.xz
      cd openmp-11.0.0.src
      sed -i'' -e '/.size __kmp_unnamed_critical_addr/d' runtime/src/z_Linux_asm.S
      sed -i'' -e 's/__kmp_unnamed_critical_addr/___kmp_unnamed_critical_addr/g' runtime/src/z_Linux_asm.S

      # OpenMP
      mkdir -p build && cd build
      cmake -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
            -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
            -DCMAKE_INSTALL_PREFIX=install \
            -DLIBOMP_ENABLE_SHARED=OFF \
            -DLIBOMP_OMPT_SUPPORT=OFF \
            -DLIBOMP_USE_HWLOC=OFF \
            ..
      cmake --build .
      cmake --build . --target install

      # OpenMP Xcode macOS SDK
      # sudo cp install/include/* \
      #       $DEVELOPER_DIR/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include
      # sudo cp install/lib/libomp.a \
      #       $DEVELOPER_DIR/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/lib/

      cd ../..
fi

if [ ! -d "MoltenVK" ];then
      wget -q https://github.com/KhronosGroup/MoltenVK/releases/download/v1.2.8/MoltenVK-all.tar
      tar -xf MoltenVK-all.tar
fi

export VULKAN_SDK=`pwd`/MoltenVK/MoltenVK

OPENMP_INCLUDE=`pwd`/openmp-11.0.0.src/build/install/include
OPENMP_LIB=`pwd`/openmp-11.0.0.src/build/install/lib/libomp.a
cp $OPENMP_INCLUDE/* $VULKAN_SDK/../MoltenVK/include
cp $OPENMP_INCLUDE/* src/ncnn/src/

# Python x86_64
# PYTHON_BIN=/Library/Developer/CommandLineTools/Library/Frameworks/Python3.framework/Versions/3.9/bin/python3
# if [ ! -n "$PYTHON_BIN" ]; then
#       PYTHON_BIN=`which python3`
# fi
# PYTHON_DIR=`dirname $PYTHON_BIN`/../

# VERSION=`${PYTHON_BIN} -V 2>&1 | cut -d " " -f 2`
# VERSION_INFO=${VERSION:0:3}

# #PYTHON_LIBRARIES=`find $PYTHON_DIR -name "libpython${VERSION_INFO}*.dylib"|tail -1`
# PYTHON_INCLUDE=`find $PYTHON_DIR -name "Python.h"|tail -1`
# PYTHON_INCLUDE_DIRS=`dirname $PYTHON_INCLUDE`

# echo $VERSION
# echo $PYTHON_BIN
# # echo $PYTHON_LIBRARIES
# echo $PYTHON_INCLUDE_DIRS

# build x86
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release \
      -DNCNN_VULKAN=ON \
      -DNCNN_BUILD_TOOLS=OFF \
      -DNCNN_BUILD_EXAMPLES=OFF \
      -DUSE_STATIC_MOLTENVK=ON \
      -DCMAKE_OSX_ARCHITECTURES="x86_64" \
      -DOpenMP_C_FLAGS="-Xclang -fopenmp" \
      -DOpenMP_CXX_FLAGS="-Xclang -fopenmp" \
      -DOpenMP_C_LIB_NAMES="libomp"\
      -DOpenMP_CXX_LIB_NAMES="libomp" \
      -DOpenMP_libomp_LIBRARY=$OPENMP_LIB \
      -DCMAKE_SKIP_RPATH=TRUE \
      -DCMAKE_SKIP_BUILD_RPATH=TRUE \
      -DVulkan_LIBRARY=$VULKAN_SDK/../MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a \
      -DCMAKE_VERBOSE_MAKEFILE=ON \
      ../src

cmake --build . -j4
cp sr_vulkan.dylib sr_vulkan.so
strip -x sr_vulkan.so
cd ..

# build arm64
mkdir -p build_arm64 && cd build_arm64
cmake -DCMAKE_BUILD_TYPE=Release \
      -DNCNN_VULKAN=ON \
      -DNCNN_BUILD_TOOLS=OFF \
      -DNCNN_BUILD_EXAMPLES=OFF \
      -DUSE_STATIC_MOLTENVK=ON \
      -DCMAKE_OSX_ARCHITECTURES="arm64" \
      -DOpenMP_C_FLAGS="-Xclang -fopenmp" \
      -DOpenMP_CXX_FLAGS="-Xclang -fopenmp" \
      -DOpenMP_C_LIB_NAMES="libomp"\
      -DOpenMP_CXX_LIB_NAMES="libomp" \
      -DOpenMP_libomp_LIBRARY=$OPENMP_LIB \
      -DPNG_ARM_NEON=off \
      -DCMAKE_SKIP_RPATH=TRUE \
      -DCMAKE_SKIP_BUILD_RPATH=TRUE \
      -DVulkan_LIBRARY=$VULKAN_SDK/../MoltenVK/static/MoltenVK.xcframework/macos-arm64_x86_64/libMoltenVK.a \
      ../src
cmake --build . -j4
cp sr_vulkan.dylib sr_vulkan.so
strip -x sr_vulkan.so

# Package
cd $oldPath
mkdir -p $PACKAGENAME
cp README.md LICENSE $PACKAGENAME
lipo -create build/sr_vulkan.so build_arm64/sr_vulkan.so -o $PACKAGENAME/sr_vulkan.so
cp -r sr_vulkan/models test $PACKAGENAME
