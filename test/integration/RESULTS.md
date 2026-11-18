# 首轮本机验证记录（历史）

第二轮已改为从 `0.jpg` 派生、真实位深输入及8进程模型测试，当前结果请打开 `results/` 每次运行目录内自动生成的 `report.html` / `report.md` / `summary.json`。此文保留首轮环境结果，不代表当前扩展仍缺方向接口或所有JXL路径均失败。

## 第二轮自动报告入口

- 完整165项： [HTML报告](results/20261118_120414_d0b8ccc2/report.html) · [Markdown报告](results/20261118_120414_d0b8ccc2/report.md)。134通过、31失败、0超时、0待执行。格式5/6通过；位深11/11通过；方向36/64通过；模型82/84通过（缺realsr权重两项）。方向失败包括JPEG/WebP开启时非1方向，以及JXL重构输入的非1方向开关用例。所有失败图片和日志均保留。
- 最终代码独立位深复测：[HTML报告](results/20261118_120926_d7e85a8f/report.html)，11项全部通过，包含RGB16 PNG和灰度16 JXL；输出均为8bit/channel。
- 最终代码六格式复测：[HTML报告](results/20261118_121224_6c2c237a/report.html)，5通过，BMP native崩溃。
- 测试框架自检29项全部通过。模型组最多8进程执行，首末模型日志完成时间跨度约66秒（不含夹具准备和首批初始化），不是完整套件总耗时。

以上自动结果目录被git忽略；本机可直接打开，分享时需连同整个运行目录复制。下面保留首轮历史记录。

日期：2026-11-18。结果仅针对本次实际加载的已安装扩展，不代表工作区最新 C++ 源码构建结果。

## 环境

- Python：`C:/Python310/python.exe`，3.10.11，64位。
- 扩展：`C:/Python310/Lib/site-packages/sr_vulkan/sr_vulkan.pyd`。
- Pillow：12.3.0；pillow-jxl-plugin：1.3.8。
- GPU：Radeon RX550/550 Series，自动模式选择 GPU 0。
- 模型目录：`G:/Github/sr-vulkan/test/models`。
- 未安装依赖、未替换扩展、未修改 native 源码或既有测试脚本。

## 自检

```sh
python -B -m unittest discover -s integration -p "test_*.py" -v
```

**13项全部通过**。包括六格式图片构造、JXL八方向独立解码、错误方向拒绝、APNG两帧及顺序、灰/近灰假通过回归、矩阵覆盖、缺模型提前失败、native崩溃和超时处理。`-O` 模式另外验证为拒绝运行并返回非零。

## 格式组

```sh
python -B integration/run_tests.py --suite formats --models models --timeout 120
```

最终严格断言运行：**6项，4通过、2失败，退出1**。

| 格式 | 结果 |
| --- | --- |
| PNG | 通过 |
| JPG | 通过 |
| WebP | 通过 |
| BMP | 子进程 native 异常退出，代码3221226356（0xC0000374） |
| APNG | 通过，包含两帧内容/顺序校验 |
| JXL | 返回空输出；`getLastError()` 为 `bad png sig` |

CPU（2线程）独立复测同样4通过、2失败，因此不是仅GPU路径的失败。错误字符串是模块返回的原始信息，不据此推断根因。

最终GPU格式日志/图片：`C:/Users/Administrator/AppData/Local/Temp/sr-vulkan-tests-3mt7ni1x`。
CPU格式日志/图片：`C:/Users/Administrator/AppData/Local/Temp/sr-vulkan-tests-dvolvejw`。

## EXIF方向组

```sh
python -B integration/run_tests.py --suite orientation --models models --timeout 30
```

**64项均明确失败，退出1**：已安装扩展缺少 `setAutoTransform`。在提交 native 任务之前报错，没有静默skip；因此本环境尚不能验证超分后的自动旋转或开关对照。图片生成器本身的四格式八方向验证已通过，不等同于 native 方向功能通过。

日志：`C:/Users/Administrator/AppData/Local/Temp/sr-vulkan-tests-ntf8po58`。

## 全模型组

```sh
python -B integration/run_tests.py --suite models --models models --timeout 180
```

**84项，82通过、2失败，退出1**。两个失败项为：

- `MODEL_REALSR_DF2K_UP4X`
- `MODEL_REALSR_DF2K_UP4X_TTA`

原因均为 `models/realsr/REALSR_DF2K_UP4X.param` 和 `.bin` 缺失，在 native 初始化/提交之前明确失败。其余82个模型均实际运行成功；对保留的82张输出再次使用最终严格颜色分类断言复核，全部通过。运行期间改进的 JSON 报告字段不影响该批用例结果；该批 `summary.json` 使用最初版本的汇总结构。

日志/图片：`C:/Users/Administrator/AppData/Local/Temp/sr-vulkan-tests-px532f8c`。

因此只能确认这82项在该环境可用，不能宣称全部模型可用；需要补齐 realsr 权重后重新执行全量组。

## 其他

新增测试文件空白格式检查通过。仓库整体 `git diff --check` 对原有修改报告4处行末空白（`src/deps_codec.cmake`、`jpeg_image.h`、`jxl_image.h`、`png_image.h`）；本次未改动这些文件。
