# sr_vulkan 图片集成测试

以 `test/0.jpg` 派生不同格式、位深和EXIF方向图片，实际调用已安装的 `sr_vulkan` 做2×超分。不会修改原图、native源码、既有测试或模型，不自动安装/下载任何资源。

## 运行

Python需匹配扩展ABI（当前环境为 `C:/Python310/python.exe`）。图片依赖见 `requirements.txt`，不要用 `-O` / `PYTHONOPTIMIZE` 禁用断言。

在 `test/` 目录运行：

```sh
# 仅测试夹具、断言、报告和调度，不初始化native
python -B -m unittest discover -s integration -p "test_*.py" -v

# 六格式同格式输入/输出，APNG真两帧
python -B integration/run_tests.py --suite formats --models models

# 高位深输入兼容性，允许输出8bit
python -B integration/run_tests.py --suite bitdepth --models models

# 四格式8方向，自动方向开启/关闭对照
python -B integration/run_tests.py --suite orientation --models models

# 全枚举模型（含TTA），最多8个native子进程
python -B integration/run_tests.py --suite models --models models --jobs 8 --timeout 600

# 完整测试与可视化报告
python -B integration/run_tests.py --suite all --models models --jobs 8 --timeout 600
```

省略 `--models` 时使用扩展的默认模型包路径。`--source <图片>` 可替换默认 `0.jpg`。任一目录参数均转成绝对路径，不依赖脚本中修改cwd。不要在整个 `test/` 下全局收集旧脚本：旧样本生成器存在import时读写图片的行为。

## 位深含义

- PNG覆盖RGB8/RGB16、灰度8/16，16指每通道位数。
- BMP覆盖8bpp调色板、16bpp RGB555/RGB565、24bpp RGB。BMP的16是每像素总位数，不能等同PNG的16bit/channel。
- JXL覆盖RGB8、灰度8/16。16bit不是JPEG重构冒充：使用真正16bit灰度编码，并独立验证码流位深/样本。
- 从8bit JPEG派生16bit样本不会恢复源图丢失的精度；夹具使用可控低位变化验证16bit输入路径，而不是测试HDR或超分算法高精度保真。
- 测试要求输入正确、可完成超分、输出尺寸/内容正确；**不要求输出保持16bit**。输出实际位深记录在结果中。

## 方向与内容验证

源图先按自身EXIF规范化再派生输入；派生的orientation1–8仅修改方向信息而不预旋转存储像素。输入独立解码出显示参考，输出不应用任何EXIF，直接比较其像素朝向及结构；不检查输出是否有EXIF，不依赖输出标签辅助纠正方向。

自动方向开启用例：输出朝向等于输入的显示朝向。扩展有 `setAutoTransform` 时显式开启，否则测其默认行为。关闭对照：输出等于输入原始像素朝向，需要该接口，缺失明确失败。神经网络及压缩有误差，比较允许误差，但错误旋转/镜像、灰图/空图不能通过。

JXL方向继续通过JPEG重构产生有效码流方向，以独立libjxl真实解码验证；EXIF box不能代替码流方向。Pillow的重构JPEG视图不作为显示金标准。使用已安装插件附带匹配的libjxl共享库（已验证Windows插件1.3.8）；找不到库或生成/核验失败明确报错，不跳过或假冒通过。

## 报告和人工查看

默认输出位于 `integration/results/<时间戳_唯一ID>/`，每次新建，持久保留；用 `--output <新目录或空目录>` 指定位置，非空目录不会覆盖。

- **report.html**：直接用浏览器打开，查看输入原始像素、输入EXIF显示朝向、参考、输出的并排图片、位深、耗时、错误及日志链接。
- **report.md**：Markdown文档，便于编辑器查看。
- **summary.json**：完整结构化环境与逐项结果。运行期间每个结果完成后更新报告。
- 每例独立目录保留原始编码输入、raw/display/reference PNG预览、原始编码输出、逐帧output PNG预览、prepared.json、worker.json及worker.log。

参考/输入预览为基础尺寸，native输出为2×；HTML并排缩放方便观察。16bit原件单独保留，PNG预览不能当作原文件位深。缺接口/缺权重/native崩溃时，已预生成的输入及参考仍可查看。输出只有实际产生时才存在，不会用参考图伪装输出。

报告按原枚举顺序展示，失败不静默skip，未执行项不算通过；初始化错误也尽量生成文档。历史首轮手工记录见 `RESULTS.md`，本次以自动生成的文档为准。

## 并发与设备

`--jobs`默认8，**只作用于模型组**；父线程调度8个独立native进程，不在一个native实例中并发提交。格式/位深/方向顺序运行。每例单独日志和目录，主线程汇总报告。

- `--device auto`沿用旧脚本自动设备选择，`--device cpu`显式CPU，`--device gpu --gpu 0`显式GPU。
- `--threads 0`为默认自动策略：模型进程CPU推理1线程，其他组最多4线程；可显式传正整数覆盖。
- GPU同时8套模型可能显存不足；按实际情况调低 `--jobs`，测试不偷偷降低并发、不把OOM/崩溃改为成功。
- `--timeout`为单例native子进程硬超时，包含初始化/推理/异步关闭宽限期；父进程负责终止回收。`stop()`异步，保留原test.py的5秒关闭宽限。
- 全部必需项通过退出0；夹具错误、缺依赖/接口/权重、错误像素、崩溃、超时均非零，不设置benchmark性能阈值。

当前已安装扩展与工作区源码可能不一致，报告会打印实际加载路径。历史首轮扩展曾缺方向接口且BMP崩溃、JPEG重构JXL空输出；环境更新后的行为以每次自动报告为准，不能从旧结果推断所有JXL路径。仓库 `models` 缺realsr普通/TTA所需权重；失败不会被测试修复或掩盖。
