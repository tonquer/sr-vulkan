#include "waifu2x_main.h"
#include "other_image.h"

//#endif // _WIN32

#include <cmath>

TaskQueue Todecode;
TaskQueue Toproc;
TaskQueue Toencode;
TaskQueue Tosave;


static int GpuId;
static int TotalJobsProc = 0;
static int NumThreads = 1;
static int TaskId = 1;
static int WebpQuality = 90;
static int RealcuganSyncgap = 1;

bool IsDebug = false;

Waifu2xChar ModelPath[1024] = {0};

int get_max_size()
{
    return Waifu2xList.size() + RealCuganList.size() + RealSrList.size() + RealEsrganList.size();
}

int waifu2x_getData(void*& out, unsigned long& outSize, double& tick, int& callBack, char * format, unsigned int timeout = 10)
{
    Task v;

    Tosave.get(v, timeout);
    if (v.id == 0)
    {
        waifu2x_set_error("waifu2x already stop");
        return -1;
    }

    if (v.id == -233)
    {
        waifu2x_set_error("waifu2x already stop");
        return -1;
    }
    callBack = v.callBack;
    if (v.fileDate) {
        free(v.fileDate);
        v.fileDate = NULL;
    }
    if (!v.isSuc) {
        waifu2x_set_error(v.err.c_str());
        return v.id;
    }
    out = v.out;
    outSize = v.outSize;

    v.out = NULL;
    strcpy(format, v.save_format.c_str());
    tick = v.allTick;
    return v.id;
}
void* waifu2x_decode(void* args)
{
    for (;;)
    {
        Task v;
        Todecode.get(v);
        if (v.id == -233)
            break;
        if (v.modelIndex >= ((int)get_max_size()))
        {
            v.isSuc = false; Tosave.put(v); continue;
        }
        ftime(&v.startTick);
        bool isSuc = to_load(v);
        ftime(&v.decodeTick);
        if (v.fileDate) {
            free(v.fileDate);
            v.fileDate = NULL;
        }
        if (isSuc)
        {
            Toproc.put(v);
        }
        else
        {
            v.isSuc = false;
            Tosave.put(v);
        }
    }
    return NULL;
}
void* waifu2x_encode(void* args)
{
    for (;;)
    {
        Task v;
        Toencode.get(v);
        if (v.id == -233)
            break;
        bool isSuc = to_save(v);
        v.clear_out_image();

        ftime(&v.encodeTick);
        double decodeTick = (v.decodeTick.time + v.decodeTick.millitm / 1000.0) - (v.startTick.time + v.startTick.millitm / 1000.0);
        double procTick = (v.procTick.time + v.procTick.millitm / 1000.0) - (v.decodeTick.time + v.decodeTick.millitm / 1000.0);
        double encodeTick = (v.encodeTick.time + v.encodeTick.millitm / 1000.0) - (v.procTick.time + v.procTick.millitm / 1000.0);
        double allTick = (v.encodeTick.time + v.encodeTick.millitm / 1000.0) - (v.startTick.time + v.startTick.millitm / 10000.0);
        v.allTick = allTick;
        waifu2x_printf(stdout, "[SR_NCNN] end encode imageId :%d, decode:%.2fs, proc:%.2fs, encode:%.2fs, \n",
            v.callBack, decodeTick, procTick, encodeTick);

        if (isSuc)
        {
            Tosave.put(v);
        }
        else
        {
            const char* err = stbi_failure_reason();
            if (err)
            {
                v.err = err;
            }
            v.isSuc = false; 
            Tosave.put(v);
        }

    }
    return NULL;
}
void* waifu2x_process_data(Task &v, const SuperResolution* waifu2x)
{
    const char* name;
    if (GpuId >= 0)
        name = ncnn::get_gpu_info(GpuId).device_name();
    else
        name = "cpu";

    waifu2x_printf(stdout, "[SR_NCNN] start encode imageId :%d, gpu:%s, format:%s, model:%s, noise:%d, scale:%d, to_scale:%.2f, tta:%d, tileSize:%d, to_tile:%d\n",
        v.callBack, name, v.save_format.c_str(), waifu2x->mode_name.c_str(), waifu2x->noise, waifu2x->scale, v.scale, waifu2x->tta_mode, waifu2x->tilesize, v.tileSize);

    int scale_run_count = 1;
    int frame = 0;
    for (std::list<ncnn::Mat*>::iterator in = v.inImage.begin(); in != v.inImage.end(); in++)
    {
        frame++;
        ncnn::Mat& inimage = **in;
        if (in == v.inImage.begin())
        {
            if (v.toH <= 0 || v.toW <= 0)
            {
                v.toH = ceil(v.scale * inimage.h);
                v.toW = ceil(v.scale * inimage.w);
            }

            //if (c == 4)
            //{
            //    v.file = "png";
            //}
            //waifu2x->process(v.inimage, v.outimage);
            scale_run_count = 1;

            if (waifu2x->scale <= 1)
            {
                scale_run_count = 1;
            }
            else if (v.toH > 0 && v.toW > 0 && inimage.h > 0 && inimage.w > 0)
            {
                scale_run_count = std::max(int(ceil(v.toW * 1.0 / inimage.w)), scale_run_count);
                scale_run_count = std::max(int(ceil(v.toH * 1.0 / inimage.h)), scale_run_count);
                scale_run_count = ceil(log(scale_run_count) / log(waifu2x->scale));
                scale_run_count = std::max(scale_run_count, 1);
            }
        }
        int toW = inimage.w * pow(waifu2x->scale, scale_run_count);
        int toH = inimage.h * pow(waifu2x->scale, scale_run_count);
        ncnn::Mat* outimage = new ncnn::Mat(toW, toH, (size_t)inimage.elemsize, (int)inimage.elemsize);

        for (int i = 0; i < scale_run_count; i++)
        {
            if (i == scale_run_count - 1)
            {
                waifu2x_printf(stdout, "[SR_NCNN] start encode imageId :%d, count:%d, frame:%d, h:%d->%d, w:%d->%d \n",
                    v.callBack, i + 1, frame, inimage.h, outimage->h, inimage.w, outimage->w);
                waifu2x->process(inimage, *outimage, v.tileSize);
                inimage.release();
            }
            else
            {
                ncnn::Mat tmpimage(inimage.w * waifu2x->scale, inimage.h * waifu2x->scale, (size_t)inimage.elemsize, (int)inimage.elemsize);
                waifu2x_printf(stdout, "[SR_NCNN] start encode imageId :%d, count:%d, frame:%d, h:%d->%d, w:%d->%d \n",
                    v.callBack, i + 1, frame, inimage.h, tmpimage.h, inimage.w, tmpimage.w);
                waifu2x->process(inimage, tmpimage, v.tileSize);
                inimage.release();
                inimage = tmpimage;
            }
        }
        v.outImage.push_back(outimage);

    }
    ftime(&v.procTick);
    v.clear_in_image();
    Toencode.put(v);
    return NULL;
}

