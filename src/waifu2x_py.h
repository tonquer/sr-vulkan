#ifndef WAIFU2X_PY_H
#define WAIFU2X_PY_H

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "waifu2x_main.h"

static PyObject*
waifu2x_py_init(PyObject* self, PyObject* args);

static PyObject*
waifu2x_py_set_debug(PyObject* self, PyObject* args);

static PyObject*
waifu2x_py_get_model_name(PyObject* self, PyObject* args);

static PyObject*
waifu2x_py_init_set(PyObject* self, PyObject* args, PyObject* kwargs);

static PyObject*
waifu2x_py_set_webp_quality(PyObject* self, PyObject* args);

static PyObject*
waifu2x_py_set_realcugan_syncgap(PyObject* self, PyObject* args);

static PyObject*
waifu2x_py_clear (PyObject* self, PyObject* args);

static PyObject*
waifu2x_py_get_error(PyObject* self, PyObject* args);

static PyObject*
waifu2x_py_remove_wait(PyObject* self, PyObject* args, PyObject* kwargs);

static PyObject*
waifu2x_py_remove(PyObject* self, PyObject* args, PyObject* kwargs);

static PyObject*
waifu2x_py_add(PyObject* self, PyObject* args, PyObject* kwargs);

static PyObject*
waifu2x_py_load(PyObject* self, PyObject* args, PyObject* kwargs);

static PyObject*
waifu2x_py_stop(PyObject* self, PyObject* args, PyObject* kwargs);

static PyObject*
waifu2x_py_get_gpu_core(PyObject* self, PyObject* args, PyObject* kwargs);

static PyObject*
waifu2x_py_get_cpu_core(PyObject* self, PyObject* args);

static PyObject*
waifu2x_py_version(PyObject* self, PyObject* args);

static PyObject*
waifu2x_py_set_path(PyObject* self, PyObject* args);

static PyObject*
waifu2x_py_get_info(PyObject* self, PyObject* args);

PyMODINIT_FUNC
PyInit_sr_ncnn_vulkan(void);

static PyMethodDef SpamMethods[] = {
    {"init",  waifu2x_py_init, METH_VARARGS,
     "Init ncnn\n"},
    {"initSet",  (PyCFunction)waifu2x_py_init_set, METH_VARARGS | METH_KEYWORDS,
     "Init setting\ngpuId: getGpuInfo get index \ncpuNum(Option): CPU model use CPU num, default cpu num / 2 \n"},
    {"setWebpQuality",  (PyCFunction)waifu2x_py_set_webp_quality, METH_VARARGS,\
     "set webp quality \ncan go from 0 (smaller output, lower quality) to 100 (best quality,larger output) \n"},
    {"setRealcuganSyncgap",  (PyCFunction)waifu2x_py_set_realcugan_syncgap, METH_VARARGS,\
     "set realcugan syncgap \ncan go from 1 (slot) to 3 (fast) \n"},
    {"add",  (PyCFunction)waifu2x_py_add, METH_VARARGS | METH_KEYWORDS,
     "Add task, \ndata: img bytes \nmodelIndex: Model enum \nbackId: call back id \nformat(Option): export fmt, default import fmt \nimport support bmp png jpg gif webp \nexport support bmp png jpg webp \nwidth(Option): export set width \nhigh(Option): export set high \nscale(Option): export set width and high \ntileSize(Option): default Auto\n"},
    {"getGpuInfo",  (PyCFunction)waifu2x_py_get_info, METH_VARARGS,
     "Get gpu list\n"},
    {"getGpuCoreNum",  (PyCFunction)waifu2x_py_get_gpu_core, METH_VARARGS | METH_KEYWORDS,
     "Get gpu core num\n"},
    {"getCpuCoreNum",  (PyCFunction)waifu2x_py_get_cpu_core, METH_VARARGS,
     "Get cpu core num\n"},
    {"remove",  (PyCFunction)waifu2x_py_remove, METH_VARARGS | METH_KEYWORDS,
     "Delete task, By callback ids\nbackIds: callback ids\n"},
    {"clear",  (PyCFunction)waifu2x_py_clear, METH_VARARGS,
     "Clear all queue\n"},
    {"removeWaitProc",  (PyCFunction)waifu2x_py_remove_wait, METH_VARARGS | METH_KEYWORDS,
     "Clear proc task, by callback ids\nbackIds: callback ids\n"},
    {"load",  (PyCFunction)waifu2x_py_load, METH_VARARGS | METH_KEYWORDS,
     "Load a complete task \nblock: 0 block, 1 not block\n"},
    {"getLastError",  (PyCFunction)waifu2x_py_get_error, METH_VARARGS,
     "Get last error msg\n"},
    {"stop",  (PyCFunction)waifu2x_py_stop, METH_VARARGS | METH_KEYWORDS,
     "Kill thread\n"},
    {"getVersion",  waifu2x_py_version, METH_VARARGS,
     "Get version\nProject: https://github.com/tonquer/waifu2x-vulkan \n"},
    {"setDebug",  waifu2x_py_set_debug, METH_VARARGS,
     "Set debug log\n True or False\n"},
     {"setDefaultPath",  waifu2x_py_set_path, METH_VARARGS,
     "Set model default path\n"},
    {"getModelName",  waifu2x_py_get_model_name, METH_VARARGS,
     "get model name by index\n"},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

static struct PyModuleDef spammodule = {
    PyModuleDef_HEAD_INIT,
    "sr_ncnn_vulkan",   /* name of module */
    "doc", /* module documentation, may be NULL */
    -1,       /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
    SpamMethods
};

static bool IsInit = false;
static bool IsInitSet = false;
static const char* Version = "1.3.0";
#endif 