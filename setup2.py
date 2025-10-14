import setuptools, sys, os
from shutil import copyfile, copytree, rmtree
from setuptools.command.build_ext import build_ext
import subprocess
from distutils.core import Extension

long_description = \
"""
#sr-vulkan
- This is modified [waifu2x-ncnn-vulkan](https://github.com/nihui/waifu2x-ncnn-vulkan), Export pyd and so files to Python
- Support Linux, Windows, MacOs
- Support import JPG, PNG, BMP, GIF, WEBP, Animated WEBP, APNG
- Support export JPG, PNG, BMP, WEBP, Animated WEBP, APNG
- Support vulkan gpu and cpu

# Install
```shell
pip install sr-vulkan
```

# Use
```shell
from sr_vulkan import sr_vulkan as sr

# init
sts = sr.init()
print("init, code:{}".format(str(sts)))
isCpuModel = False
if sts < 0:
    # cpu model
    isCpuModel = True

gpuList = sr.getGpuInfo()
print(gpuList)
sts = sr.initSet(gpuId=0)
print("init set, code:{}".format(str(sts)))

# Model List:
#    MODEL_WAIFU2X_CUNET_UP1X_DENOISE0X",
#    MODEL_WAIFU2X_CUNET_UP1X_DENOISE1X",
#    MODEL_WAIFU2X_CUNET_UP1X_DENOISE2X",
#    MODEL_WAIFU2X_CUNET_UP1X_DENOISE3X",
#    MODEL_WAIFU2X_CUNET_UP2X",
#    MODEL_WAIFU2X_CUNET_UP2X_DENOISE0X",
#    MODEL_WAIFU2X_CUNET_UP2X_DENOISE1X",
#    MODEL_WAIFU2X_CUNET_UP2X_DENOISE2X",
#    MODEL_WAIFU2X_CUNET_UP2X_DENOISE3X",
#    MODEL_WAIFU2X_ANIME_UP2X"
#    MODEL_WAIFU2X_ANIME_UP2X_DENOISE0X",
#    MODEL_WAIFU2X_ANIME_UP2X_DENOISE1X",
#    MODEL_WAIFU2X_ANIME_UP2X_DENOISE2X",
#    MODEL_WAIFU2X_ANIME_UP2X_DENOISE3X",
#    MODEL_WAIFU2X_PHOTO_UP2X",
#    MODEL_WAIFU2X_PHOTO_UP2X_DENOISE0X",
#    MODEL_WAIFU2X_PHOTO_UP2X_DENOISE1X",
#    MODEL_WAIFU2X_PHOTO_UP2X_DENOISE2X",
#    MODEL_WAIFU2X_PHOTO_UP2X_DENOISE3X",
#    
#    "MODEL_REALCUGAN_PRO_UP2X",
#    "MODEL_REALCUGAN_PRO_UP2X_CONSERVATIVE",
#    "MODEL_REALCUGAN_PRO_UP2X_DENOISE3X",
#    "MODEL_REALCUGAN_PRO_UP3X",
#    "MODEL_REALCUGAN_PRO_UP3X_CONSERVATIVE",
#    "MODEL_REALCUGAN_PRO_UP3X_DENOISE3X",
#    "MODEL_REALCUGAN_SE_UP2X",
#    "MODEL_REALCUGAN_SE_UP2X_CONSERVATIVE",
#    "MODEL_REALCUGAN_SE_UP2X_DENOISE1X",
#    "MODEL_REALCUGAN_SE_UP2X_DENOISE2X",
#    "MODEL_REALCUGAN_SE_UP2X_DENOISE3X",
#    "MODEL_REALCUGAN_SE_UP3X",
#    "MODEL_REALCUGAN_SE_UP3X_CONSERVATIVE",
#    "MODEL_REALCUGAN_SE_UP3X_DENOISE3X",
#    "MODEL_REALCUGAN_SE_UP4X",
#    "MODEL_REALCUGAN_SE_UP4X_CONSERVATIVE",
#    "MODEL_REALCUGAN_SE_UP4X_DENOISE3X",
#    
#    "MODEL_REALSR_DF2K_UP4X"
#    "MODEL_REALESRGAN_ANIMAVIDEOV3_UP2X",
#    "MODEL_REALESRGAN_ANIMAVIDEOV3_UP3X",
#    "MODEL_REALESRGAN_ANIMAVIDEOV3_UP4X",
#    "MODEL_REALESRGAN_X4PLUS_UP4X",
#    "MODEL_REALESRGAN_X4PLUSANIME_UP4X"

# add picture ...
# waifu2x.add(data=imgData, modelIndex=sr.MODEL_ANIME_STYLE_ART_RGB_NOISE0, backId=0, scale=2.5)
# waifu2x.add(data=imgData, modelIndex=sr.MODEL_ANIME_STYLE_ART_RGB_NOISE0, backId=0, format="webp", width=1000, high=1000)

# load picture...
# newData, format, backId, tick = waifu2x.load(0)
```

"""
Version = "1.2.0"

Plat = sys.platform

print(Plat)

setuptools.setup(
    name="sr-vulkan",
    version=Version,
    author="tonquer",
    license="MIT",
    author_email="tonquer@qq.com",
    description="A super resolution python tool, use nihui/waifu2x-ncnn-vulkan, nihui/realsr-ncnn-vulkan, nihui/realcugan-ncnn-vulkan, xinntao/Real-ESRGAN-ncnn-vulkan",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/tonquer/waifu2x-vulkan",
    packages=setuptools.find_packages(),
    install_requires=[],
    classifiers=[
        "Operating System :: OS Independent",
        "Programming Language :: Python :: 3",
        'Programming Language :: Python :: 3.6',
        'Programming Language :: Python :: 3.7',
        'Programming Language :: Python :: 3.8',
        'Programming Language :: Python :: 3.9',
        'Programming Language :: Python :: 3.10',
        'Programming Language :: Python :: 3.11',
        'Programming Language :: Python :: 3.12',
        "License :: OSI Approved :: MIT License",
    ],
    entry_points={
        "pyinstaller40": [
            "hook-dirs = sr_vulkan:get_hook_dirs"
        ]
    },
    python_requires = ">=3.6",
    include_package_data=True,
)
# python setup2.py bdist_wheel --plat-name=win-amd64 --python-tag=cp36.cp37.cp38.cp39.cp310.cp311.cp312.cp313
# python3 setup2.py bdist_wheel --plat-name=manylinux_2_31_aarch64 --python-tag=cp39
# python3 setup2.py bdist_wheel --plat-name=macosx_10_9_universal2 --python-tag=cp36.cp37.cp38.cp39.cp310.cp311.cp312.cp313