void* waifu2x_proc(void* args)
{
    const SuperResolution* waifu2x;
    for (;;)
    {
        Task v;
        Toproc.get(v);
        if (v.id == -233)
            break;

        if (v.modelIndex >= Waifu2xList.size() + RealCuganList.size() + RealSrList.size())
        {
            waifu2x = RealEsrganList[v.modelIndex - Waifu2xList.size() - RealCuganList.size() - RealSrList.size()];
            waifu2x_process_data(v, waifu2x);
        }
        else if (v.modelIndex >= Waifu2xList.size() + RealCuganList.size())
        {
            waifu2x = RealSrList[v.modelIndex - Waifu2xList.size() - RealCuganList.size()];
            waifu2x_process_data(v, waifu2x);
        }
        else if (v.modelIndex >= Waifu2xList.size())
        {
            waifu2x = RealCuganList[v.modelIndex - Waifu2xList.size()];
            waifu2x_process_data(v, waifu2x);
        }
        else
        {
            waifu2x = Waifu2xList[v.modelIndex];
            waifu2x_process_data(v, waifu2x);
        }
    }
    return NULL;
}

void* waifu2x_to_stop(void* args)
{
    for (int i = 0; i < (int)OtherThreads.size(); i++)
    {
        OtherThreads[i]->join();
        delete OtherThreads[i];
    }
    for (int i = 0; i < TotalJobsProc; i++)
    {
        ProcThreads[i]->join();
        delete ProcThreads[i];
    }
    ncnn::destroy_gpu_instance();
    return 0;
}

