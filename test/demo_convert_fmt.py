# 使用sr进行格式转换


import os, sys
current_path = os.path.abspath(__file__)
os.chdir(os.path.dirname(current_path))
from sr_vulkan import sr_vulkan as sr
import time

if __name__ == "__main__":
    f = open("0.jpg", "rb")
    data = f.read()
    f.close()
    newData = sr.convert(data, "webp")
    if not newData:
        print(f"convert err:{sr.getLastError()}")
        exit(-1)
    # f = open("0.webp", "w+")
    # f.write(newData)
    # f.close()
