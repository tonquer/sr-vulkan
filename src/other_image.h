#ifndef OTHER_IMAGE_H
#define OTHER_IMAGE_H

//#if _WIN32
// image decoder and encoder with wic
//#include "wic_image.h"
//#else // _WIN32
// image decoder and encoder with stb
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_TGA
#define STBI_NO_HDR
#define STBI_NO_PNM
#define STBI_NO_PIC
#define STBI_NO_STDIO
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image_resize.h"
#include "waifu2x_main.h"
#include "webp_image.h"
#include "apng_image.h"
#include "png_image.h"
#include "jpeg_image.h"
#include "jxl_image.h"
#include "common_image.h"

static void check_to_resize(Task& v)
{
    std::list<ncnn::Mat*>::iterator it;

    for (it = v.outImage.begin(); it != v.outImage.end(); it++)
    {
        ncnn::Mat* inimage = *it;
        if (!inimage || inimage->empty()) {
            continue;
        }

        if ((v.toW > 0 && v.toH > 0) && (v.toW != (unsigned long)inimage->w || v.toH != (unsigned long)inimage->h))
        {
            ncnn::Mat* new_mat = new ncnn::Mat();
            new_mat->create((int)v.toW, (int)v.toH, (size_t)inimage->elemsize, inimage->elempack);
            if (new_mat->empty())
            {
                delete new_mat;
                continue;
            }
            int alpha_channel = (inimage->elempack == 4) ? 3 : STBIR_ALPHA_CHANNEL_NONE;
            stbir_resize(inimage->data, inimage->w, inimage->h, 0, new_mat->data, (int)v.toW, (int)v.toH, 0, STBIR_TYPE_UINT8, inimage->elempack, alpha_channel, 0,
                STBIR_EDGE_CLAMP, STBIR_EDGE_CLAMP,
                STBIR_FILTER_BOX, STBIR_FILTER_BOX,
                STBIR_COLORSPACE_SRGB, nullptr
            );

            inimage->release();
            delete inimage;
            *it = new_mat;
        }
    }
}

bool bmp_save(Task &v)
{
    if (v.outImage.empty() || !v.outImage.front() || v.outImage.front()->empty())
        return false;

    ncnn::Mat& outimage = *v.outImage.front();
    WriteData data(outimage.h, outimage.w, outimage.elempack);
    if (data.failed)
        return false;

    int ok = stbi_write_bmp_to_func((stbi_write_func*)write_jpg_to_mem, (void*)&data, outimage.w, outimage.h, outimage.elempack, outimage.data);
    if (!ok || data.failed || data.writeSize == 0 || data.writeSize > static_cast<size_t>(INT_MAX))
        return false;

    v.out = data.detach();
    v.outSize = static_cast<int>(data.writeSize);
    return true;
}

bool png_save(Task& v)
{
    if (v.outImage.empty() || !v.outImage.front() || v.outImage.front()->empty())
        return false;
    ncnn::Mat& outimage = *v.outImage.front();
    return spng_save_memery(v, outimage.w, outimage.h, outimage.elempack, (const unsigned char*)outimage.data) != 0;
}

bool jpg_save(Task& v)
{
    if (v.outImage.empty() || !v.outImage.front() || v.outImage.front()->empty())
        return false;
    ncnn::Mat& outimage = *v.outImage.front();
    return jpeg_save_memery(v, outimage.w, outimage.h, outimage.elempack, (const unsigned char*)outimage.data) != 0;
}


bool to_save(Task &v)
{
    if (!v.save_format.compare("bmp"))
    {
        check_to_resize(v);
        return bmp_save(v);
    }
    else if (!v.save_format.compare("png"))
    {
        check_to_resize(v);
        return png_save(v);
    }
    else if (!v.save_format.compare("jpg") || !v.save_format.compare("jpeg"))
    {
        check_to_resize(v);
        return jpg_save(v);
    }
    else if (!v.save_format.compare("webp"))
    {
        check_to_resize(v);
        if (v.outImage.size() <= 1)
            return webp_save(v);
        else
            return webp_save_ani(v);
    }
    else if (!v.save_format.compare("apng"))
    {
        check_to_resize(v);
        return apng_save(v);
    }
    else if (!v.save_format.compare("jxl"))
    {
        check_to_resize(v);
        return jxl_save(v);
    }
    return false;
}

