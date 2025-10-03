import setuptools, sys, os
from shutil import copyfile, copytree, rmtree
from setuptools.command.build_ext import build_ext
import subprocess
from distutils.core import Extension
import platform
import logging
logging.basicConfig(level=logging.DEBUG)

IsMacosBuildUniversal2 = False

long_description = \
"""
# sr-ncnn-vulkan-python
- This is modified [waifu2x-ncnn-vulkan](https://github.com/nihui/waifu2x-ncnn-vulkan), [realsr-ncnn-vulkan](https://github.com/nihui/realsr-ncnn-vulkan) [realcugan-ncnn-vulkan](https://github.com/nihui/realcugan-ncnn-vulkan), Export pyd and so files to Python
- Support Linux, Windows, MacOs
- Support import JPG, PNG, BMP, GIF, WEBP, Animated WEBP, APNG
- Support export JPG, PNG, BMP, WEBP, Animated WEBP, APNG
- Support vulkan gpu and cpu

# Install
```shell
pip install sr-ncnn-vulkan
```

# Use
```shell
from sr-ncnn-vulkan import sr-ncnn-vulkan as sr

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
    
    
# add picture ...
# sr.add(data=imgData, modelIndex=sr.MODEL_ANIME_STYLE_ART_RGB_NOISE0, backId=0, scale=2.5)
# sr.add(data=imgData, modelIndex=sr.MODEL_ANIME_STYLE_ART_RGB_NOISE0, backId=0, format="webp", width=1000, high=1000)

# load picture...
# newData, format, backId, tick = sr.load(0)
```

"""
Version = "1.2.0"

Plat = sys.platform

print(Plat)

build_temp = "build/temp/"
if Plat == "darwin":
    example_module = Extension('sr_ncnn_vulkan.sr_ncnn_vulkan',
    include_dirs=["build/temp/ncnntmp/src", "build/temp/pngtmp/", "build/temp/pngtmp/zlib", "src/ncnn/src", "src/libwebp/src",  "src/libpng", "src/libpng/zlib", "build/temp/src", "VulkanSDK/macos/include"],
    sources=['src/waifu2x_main.cpp', 'src/waifu2x_py.cpp', 'src/waifu2x.cpp', 'src/realcugan.cpp', 'src/realsr.cpp', 'src/realesrgan.cpp'],
    extra_objects=[
        build_temp + "/ncnntmp/src/libncnn.a",
        build_temp + "/webptmp/libwebp.a",
        build_temp + "/webptmp/libwebpmux.a",
        build_temp + "/webptmp/libwebpdemux.a",
        build_temp + "/pngtmp/libpng.a",
        build_temp + "/pngtmp/zlib/libz.a",
        "VulkanSDK/macos/libMoltenVK.a",
        build_temp + "/ncnntmp/glslang/SPIRV/libSPIRV.a",
        build_temp + "/ncnntmp/glslang/glslang/libglslang.a",
        build_temp + "/ncnntmp/glslang/glslang/libMachineIndependent.a",
        build_temp + "/ncnntmp/glslang/OGLCompilersDLL/libOGLCompiler.a",
        build_temp + "/ncnntmp/glslang/glslang/OSDependent/Unix/libOSDependent.a",
        build_temp + "/ncnntmp/glslang/glslang/libGenericCodeGen.a",
        "-framework", "Metal",
        "-framework", "QuartzCore",
        "-framework", "CoreGraphics",
        "-framework", "Cocoa",
        "-framework", "IOKit",
        "-framework", "IOSurface",
        "-framework", "Foundation",
        "-framework", "CoreFoundation",
    ],
    )
    models = [example_module]
elif Plat in ["win32", "win64"]:
    example_module = Extension('sr_ncnn_vulkan.sr_ncnn_vulkan',
    include_dirs=["build/temp/ncnntmp/src", "build/temp/pngtmp/", "build/temp/pngtmp/zlib", "src/ncnn/src", "src/libwebp/src", "src/libpng", "src/libpng/zlib", "build/temp/src", "VulkanSDK/Include"],
    sources=['src/waifu2x_main.cpp', 'src/waifu2x_py.cpp', 'src/waifu2x.cpp', 'src/realcugan.cpp', 'src/realsr.cpp', 'src/realesrgan.cpp'],
    define_macros=[("WIN32",1), ("NOMINMAX",1), ("NDEBUG",1)],
    extra_objects=[
        build_temp + "/ncnntmp/src/Release/ncnn.lib",
        build_temp + "/webptmp/Release/webp.lib",
        build_temp + "/webptmp/Release/webpmux.lib",
        build_temp + "/webptmp/Release/webpdemux.lib",
        build_temp + "/pngtmp/Release/libpng16_static.lib",
        build_temp + "/pngtmp/zlib/Release/zlibstatic.lib",
        "VulkanSDK/windows/vulkan-1.lib",
        build_temp + "/ncnntmp/glslang/SPIRV/Release/SPIRV.lib",
        build_temp + "/ncnntmp/glslang/glslang/Release/glslang.lib",
        build_temp + "/ncnntmp/glslang/glslang/Release/MachineIndependent.lib",
        build_temp + "/ncnntmp/glslang/OGLCompilersDLL/Release/OGLCompiler.lib",
        build_temp + "/ncnntmp/glslang/glslang/OSDependent/Windows/Release/OSDependent.lib",
        build_temp + "/ncnntmp/glslang/glslang/Release/GenericCodeGen.lib",
    ],
    )
    models = [example_module]