int waifu2x_addModel(const Waifu2xChar* name, int scale2, int noise2, int tta_mode, int num_threads, int index)
{

    Waifu2xChar parampath[1024];
    Waifu2xChar modelpath[1024];
#if _WIN32
    // Waifu2xList.push_back(NULL);
    swprintf(parampath, L"%s/models/waifu2x/%s.param", ModelPath, name);
    swprintf(modelpath, L"%s/models/waifu2x/%s.bin", ModelPath, name);
#else
    // Waifu2xList.push_back(NULL);
    sprintf(parampath, "%s/models/waifu2x/%s.param", ModelPath, name);
    sprintf(modelpath, "%s/models/waifu2x/%s.bin", ModelPath, name);
#endif

    int prepadding = 18;
    int tilesize = 0;
    uint32_t heap_budget;
    if (GpuId == -1) heap_budget = 4000;
    else heap_budget = ncnn::get_gpu_device(GpuId)->get_heap_budget();
#if WIN32
    if (!wcsncmp(name, L"WAIFU2X_CUNET", 13))
#else
    if (!strncmp(name, "WAIFU2X_CUNET", 13))
#endif
    {
        if (noise2 == -1)
        {
            prepadding = 18;
        }
        else if (scale2 == 1)
        {
            prepadding = 28;
        }
        else if (scale2 == 2)
        {
            prepadding = 18;
        }
        if (heap_budget > 2600)
            tilesize = 400;
        else if (heap_budget > 740)
            tilesize = 200;
        else if (heap_budget > 250)
            tilesize = 100;
        else
            tilesize = 32;
    }

#if WIN32
    else if (!wcsncmp(name, L"WAIFU2X_ANIME", 13))
#else
    else if (!strncmp(name, "WAIFU2X_ANIME", 13))
#endif
    {
        prepadding = 7;
        if (heap_budget > 1900)
            tilesize = 400;
        else if (heap_budget > 550)
            tilesize = 200;
        else if (heap_budget > 190)
            tilesize = 100;
        else
            tilesize = 32;
    }
#if WIN32
    else if (!wcsncmp(name, L"WAIFU2X_PHOTO", 13))
#else
    else if (!strncmp(name, "WAIFU2X_PHOTO", 13))
#endif
    {
        prepadding = 7;
        if (heap_budget > 1900)
            tilesize = 400;
        else if (heap_budget > 550)
            tilesize = 200;
        else if (heap_budget > 190)
            tilesize = 100;
        else
            tilesize = 32;
    }
    if (GpuId == -1) tilesize = 400;

#if _WIN32

    struct _stat buffer;
    if (_wstat((wchar_t *)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_NCNN] not found path %s\n", parampath);
        return Waifu2xError::NotModel;
    }
    if (_wstat((wchar_t *)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_NCNN] not found path %s\n", modelpath);
        return Waifu2xError::NotModel;
    }
#else

    struct stat buffer;
    if (stat((char *)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_NCNN] not found path %s\n", parampath);
        return Waifu2xError::NotModel;
    }
    if (stat((char *)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_NCNN] not found path %s\n", modelpath);
        return Waifu2xError::NotModel;
    }
#endif

#if _WIN32
    //_bstr_t t1 = parampath;
    //std::wstring paramfullpath((wchar_t*)t1);

    //_bstr_t t2 = modelpath;
    //std::wstring modelfullpath((wchar_t*)t2);
    std::wstring paramfullpath(parampath);
    std::wstring modelfullpath(modelpath);

    _bstr_t b(name);
    const char* name2 = b;
#else
    std::string paramfullpath(parampath);
    std::string modelfullpath(modelpath);
    const char* name2 = name;
#endif
    Waifu2x* waifu = new Waifu2x(GpuId, tta_mode, num_threads, name2);
    waifu->load(paramfullpath, modelfullpath);
    waifu->noise = noise2;
    waifu->scale = scale2;
    //if (GpuId == -1)
    //{
        // cpu only
        //tilesize = 4000;
    //}

    waifu->tilesize = tilesize;
    waifu->prepadding = prepadding;
    Waifu2xList[index] = waifu;
    return 1;
}

