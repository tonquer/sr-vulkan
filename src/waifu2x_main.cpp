#include "waifu2x_main.h"
#include "other_image.h"

//#endif // _WIN32

#include <cmath>
#include <atomic>
#include <csignal>

static TaskQueue Todecode;
static TaskQueue Toproc;
static TaskQueue Toencode;
static TaskQueue Tosave;


static int GpuId;
static int TotalJobsProc = 0;
static int TotalJobsDecode = 0;
static int TotalJobsEncode = 0;
static int NumThreads = 1;          // cpu threads
static std::atomic<int> AutoTaskId(1);

static int WebpQuality = 100;
static int RealcuganSyncgap = 1;

static bool IsDebug = false;

Waifu2xChar GlobalModelPath[1024] = {0};
Waifu2xChar DefaultModelPath[1024] = {0};



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
    v.clear_in_image();
    v.clear_out_image();

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
        if (IsExitFlag)
            break;
        int modelLen = sizeof(AllModel) / sizeof(AllModel[0]) * 2;

        if (v.modelIndex >= (modelLen))
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
            if (v.modelIndex < 0 && !v.inImage.empty()) {
                ftime(&v.procTick);
                if (v.toH <= 0 || v.toW <= 0)
                {
                    v.toW = ceil(v.scale * v.inImage.front()->w);
                    v.toH = ceil(v.scale * v.inImage.front()->h);
                }
                v.outImage = v.inImage;
                v.inImage.clear();
                Toencode.put(v);
            }
            else {
                Toproc.put(v);
            }
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
        if (IsExitFlag)
            break;
        bool isSuc = to_save(v);
        v.clear_out_image();

        ftime(&v.encodeTick);
        double decodeTick = (v.decodeTick.time + v.decodeTick.millitm / 1000.0) - (v.startTick.time + v.startTick.millitm / 1000.0);
        double procTick = (v.procTick.time + v.procTick.millitm / 1000.0) - (v.decodeTick.time + v.decodeTick.millitm / 1000.0);
        double encodeTick = (v.encodeTick.time + v.encodeTick.millitm / 1000.0) - (v.procTick.time + v.procTick.millitm / 1000.0);
        double allTick = (v.encodeTick.time + v.encodeTick.millitm / 1000.0) - (v.startTick.time + v.startTick.millitm / 10000.0);
        v.allTick = allTick;
        waifu2x_printf(stdout, "[SR_VULKAN] end encode imageId :%d, decode:%.2fs, proc:%.2fs, encode:%.2fs, \n",
            v.callBack, decodeTick, procTick, encodeTick);

        if (isSuc)
        {
            Tosave.put(v);
        }
        else
        {
            const char* err = stbi_failure_reason();
            if (err && v.err.empty())
            {
                v.err = err;
            }
            v.isSuc = false; 
            Tosave.put(v);
        }

    }
    return NULL;
}
void* waifu2x_process_data(Task &v, const SuperResolution* waifu2x, LoadThreadParams* ltp)
{
    const char* name;
    if (GpuId >= 0)
        name = ncnn::get_gpu_info(GpuId).device_name();
    else
        name = "cpu";

    if (v.toH <= 0 || v.toW <= 0)
    {
        waifu2x_printf(stdout, "[SR_VULKAN] start encode thread: %d, imageId :%d, gpu:%s, format:%s, model:%s, noise:%d, scale:%d, to_scale:%.2f, tta:%d, tileSize:%d, to_tile:%d\n",
            ltp->thread_id, v.callBack, name, v.save_format.c_str(), waifu2x->mode_name.c_str(), waifu2x->noise, waifu2x->scale, v.scale, waifu2x->tta_mode, waifu2x->tilesize, v.tileSize);
    }
    else {
        waifu2x_printf(stdout, "[SR_VULKAN] start encode thread: %d, imageId :%d, gpu:%s, format:%s, model:%s, noise:%d, scale:%d, to_size:%d_%d, tta:%d, tileSize:%d, to_tile:%d\n",
            ltp->thread_id, v.callBack, name, v.save_format.c_str(), waifu2x->mode_name.c_str(), waifu2x->noise, waifu2x->scale, v.toW, v.toH, waifu2x->tta_mode, waifu2x->tilesize, v.tileSize);
    }
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
                waifu2x_printf(stdout, "[SR_VULKAN] start encode thread:%d, imageId :%d, count:%d, frame:%d, h:%d->%d, w:%d->%d \n",
                    ltp->thread_id, v.callBack, i + 1, frame, inimage.h, outimage->h, inimage.w, outimage->w);
                waifu2x->process(inimage, *outimage, v.tileSize);
                inimage.release();
            }
            else
            {
                ncnn::Mat tmpimage(inimage.w * waifu2x->scale, inimage.h * waifu2x->scale, (size_t)inimage.elemsize, (int)inimage.elemsize);
                waifu2x_printf(stdout, "[SR_VULKAN] start encode thread:%d, imageId :%d, count:%d, frame:%d, h:%d->%d, w:%d->%d \n",
                    ltp->thread_id, v.callBack, i + 1, frame, inimage.h, tmpimage.h, inimage.w, tmpimage.w);
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
    LoadThreadParams* ltp = (LoadThreadParams*)args;
    init_waifu2x_model_size(ltp);
    const SuperResolution* waifu2x;
    for (;;)
    {
        Task v;
        Toproc.get(v);
        if (v.id == -233)
            break;
        if (IsExitFlag)
            break;
        int sts = waifu2x_check_init_model(v.modelIndex, ltp);
        if (sts < 0)
        {
            waifu2x_set_error("load model fail");
            if (sts == Waifu2xError::NotModel)
            {
                waifu2x_set_error("invalid model index");
            }
            v.err = waifu2x_get_error();
            v.isSuc = false;
            Tosave.put(v);
            continue;
        }

        if (v.modelIndex >= ltp->waifu2xList.size() + ltp->realCuganList.size() + ltp->realSrList.size())
        {
            waifu2x = ltp->realEsrganList[v.modelIndex - ltp->waifu2xList.size() - ltp->realCuganList.size() - ltp->realSrList.size()];
            waifu2x_process_data(v, waifu2x, ltp);
        }
        else if (v.modelIndex >= ltp->waifu2xList.size() + ltp->realCuganList.size())
        {
            waifu2x = ltp->realSrList[v.modelIndex - ltp->waifu2xList.size() - ltp->realCuganList.size()];
            waifu2x_process_data(v, waifu2x, ltp);
        }
        else if (v.modelIndex >= ltp->waifu2xList.size())
        {
            waifu2x = ltp->realCuganList[v.modelIndex - ltp->waifu2xList.size()];
            waifu2x_process_data(v, waifu2x, ltp);
        }
        else
        {
            waifu2x = ltp->waifu2xList[v.modelIndex];
            waifu2x_process_data(v, waifu2x, ltp);
        }
    }
    delete ltp;
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

int waifu2x_addModel(const Waifu2xChar* name, int scale2, int noise2, int tta_mode, int num_threads, int index, LoadThreadParams* ltp)
{

    Waifu2xChar parampath[1024];
    Waifu2xChar modelpath[1024];
    if (waifu2x_get_path_size() == 0)
    {
#if _WIN32
        // Waifu2xList.push_back(NULL);
        swprintf(parampath, L"%s/../sr_vulkan_model_waifu2x/models/%s.param", DefaultModelPath, name);
        swprintf(modelpath, L"%s/../sr_vulkan_model_waifu2x/models/%s.bin", DefaultModelPath, name);
#else
        // Waifu2xList.push_back(NULL);
        sprintf(parampath, "%s/../sr_vulkan_model_waifu2x/models/%s.param", DefaultModelPath, name);
        sprintf(modelpath, "%s/../sr_vulkan_model_waifu2x/models/%s.bin", DefaultModelPath, name);
#endif
    } else {
#if _WIN32
        // Waifu2xList.push_back(NULL);
        swprintf(parampath, L"%s/waifu2x/%s.param", GlobalModelPath, name);
        swprintf(modelpath, L"%s/waifu2x/%s.bin", GlobalModelPath, name);
#else
        // Waifu2xList.push_back(NULL);
        sprintf(parampath, "%s/waifu2x/%s.param", GlobalModelPath, name);
        sprintf(modelpath, "%s/waifu2x/%s.bin", GlobalModelPath, name);
#endif
    }


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
        waifu2x_printf(stderr, L"[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, parampath);
        waifu2x_printf(stderr, L"[SR_VULKAN] please use pip install sr_vulkan_model_waifu2x\n");
        return Waifu2xError::NotModel;
    }
    if (_wstat((wchar_t *)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, modelpath);
        waifu2x_printf(stderr, L"[SR_VULKAN] please use pip install sr_vulkan_model_waifu2x\n");
        return Waifu2xError::NotModel;
    }
#else

    struct stat buffer;
    if (stat((char *)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, parampath);
        waifu2x_printf(stderr, "[SR_VULKAN] please use pip install sr_vulkan_model_waifu2x\n");
        return Waifu2xError::NotModel;
    }
    if (stat((char *)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_VULKAN] thread:%d, ot found path %s\n", ltp->thread_id, modelpath);
        waifu2x_printf(stderr, "[SR_VULKAN] please use pip install sr_vulkan_model_waifu2x\n");
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
    ltp->waifu2xList[index] = waifu;
    return 1;
}

int realcugan_addModel(const Waifu2xChar* name, int scale, int noise, int tta_mode, int num_threads, int index, LoadThreadParams* ltp)
{
    Waifu2xChar parampath[1024];
    Waifu2xChar modelpath[1024];
    if (waifu2x_get_path_size() == 0)
    {
#if _WIN32
        // Waifu2xList.push_back(NULL);
        swprintf(parampath, L"%s/../sr_vulkan_model_realcugan/models/%s.param", DefaultModelPath, name);
        swprintf(modelpath, L"%s/../sr_vulkan_model_realcugan/models/%s.bin", DefaultModelPath, name);
#else
        // Waifu2xList.push_back(NULL);
        sprintf(parampath, "%s/../sr_vulkan_model_realcugan/models/%s.param", DefaultModelPath, name);
        sprintf(modelpath, "%s/../sr_vulkan_model_realcugan/models/%s.bin", DefaultModelPath, name);
#endif
    } else {
#if _WIN32
        // Waifu2xList.push_back(NULL);
        swprintf(parampath, L"%s/realcugan/%s.param", GlobalModelPath, name);
        swprintf(modelpath, L"%s/realcugan/%s.bin", GlobalModelPath, name);
#else
        // Waifu2xList.push_back(NULL);
        sprintf(parampath, "%s/realcugan/%s.param", GlobalModelPath, name);
        sprintf(modelpath, "%s/realcugan/%s.bin", GlobalModelPath, name);
#endif
    }
    int prepadding = 18;
    int tilesize = 400;
    uint32_t heap_budget;
    if (GpuId == -1) heap_budget = 4000;
    else heap_budget = ncnn::get_gpu_device(GpuId)->get_heap_budget();

#if _WIN32

    struct _stat buffer;
    if (_wstat((wchar_t*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, parampath);
        waifu2x_printf(stderr, L"[SR_VULKAN] please use pip install sr_vulkan_model_realcugan\n");
        return Waifu2xError::NotModel;
    }
    if (_wstat((wchar_t*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, modelpath);
        waifu2x_printf(stderr, L"[SR_VULKAN] please use pip install sr_vulkan_model_realcugan\n");
        return Waifu2xError::NotModel;
    }
#else

    struct stat buffer;
    if (stat((char*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, parampath);
        waifu2x_printf(stderr, "[SR_VULKAN] please use pip install sr_vulkan_model_realcugan\n");
        return Waifu2xError::NotModel;
    }
    if (stat((char*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, modelpath);
        waifu2x_printf(stderr, "[SR_VULKAN] please use pip install sr_vulkan_model_realcugan\n");
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
    ltp->realCuganList[index] = waifu;
    return 1;
}

int realsr_addModel(const Waifu2xChar* name, int scale, int noise, int tta_mode, int num_threads, int index, LoadThreadParams* ltp)
{

    Waifu2xChar parampath[1024];
    Waifu2xChar modelpath[1024];
   if (waifu2x_get_path_size() == 0)
    {
#if _WIN32
        // Waifu2xList.push_back(NULL);
        swprintf(parampath, L"%s/../sr_vulkan_model_realsr/models/%s.param", DefaultModelPath, name);
        swprintf(modelpath, L"%s/../sr_vulkan_model_realsr/models/%s.bin", DefaultModelPath, name);
#else
        // Waifu2xList.push_back(NULL);
        sprintf(parampath, "%s/../sr_vulkan_model_realsr/models/%s.param", DefaultModelPath, name);
        sprintf(modelpath, "%s/../sr_vulkan_model_realsr/models/%s.bin", DefaultModelPath, name);
#endif
    } else {
#if _WIN32
        // Waifu2xList.push_back(NULL);
        swprintf(parampath, L"%s/realsr/%s.param", GlobalModelPath, name);
        swprintf(modelpath, L"%s/realsr/%s.bin", GlobalModelPath, name);
#else
        // Waifu2xList.push_back(NULL);
        sprintf(parampath, "%s/realsr/%s.param", GlobalModelPath, name);
        sprintf(modelpath, "%s/realsr/%s.bin", GlobalModelPath, name);
#endif
    }

    int prepadding = 10;
    int tilesize = 200;
    uint32_t heap_budget;
    if (GpuId == -1) heap_budget = 4000;
    else heap_budget = ncnn::get_gpu_device(GpuId)->get_heap_budget();

#if _WIN32

    struct _stat buffer;
    if (_wstat((wchar_t*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, parampath);
        waifu2x_printf(stderr, L"[SR_VULKAN] please use pip install sr_vulkan_model_realsr\n");
        return Waifu2xError::NotModel;
    }
    if (_wstat((wchar_t*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, modelpath);
        waifu2x_printf(stderr, L"[SR_VULKAN] please use pip install sr_vulkan_model_realsr\n");
        return Waifu2xError::NotModel;
    }
#else

    struct stat buffer;
    if (stat((char*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, parampath);
        waifu2x_printf(stderr, "[SR_VULKAN] please use pip install sr_vulkan_model_realsr\n");
        return Waifu2xError::NotModel;
    }
    if (stat((char*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, modelpath);
        waifu2x_printf(stderr, "[SR_VULKAN] please use pip install sr_vulkan_model_realsr\n");
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
    ltp->realSrList[index] = waifu;
    return 1;
}

int realesrgan_addModel(const Waifu2xChar* name, int scale, int noise, int tta_mode, int num_threads, int index, LoadThreadParams* ltp)
{

    Waifu2xChar parampath[1024];
    Waifu2xChar modelpath[1024];
   if (waifu2x_get_path_size() == 0)
    {
#if _WIN32
        // Waifu2xList.push_back(NULL);
        swprintf(parampath, L"%s/../sr_vulkan_model_realesrgan/models/%s.param", DefaultModelPath, name);
        swprintf(modelpath, L"%s/../sr_vulkan_model_realesrgan/models/%s.bin", DefaultModelPath, name);
#else
        // Waifu2xList.push_back(NULL);
        sprintf(parampath, "%s/../sr_vulkan_model_realesrgan/models/%s.param", DefaultModelPath, name);
        sprintf(modelpath, "%s/../sr_vulkan_model_realesrgan/models/%s.bin", DefaultModelPath, name);
#endif
    } else {
#if _WIN32
        // Waifu2xList.push_back(NULL);
        swprintf(parampath, L"%s/realesrgan/%s.param", GlobalModelPath, name);
        swprintf(modelpath, L"%s/realesrgan/%s.bin", GlobalModelPath, name);
#else
        // Waifu2xList.push_back(NULL);
        sprintf(parampath, "%s/realesrgan/%s.param", GlobalModelPath, name);
        sprintf(modelpath, "%s/realesrgan/%s.bin", GlobalModelPath, name);
#endif
    }

    int prepadding = 10;
    int tilesize = 200;
    uint32_t heap_budget;
    if (GpuId == -1) heap_budget = 4000;
    else heap_budget = ncnn::get_gpu_device(GpuId)->get_heap_budget();

#if _WIN32

    struct _stat buffer;
    if (_wstat((wchar_t*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, parampath);
        waifu2x_printf(stderr, L"[SR_VULKAN] please use pip install sr_vulkan_model_realesrgan\n");
        return Waifu2xError::NotModel;
    }
    if (_wstat((wchar_t*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, L"[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, modelpath);
        waifu2x_printf(stderr, L"[SR_VULKAN] please use pip install sr_vulkan_model_realesrgan\n");
        return Waifu2xError::NotModel;
    }
#else

    struct stat buffer;
    if (stat((char*)parampath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, parampath);
        waifu2x_printf(stderr, "[SR_VULKAN] please use pip install sr_vulkan_model_realesrgan\n");
        return Waifu2xError::NotModel;
    }
    if (stat((char*)modelpath, &buffer) != 0)
    {
        waifu2x_printf(stderr, "[SR_VULKAN] thread:%d, not found path %s\n", ltp->thread_id, modelpath);
        waifu2x_printf(stderr, "[SR_VULKAN] please use pip install sr_vulkan_model_realesrgan\n");
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
    ltp->realEsrganList[index] = waifu;
    return 1;
}

int waifu2x_init()
{
#if _WIN32
    CoInitializeEx(NULL, COINIT_MULTITHREADED);
#endif
    IsExitFlag = false;
    return ncnn::create_gpu_instance();
}

int waifu2x_get_path_size()
{
#if _WIN32
    return wcslen(GlobalModelPath);
#else
    return strlen(GlobalModelPath);
#endif
}

int waifu2x_init_default_path(const Waifu2xChar* modelPath2)
{
#if _WIN32
    if (modelPath2)
    {
        memset(DefaultModelPath, 0, 1024);
        wcscpy(DefaultModelPath, modelPath2);
    };
#else
    if (modelPath2)
    {
        memset(DefaultModelPath, 0, 1024);
        strcpy(DefaultModelPath, modelPath2);
    };
#endif
    return 0;
}

int waifu2x_init_path(const Waifu2xChar* modelPath2)
{
#if _WIN32
    if (modelPath2)
    {
        memset(GlobalModelPath, 0, 1024);
        wcscpy(GlobalModelPath, modelPath2);
    };
#else
    if (modelPath2)
    {
        memset(GlobalModelPath, 0, 1024);
        strcpy(GlobalModelPath, modelPath2);
    };
#endif
    return 0;
}

int waifu2x_set_thread_num(int decodeThreadNum, int procThreadNum, int encodeThreadNum) {
    if (decodeThreadNum < 0 || decodeThreadNum > 128 || procThreadNum < 0 || procThreadNum > 128 || encodeThreadNum < 0 || encodeThreadNum > 128) 
    { 
        waifu2x_set_error("invalid thread num params");
        return -1; 
    };
    TotalJobsProc = procThreadNum;
    TotalJobsDecode = decodeThreadNum;
    TotalJobsEncode = encodeThreadNum;
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
    
    std::signal(SIGINT, signal_handler);

    if (TotalJobsDecode <= 0) { TotalJobsDecode = 1; }
    if (TotalJobsEncode <= 0) { TotalJobsEncode = 1; }

    if (gpuId2 == -1)
    {
        jobs_proc = std::min(jobs_proc, cpu_count);
        if (jobs_proc <= 0) { jobs_proc = std::max(1, cpu_count / 2); }
        NumThreads = jobs_proc;
        if (TotalJobsProc <= 0) { TotalJobsProc = 1; }
    }
    else
    {
        int gpu_queue_count = ncnn::get_gpu_info(gpuId2).compute_queue_count();
        if (TotalJobsProc <= 0) { TotalJobsProc = 1; }
        NumThreads = 1;
    }
    int modelLen = sizeof(AllModel) / sizeof(AllModel[0]);
    waifu2x_printf(stdout, "[SR_VULKAN] init thread num, decode:%d, proc:%d, encode:%d, model num:%d\n", TotalJobsDecode, TotalJobsProc, TotalJobsEncode, modelLen);

    // waifu2x proc
    ProcThreads.resize(TotalJobsProc);
    {

        for (int j = 0; j < TotalJobsDecode; j++)
        {
            ncnn::Thread* save_thread = new ncnn::Thread(waifu2x_decode);
            OtherThreads.push_back(save_thread);
        }
        for (int j = 0; j < TotalJobsEncode; j++)
        {
            ncnn::Thread *load_thread = new ncnn::Thread(waifu2x_encode);
            OtherThreads.push_back(load_thread);
        }

        int total_jobs_proc_id = 0;
        for (int j = 0; j < TotalJobsProc; j++)
        {
            LoadThreadParams* ltp = new LoadThreadParams();
            ltp->thread_id = j + 1;
            ProcThreads[total_jobs_proc_id++] = new ncnn::Thread(waifu2x_proc, (void*)ltp);
        }
    }
    GpuId = gpuId2;
    //waifu2x_printf(stdout, "init success, threadNum:%d\n", TotalJobsProc);
    return 0;
}

void init_waifu2x_model_size(LoadThreadParams* ltp)
{

    int len = sizeof(AllModel) / sizeof(AllModel[0]);
    for (int j = 0; j < len; j++)
    {
#if _WIN32
        std::string oldName = AllModel[j];
        wchar_t* oldName2 = _bstr_t(oldName.c_str());
        std::wstring name = oldName2;

        if (name.find(L"WAIFU2X") != std::string::npos) {
            ltp->waifu2xList.push_back(NULL);
            ltp->waifu2xList.push_back(NULL);
        }
        else if (name.find(L"REALCUGAN") != std::string::npos) {
            ltp->realCuganList.push_back(NULL);
            ltp->realCuganList.push_back(NULL);
        }
        else if (name.find(L"REALSR") != std::string::npos) {
            ltp->realSrList.push_back(NULL);
            ltp->realSrList.push_back(NULL);
        }
        else if (name.find(L"REALESRGAN") != std::string::npos) {
            ltp->realEsrganList.push_back(NULL);
            ltp->realEsrganList.push_back(NULL);
        }
#else
        std::string name = AllModel[j];
        if (name.find("WAIFU2X") != std::string::npos) {
            ltp->waifu2xList.push_back(NULL);
            ltp->waifu2xList.push_back(NULL);
        }
        else if (name.find("REALCUGAN") != std::string::npos) {
            ltp->realCuganList.push_back(NULL);
            ltp->realCuganList.push_back(NULL);
        }
        else if (name.find("REALSR") != std::string::npos) {
            ltp->realSrList.push_back(NULL);
            ltp->realSrList.push_back(NULL);
        }
        else if (name.find("REALESRGAN") != std::string::npos) {
            ltp->realEsrganList.push_back(NULL);
            ltp->realEsrganList.push_back(NULL);
        }
#endif
    }
}

int waifu2x_check_init_model(int initModel, LoadThreadParams* ltp)
{   
    std::lock_guard<std::mutex> lock(g_load_model);
    if (initModel < 0 || initModel >= (int)ltp->get_max_size())
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
    if (initModel >= ((int)ltp->waifu2xList.size() + (int)ltp->realCuganList.size()) + (int)ltp->realSrList.size())
    {
        initModel = initModel - (int)ltp->waifu2xList.size() - (int)ltp->realCuganList.size() - (int)ltp->realSrList.size();
        if (ltp->realEsrganList[initModel])
        {
            return 1;
        }
#if _WIN32
        waifu2x_printf(stdout, "[SR_VULKAN] thread:%d, load model %s, index:%d\n", ltp->thread_id, oldName.c_str(), modelIndex);
#else
        waifu2x_printf(stdout, "[SR_VULKAN] thread:%d, load model %s, index:%d\n", ltp->thread_id, name.c_str(), modelIndex);
#endif
        if (GpuId == -1)
        {
#if _WIN32
            waifu2x_printf(stderr, L"[SR_VULKAN] RealESRGAN not support cpu model \n");
#else
            waifu2x_printf(stderr, "[SR_VULKAN] RealESRGAN not support cpu model \n");
#endif
            waifu2x_set_error("[SR_VULKAN] RealESRGAN not support cpu model");
            return Waifu2xError::NotSupport;
        }
        return realesrgan_addModel(name.c_str(), scale, noise, tta, NumThreads, initModel, ltp);
    }
    else if (initModel >= ((int)ltp->waifu2xList.size() + (int)ltp->realCuganList.size()))
    {
        initModel = initModel - (int)ltp->waifu2xList.size() - (int)ltp->realCuganList.size();
        if (ltp->realSrList[initModel])
        {
            return 1;
        }
#if _WIN32
        waifu2x_printf(stdout, "[SR_VULKAN] thread:%d, load model %s, index:%d\n", ltp->thread_id, oldName.c_str(), modelIndex);
#else
        waifu2x_printf(stdout, "[SR_VULKAN] thread:%d, load model %s, index:%d\n", ltp->thread_id, name.c_str(), modelIndex);
#endif
        return realsr_addModel(name.c_str(), scale, noise, tta, NumThreads, initModel, ltp);
    }
    else if (initModel >= ((int)ltp->waifu2xList.size()))
    {
        initModel = initModel - (int)ltp->waifu2xList.size();
        if (ltp->realCuganList[initModel])
        {
            return 1;
        }
#if _WIN32
        waifu2x_printf(stdout, "[SR_VULKAN] thread:%d, load model %s, index:%d\n", ltp->thread_id, oldName.c_str(), modelIndex);
#else
        waifu2x_printf(stdout, "[SR_VULKAN] thread:%d, load model %s, index:%d\n", ltp->thread_id, name.c_str(), modelIndex);
#endif
        return realcugan_addModel(name.c_str(), scale, noise, tta, NumThreads, initModel, ltp);
    }
    else {
        if (ltp->waifu2xList[initModel])
        {
            return 1;
        }
#if _WIN32
        waifu2x_printf(stdout, "[SR_VULKAN] thread:%d, load model %s, index:%d\n", ltp->thread_id, oldName.c_str(), modelIndex);
#else
        waifu2x_printf(stdout, "[SR_VULKAN] thread:%d, load model %s, index:%d\n", ltp->thread_id, name.c_str(), modelIndex);
#endif
        return waifu2x_addModel(name.c_str(), scale, noise, tta, NumThreads, initModel, ltp);
    }


    return 1;
}


int waifu2x_addData(const unsigned char* data, unsigned int size, int callBack, int modelIndex, const char* format, unsigned long toW, unsigned long toH, float scale, int tileSize)
{
    Task v;
    AutoTaskId ++;
    v.id = AutoTaskId;
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

    if (format) v.save_format = format;
    
    transform(v.save_format.begin(), v.save_format.end(), v.save_format.begin(), ::tolower);
    if (v.save_format.length() == 0 || !v.save_format.compare("jpg") || !v.save_format.compare("jpeg") || 
    !v.save_format.compare("png") || !v.save_format.compare("webp") || !v.save_format.compare("jpg") || 
    !v.save_format.compare("bmp") || !v.save_format.compare("apng") || !v.save_format.compare("jxl"))
    {
        Todecode.put(v);
        return v.id;
    }

    waifu2x_set_error("invalid pictrue format");
    return -1;
}

int waifu2x_stop()
{
    waifu2x_clear();
    {
        IsExitFlag = true;
        Task end;
        end.id = -233;
        for (int i = 0; i < TotalJobsProc; i++)
        {
            Toproc.put(end);
        }

        Toproc.broadcast();
        Tosave.broadcast();
        Toencode.broadcast();
        Todecode.broadcast();
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
    std::lock_guard<std::mutex> lock(g_mutex);
    ErrMsg = err;
}

std::string waifu2x_get_error()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    return ErrMsg;
}

void signal_handler(int signum) {
    /*if (signum == SIGINT) {
        IsExitFlag = true;
        Toencode.broadcast();
        Toproc.broadcast();
        Todecode.broadcast();
        Tosave.broadcast();
    }*/
}