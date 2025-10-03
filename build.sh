LIB_NAME=sr-ncnn-vulkan
TAG_NAME=v1.3.0
PACKAGE_PREFIX=${LIB_NAME}-${TAG_NAME}}
PACKAGENAME=${PACKAGE_PREFIX}-ubuntu
oldPath=`pwd`
# Vulkan SDK
# if [ ! -d "1.2.198.1" ];then
#       wget 'https://sdk.lunarg.com/sdk/download/1.2.198.1/linux/vulkansdk-linux-x86_64-1.2.198.1.tar.gz?Human=true' \
#       -O vulkansdk-linux-x86_64-1.2.198.1.tar.gz
#       tar -xf vulkansdk-linux-x86_64-1.2.198.1.tar.gz
#       rm -rf 1.2.198.1/source 1.2.198.1/samples
#       find 1.2.198.1 -type f | grep -v -E 'vulkan|glslang' | xargs rm
# fi
# export VULKAN_SDK=`pwd`/1.2.198.1/x86_64
mkdir -p build && cd build

# Python x86_64
if [ ! -n "$PYTHON_BIN" ]; then
      PYTHON_BIN=`which python3`
fi
PYTHON_DIR=`dirname $PYTHON_BIN`/../

VERSION=`${PYTHON_BIN} -V 2>&1 | cut -d " " -f 2`
VERSION_INFO=${VERSION:0:3}

PYTHON_LIBRARIES=`find $PYTHON_DIR -name "libpython${VERSION_INFO}*.a"|tail -1`
PYTHON_INCLUDE=`find $PYTHON_DIR -name "Python.h"|tail -1`
PYTHON_INCLUDE_DIRS=`dirname $PYTHON_INCLUDE`

echo $VERSION
echo $PYTHON_BIN
echo $PYTHON_LIBRARIES
echo $PYTHON_INCLUDE_DIRS

cmake -DCMAKE_BUILD_TYPE=Release \
      -DVulkan_LIBRARY="$oldPath/VulkanSDK/linux/libvulkan.so" \
      -DVulkan_INCLUDE_DIR="$oldPath/VulkanSDK/Include" \
      -DDCMAKE_VERBOSE_MAKEFILE=On \
      -DNCNN_VULKAN=ON \
      -DNCNN_BUILD_TOOLS=OFF \
      -DNCNN_BUILD_EXAMPLES=OFF \
      -DPYTHON_INCLUDE_DIRS=$PYTHON_INCLUDE_DIRS \
      ../src
cmake --build .
# cp libsr_ncnn_vulkan.so sr_ncnn_vulkan.so
strip -x sr_ncnn_vulkan.so

# Package
cd $oldPath
mkdir -p $PACKAGENAME
cp README.md LICENSE $PACKAGENAME
cp build/sr_ncnn_vulkan.so $PACKAGENAME
cp -r sr_ncnn_vulkan/models test $PACKAGENAME
