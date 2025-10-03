#ifndef WAIFU2X_MAIN_H
#define WAIFU2X_MAIN_H
#include <stdio.h>
#include <algorithm>
#include <queue>
#include <vector>
#include <clocale>
#include <wchar.h>
#include <set>
#include "cpu.h"
#include "gpu.h"
#include "platform.h"
#if _WIN32
    #include <comutil.h> 
    #pragma comment(lib, "comsuppw.lib")
#endif
#include "waifu2x.h"
#include "realcugan.h"
#include "realesrgan.h"
#include "realsr.h"
#include <time.h>
#include <sys/timeb.h>
#include <sys/stat.h>

#if _WIN32
typedef wchar_t Waifu2xChar;
#else
typedef char Waifu2xChar;
#endif

enum Waifu2xError {
    NotModel = -20,
    NotSupport = -30
};

class Task
{
public:
    int id = 0;

    void* fileDate;
    int fileSize;
    int allFileSize=0;
    std::string load_format;
    std::string save_format;
    std::string err;
    bool isSuc = true;

    int callBack = 0;
    int modelIndex;
    unsigned long toW;
    unsigned long toH;
    float scale = 2;
    int tileSize = 0;
    int webp_quality = 90;

    struct timeb startTick;
    struct timeb decodeTick;
    struct timeb encodeTick;
    struct timeb procTick;
    struct timeb saveTick;
    double allTick = 0;

    std::list<int> inFrame;
    std::list<ncnn::Mat *> inImage;
    std::list<ncnn::Mat *> outImage;

    void* out = 0;
    int outSize = 0;
    int allOutSize = 0;

    void clear_in_image()
    {
        for (std::list<ncnn::Mat*>::iterator in = this->inImage.begin(); in != this->inImage.end(); in++)
        {
            delete *in;
        }
        this->inImage.clear();
    }
    void clear_out_image()
    {
        for (std::list<ncnn::Mat*>::iterator in = this->outImage.begin(); in != this->outImage.end(); in++)
        {
           delete *in;
        }
        this->outImage.clear();
    }
};

class TaskQueue
{
public:
    TaskQueue()
    {
    }

    void put(const Task& v)
    {
        lock.lock();

        tasks.push(v);

        lock.unlock();

        condition.signal();
    }

    void get(Task& v, int timeout = 0)
    {
        lock.lock();
        if (timeout == 0)
        {
            while (tasks.size() == 0)
            {
                condition.wait(lock);
            }
        }
        else {
            if (tasks.size() == 0)
            {
                lock.unlock();
                condition.signal();
                return;
            }
        }

        v = tasks.front();
        tasks.pop();

        lock.unlock();

        condition.signal();
    }

    void clear()
    {
        lock.lock();

        while (!tasks.empty())
        {

            Task v = tasks.front();
            tasks.pop();
            if (v.fileDate) free(v.fileDate);
            v.fileDate = NULL;
            if (v.out) free(v.out);
            v.out = NULL;
            v.clear_in_image();
            v.clear_out_image();
        }

        lock.unlock();

        condition.signal();
    }

    void remove(std::set<int>& taskIds)
    {
        lock.lock();
        std::set<int>::iterator ite1 = taskIds.begin();
        std::set<int>::iterator ite2 = taskIds.end();
        std::queue<Task> newData;
        while (!tasks.empty())
        {
            Task v = tasks.front();
            tasks.pop();
            ite1 = taskIds.find(v.callBack);
            if (ite1 == ite2)
            {
                newData.push(v);
            }
            else
            {
                if (v.fileDate) free(v.fileDate);
                v.fileDate = NULL;
                if (v.out) free(v.out);
                v.out = NULL;
                v.clear_in_image();
                v.clear_out_image();
            }
        }
        tasks = newData;
        lock.unlock();
        condition.signal();
    }
private:
    ncnn::Mutex lock;
    ncnn::ConditionVariable condition;
    std::queue<Task> tasks;
};

