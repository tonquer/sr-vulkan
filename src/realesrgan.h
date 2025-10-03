// realesrgan implemented with ncnn library

#ifndef REALESRGAN_H
#define REALESRGAN_H

#include "waifu2x.h"
#include <string>

// ncnn
#include "net.h"
#include "gpu.h"
#include "layer.h"

class RealESRGAN : public SuperResolution
{
public:
    RealESRGAN(int gpuid, bool tta_mode = false, int num_threads = 1, const char* name = NULL);
    ~RealESRGAN();

#if _WIN32
    int load(const std::wstring& parampath, const std::wstring& modelpath);
#else
    int load(const std::string& parampath, const std::string& modelpath);
#endif

    int process(const ncnn::Mat& inimage, ncnn::Mat& outimage, int tileSize) const;

private:
    ncnn::Net net;
    ncnn::Pipeline* realesrgan_preproc;
    ncnn::Pipeline* realesrgan_postproc;
    ncnn::Layer* bicubic_2x;
    ncnn::Layer* bicubic_3x;
    ncnn::Layer* bicubic_4x;
};

#endif // REALESRGAN_H