else:
    # linux
    if 'arm64' in platform.platform() or 'aarch64' in platform.platform():
        os.rename('VulkanSDK/linux/libvulkan.so', 'VulkanSDK/linux/libvulkan.x86.so')
        os.rename('VulkanSDK/linux/libvulkan.aarch64.so', 'VulkanSDK/linux/libvulkan.so')
    example_module = Extension('sr_ncnn_vulkan.sr_ncnn_vulkan',
    include_dirs=["build/temp/ncnntmp/src", "build/temp/pngtmp/", "build/temp/pngtmp/zlib", "src/ncnn/src", "src/libwebp/src",  "src/libpng", "src/libpng/zlib", "build/temp/src", "VulkanSDK/Include"],
    sources=['src/waifu2x_main.cpp', 'src/waifu2x_py.cpp', 'src/waifu2x.cpp', 'src/realcugan.cpp', 'src/realsr.cpp', 'src/realesrgan.cpp'],
    extra_objects=[
        build_temp + "/ncnntmp/src/libncnn.a",
        build_temp + "/webptmp/libwebp.a",
        build_temp + "/webptmp/libwebpmux.a",
        build_temp + "/webptmp/libwebpdemux.a",
        build_temp + "/pngtmp/libpng.a",
        build_temp + "/pngtmp/zlib/libz.a",
        "VulkanSDK/linux/libvulkan.so",
        build_temp + "/ncnntmp/glslang/SPIRV/libSPIRV.a",
        build_temp + "/ncnntmp/glslang/glslang/libglslang.a",
        build_temp + "/ncnntmp/glslang/glslang/libMachineIndependent.a",
        build_temp + "/ncnntmp/glslang/OGLCompilersDLL/libOGLCompiler.a",
        build_temp + "/ncnntmp/glslang/glslang/OSDependent/Unix/libOSDependent.a",
        build_temp + "/ncnntmp/glslang/glslang/libGenericCodeGen.a",
        
    ],
    libraries=["gomp"],
    extra_compile_args=["-fopenmp"],
    extra_link_args=["-fopenmp"],

    )
    models = [example_module]

PLAT_TO_CMAKE = {
    "win32": "Win32",
    "win-amd64": "x64",
    "win-arm32": "ARM",
    "win-arm64": "ARM64",
}

