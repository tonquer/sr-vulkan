#ifndef WEBP_IMAGE_H
#define WEBP_IMAGE_H

// webp image decoder and encoder with libwebp
#include <stdio.h>
#include <stdlib.h>
#include <cstring>
#include <limits.h>
#include "webp/decode.h"
#include "webp/encode.h"
#include "webp/demux.h"
#include "webp/mux.h"
#include "common_image.h"

static inline bool is_webp_data(const void* data, int size)
{
    if (!data || size < 12)
        return false;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    return std::memcmp(bytes, "RIFF", 4) == 0 && std::memcmp(bytes + 8, "WEBP", 4) == 0;
}

static inline bool webp_init_config(WebPConfig& config, int webp_quality)
{
    float quality = static_cast<float>(webp_quality < 0 ? 0 : (webp_quality > 100 ? 100 : webp_quality));
    if (!WebPConfigPreset(&config, WEBP_PRESET_DEFAULT, quality))
        return false;
    config.lossless = 0;
    return WebPValidateConfig(&config) != 0;
}

static bool load_webp_ani_impl(Task& v, bool has_alpha)
{
    bool ok = false;
    uint32_t frame_index = 0;
    int prev_frame_timestamp = 0;
    WebPAnimDecoder* dec = NULL;
    WebPAnimInfo anim_info;
    WebPAnimDecoderOptions opt;
    const int out_channels = has_alpha ? 4 : 3;

    if (!WebPAnimDecoderOptionsInit(&opt))
        return false;

    const WebPData webp_data = { static_cast<const uint8_t*>(v.fileDate), static_cast<size_t>(v.fileSize) };
    opt.color_mode = MODE_RGBA;
    opt.use_threads = 0;
    dec = WebPAnimDecoderNew(&webp_data, &opt);
    if (dec == NULL)
        goto End;

    if (!WebPAnimDecoderGetInfo(dec, &anim_info))
    {
        fprintf(stderr, "Error getting global info about the animation\n");
        goto End;
    }

    if (anim_info.canvas_width == 0 || anim_info.canvas_height == 0 ||
        anim_info.canvas_width > static_cast<uint32_t>(INT_MAX) ||
        anim_info.canvas_height > static_cast<uint32_t>(INT_MAX))
    {
        goto End;
    }

    while (WebPAnimDecoderHasMoreFrames(dec))
    {
        uint8_t* frame_rgba = NULL;
        int timestamp = 0;

        if (!WebPAnimDecoderGetNext(dec, &frame_rgba, &timestamp))
        {
            fprintf(stderr, "Error decoding frame #%u\n", frame_index);
            goto End;
        }

        int duration = timestamp - prev_frame_timestamp;
        if (duration < 0)
            duration = 0;

        ncnn::Mat* inimage = new ncnn::Mat();
        inimage->create(static_cast<int>(anim_info.canvas_width),
                        static_cast<int>(anim_info.canvas_height),
                        static_cast<size_t>(out_channels),
                        out_channels);
        if (inimage->empty())
        {
            delete inimage;
            goto End;
        }

        const size_t num_pixels = static_cast<size_t>(anim_info.canvas_width) * static_cast<size_t>(anim_info.canvas_height);
        if (out_channels == 4)
        {
            std::memcpy(inimage->data, frame_rgba, num_pixels * 4);
        }
        else
        {
            const uint8_t* src = frame_rgba;
            uint8_t* dst = static_cast<uint8_t*>(inimage->data);
            for (size_t p = 0; p < num_pixels; ++p)
            {
                dst[0] = src[0];
                dst[1] = src[1];
                dst[2] = src[2];
                src += 4;
                dst += 3;
            }
        }

        v.inImage.push_back(inimage);
        v.inFrame.push_back(duration);

        ++frame_index;
        prev_frame_timestamp = timestamp;
    }

    if (frame_index == 0)
        goto End;

    if (v.save_format.length() == 0) v.save_format = "webp";
    v.load_format = "webp";
    ok = true;

End:
    if (!ok)
    {
        v.clear_in_image();
        v.inFrame.clear();
    }
    if (dec)
        WebPAnimDecoderDelete(dec);
    return ok;
}

bool load_webp_ani(Task& v)
{
    if (!is_webp_data(v.fileDate, v.fileSize))
        return false;

    const WebPData webp_data = { static_cast<const uint8_t*>(v.fileDate), static_cast<size_t>(v.fileSize) };
    WebPDemuxer* demux = WebPDemux(&webp_data);
    if (!demux)
        return false;

    uint32_t flags = WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS);
    WebPDemuxDelete(demux);
    return load_webp_ani_impl(v, (flags & ALPHA_FLAG) != 0);
}

