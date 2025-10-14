import sr_vulkan
from pathlib import Path

sr_vulkan_path = Path(sr_vulkan.__file__).parent

models_path = sr_vulkan_path / "models"
datas = [(str(models_path), "sr_vulkan/models")]