int waifu2x_addData(const unsigned char* data, unsigned int size, int callBack, int modelIndex, const char* format, unsigned long toW, unsigned long toH, float scale, int);
int waifu2x_getData(void*& out, unsigned long& outSize, double& tick, int& callBack, char *, unsigned int timeout);
int waifu2x_init();
int waifu2x_init_set(int gpuId2, int threadNum);
int waifu2x_init_path(const Waifu2xChar*);
int waifu2x_get_path_size();
int waifu2x_stop();
int waifu2x_clear();
int waifu2x_set_debug(bool);
int waifu2x_set_webp_quality(int);
int waifu2x_set_realcugan_syncgap(int);
int waifu2x_printf(void* p, const char* fmt, ...);
int waifu2x_printf(void* p, const wchar_t* fmt, ...);
int waifu2x_remove_wait(std::set<int>&);
int waifu2x_remove(std::set<int>&);
void waifu2x_set_error(const char* err);
std::string waifu2x_get_error();

static std::string ErrMsg;
static std::vector<ncnn::Thread*> ProcThreads;
static std::vector<ncnn::Thread*> OtherThreads;
static std::vector<Waifu2x*> Waifu2xList;
static std::vector<RealCUGAN*> RealCuganList;
static std::vector<RealSR*> RealSrList;
static std::vector<RealESRGAN*> RealEsrganList;

class WriteData
{
public:
    int size = 0;
    int writeSize = 0;
    void *data = NULL;
    WriteData(int h, int w, int n)
    {

        size = h*w*n;
        data = malloc(size);
    }
    ~WriteData()
    {
        free(data);
        data = NULL;
    }


};
static void write_jpg_to_mem(void *writeData, void *data, int size)
{
    WriteData *write = static_cast<WriteData *>(writeData);
    memcpy((unsigned char *)write->data + write->writeSize, data, size);
    write->writeSize += size;
}


static const std::string AllModel[] =
{
    "WAIFU2X_CUNET_UP1X_DENOISE0X",
    "WAIFU2X_CUNET_UP1X_DENOISE1X",
    "WAIFU2X_CUNET_UP1X_DENOISE2X",
    "WAIFU2X_CUNET_UP1X_DENOISE3X",
    "WAIFU2X_CUNET_UP2X",
    "WAIFU2X_CUNET_UP2X_DENOISE0X",
    "WAIFU2X_CUNET_UP2X_DENOISE1X",
    "WAIFU2X_CUNET_UP2X_DENOISE2X",
    "WAIFU2X_CUNET_UP2X_DENOISE3X",

    "WAIFU2X_ANIME_UP2X",
    "WAIFU2X_ANIME_UP2X_DENOISE0X",
    "WAIFU2X_ANIME_UP2X_DENOISE1X",
    "WAIFU2X_ANIME_UP2X_DENOISE2X",
    "WAIFU2X_ANIME_UP2X_DENOISE3X",

    "WAIFU2X_PHOTO_UP2X",
    "WAIFU2X_PHOTO_UP2X_DENOISE0X",
    "WAIFU2X_PHOTO_UP2X_DENOISE1X",
    "WAIFU2X_PHOTO_UP2X_DENOISE2X",
    "WAIFU2X_PHOTO_UP2X_DENOISE3X",

    "REALCUGAN_PRO_UP2X",
    "REALCUGAN_PRO_UP2X_CONSERVATIVE",
    "REALCUGAN_PRO_UP2X_DENOISE3X",

    "REALCUGAN_PRO_UP3X",
    "REALCUGAN_PRO_UP3X_CONSERVATIVE",
    "REALCUGAN_PRO_UP3X_DENOISE3X",

    "REALCUGAN_SE_UP2X",
    "REALCUGAN_SE_UP2X_CONSERVATIVE",
    "REALCUGAN_SE_UP2X_DENOISE1X",
    "REALCUGAN_SE_UP2X_DENOISE2X",
    "REALCUGAN_SE_UP2X_DENOISE3X",

    "REALCUGAN_SE_UP3X",
    "REALCUGAN_SE_UP3X_CONSERVATIVE",
    "REALCUGAN_SE_UP3X_DENOISE3X",

    "REALCUGAN_SE_UP4X",
    "REALCUGAN_SE_UP4X_CONSERVATIVE",
    "REALCUGAN_SE_UP4X_DENOISE3X",

    "REALSR_DF2K_UP4X",

    "REALESRGAN_ANIMAVIDEOV3_UP2X",
    "REALESRGAN_ANIMAVIDEOV3_UP3X",
    "REALESRGAN_ANIMAVIDEOV3_UP4X",
    "REALESRGAN_X4PLUS_UP4X",
    "REALESRGAN_X4PLUSANIME_UP4X"
};
#endif // WAIFU2X_MAIN_H