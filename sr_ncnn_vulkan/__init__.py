from pathlib import Path


def get_hook_dirs():
    package_folder = Path(__file__).parent
    return [str(package_folder.absolute())]
