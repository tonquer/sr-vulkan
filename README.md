# sr-vulkan
- This is modified [waifu2x-ncnn-vulkan](https://github.com/nihui/waifu2x-ncnn-vulkan), [realsr-ncnn-vulkan](https://github.com/nihui/realsr-ncnn-vulkan) [realcugan-ncnn-vulkan](https://github.com/nihui/realcugan-ncnn-vulkan), Export pyd and so files to Python
- Support Linux, Windows, MacOs
- Support import JPG, PNG, BMP, GIF, WEBP, Animated WEBP, APNG
- Support export JPG, PNG, BMP, WEBP, Animated WEBP, APNG

# Install
```shell
pip install sr-vulkan -v
```

# Install Model
```shell
pip install sr-vulkan-model-waifu2x
pip install sr-vulkan-model-realcugan
pip install sr-vulkan-model-realesrgan
pip install sr-vulkan-model-realsr
```

# Use
```shell
from sr_vulkan import sr_vulkan as sr

# init
sr.setDebug(True)
sts = sr.init()
if sts < 0:
    # cpu model
    isCpuModel = True
gpuList = sr.getGpuInfo()
print(gpuList)
sts = sr.initSet(gpuId=0, threadNum=2)
assert sts==0

# add picture ...
# sr.add(...)

# load picture...
# newData, status, backId, tick = sr.load(0)
```

## Example
- Please see [test](https://github.com/tonquer/sr-vulkan/blob/main/test/test.py) Example

## Build
```shell
pip install wheel
python setup.py bdist_wheel
```