class CMakeBuild(build_ext):
    def to_build_ncnn(self):
        return
    
    def to_build_zlib(self):
        return
    
    def to_build_png(self):
        return
    
    def to_build_webp(self):
        return
    
    def to_build_jpeg(self):
        return
    
    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        extdir = os.path.join(extdir, "ncnn")

        # required for auto-detection of auxiliary "native" libs
        if not extdir.endswith(os.path.sep):
            extdir += os.path.sep

        cfg = "Debug" if self.debug else "Release"

        # CMake lets you override the generator - we need to check this.
        # Can be set with Conda-Build, for example.
        cmake_generator = os.environ.get("CMAKE_GENERATOR", "")

        # Set Python_EXECUTABLE instead if you use PYBIND11_FINDPYTHON
        # EXAMPLE_VERSION_INFO shows you how to pass a value into the C++ code
        # from Python.
        cmake_args = [
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={}".format(extdir),
            "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE={}".format(extdir),
            "-DCMAKE_BUILD_TYPE={}".format(cfg),  # not used on MSVC, but no harm
        ]
        build_args = []

        if self.compiler.compiler_type != "msvc":
            # Using Ninja-build since it a) is available as a wheel and b)
            # multithreads automatically. MSVC would require all variables be
            # exported for Ninja to pick it up, which is a little tricky to do.
            # Users can override the generator with CMAKE_GENERATOR in CMake
            # 3.15+.
            pass
            # if not cmake_generator:
                # cmake_args += ["-GNinja"]
        else:
            # Single config generators are handled "normally"
            single_config = any(x in cmake_generator for x in {"NMake", "Ninja"})

            # CMake allows an arch-in-generator style for backward compatibility
            contains_arch = any(x in cmake_generator for x in {"ARM", "Win64"})

            # Specify the arch if using MSVC generator, but only if it doesn't
            # contain a backward-compatibility arch spec already in the
            # generator name.
            if not single_config and not contains_arch:
                cmake_args += ["-A", PLAT_TO_CMAKE[self.plat_name]]

            # Multi-config generators have a different way to specify configs
            if not single_config:
                cmake_args += [
                    "-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_{}={}".format(cfg.upper(), extdir)
                ]
                build_args += ["--config", cfg]

        # Set CMAKE_BUILD_PARALLEL_LEVEL to control the parallel build level
        # across all generators.
        if "CMAKE_BUILD_PARALLEL_LEVEL" not in os.environ:
            # self.parallel is a Python 3 only way to set parallel jobs by hand
            # using -j in the build_ext call, not supported by pip or PyPA-build.
            if hasattr(self, "parallel") and self.parallel:
                # CMake 3.12+ only.
                build_args += ["-j{}".format(self.parallel)]
        all_build_args = []
        
        if Plat == "darwin" and ("arm64" in platform.machine() or IsMacosBuildUniversal2):
                all_build_args += [
                    "-DCMAKE_SYSTEM_PROCESSOR=arm64;x86_64",
                    "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64",
                    "-DFLAG_-mno-sse2=OFF"
                ]
        cmake_args += all_build_args
        ncnn_build_temp = build_temp + "ncnntmp/"
        webp_build_temp = build_temp + "webptmp/"
        png_build_temp = build_temp + "pngtmp/"
        if not os.path.exists(ncnn_build_temp):
            os.makedirs(ncnn_build_temp)
        if not os.path.exists(webp_build_temp):
            os.makedirs(webp_build_temp)
        if not os.path.exists(png_build_temp):
            os.makedirs(png_build_temp)

        # build libpng
        png_args = [
            "-DPNG_STATIC=ON",
            "-DPNG_SHARED=OFF",
            "-DPNG_TESTS=OFF",
            "-DPNG_ARM_NEON=off",
            "-DPNG_EXECUTABLES=OFF",
        ] + all_build_args
        subprocess.check_call(
            ["cmake", os.path.abspath("src/libpng")] + png_args, cwd=png_build_temp
        )
        subprocess.check_call(
            ["cmake", "--build", "."] + build_args, cwd=png_build_temp
        )

        # build webp
        webp_args = [
            "-DWEBP_ENABLE_SIMD=ON",
            "-DWEBP_BUILD_ANIM_UTILS=OFF",
            "-DWEBP_BUILD_CWEBP=OFF",
            "-DWEBP_BUILD_DWEBP=OFF",
            "-DWEBP_BUILD_GIF2WEBP=OFF",
            "-DWEBP_BUILD_IMG2WEBP=OFF",
            "-DWEBP_BUILD_VWEBP=OFF",
            "-DWEBP_BUILD_WEBPINFO=OFF",
            "-DWEBP_BUILD_WEBPMUX=OFF",
            "-DWEBP_BUILD_EXTRAS=OFF",
            "-DWEBP_BUILD_WEBP_JS=OFF",
            "-DWEBP_NEAR_LOSSLESS=OFF",
            "-DWEBP_ENABLE_SWAP_16BIT_CSP=OFF",
        ]
        subprocess.check_call(
            ["cmake", os.path.abspath("src/libwebp")] + webp_args, cwd=webp_build_temp
        )
        subprocess.check_call(
            ["cmake", "--build", "."] + build_args, cwd=webp_build_temp
        )

        # build ncnn
        if Plat == "darwin":
            cmake_args += [
                "-DOpenMP_C_FLAGS=\"-Xclang -fopenmp\"",
                "-DOpenMP_CXX_FLAGS=\"-Xclang -fopenmp\"",
                "-DOpenMP_C_LIB_NAMES=libomp",
                "-DOpenMP_CXX_LIB_NAMES=libomp",
                "-DOpenMP_libomp_LIBRARY={}".format(os.path.abspath("VulkanSDK/macos/libomp.a")),
                "-DVulkan_LIBRARY={}".format(os.path.abspath("VulkanSDK/macos")),
                "-DVulkan_INCLUDE_DIR={}".format(os.path.abspath("VulkanSDK/macos/include")),
            ] + all_build_args
        elif Plat in ["win32", "win64"]:
            cmake_args += [
                "-DVulkan_LIBRARY={}".format(os.path.abspath("VulkanSDK/windows")),
                "-DVulkan_INCLUDE_DIR={}".format(os.path.abspath("VulkanSDK/Include")),
            ]
        else:
            cmake_args += [
                "-DVulkan_LIBRARY={}".format(os.path.abspath("VulkanSDK/linux/")),
                "-DVulkan_INCLUDE_DIR={}".format(os.path.abspath("VulkanSDK/Include")),
            ]
        subprocess.check_call(
            ["cmake", os.path.abspath("src/ncnn")] + cmake_args, cwd=ncnn_build_temp
        )
        subprocess.check_call(
            ["cmake", "--build", "."] + build_args, cwd=ncnn_build_temp
        )

        self.force = True
        return super(self.__class__, self).build_extension(ext)

setuptools.setup(
    name="sr-ncnn-vulkan",
    version=Version,
    author="tonquer",
    license="MIT",
    author_email="tonquer@qq.com",
    description="A super resolution python tool, use nihui/waifu2x-ncnn-vulkan, nihui/realsr-ncnn-vulkan, nihui/realcugan-ncnn-vulkan, xinntao/Real-ESRGAN-ncnn-vulkan",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/tonquer/sr-ncnn-vulkan",
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
            "hook-dirs = sr_ncnn_vulkan:get_hook_dirs"
        ]
    },
    cmdclass={"build_ext": CMakeBuild},
    ext_modules=models,
)

# python setup.py sdist
