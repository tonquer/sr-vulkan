#include "waifu2x_py.h"

PyMODINIT_FUNC
PyInit_sr_ncnn_vulkan(void)
{
    PyObject* m;

    m = PyModule_Create(&spammodule);
    if (m == NULL)
        return NULL;

    char modelName[256];
    char modelNameTTa[256];
    int index = 0;
    int len = sizeof(AllModel) / sizeof(AllModel[0]);
    for (int j = 0; j < len; j++)
    {
        std::string name = AllModel[j];
;       sprintf(modelName, "MODEL_%s", name.c_str());
        sprintf(modelNameTTa, "%s_TTA", modelName);
        PyModule_AddIntConstant(m, modelName, index++);
        PyModule_AddIntConstant(m, modelNameTTa, index++);
    }

    return m;
}

static PyObject*
waifu2x_py_init(PyObject* self, PyObject* args)
{
    if (IsInit)
    {
        return PyLong_FromLong(0);
    }
    int sts = waifu2x_init();
    IsInit = true;
    return PyLong_FromLong(sts);
}

static PyObject*
waifu2x_py_init_set(PyObject* self, PyObject* args, PyObject* kwargs)
{
    if (!IsInit) return PyLong_FromLong(-1);
    if (IsInitSet) return PyLong_FromLong(0);
    Py_ssize_t  gpuId = 0;
    Py_ssize_t  cpuNum = 0;
    Py_ssize_t  noSetDefaultPath = 0;
    char* kwarg_names[] = { (char*)"gpuId",(char*)"cpuNum", (char*)"noDefaultPath", NULL };
    int sts;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "i|ii", kwarg_names, &gpuId, &cpuNum, &noSetDefaultPath))
        return PyLong_FromLong(-1);
    
    if (!noSetDefaultPath && waifu2x_get_path_size() <= 0)
    {
        PyObject* pyModule = PyImport_ImportModule("sr_ncnn_vulkan");
        PyObject* v = PyObject_GetAttrString(pyModule, "__file__");

        PyObject* pathModule = PyImport_ImportModule("os.path");
        PyObject* func = PyObject_GetAttrString(pathModule, "dirname");

        PyObject* pyargs = PyTuple_New(1);
        PyTuple_SET_ITEM(pyargs, 0, v);
        PyObject* rel = PyObject_CallObject(func, pyargs);


#if _WIN32
        const wchar_t* path = NULL;
        PyArg_Parse(rel, "u", &path);
#else 
        const char* path = NULL;
        PyArg_Parse(rel, "s", &path);
#endif
        sts = waifu2x_init_path(path);
    }
    sts = waifu2x_init_set(gpuId, cpuNum);
    if (!sts) IsInitSet = true;
    return PyLong_FromLong(sts);
}

static PyObject*
waifu2x_py_set_webp_quality(PyObject* self, PyObject* args)
{
    int quality=0;
    if (!PyArg_ParseTuple(args, "i", &quality))
        PyLong_FromLong(-1);
    if (quality <= 0 || quality > 100)
    {
        return PyLong_FromLong(-1);
    }
    waifu2x_set_webp_quality(quality);
    return PyLong_FromLong(0);
}

static PyObject*
waifu2x_py_set_realcugan_syncgap(PyObject* self, PyObject* args)
{
    int syncgap = 0;
    if (!PyArg_ParseTuple(args, "i", &syncgap))
        PyLong_FromLong(-1);
    if (syncgap <= 0 || syncgap > 3)
    {
        return PyLong_FromLong(-1);
    }
    waifu2x_set_realcugan_syncgap(syncgap);
    return PyLong_FromLong(0);
}

static PyObject*
waifu2x_py_clear(PyObject* self, PyObject* args)
{
    if (!IsInitSet)
    {
        return PyLong_FromLong(0);
    }
    waifu2x_clear();
    return PyLong_FromLong(0);
}


static PyObject*
waifu2x_py_get_error(PyObject* self, PyObject* args)
{
    std::string err = waifu2x_get_error();
    return PyUnicode_FromString(err.c_str());
}

static PyObject*
waifu2x_py_set_debug(PyObject* self, PyObject* args)
{
    unsigned int isDebug;
    if (!PyArg_ParseTuple(args, "i", &isDebug))
        Py_RETURN_NONE;
    waifu2x_set_debug(bool(isDebug));
    return PyLong_FromLong(0);
}