bool load_webp(Task& v)
{
    if (!is_webp_data(v.fileDate, v.fileSize))
        return false;

    const WebPData webp_data = { static_cast<const uint8_t*>(v.fileDate), static_cast<size_t>(v.fileSize) };
    WebPDemuxer* demux = WebPDemux(&webp_data);
    if (!demux)
        return false;

    uint32_t flags = WebPDemuxGetI(demux, WEBP_FF_FORMAT_FLAGS);
    if (flags & ANIMATION_FLAG)
    {
        bool has_alpha = (flags & ALPHA_FLAG) != 0;
        WebPDemuxDelete(demux);
        return load_webp_ani_impl(v, has_alpha);
    }

    uint32_t canvas_w = WebPDemuxGetI(demux, WEBP_FF_CANVAS_WIDTH);
    uint32_t canvas_h = WebPDemuxGetI(demux, WEBP_FF_CANVAS_HEIGHT);
    int channels = (flags & ALPHA_FLAG) ? 4 : 3;

    int orientation = 1;
    if (v.auto_transfom && (flags & EXIF_FLAG))
    {
        WebPChunkIterator chunk_iter;
        if (WebPDemuxGetChunk(demux, "EXIF", 1, &chunk_iter))
        {
            orientation = get_exif_orientation(chunk_iter.chunk.bytes, chunk_iter.chunk.size);
            WebPDemuxReleaseChunkIterator(&chunk_iter);
        }
    }
    WebPDemuxDelete(demux);

    if (canvas_w == 0 || canvas_h == 0 ||
        canvas_w > static_cast<uint32_t>(INT_MAX) ||
        canvas_h > static_cast<uint32_t>(INT_MAX))
    {
        return false;
    }

    int width = static_cast<int>(canvas_w);
    int height = static_cast<int>(canvas_h);
    size_t stride = static_cast<size_t>(width) * static_cast<size_t>(channels);
    if (stride > static_cast<size_t>(INT_MAX))
        return false;
    size_t buf_size = stride * static_cast<size_t>(height);

    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config))
        return false;

    ncnn::Mat* inimage = new ncnn::Mat();
    inimage->create(width, height, static_cast<size_t>(channels), channels);
    if (inimage->empty())
    {
        delete inimage;
        return false;
    }

    config.output.colorspace = (channels == 4) ? MODE_RGBA : MODE_RGB;
    config.output.u.RGBA.stride = static_cast<int>(stride);
    config.output.u.RGBA.size = buf_size;
    config.output.u.RGBA.rgba = static_cast<uint8_t*>(inimage->data);
    config.output.is_external_memory = 1;

    VP8StatusCode code = WebPDecode(static_cast<const uint8_t*>(v.fileDate), static_cast<size_t>(v.fileSize), &config);
    WebPFreeDecBuffer(&config.output);

    if (code != VP8_STATUS_OK)
    {
        delete inimage;
        return false;
    }

    if (orientation != 1)
    {
        int out_w = width;
        int out_h = height;
        uint8_t* rotated = apply_orientation(static_cast<const uint8_t*>(inimage->data), out_w, out_h, channels, orientation);
        if (rotated != nullptr)
        {
            ncnn::Mat* rotated_mat = new ncnn::Mat();
            rotated_mat->create(out_w, out_h, static_cast<size_t>(channels), channels);
            if (rotated_mat->empty())
            {
                free(rotated);
                delete rotated_mat;
                delete inimage;
                return false;
            }
            std::memcpy(rotated_mat->data, rotated,
                        static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * static_cast<size_t>(channels));
            free(rotated);
            delete inimage;
            inimage = rotated_mat;
        }
    }

    v.inImage.push_back(inimage);
    v.inFrame.push_back(100);
    if (v.save_format.length() == 0) v.save_format = "webp";
    v.load_format = "webp";
    return true;
}

size_t webp_encode(ncnn::Mat& outimage, unsigned int toW, unsigned int toH, int webp_quality, uint8_t** out)
{
    if (!out)
        return 0;
    *out = nullptr;

    if (outimage.empty() || outimage.w <= 0 || outimage.h <= 0)
        return 0;
    if (outimage.elemsize != 3 && outimage.elemsize != 4)
        return 0;

    WebPConfig config;
    if (!webp_init_config(config, webp_quality))
        return 0;

    WebPPicture pic;
    if (!WebPPictureInit(&pic))
        return 0;

    pic.use_argb = 0;
    pic.width = outimage.w;
    pic.height = outimage.h;

    int stride = outimage.w * static_cast<int>(outimage.elemsize);
    int import_ok = (outimage.elemsize == 3)
        ? WebPPictureImportRGB(&pic, static_cast<const uint8_t*>(outimage.data), stride)
        : WebPPictureImportRGBA(&pic, static_cast<const uint8_t*>(outimage.data), stride);
    if (!import_ok)
    {
        WebPPictureFree(&pic);
        return 0;
    }

    if (toW > 0 && toH > 0 &&
        (static_cast<unsigned int>(outimage.w) != toW || static_cast<unsigned int>(outimage.h) != toH))
    {
        if (!WebPPictureRescale(&pic, static_cast<int>(toW), static_cast<int>(toH)))
        {
            WebPPictureFree(&pic);
            return 0;
        }
    }

    WebPMemoryWriter wrt;
    WebPMemoryWriterInit(&wrt);
    pic.writer = WebPMemoryWrite;
    pic.custom_ptr = &wrt;

    int ok = WebPEncode(&config, &pic);
    WebPPictureFree(&pic);

    if (!ok)
    {
        WebPMemoryWriterClear(&wrt);
        return 0;
    }

    *out = wrt.mem;
    return wrt.size;
}

