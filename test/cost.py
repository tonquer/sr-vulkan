import os, sys
current_path = os.path.abspath(__file__)
os.chdir(os.path.dirname(current_path))
from sr_ncnn_vulkan import sr_ncnn_vulkan as sr
from sr_ncnn_vulkan import enum
import time

if __name__ == "__main__":

    # 初始化ncnn
    # init ncnn
    sts = sr.init()
    
    isCpuModel = False
    if sts < 0:
        # cpu model
        isCpuModel = True
        
    
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

    # 开启打印
    # open debug log
    sr.setDebug(True)

    f = open("0.jpg", "rb")
    data = f.read()
    f.close()
    backId = 1
    count = 0
    for name in enum.AllModelNames:
        index = getattr(sr, name)
        sr.add(data, index, backId, 2.5)
        info = sr.load(0)
        if not info:
            print("not found, {}".format(name))
            continue 
        newData, foramt, backId, tick = info
        print(name, tick)
    exit(1)