static PyObject*
waifu2x_py_get_model_name(PyObject* self, PyObject* args)
{
    Py_ssize_t modelIndex = 0;
    if (!PyArg_ParseTuple(args, "i", &modelIndex))
        Py_RETURN_NONE;
    modelIndex = modelIndex / 2;
    int len = sizeof(AllModel) / sizeof(AllModel[0]);
    char errMsg[512];
    if (modelIndex >= len) {
        sprintf(errMsg, "[SR_NCNN] index error, index:%d, max_index:%d", modelIndex, len);
        waifu2x_set_error(errMsg);
        Py_RETURN_NONE;
    }
    std::string name = AllModel[modelIndex];
    return PyUnicode_FromString(name.c_str());
}

static PyObject*
waifu2x_py_set_path(PyObject* self, PyObject* args)
{
    int sts;
#if _WIN32
    const wchar_t* modelPath = 0;
    if (!PyArg_ParseTuple(args, "|u", &modelPath))
        Py_RETURN_NONE;
#else
    const char* modelPath = 0;
    if (!PyArg_ParseTuple(args, "|s", &modelPath))
        Py_RETURN_NONE;
#endif

    if (modelPath)
    {
        sts = waifu2x_init_path(modelPath);
    }
    else
    {
        PyObject* pyModule = PyImport_ImportModule("sr_ncnn_vulkan");
        PyObject* v = PyObject_GetAttrString(pyModule, "__file__");

        PyObject* pathModule = PyImport_ImportModule("os.path");
        PyObject* func = PyObject_GetAttrString(pathModule, "dirname");

        PyObject* pyargs = PyTuple_New(1);
        PyTuple_SET_ITEM(pyargs, 0, v);
        PyObject* rel = PyObject_CallObject(func, pyargs);
#if _WIN32
        const wchar_t* path = NULL;
        PyArg_Parse(rel, "u#", &path);
#else
        const char* path = NULL;
        PyArg_Parse(rel, "s#", &path);
#endif
        sts = waifu2x_init_path(path);
    }
    return PyLong_FromLong(sts);
}

static PyObject*
waifu2x_py_remove_wait(PyObject* self, PyObject* args, PyObject* kwargs)
{
    if (!IsInitSet)
    {
        Py_RETURN_NONE;
    }
    PyObject* bufobj;
    char * kwarg_names[] = { (char*)"backIds", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", kwarg_names, &bufobj))
        Py_RETURN_NONE;

    Py_ssize_t list_len = PyObject_Size(bufobj);
    if (list_len <= 0)
    {
        Py_RETURN_NONE;
    }
    std::set<int> taskIds;
    PyObject* list_item = NULL;
    Py_ssize_t  taskId;
    for (int i = 0; i < list_len; i++)
    {
        list_item = PyList_GetItem(bufobj, i);
        PyArg_Parse(list_item, "i", &taskId);
        taskIds.insert(taskId);
    }
    waifu2x_remove_wait(taskIds);
    return PyLong_FromLong(0);
}

static PyObject*
waifu2x_py_remove(PyObject* self, PyObject* args, PyObject* kwargs)
{
    if (!IsInitSet)
    {
        Py_RETURN_NONE;
    }
    PyObject* bufobj;

    char* kwarg_names[] = { (char*)"backIds", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O", kwarg_names, &bufobj))
        Py_RETURN_NONE;

    Py_ssize_t list_len = PyObject_Size(bufobj);
    if (list_len <= 0)
    {
        Py_RETURN_NONE;
    }
    std::set<int> taskIds;
    PyObject* list_item = NULL;
    Py_ssize_t  taskId;
    for (int i = 0; i < list_len; i++)
    {
        list_item = PyList_GetItem(bufobj, i);
        PyArg_Parse(list_item, "i", &taskId);
        taskIds.insert(taskId);
    }
    waifu2x_remove(taskIds);
    return PyLong_FromLong(0);
}


static PyObject*
waifu2x_py_add(PyObject* self, PyObject* args, PyObject* kwargs)
{
    if (!IsInitSet)
    {
        waifu2x_set_error("waifu2x not init");
        return PyLong_FromLong(-1);
    }
    const char* b = NULL;
    Py_ssize_t size;
    int sts = 1;
    Py_ssize_t  callBack = 0;
    Py_ssize_t  modelIndex = 0;
    const char* format = NULL;
    Py_ssize_t  width = 0;
    Py_ssize_t  high = 0;
    Py_ssize_t  tileSize = 0;
    float scale = 0;

    char* kwarg_names[] = { (char*)"data",(char*)"modelIndex",(char*)"backId",(char*)"scale", (char*)"format", (char *)"tileSize", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "y#iif|si", kwarg_names, &b, &size, &modelIndex, &callBack, &scale, &format, &tileSize))
    {
        PyErr_Clear();
        char* kwarg_names2[] = { (char*)"data",(char*)"modelIndex",(char*)"backId", (char*)"width", (char*)"height",(char*)"format", (char*)"tileSize", NULL};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "y#iiii|si", kwarg_names2, &b, &size, &modelIndex, &callBack, &width, &high, &format, &tileSize))
        {
            waifu2x_set_error("invalid params");
            return PyLong_FromLong(-2);
        }
    }
        
    //fprintf(stdout, "point:%p, size:%d, index:%d, back:%d, scale:%f \n", b, size, modelIndex, callBack, scale);
    if (!b)
    {
        waifu2x_set_error("invalid data");
        return PyLong_FromLong(-3);
    }
    //b = (unsigned char* )PyBytes_AsString((PyObject*)c);
    unsigned char* data = NULL;

    data = (unsigned char*)malloc(size);
    memcpy(data, b, size);
    sts = waifu2x_addData(data, size, callBack, modelIndex, format, width, high, scale, tileSize);
    return PyLong_FromLong(sts);
}
static PyObject*
waifu2x_py_get_info(PyObject* self, PyObject* args)
{
    if (!IsInit)
    {
        Py_RETURN_NONE;
    }
    int gpu_count = ncnn::get_gpu_count();
    if (gpu_count <= 0)
    {
        Py_RETURN_NONE;
    }
    PyObject* pyList = PyList_New(gpu_count);
    for (int i = 0; i < gpu_count; i++)
    {
        const char* name = ncnn::get_gpu_info(i).device_name();
        PyObject* item = Py_BuildValue("s", name);
        PyList_SetItem(pyList, i, item);
    }
    return pyList;
}