bool webp_save_ani(Task& v)
{
    if (v.outImage.empty())
        return false;

    ncnn::Mat* first_ptr = *v.outImage.begin();
    if (!first_ptr || first_ptr->empty() || first_ptr->w <= 0 || first_ptr->h <= 0 ||
        (first_ptr->elemsize != 3 && first_ptr->elemsize != 4))
    {
        return false;
    }

    int canvas_w = (v.toW > 0 && v.toH > 0) ? static_cast<int>(v.toW) : first_ptr->w;
    int canvas_h = (v.toW > 0 && v.toH > 0) ? static_cast<int>(v.toH) : first_ptr->h;

    WebPConfig config;
    if (!webp_init_config(config, v.webp_quality))
        return false;

    WebPAnimEncoderOptions anim_options;
    if (!WebPAnimEncoderOptionsInit(&anim_options))
        return false;
    anim_options.anim_params.bgcolor = 0;
    anim_options.anim_params.loop_count = 0;

    WebPAnimEncoder* enc = WebPAnimEncoderNew(canvas_w, canvas_h, &anim_options);
    if (!enc)
        return false;

    bool ok = false;
    int timestamp_ms = 0;
    WebPData outputData;
    WebPDataInit(&outputData);

    std::list<ncnn::Mat*>::iterator i = v.outImage.begin();
    std::list<int>::iterator j = v.inFrame.begin();
    for (; i != v.outImage.end(); ++i)
    {
        if (!*i || (*i)->empty())
            goto End;

        ncnn::Mat& frame = **i;
        if (frame.w <= 0 || frame.h <= 0 || (frame.elemsize != 3 && frame.elemsize != 4))
            goto End;

        int duration = 100;
        if (j != v.inFrame.end())
        {
            duration = (*j > 0) ? *j : 0;
            ++j;
        }

        WebPPicture pic;
        if (!WebPPictureInit(&pic))
            goto End;

        // WebPAnimEncoderAdd works in ARGB internally; importing directly avoids lossy YUV->ARGB conversion
        pic.use_argb = 1;
        pic.width = frame.w;
        pic.height = frame.h;

        int stride = frame.w * static_cast<int>(frame.elemsize);
        int import_ok = (frame.elemsize == 3)
            ? WebPPictureImportRGB(&pic, static_cast<const uint8_t*>(frame.data), stride)
            : WebPPictureImportRGBA(&pic, static_cast<const uint8_t*>(frame.data), stride);
        if (!import_ok)
        {
            WebPPictureFree(&pic);
            goto End;
        }

        if (pic.width != canvas_w || pic.height != canvas_h)
        {
            if (!WebPPictureRescale(&pic, canvas_w, canvas_h))
            {
                WebPPictureFree(&pic);
                goto End;
            }
        }

        int add_ok = WebPAnimEncoderAdd(enc, &pic, timestamp_ms, &config);
        WebPPictureFree(&pic);
        if (!add_ok)
            goto End;

        timestamp_ms += duration;
    }

    if (!WebPAnimEncoderAdd(enc, NULL, timestamp_ms, NULL))
        goto End;

    if (!WebPAnimEncoderAssemble(enc, &outputData))
        goto End;

    v.out = (void*)outputData.bytes;
    v.outSize = static_cast<int>(outputData.size);
    ok = true;

End:
    if (!ok)
        WebPDataClear(&outputData);
    WebPAnimEncoderDelete(enc);
    return ok;
}

bool webp_save(Task& v)
{
    if (v.outImage.empty())
        return false;

    ncnn::Mat* out_ptr = *v.outImage.begin();
    if (!out_ptr || out_ptr->empty())
        return false;

    ncnn::Mat& outimage = *out_ptr;
    if (outimage.elemsize != 3 && outimage.elemsize != 4)
        return false;

    uint8_t* output = nullptr;
    size_t length = webp_encode(outimage, v.toW, v.toH, v.webp_quality, &output);
    if (length == 0 || !output)
        return false;

    v.out = (void*)output;
    v.outSize = static_cast<int>(length);
    return true;
}

#endif // WEBP_IMAGE_H
