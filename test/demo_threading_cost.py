""" 耗时测试
"""
import math
import queue
import threading
import time
import os
file_path = os.path.abspath(__file__)

from sr_vulkan import sr_vulkan as sr
from sr_vulkan import enum

task_filename_dict = {}

def receive_images(tasks, main_tasks, errors):
    """每个成功提交的任务读取一个结果，backId 用于关联输出文件。"""
    global task_filename_dict
    try:
        while tasks.get() is not None:
            info = sr.load(0)
            if info is None:
                break
            new_data, image_format, back_id, tick = info
            if not new_data:
                print(f'超分输出为空：backId:{back_id}, {sr.getLastError()}')
                main_tasks.put((back_id, -1))
                continue
            name = task_filename_dict.get(back_id)
            path = "sr-tmp"
            filename = path + f'/{name}.{image_format}'
            if not os.path.isdir(path):
                os.mkdir(path)
            f = open(filename, "wb+")
            f.write(new_data)
            f.close()
            main_tasks.put((back_id, tick))
            print('已保存：{}，耗时：{}'.format(filename, tick), flush=True)
        print("thread end")
    except Exception as error:
        errors.append(error)


def main():
    tasks = queue.Queue()
    main_tasks = queue.Queue()
    errors = []
    modelNameDict = {}
    modelCost = []
    f = open(os.path.join(os.path.dirname(file_path), "0.jpg"), "rb")
    data = f.read()
    f.close()
    try:
        status = sr.init()
        gpu_infos = sr.getGpuInfo()
        sr.setThreadNum(1, 1, 1)
        sr.setAutoTransform(True)
        # sr.setDebug(True)

        use_cpu = status < 0 or not gpu_infos or (len(gpu_infos) == 1 and 'LLVM' in gpu_infos[0])
        status = sr.initSet(-1, sr.getCpuCoreNum()) if use_cpu else sr.initSet(0)
        if status != 0:
            raise RuntimeError('初始化失败：' + sr.getLastError())
        print('设备：{}，GPU列表：{}'.format('CPU' if use_cpu else 'GPU 0', gpu_infos), flush=True)
        receiverList = []
        threadNum = 1
        for i in range(threadNum):
            receiver = threading.Thread(target=receive_images,
                                        args=(tasks, main_tasks, errors),
                                        name=f'sr-result-loader{i}')
            receiver.start()
            receiverList.append(receiver)

        processNum = 0
        for back_id, v  in enumerate(enum.AllModelNames, 1):
            modelName = v
            model = getattr(sr, modelName)
            modelNameDict[back_id] = modelName
            task_filename_dict[back_id] = f"tmp_{modelName}_{back_id}"
            taskId = sr.add(data, model, back_id, scale=2)
            if taskId <= 0:
                raise RuntimeError('提交任务{}失败：{}'.format(back_id, sr.getLastError()))
            tasks.put(back_id)
            processNum += 1
            print('已提交：taskId={}，backId={}'.format(taskId, back_id), flush=True)

        for i in range(threadNum):
            tasks.put(None)
            
    finally:
        baseCost = 0
        while processNum > 0:
            back_id, tick = main_tasks.get()
            modelName = modelNameDict.get(back_id)
            modelCost.append((modelName, tick))
            if modelName == "MODEL_WAIFU2X_CUNET_UP2X_DENOISE3X":
                baseCost = max(0.0001, tick)
            processNum -= 1

        modelCost.sort(key=lambda a:a[1])

        for i, v in enumerate(modelCost,1):
            rate = math.trunc(v[1]/baseCost*100)
            print('排名：{}，模型：{}, 基准：{}%, 耗时：{}'.format(i, v[0], rate, v[1]), flush=True)
        sr.stop()
        # 与 test.py 一致：stop 是异步关闭，留出退出宽限时间。
        time.sleep(5)
        print("end")


if __name__ == '__main__':
    main()