bool stbi_xload(Task &v)
{
    if (!v.fileDate || v.fileSize <= 0)
        return false;

    bool ok = true;
    int x = 0, y = 0, c = 0;
    int frames = 0;
    int req_comp = 0;
    std::string format;
    stbi__context s;
    stbi__start_mem(&s, (unsigned char *)v.fileDate, v.fileSize);
    int* delays = 0;
    void* pixeldata = 0;
    if (stbi__gif_test(&s))
    {
        pixeldata = stbi__load_gif_main(&s, &delays, &x, &y, &frames, &c, 4);
        if (!pixeldata || x <= 0 || y <= 0 || frames <= 0)
        {
            ok = false;
            goto End;
        }
        format = "gif";
        size_t totalSize = static_cast<size_t>(x) * static_cast<size_t>(y) * 4;
        for (int i = 0; i < frames; i++)
        {
            ncnn::Mat* inimage = new ncnn::Mat();
            inimage->create(x, y, (size_t)4, 4);
            if (inimage->empty())
            {
                delete inimage;
                v.clear_in_image();
                v.inFrame.clear();
                ok = false;
                goto End;
            }
            size_t blockOffset = totalSize * static_cast<size_t>(i);
            std::memcpy(inimage->data, static_cast<const unsigned char*>(pixeldata) + blockOffset, totalSize);
            int duration = delays ? delays[i] : 100;
            v.inImage.push_back(inimage);
            v.inFrame.push_back(duration);
        }
    }
    else
    {
        stbi__result_info ri;
        std::memset(&ri, 0, sizeof(ri));
        ri.bits_per_channel = 8;
        ri.channel_order = STBI_ORDER_RGB;
        ri.num_channels = 0;

        if (stbi__jpeg_test(&s)) { pixeldata = stbi__jpeg_load(&s, &x, &y, &c, req_comp, &ri); format = "jpg"; }
        else if (stbi__png_test(&s)) { pixeldata = stbi__png_load(&s, &x, &y, &c, req_comp, &ri); format = "png"; }
        else if (stbi__bmp_test(&s)) { pixeldata = stbi__bmp_load(&s, &x, &y, &c, req_comp, &ri); format = "bmp"; }
        if (!pixeldata || x <= 0 || y <= 0 || c <= 0) { ok = false; goto End; }
        if (ri.bits_per_channel != 8)
        {
            STBI_ASSERT(ri.bits_per_channel == 16);
            pixeldata = stbi__convert_16_to_8((stbi__uint16*)pixeldata, x, y, c);
            if (!pixeldata) { ok = false; goto End; }
            ri.bits_per_channel = 8;
        }

        if (c == 1)
        {
            stbi_image_free(pixeldata);
            int orig_c = 0;
            pixeldata = stbi_load_from_memory((unsigned char*)v.fileDate, v.fileSize, &x, &y, &orig_c, 3);
            c = 3;
        }
        else if (c == 2)
        {
            // grayscale + alpha -> rgba
            stbi_image_free(pixeldata);
            int orig_c = 0;
            pixeldata = stbi_load_from_memory((unsigned char*)v.fileDate, v.fileSize, &x, &y, &orig_c, 4);
            c = 4;
        }

        if (!pixeldata || x <= 0 || y <= 0 || (c != 3 && c != 4)) { ok = false; goto End; }
        ncnn::Mat *inimage = new ncnn::Mat();
        inimage->create(x, y, (size_t)c, c);
        if (inimage->empty())
        {
            delete inimage;
            ok = false;
            goto End;
        }
        std::memcpy(inimage->data, pixeldata, static_cast<size_t>(x) * static_cast<size_t>(y) * static_cast<size_t>(c));
        v.inImage.push_back(inimage);
        v.inFrame.push_back(100);
    }
End:
    if (ok)
    {
        if (v.save_format.length() == 0) v.save_format = format;
        if (!v.save_format.compare("gif")) v.save_format = "webp";
        v.load_format = format;
    }

    if (pixeldata) stbi_image_free(pixeldata);
    if (delays) stbi_image_free(delays);
    return ok;
}
//STBIDEF stbi_uc* stbi_xload(char const* filename, int** delays, int* width, int* height, int* frames, int* nrChannels)
//{
//    unsigned char* result;
//    stbi__context s;
//    result = stbi_load_gif_from_memory(filename, delays, width, height, frames, nrChannels, 0);
//
//    return result;
//}

bool to_load(Task& v)
{

    bool isSuc=true;
    isSuc = load_webp(v);
    if (!isSuc)
    {
        isSuc = load_apng(v);
    }
    if (!isSuc)
    {
        isSuc = load_jpeg(v);
    }
    if (!isSuc)
    {
        isSuc = load_spng(v);
    }
    if (!isSuc)
    {
        isSuc = load_png(v);
    }
    if (!isSuc)
    {
        isSuc = load_jxl(v);
    }
    if(!isSuc)
    {
        isSuc = stbi_xload(v);
        if (!isSuc) 
        {
            const char* err = stbi_failure_reason();
            if (err)
            {
                v.err = err;
            }
        }
    }
    return isSuc;
}
#endif