static PyObject*
waifu2x_py_get_gpu_core(PyObject* self, PyObject* args, PyObject* kwargs)
{
    if (!IsInit)
    {
        return PyLong_FromLong(0);
    }
    Py_ssize_t gpuId = 0;
    char* kwarg_names[] = { (char*)"gpuId", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i", kwarg_names, &gpuId))
        Py_RETURN_NONE;

    int gpu_count = ncnn::get_gpu_count();
    if (gpu_count <= 0)
    {
        return PyLong_FromLong(0);
    }
    int gpu_queue_count = ncnn::get_gpu_info(gpuId).compute_queue_count();
    return PyLong_FromLong(gpu_queue_count);
}

static PyObject*
waifu2x_py_get_cpu_core(PyObject* self, PyObject* args)
{
    if (!IsInit)
    {
        return PyLong_FromLong(0);
    }
    int cpu_queue_count = ncnn::get_cpu_count();
    return PyLong_FromLong(cpu_queue_count);
}


static PyObject*
waifu2x_py_load(PyObject* self, PyObject* args, PyObject* kwargs)
{
    if (!IsInitSet) 
    { 
        waifu2x_set_error("waifu2x not init");
        Py_RETURN_NONE; 
    }
    void* out = NULL;
    unsigned long outSize = 0;
    Py_ssize_t block = 0;
    double tick = 0;
    int callBack;
    char* kwarg_names[] = { (char*)"block", NULL };
    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i", kwarg_names, &block))
        Py_RETURN_NONE;
    PyThreadState* save;
    save = PyEval_SaveThread();
    char format[5] = "";

    int sts = waifu2x_getData(out, outSize, tick, callBack, format, block);
    PyEval_RestoreThread(save);
    if (sts <= 0)
    {
        Py_RETURN_NONE;
    }
    PyObject* data = Py_BuildValue("y#sid", (char*)out, (Py_ssize_t)outSize, format, callBack, tick);
    if (out) free(out);
    return data;
}

static PyObject*
waifu2x_py_stop(PyObject* self, PyObject* args, PyObject* kwargs)
{
    if (!IsInit)
    {
        return PyLong_FromLong(0);
    }
    //Py_ssize_t block = 0;
    //char* kwarg_names[] = { (char*)"block", NULL };
    //if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|i", kwarg_names, &block))
    //    Py_RETURN_NONE;
    IsInit = false;
    IsInitSet = false;
    int sts = waifu2x_stop();
    return PyLong_FromLong(sts);
}


static PyObject*
waifu2x_py_version(PyObject* self, PyObject* args)
{
    PyObject* data = Py_BuildValue("s", Version);
    return data;
}