int realcugan_addModel(const Waifu2xChar* name, int scale, int noise, int tta_mode, int num_threads, int index)
{
    Waifu2xChar parampath[1024];
    Waifu2xChar modelpath[1024];
#if _WIN32
    // Waifu2xList.push_back(NULL);
    swprintf(parampath, L"%s/models/realcugan/%s.param", ModelPath, name);
    swprintf(modelpath, L"%s/models/realcugan/%s.bin", ModelPath, name);
#else
    // Waifu2xList.push_back(NULL);
    sprintf(parampath, "%s/models/realcugan/%s.param", ModelPath, name);
    sprintf(modelpath, "%s/models/realcugan/%s.bin", ModelPath, name);
#endif
    int prepadding = 18;
    int tilesize = 400;
    uint32_t heap_budget;
    if (GpuId == -1) heap_budget = 4000;
    else heap_budget = ncnn::get_gpu_device(GpuId)->get_heap_budget();

#if _WIN32

    struct _stat buffer;
    if (_wstat((wchar_t*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_NCNN] not found path %s\n", parampath);
        return Waifu2xError::NotModel;
    }
    if (_wstat((wchar_t*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_NCNN] not found path %s\n", modelpath);
        return Waifu2xError::NotModel;
    }
#else

    struct stat buffer;
    if (stat((char*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_NCNN] not found path %s\n", parampath);
        return Waifu2xError::NotModel;
    }
    if (stat((char*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_NCNN] not found path %s\n", modelpath);
        return Waifu2xError::NotModel;
    }
#endif

#if _WIN32
    //_bstr_t t1 = parampath;
    //std::wstring paramfullpath((wchar_t*)t1);

    //_bstr_t t2 = modelpath;
    //std::wstring modelfullpath((wchar_t*)t2);
    std::wstring paramfullpath(parampath);
    std::wstring modelfullpath(modelpath);

    _bstr_t b(name);
    const char* name2 = b;
#else
    std::string paramfullpath(parampath);
    std::string modelfullpath(modelpath);
    const char* name2 = name;
#endif
    RealCUGAN* waifu = new RealCUGAN(GpuId, tta_mode, num_threads, name2);
    waifu->scale = scale;
    waifu->noise = noise;
    waifu->load(paramfullpath, modelfullpath);
    if (scale == 2)
    {
        prepadding = 18;
        if (heap_budget > 1300)
            tilesize = 400;
        else if (heap_budget > 800)
            tilesize = 300;
        else if (heap_budget > 400)
            tilesize = 200;
        else if (heap_budget > 200)
            tilesize = 100;
        else
            tilesize = 32;
    }
    if (scale == 3)
    {
        prepadding = 14;
        if (heap_budget > 3300)
            tilesize = 400;
        else if (heap_budget > 1900)
            tilesize = 300;
        else if (heap_budget > 950)
            tilesize = 200;
        else if (heap_budget > 320)
            tilesize = 100;
        else
            tilesize = 32;
    }
    if (scale == 4)
    {
        prepadding = 19;
        if (heap_budget > 1690)
            tilesize = 400;
        else if (heap_budget > 980)
            tilesize = 300;
        else if (heap_budget > 530)
            tilesize = 200;
        else if (heap_budget > 240)
            tilesize = 100;
        else
            tilesize = 32;
    }
    if (GpuId == -1) tilesize = 400;
    //if (GpuId == -1)
    //{
        // cpu only
        //tilesize = 4000;
    //}

    waifu->tilesize = tilesize;
    waifu->prepadding = prepadding;
    waifu->syncgap = RealcuganSyncgap;
    RealCuganList[index] = waifu;
    return 1;
}

int realsr_addModel(const Waifu2xChar* name, int scale, int noise, int tta_mode, int num_threads, int index)
{

    Waifu2xChar parampath[1024];
    Waifu2xChar modelpath[1024];
#if _WIN32
    // Waifu2xList.push_back(NULL);
    swprintf(parampath, L"%s/models/realsr/%s.param", ModelPath, name);
    swprintf(modelpath, L"%s/models/realsr/%s.bin", ModelPath, name);
#else
    sprintf(parampath, "%s/models/realsr/%s.param", ModelPath, name);
    sprintf(modelpath, "%s/models/realsr/%s.bin", ModelPath, name);
#endif

    int prepadding = 10;
    int tilesize = 200;
    uint32_t heap_budget;
    if (GpuId == -1) heap_budget = 4000;
    else heap_budget = ncnn::get_gpu_device(GpuId)->get_heap_budget();

#if _WIN32

    struct _stat buffer;
    if (_wstat((wchar_t*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_NCNN] not found path %s\n", parampath);
        return Waifu2xError::NotModel;
    }
    if (_wstat((wchar_t*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_NCNN] not found path %s\n", modelpath);
        return Waifu2xError::NotModel;
    }
#else

    struct stat buffer;
    if (stat((char*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_NCNN] not found path %s\n", parampath);
        return Waifu2xError::NotModel;
    }
    if (stat((char*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_NCNN] not found path %s\n", modelpath);
        return Waifu2xError::NotModel;
    }
#endif

#if _WIN32
    //_bstr_t t1 = parampath;
    //std::wstring paramfullpath((wchar_t*)t1);

    //_bstr_t t2 = modelpath;
    //std::wstring modelfullpath((wchar_t*)t2);
    std::wstring paramfullpath(parampath);
    std::wstring modelfullpath(modelpath);

    _bstr_t b(name);
    const char* name2 = b;
#else
    std::string paramfullpath(parampath);
    std::string modelfullpath(modelpath);
    const char* name2 = name;
#endif
    RealSR* waifu = new RealSR(GpuId, tta_mode, num_threads, name2);
    waifu->load(paramfullpath, modelfullpath);
    waifu->scale = scale;
    waifu->noise = noise;
    {
        if (heap_budget > 1900)
            tilesize = 200;
        else if (heap_budget > 550)
            tilesize = 100;
        else if (heap_budget > 190)
            tilesize = 64;
        else
            tilesize = 32;
    }
    if (GpuId == -1) tilesize = 200;
    //if (GpuId == -1)
    //{
        // cpu only
        //tilesize = 4000;
    //}

    waifu->tilesize = tilesize;
    waifu->prepadding = prepadding;
    RealSrList[index] = waifu;
    return 1;
}

int realesrgan_addModel(const Waifu2xChar* name, int scale, int noise, int tta_mode, int num_threads, int index)
{

    Waifu2xChar parampath[1024];
    Waifu2xChar modelpath[1024];
#if _WIN32
    // Waifu2xList.push_back(NULL);
    swprintf(parampath, L"%s/models/realesrgan/%s.param", ModelPath, name);
    swprintf(modelpath, L"%s/models/realesrgan/%s.bin", ModelPath, name);
#else
    sprintf(parampath, "%s/models/realesrgan/%s.param", ModelPath, name);
    sprintf(modelpath, "%s/models/realesrgan/%s.bin", ModelPath, name);
#endif

    int prepadding = 10;
    int tilesize = 200;
    uint32_t heap_budget;
    if (GpuId == -1) heap_budget = 4000;
    else heap_budget = ncnn::get_gpu_device(GpuId)->get_heap_budget();

#if _WIN32

    struct _stat buffer;
    if (_wstat((wchar_t*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_NCNN] not found path %s\n", parampath);
        return Waifu2xError::NotModel;
    }
    if (_wstat((wchar_t*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_NCNN] not found path %s\n", modelpath);
        return Waifu2xError::NotModel;
    }
#else

    struct stat buffer;
    if (stat((char*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_NCNN] not found path %s\n", parampath);
        return Waifu2xError::NotModel;
    }
    if (stat((char*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_NCNN] not found path %s\n", modelpath);
        return Waifu2xError::NotModel;
    }
#endif

#if _WIN32
    //_bstr_t t1 = parampath;
    //std::wstring paramfullpath((wchar_t*)t1);

    //_bstr_t t2 = modelpath;
    //std::wstring modelfullpath((wchar_t*)t2);
    std::wstring paramfullpath(parampath);
    std::wstring modelfullpath(modelpath);

    _bstr_t b(name);
    const char* name2 = b;
#else
    std::string paramfullpath(parampath);
    std::string modelfullpath(modelpath);
    const char* name2 = name;
#endif
    RealESRGAN* waifu = new RealESRGAN(GpuId, tta_mode, num_threads, name2);
    waifu->load(paramfullpath, modelfullpath);
    waifu->scale = scale;
    waifu->noise = noise;
    {
        if (heap_budget > 1900)
            tilesize = 200;
        else if (heap_budget > 550)
            tilesize = 100;
        else if (heap_budget > 190)
            tilesize = 64;
        else
            tilesize = 32;
    }
    if (GpuId == -1) tilesize = 200;
    //if (GpuId == -1)
    //{
        // cpu only
        //tilesize = 4000;
    //}

    waifu->tilesize = tilesize;
    waifu->prepadding = prepadding;
    RealEsrganList[index] = waifu;
    return 1;
}

int waifu2x_init()
{
#if _WIN32
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif

    return ncnn::create_gpu_instance();
}

int waifu2x_get_path_size()
{
#if _WIN32
    return wcslen(ModelPath);
#else
    return strlen(ModelPath);
#endif
}

int waifu2x_init_path(const Waifu2xChar* modelPath2)
{
#if _WIN32
    if (modelPath2)
    {
        memset(ModelPath, 0, 1024);
        wcscpy(ModelPath, modelPath2);
    };
#else
    if (modelPath2)
    {
        memset(ModelPath, 0, 1024);
        strcpy(ModelPath, modelPath2);
    };
#endif
    return 0;
}

int waifu2x_init_set(int gpuId2, int cpuNum)
{
    if (cpuNum < 0 || cpuNum > 128) 
    { 
        waifu2x_set_error("invalid cpu num params");
        return -1; 
    };

    int jobs_proc = cpuNum;

    //if (gpuId2 == 0) gpuId2 = ncnn::get_default_gpu_index();
    int cpu_count = std::max(1, ncnn::get_cpu_count());
    int gpu_count = ncnn::get_gpu_count();
    if (gpu_count == 0) gpuId2 = -1;

    if (gpuId2 < -1 || gpuId2 >= gpu_count)
    {
        waifu2x_set_error("invalid gpu device index params");
        return -1;
    }
    if (gpuId2 == -1)
    {
        jobs_proc = std::min(jobs_proc, cpu_count);
        if (jobs_proc <= 0) { jobs_proc = std::max(1, cpu_count / 2); }
        NumThreads = jobs_proc;
        TotalJobsProc = 1;
    }
    else
    {
        int gpu_queue_count = ncnn::get_gpu_info(gpuId2).compute_queue_count();
        if (TotalJobsProc <= 0) { TotalJobsProc = std::min(2, gpu_queue_count); }
        NumThreads = 1;
    }
    int len = sizeof(AllModel) / sizeof(AllModel[0]);
    for (int j = 0; j < len; j++)
    {
#if _WIN32
        std::string oldName = AllModel[j];
        wchar_t* oldName2 = _bstr_t(oldName.c_str());
        std::wstring name = oldName2;

        if (name.find(L"WAIFU2X") != std::string::npos) {
            Waifu2xList.push_back(NULL);
            Waifu2xList.push_back(NULL);
        }
        else if (name.find(L"REALCUGAN") != std::string::npos) {
            RealCuganList.push_back(NULL);
            RealCuganList.push_back(NULL);
        }
        else if (name.find(L"REALSR") != std::string::npos) {
            RealSrList.push_back(NULL);
            RealSrList.push_back(NULL);
        }
        else if (name.find(L"REALESRGAN") != std::string::npos) {
            RealEsrganList.push_back(NULL);
            RealEsrganList.push_back(NULL);
        }
#else
        std::string name = AllModel[j];
        if (name.find("WAIFU2X") != std::string::npos) {
            Waifu2xList.push_back(NULL);
            Waifu2xList.push_back(NULL);
        }
        else if (name.find("REALCUGAN") != std::string::npos) {
            RealCuganList.push_back(NULL);
            RealCuganList.push_back(NULL);
        }
        else if (name.find("REALSR") != std::string::npos) {
            RealSrList.push_back(NULL);
            RealSrList.push_back(NULL);
        }
        else if (name.find("REALESRGAN") != std::string::npos) {
            RealEsrganList.push_back(NULL);
            RealEsrganList.push_back(NULL);
        }
#endif
    }
    int modelLen = sizeof(AllModel) / sizeof(AllModel[0]);
    waifu2x_printf(stdout, "[SR_NCNN] init model num:%d, waifu2x:%d, realcugan:%d, realsr:%d, realesrgan:%d\n", modelLen, Waifu2xList.size(), RealCuganList.size(), RealSrList.size(), RealEsrganList.size());

    // waifu2x proc
    ProcThreads.resize(TotalJobsProc);
    {

        ncnn::Thread *load_thread = new ncnn::Thread(waifu2x_encode);
        ncnn::Thread* save_thread = new ncnn::Thread(waifu2x_decode);
        OtherThreads.push_back(load_thread);
        OtherThreads.push_back(save_thread);

        int total_jobs_proc_id = 0;
        for (int j = 0; j < TotalJobsProc; j++)
        {
            ProcThreads[total_jobs_proc_id++] = new ncnn::Thread(waifu2x_proc);
        }
    }
    GpuId = gpuId2;
    //waifu2x_printf(stdout, "init success, threadNum:%d\n", TotalJobsProc);
    return 0;
}

int waifu2x_check_init_model(int initModel)
{   
    if (initModel < 0 || initModel >= (int)get_max_size())
    {
        return Waifu2xError::NotModel;
    }
    int scale = 1;
    int noise = 0;
    bool tta = (initModel % 2 == 1);
    int modelIndex = initModel / 2;
#if _WIN32
    std::string oldName = AllModel[modelIndex];

    wchar_t * oldName2 = _bstr_t(oldName.c_str());
    std::wstring name = oldName2;
    if (name.find(L"UP2X") != std::string::npos)
    {
        scale = 2;
    } else if(name.find(L"UP3X") != std::string::npos)
    {
        scale = 3;
    } else if (name.find(L"UP4X") != std::string::npos)
    {
        scale = 4;
    }

    if (name.find(L"DENOISE0X") != std::string::npos)
    {
        noise = 0;
    }
    else if (name.find(L"DENOISE1X") != std::string::npos)
    {
        noise = 1;
    }
    else if (name.find(L"DENOISE2X") != std::string::npos)
    {
        noise = 2;
    }
    else if (name.find(L"DENOISE3X") != std::string::npos)
    {
        noise = 3;
    }
#else
    std::string name = AllModel[modelIndex];


    if (name.find("UP2X") != std::string::npos)
    {
        scale = 2;
    }
    else if (name.find("UP3X") != std::string::npos)
    {
        scale = 3;
    }
    else if (name.find("UP4X") != std::string::npos)
    {
        scale = 4;
    }

    if (name.find("DENOISE0X") != std::string::npos)
    {
        noise = 0;
    }
    else if (name.find("DENOISE1X") != std::string::npos)
    {
        noise = 1;
    }
    else if (name.find("DENOISE2X") != std::string::npos)
    {
        noise = 2;
    }
    else if (name.find("DENOISE3X") != std::string::npos)
    {
        noise = 3;
    }
#endif
    if (initModel >= ((int)Waifu2xList.size() + (int)RealCuganList.size()) + (int)RealSrList.size())
    {
        initModel = initModel - (int)Waifu2xList.size() - (int)RealCuganList.size() - (int)RealSrList.size();
        if (RealEsrganList[initModel])
        {
            return 1;
        }
#if _WIN32
        waifu2x_printf(stdout, "[SR_NCNN] load model %s, index:%d\n", oldName.c_str(), modelIndex);
#else
        waifu2x_printf(stdout, "[SR_NCNN] load model %s, index:%d\n", name.c_str(), modelIndex);
#endif
        if (GpuId == -1)
        {
#if _WIN32
            waifu2x_printf(stderr, L"[SR_NCNN] RealESRGAN not support cpu model \n");
#else
            waifu2x_printf(stderr, "[SR_NCNN] RealESRGAN not support cpu model \n");
#endif
            waifu2x_set_error("RealESRGAN not support cpu model");
            return Waifu2xError::NotSupport;
        }
        return realesrgan_addModel(name.c_str(), scale, noise, tta, NumThreads, initModel);
    }
    else if (initModel >= ((int)Waifu2xList.size() + (int)RealCuganList.size()))
    {
        initModel = initModel - (int)Waifu2xList.size() - (int)RealCuganList.size();
        if (RealSrList[initModel])
        {
            return 1;
        }
#if _WIN32
        waifu2x_printf(stdout, "[SR_NCNN] load model %s, index:%d\n", oldName.c_str(), modelIndex);
#else
        waifu2x_printf(stdout, "[SR_NCNN] load model %s, index:%d\n", name.c_str(), modelIndex);
#endif
        return realsr_addModel(name.c_str(), scale, noise, tta, NumThreads, initModel);
    }
    else if (initModel >= ((int)Waifu2xList.size()))
    {
        initModel = initModel - (int)Waifu2xList.size();
        if (RealCuganList[initModel])
        {
            return 1;
        }
#if _WIN32
        waifu2x_printf(stdout, "[SR_NCNN] load model %s, index:%d\n", oldName.c_str(), modelIndex);
#else
        waifu2x_printf(stdout, "[SR_NCNN] load model %s, index:%d\n", name.c_str(), modelIndex);
#endif
        return realcugan_addModel(name.c_str(), scale, noise, tta, NumThreads, initModel);
    }
    else {
        if (Waifu2xList[initModel])
        {
            return 1;
        }
#if _WIN32
        waifu2x_printf(stdout, "[SR_NCNN] load model %s, index:%d\n", oldName.c_str(), modelIndex);
#else
        waifu2x_printf(stdout, "[SR_NCNN] load model %s, index:%d\n", name.c_str(), modelIndex);
#endif
        return waifu2x_addModel(name.c_str(), scale, noise, tta, NumThreads, initModel);
    }


    return 1;
}


int waifu2x_addData(const unsigned char* data, unsigned int size, int callBack, int modelIndex, const char* format, unsigned long toW, unsigned long toH, float scale, int tileSize)
{
    Task v;
    TaskId ++;
    v.id = TaskId;
    v.fileDate = (void*)data;
    v.fileSize = size;
    v.callBack = callBack;
    v.modelIndex = modelIndex;
    v.toH = toH;
    v.toW = toW;
    v.scale = scale;
    v.tileSize = tileSize;
    v.webp_quality = WebpQuality;
    if ((toH <= 0 || toW <= 0) && scale <= 0)
    {
        waifu2x_set_error("invalid scale params");
        return -1;
    }
    int sts = waifu2x_check_init_model(modelIndex);
    if (sts < 0)
    {
        if (sts == Waifu2xError::NotModel)
        {
            waifu2x_set_error("invalid model index");
        }
        return sts;
    }
    if (format) v.save_format = format;
    
    transform(v.save_format.begin(), v.save_format.end(), v.save_format.begin(), ::tolower);
    if (v.save_format.length() == 0 || !v.save_format.compare("jpg") || !v.save_format.compare("jpeg") || !v.save_format.compare("png") || !v.save_format.compare("webp") || !v.save_format.compare("jpg") || !v.save_format.compare("bmp") || !v.save_format.compare("apng"))
    {
        Todecode.put(v);
        return TaskId;
    }

    waifu2x_set_error("invalid pictrue format");
    return -1;
}

int waifu2x_stop()
{
    waifu2x_clear();
    {
        Task end;
        end.id = -233;
        for (int i = 0; i < TotalJobsProc; i++)
        {
            Toproc.put(end);
        }

        Task end2;
        end2.id = -233;
        Tosave.put(end2);
        Toencode.put(end2);
        Todecode.put(end2);
        ncnn::Thread t = ncnn::Thread(waifu2x_to_stop);
    }
    return 0;
}

int waifu2x_clear()
{   
    Toencode.clear();
    Toproc.clear();
    Todecode.clear();
    Tosave.clear();
    return 0;
}

int waifu2x_set_debug(bool isdebug)
{
    IsDebug = isdebug;
    return 0;
}

int waifu2x_set_webp_quality(int webp_quality)
{
    WebpQuality = webp_quality;
    return 0;
}

int waifu2x_set_realcugan_syncgap(int syncgap)
{
    RealcuganSyncgap = syncgap;
    return 0;
}

int waifu2x_remove_wait(std::set<int>& taskIds)
{
    Todecode.remove(taskIds);
    Toproc.remove(taskIds);
    return 0;
}

int waifu2x_remove(std::set<int> &taskIds)
{
    Toencode.remove(taskIds);
    Toproc.remove(taskIds);
    Todecode.remove(taskIds);
    Tosave.remove(taskIds);
    return 0;
}

int waifu2x_printf(void* p, const char* fmt, ...)
{
    if (IsDebug) {
        FILE* f = (FILE*)p;
        va_list vargs;
        int result;
        va_start(vargs, fmt);
        result = vfprintf(f, fmt, vargs);
        va_end(vargs);
        return result;
    }
        return 0;
}

int waifu2x_printf(void* p, const wchar_t* fmt, ...)
{
    if (IsDebug) {
        FILE* f = (FILE*)p;
        va_list vargs;
        int result;
        va_start(vargs, fmt);
        result = vfwprintf(f, fmt, vargs);
        va_end(vargs);
        return result;
    }
    return 0;
}

void waifu2x_set_error(const char* err)
{
    ErrMsg = err;
}

std::string waifu2x_get_error()
{
    return ErrMsg;
}