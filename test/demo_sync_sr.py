# 使用sr进行超分

import os, sys
current_path = os.path.abspath(__file__)
os.chdir(os.path.dirname(current_path))
from sr_vulkan import sr_vulkan as sr
import time


if __name__ == "__main__":

    # 初始化ncnn
    # init ncnn
    sts = sr.init()
    
    isCpuModel = False
    if sts < 0:
        # cpu model
        isCpuModel = True
    
    # 设置自定义models路径，或者使用pip安装其他models    
    # pip install sr-vulkan-model-waifu2x sr-vulkan-model-realcugan sr-vulkan-model-realesrgan sr-vulkan-model-realsr
    # sr.setModelPath("models")

    # 开启打印
    # open debug log
    # sr.setDebug(True)

    # 设置web图质量
    # sr.setWebpQuality(80)

    # 设置自动处理旋转exif图
    # sr.setAutoTransform(True)
    
    # 设置 decode/proc/encode线程数量
    # sr.setThreadNum(1, 1, 1)

    # 获得Gpu列表
    # get gpu list
    infos = sr.getGpuInfo()
    
    # if llvm gpu, use cpu model
    if infos and len(infos) == 1 and "LLVM" in infos[0]:
        isCpuModel = True

    cpuNum = sr.getCpuCoreNum()
    gpuNum = sr.getGpuCoreNum(0)
    print("init, code:{}, gpuList:{}, cpuNum:{}, gpuNum:{}".format(str(sts), infos, cpuNum, gpuNum))    

    # 选择Gpu, 如果使用cpu模式，设置使用的cpu核心数
    # select gpu, if cpu model set cpu num
    if isCpuModel:
        sts = sr.initSet(-1, cpuNum)
    else:
        sts = sr.initSet(0)
    print("init set, code:{}".format(str(sts)))

    f = open("0.jpg", "rb")
    data = f.read()
    f.close()
    backId = 1
    count = 0
    
    # start convert, by setting scale
    if sr.add(data, sr.MODEL_WAIFU2X_CUNET_UP2X_DENOISE3X, backId, scale=2) > 0:
        count += 1
        # load等待
        info = sr.load(0)
        if info:
            newData, foramt, callBackId, tick = info
            # backId == callBackId

            f = open(str(callBackId) + "." + foramt, "wb+")
            f.write(newData)
            f.close()
        else:
            print("sr error, {}".format(sr.getLastError()))
    else:
        print("sr error, {}".format(sr.getLastError()))

    # 固定长宽
    # CPU模式默认tileSize=800，如果内存小，请调低改值
    # start convert, by setting width and high
    # CPU Model default tileSize=800, if the memory is too small, Please turn down
    # if sr.add(data, sr.MODEL_WAIFU2X_CUNET_UP2X_DENOISE3X, backId, 900, 800, tileSize=200, format="webp") > 0:
        # count += 1
    
    # free ncnn, close thread
    sr.stop()
    time.sleep(5)
