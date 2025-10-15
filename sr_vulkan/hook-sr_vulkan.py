import sr_vulkan
from pathlib import Path

sr_vulkan_path = Path(sr_vulkan.__file__).parent
hiddenimports = ["sr_vulkan_model_waifu2x", "sr_vulkan_model_realsr", "sr_vulkan_model_realcugan", "sr_vulkan_model_realesrgan"]
