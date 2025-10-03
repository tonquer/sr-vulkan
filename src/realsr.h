// realsr implemented with ncnn library

#ifndef REALSR_H
#define REALSR_H

#include <string>

#include "waifu2x.h"
// ncnn
#include "net.h"
#include "gpu.h"
#include "layer.h"

class RealSR : public SuperResolution
{
public:
    RealSR(int gpuid, bool tta_mode = false, int num_threads = 1, const char* name = NULL);
    ~RealSR();

#if _WIN32
    int load(const std::wstring& parampath, const std::wstring& modelpath);
#else
    int load(const std::string& parampath, const std::string& modelpath);
#endif

    int process(const ncnn::Mat& inimage, ncnn::Mat& outimage, int tileSize) const;

    int process_cpu(const ncnn::Mat& inimage, ncnn::Mat& outimage, int tileSize) const;


private:
    ncnn::VulkanDevice* vkdev;
    ncnn::Net net;
    ncnn::Pipeline* realsr_preproc;
    ncnn::Pipeline* realsr_postproc;
    ncnn::Layer* bicubic_4x;
};

#endif // REALSR_H
