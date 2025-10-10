$LIB_NAME='sr-ncnn-vulan'
$TAG_NAME='v1.3.0'
$PACKAGE_PREFIX=($LIB_NAME + '-' + $TAG_NAME)
$PACKAGENAME=($PACKAGE_PREFIX + '-windows')

$oldPath=$pwd
# Vulkan SDK
# $TRUE_FALSE=(Test-Path ".\VulkanSDK")

# if(! $TRUE_FALSE)
# {
#   Invoke-WebRequest -Uri `
#     https://sdk.lunarg.com/sdk/download/1.2.198.1/windows/VulkanSDK-1.2.198.1-Installer.exe?Human=true `
#     -OutFile VulkanSDK-1.2.198.1-Installer.exe
#   try
#   {
#     7z x -aoa .\VulkanSDK-1.2.198.1-Installer.exe -oVulkanSDK
#   }
#   Catch
#   {
#     &"C:\Program Files\7-Zip\7z.exe" x -aoa .\VulkanSDK-1.2.198.1-Installer.exe -oVulkanSDK
#   }
#   Remove-Item .\VulkanSDK\Demos, `
#               .\VulkanSDK\Samples, `
#               .\VulkanSDK\Third-Party, `
#               .\VulkanSDK\Tools, `
#               .\VulkanSDK\Tools32, `
#               .\VulkanSDK\Bin32, `
#               .\VulkanSDK\Lib32 `
#               -Recurse
# }

# Python (x86_64)
# $Env:pythonLocation='C:\Python37'
# $Env:VULKAN_SDK=((Get-Location).Path + '\VulkanSDK')

if(! $Env:PYTHON_BIN)
{
  $Env:PYTHON_BIN=(cmd /c where python|select -first 1)
}
$PYTHON_DIR=$Env:PYTHON_BIN.Replace("python.exe", "")
$V=(&$Env:PYTHON_BIN -V 2>&1).Replace("Python ", "").Substring(0, 3).Replace(".", "")

$PYTHON_LIBRARIES="$($PYTHON_DIR + '\libs\python3.lib;' + $PYTHON_DIR + "\libs\python" + $V + ".lib")"
$PYTHON_INCLUDE_DIRS="$($PYTHON_DIR + '\include')"

echo $PYTHON_DIR
echo $Env:PYTHON_BIN
echo $V
echo $PYTHON_INCLUDE_DIRS
echo $PYTHON_LIBRARIES
mkdir -Force build_win; Set-Location .\build_win\
cmake `
      -DVulkan_LIBRARY="..\VulkanSDK\windows\vulkan-1.lib" `
      -DVulkan_INCLUDE_DIR="..\VulkanSDK\Include" `
      -DDCMAKE_BUILD_TYPE="Release" `
      -DDCMAKE_VERBOSE_MAKEFILE=On `
      -DNCNN_VULKAN=ON `
      -DNCNN_BUILD_TOOLS=OFF `
      -DNCNN_BUILD_EXAMPLES=OFF `
      -DUSE_FIND_PYTHON=OFF `
      -DPython3_INCLUDE_DIRS="$PYTHON_INCLUDE_DIRS" `
      -DPYTHON_LIBRARY="$PYTHON_LIBRARIES" `
      -DCMAKE_VERBOSE_MAKEFILE=ON `
      ..\src
cmake --build . --config Release
Set-Location .\Release\

$filePath = "sr-ncnn-vulkan.pyd"
if (Test-Path -Path $filePath) {
    # Package
    Set-Location $oldPath
    mkdir -Force "$($PACKAGENAME)"
    Copy-Item -Force -Verbose -Path "README.md" -Destination "$($PACKAGENAME)"
    Copy-Item -Force -Verbose -Path "LICENSE" -Destination "$($PACKAGENAME)"
    Copy-Item -Force -Verbose -Path "build_win\Release\sr_ncnn_vulkan.pyd" -Destination "$($PACKAGENAME)"
    Copy-Item -Force -Verbose -Recurse -Path "sr_ncnn_vulkan\models" -Destination "$($PACKAGENAME)"
    Copy-Item -Force -Verbose -Recurse -Path "test" -Destination "$($PACKAGENAME)"
} else {
    Write-Host "sr_ncnn_vulkan.dll not found"
}

