import setuptools, sys, os
from shutil import copyfile, copytree, rmtree
from setuptools.command.build_ext import build_ext
import subprocess
from distutils.core import Extension
import platform
import logging
import pathlib
logging.basicConfig(level=logging.DEBUG)

IsMacosBuildUniversal2 = False

long_description = \
"""
# sr-vulkan
- This is modified [waifu2x-ncnn-vulkan](https://github.com/nihui/waifu2x-ncnn-vulkan), [realsr-ncnn-vulkan](https://github.com/nihui/realsr-ncnn-vulkan) [realcugan-ncnn-vulkan](https://github.com/nihui/realcugan-ncnn-vulkan) [xinntao/Real-ESRGAN-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan/), Export pyd and so files to Python
- Support Linux, Windows, MacOs
- Support import JPG, PNG, BMP, GIF, WEBP, Animated WEBP, APNG
- Support export JPG, PNG, BMP, WEBP, Animated WEBP, APNG
- Support vulkan gpu and cpu

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
#    "MODEL_REALESRGAN_ANIMAVIDEOV3_UP2X",
#    "MODEL_REALESRGAN_ANIMAVIDEOV3_UP3X",
#    "MODEL_REALESRGAN_ANIMAVIDEOV3_UP4X",
#    "MODEL_REALESRGAN_X4PLUS_UP4X",
#    "MODEL_REALESRGAN_X4PLUSANIME_UP4X"
#    "MODEL_REALSR_DF2K_UP4X"
    
    
# add picture ...
# sr.add(data=imgData, modelIndex=sr.MODEL_ANIME_STYLE_ART_RGB_NOISE0, backId=0, scale=2.5)
# sr.add(data=imgData, modelIndex=sr.MODEL_ANIME_STYLE_ART_RGB_NOISE0, backId=0, format="webp", width=1000, high=1000)

# load picture...
# newData, format, backId, tick = sr.load(0)
```

"""
Version = "2.0.0"

Plat = sys.platform

print(Plat)

class CMakeExtension(Extension):
    def __init__(self, name):
        super().__init__(name, sources=[])
        
        
class BuildExt(build_ext):
    def run(self):
        for ext in self.extensions:
            if isinstance(ext, CMakeExtension):
                self.build_cmake(ext)
        super().run()

    def build_cmake(self, ext):
        cwd = pathlib.Path().absolute()

        build_temp = f"{pathlib.Path(self.build_temp)}/{ext.name}"
        os.makedirs(build_temp, exist_ok=True)
        extdir = pathlib.Path(self.get_ext_fullpath(ext.name))
        extdir.mkdir(parents=True, exist_ok=True)
        all_build_args = []
        
        if Plat == "darwin" and ("arm64" in platform.machine() or IsMacosBuildUniversal2):
                all_build_args += [
                    "-DCMAKE_SYSTEM_PROCESSOR=arm64",
                    "-DCMAKE_OSX_ARCHITECTURES=arm64",
                    "-DFLAG_-mno-sse2=OFF"
                ] 
                
        config = "Debug" if self.debug else "Release"
        print("out_file, {}".format(str(extdir.parent.absolute()) + "/sr_vulkan/"))
        cmake_args = [
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=" + str(extdir.parent.absolute()) + "/sr_vulkan/",
            "-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=" + str(extdir.parent.absolute()) + "/sr_vulkan/",
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=" + str(extdir.parent.absolute()) + "/sr_vulkan/",
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE="+ str(extdir.parent.absolute()) + "/sr_vulkan/",
            "-DCMAKE_BUILD_TYPE=" + config,
        ]
        if Plat == "darwin":
            cmake_args += [
                "-DOpenMP_C_FLAGS=\"-Xclang -fopenmp\"",
                "-DOpenMP_CXX_FLAGS=\"-Xclang -fopenmp\"",
                "-DOpenMP_C_LIB_NAMES=libomp",
                "-DOpenMP_CXX_LIB_NAMES=libomp",
                "-DCMAKE_SKIP_RPATH=TRUE" ,
                "-DCMAKE_SKIP_BUILD_RPATH=TRUE",
                "-DOpenMP_libomp_LIBRARY={}".format(os.path.abspath("VulkanSDK/macos/libomp.a")),
                "-DVulkan_LIBRARY={}".format(os.path.abspath("VulkanSDK/macos/libMoltenVK.a")),
                "-DVulkan_INCLUDE_DIR={}".format(os.path.abspath("VulkanSDK/macos/include")),
            ] + all_build_args
        elif Plat in ["win32", "win64"]:
            cmake_args += [
                "-DVulkan_LIBRARY={}".format(os.path.abspath("VulkanSDK/windows/vulkan-1.lib")),
                "-DVulkan_INCLUDE_DIR={}".format(os.path.abspath("VulkanSDK/Include")),
            ]
        else:
            cmake_args += [
                "-DVulkan_LIBRARY={}".format(os.path.abspath("VulkanSDK/linux/libvulkan.so")),
                "-DVulkan_INCLUDE_DIR={}".format(os.path.abspath("VulkanSDK/Include")),
            ]
            
        build_args = [
            "--config", config
        ]

        os.chdir(build_temp)
        self.spawn(["cmake", f"{str(cwd)}/{ext.name}"] + cmake_args)
        if not self.dry_run:
            self.spawn(["cmake", "--build", "."] + build_args)
        print(build_temp)
        os.chdir(str(cwd))
        
extModule = CMakeExtension("src")
setuptools.setup(
    name="sr-vulkan",
    version=Version,
    author="tonquer",
    license="MIT",
    author_email="tonquer@qq.com",
    description="A super resolution python tool, use nihui/waifu2x-ncnn-vulkan, nihui/realsr-ncnn-vulkan, nihui/realcugan-ncnn-vulkan, xinntao/Real-ESRGAN-ncnn-vulkan",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/tonquer/sr-vulkan",
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
    python_requires = ">=3.6",
    include_package_data=True,
    entry_points={
        "pyinstaller40": [
            "hook-dirs = sr_vulkan:get_hook_dirs"
        ]
    },
    cmdclass={"build_ext": BuildExt},
    ext_modules=[extModule],
)

# python setup.py sdist
