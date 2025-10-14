# enable global link time optimization
cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0069 NEW)
set(CMAKE_POLICY_DEFAULT_CMP0069 NEW)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

project(sr-vulkan)

set(CMAKE_BUILD_TYPE Release)
set(CMAKE_CXX_STANDARD 11)
option(USE_SYSTEM_NCNN "build with system libncnn" OFF)
option(USE_SYSTEM_WEBP "build with system libwebp" OFF)
option(USE_STATIC_MOLTENVK "link moltenvk static library" OFF)
option(USE_FIND_PYTHON "Documentation for USE_FIND_PYTHON" ON)
# set(CMAKE_EXE_LINKER_FLAGS /NODEFAULTLIB:"LIBCMT.lib")

if(USE_FIND_PYTHON)
    find_package(Python3 COMPONENTS Interpreter Development)
    set(Python3_USE_STATIC_LIBS FALSE)
endif()

if(NOT WIN32)
    set(CMAKE_THREAD_LIBS_INIT "-lpthread")
endif()


set(CMAKE_HAVE_THREADS_LIBRARY 1)
set(CMAKE_USE_WIN32_THREADS_INIT 0)
set(CMAKE_USE_PTHREADS_INIT 1)
set(THREADS_PREFER_PTHREAD_FLAG ON)

find_package(Threads)
find_package(OpenMP)

if(APPLE)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -undefined dynamic_lookup")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -undefined dynamic_lookup")
    message(STATUS "Detected macOS version: ${MACOS_VERSION}")
    if(OPENMP_FOUND)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS} -undefined dynamic_lookup")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS} -undefined dynamic_lookup")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
    endif()
else()
    if(OPENMP_FOUND)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS} ")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS} ")
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${OpenMP_EXE_LINKER_FLAGS}")
    endif()
endif()

set(CMAKE_POSITION_INDEPENDENT_CODED ON)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_supported_output)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(WARNING "IPO is not supported: ${ipo_supported_output}")
endif()

add_custom_target(generate-spirv DEPENDS ${SHADER_SPV_HEX_FILES})

if(MINGW)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -municode")
endif()

add_library(sr-vulkan SHARED waifu2x_main.cpp waifu2x.cpp realcugan.cpp realsr.cpp waifu2x_py.cpp realesrgan.cpp)
add_dependencies(sr-vulkan generate-spirv)

include(${CMAKE_CURRENT_SOURCE_DIR}/deps_ncnn.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/deps_codec.cmake)

target_include_directories(sr-vulkan PRIVATE ${Python3_INCLUDE_DIRS})
message("include " ${Python3_INCLUDE_DIRS})

if(WIN32)
    target_link_directories(sr-vulkan PRIVATE ${Python3_LIBRARY_DIRS})
endif()

set(WAIFU2X_LINK_LIBRARIES ncnn ${DEPS_CODEC_LIBRARIES} ${Vulkan_LIBRARY})

if(USE_STATIC_MOLTENVK)
    find_library(CoreFoundation NAMES CoreFoundation)
    find_library(Foundation NAMES Foundation)
    find_library(Metal NAMES Metal)
    find_library(QuartzCore NAMES QuartzCore)
    find_library(CoreGraphics NAMES CoreGraphics)
    find_library(Cocoa NAMES Cocoa)
    find_library(IOKit NAMES IOKit)
    find_library(IOSurface NAMES IOSurface)

    list(APPEND WAIFU2X_LINK_LIBRARIES
        ${Metal}
        ${QuartzCore}
        ${CoreGraphics}
        ${Cocoa}
        ${IOKit}
        ${IOSurface}
        ${Foundation}
        ${CoreFoundation}
    )
endif()

if(WIN32)
    set_target_properties(sr-vulkan PROPERTIES SUFFIX ".pyd")
endif()

if(APPLE)
    set_target_properties(sr-vulkan PROPERTIES SUFFIX ".so")
endif()

target_link_libraries(sr-vulkan ${WAIFU2X_LINK_LIBRARIES})
set_target_properties(sr-vulkan PROPERTIES OUTPUT_NAME "sr_vulkan" PREFIX "")

include(GNUInstallDirs)
install(TARGETS sr-vulkan RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR})