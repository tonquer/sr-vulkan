import sr_ncnn_vulkan
from pathlib import Path

sr_ncnn_vulkan_path = Path(sr_ncnn_vulkan.__file__).parent

models_path = sr_ncnn_vulkan_path / "models"
datas = [(str(models_path), "sr_ncnn_vulkan/models")]
