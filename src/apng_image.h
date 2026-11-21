#ifndef APNG_IMAGE_H
#define APNG_IMAGE_H

#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <cstring>
#include <algorithm>
#include <vector>
#include <png.h>
#include "waifu2x_main.h"

#ifndef SR_NOINLINE
#if defined(_MSC_VER)
#define SR_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define SR_NOINLINE __attribute__((noinline))
#else
#define SR_NOINLINE
#endif
#endif

struct apng_read_context
{
    const unsigned char* data;
    size_t size;
    size_t offset;
};

struct apng_decode_buffers
{
    unsigned char* p_image;
    unsigned char* p_frame;
    unsigned char* p_temp;
    png_bytepp rows_image;
    png_bytepp rows_frame;
};

static void apng_silent_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
    (void)error_msg;
    png_longjmp(png_ptr, 1);
}

static void apng_silent_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
{
    (void)png_ptr;
    (void)warning_msg;
}

void apng_write_fn(png_structp png_ptr, unsigned char* data, size_t len)
{
    Task* v = static_cast<Task*>(png_get_io_ptr(png_ptr));
    if (!v || len == 0)
        return;

    size_t cur_size = (v->outSize > 0) ? static_cast<size_t>(v->outSize) : 0;
    size_t cur_cap = (v->allOutSize > 0) ? static_cast<size_t>(v->allOutSize) : 0;
    if (len > static_cast<size_t>(INT_MAX) - cur_size)
    {
        png_error(png_ptr, "apng output buffer overflow");
        return;
    }

    size_t needed = cur_size + len;
    if (needed > cur_cap)
    {
        size_t new_cap = (cur_cap > 0) ? cur_cap : 4096;
        while (new_cap < needed)
        {
            if (new_cap > static_cast<size_t>(INT_MAX) / 2)
            {
                new_cap = needed;
                break;
            }
            new_cap *= 2;
        }
        void* new_ptr = realloc(v->out, new_cap);
        if (!new_ptr)
        {
            png_error(png_ptr, "apng output realloc failed");
            return;
        }
        v->out = new_ptr;
        v->allOutSize = static_cast<int>(new_cap);
    }

    std::memcpy(static_cast<unsigned char*>(v->out) + cur_size, data, len);
    v->outSize = static_cast<int>(needed);
}

static void apng_read_ctx_fn(png_structp png_ptr, png_bytep data, png_size_t len)
{
    apng_read_context* ctx = static_cast<apng_read_context*>(png_get_io_ptr(png_ptr));
    if (!ctx || len == 0)
        return;
    if (len > ctx->size - ctx->offset)
    {
        png_error(png_ptr, "apng unexpected end of input");
        return;
    }
    std::memcpy(data, ctx->data + ctx->offset, len);
    ctx->offset += len;
}

void BlendOver(unsigned char** rows_dst, unsigned char** rows_src, unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    for (unsigned int j = 0; j < h; j++)
    {
        unsigned char* sp = rows_src[j];
        unsigned char* dp = rows_dst[j + y] + static_cast<size_t>(x) * 4;

        for (unsigned int i = 0; i < w; i++, sp += 4, dp += 4)
        {
            if (sp[3] == 255)
            {
                std::memcpy(dp, sp, 4);
            }
            else if (sp[3] != 0)
            {
                if (dp[3] != 0)
                {
                    int u = sp[3] * 255;
                    int v = (255 - sp[3]) * dp[3];
                    int al = u + v;
                    dp[0] = static_cast<unsigned char>((sp[0] * u + dp[0] * v) / al);
                    dp[1] = static_cast<unsigned char>((sp[1] * u + dp[1] * v) / al);
                    dp[2] = static_cast<unsigned char>((sp[2] * u + dp[2] * v) / al);
                    dp[3] = static_cast<unsigned char>(al / 255);
                }
                else
                {
                    std::memcpy(dp, sp, 4);
                }
            }
        }
    }
}

typedef bool (*apng_push_frame_fn)(void* user_ctx, unsigned int width, unsigned int height,
                                   unsigned int channels, size_t row_size,
                                   const unsigned char* p_image, unsigned char* const* rows_image,
                                   int duration_ms);

static bool apng_push_frame_cb(void* user_ctx, unsigned int width, unsigned int height,
                               unsigned int channels, size_t row_size,
                               const unsigned char* p_image, unsigned char* const* rows_image,
                               int duration_ms)
{
    Task* v = static_cast<Task*>(user_ctx);
    ncnn::Mat* inimage = new ncnn::Mat();
    inimage->create(static_cast<int>(width), static_cast<int>(height), static_cast<size_t>(channels), static_cast<int>(channels));
    if (inimage->empty())
    {
        delete inimage;
        return false;
    }

    size_t dst_row = static_cast<size_t>(width) * channels;
    if (row_size == dst_row)
    {
        std::memcpy(inimage->data, p_image, static_cast<size_t>(height) * row_size);
    }
    else
    {
        unsigned char* dst = static_cast<unsigned char*>(inimage->data);
        for (unsigned int j = 0; j < height; j++)
            std::memcpy(dst + static_cast<size_t>(j) * dst_row, rows_image[j], dst_row);
    }

    v->inImage.push_back(inimage);
    v->inFrame.push_back(duration_ms);
    return true;
}

// Pure-C noinline helper so setjmp/longjmp never crosses C++ EH frames or clobbers local pointers.
SR_NOINLINE static bool apng_decode_core(apng_read_context* read_ctx, apng_decode_buffers* bufs,
                                         apng_push_frame_fn push_frame, void* user_ctx)
{
    bool ok = false;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, &apng_silent_error_fn, &apng_silent_warning_fn);
    if (!png_ptr)
        goto End;

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
        goto End;

    if (setjmp(png_jmpbuf(png_ptr)))
        goto End;

    png_set_read_fn(png_ptr, read_ctx, &apng_read_ctx_fn);
    png_set_sig_bytes(png_ptr, 8);
    png_read_info(png_ptr, info_ptr);

    if (!png_get_valid(png_ptr, info_ptr, PNG_INFO_acTL))
        goto End;

    {
        unsigned int nFrameCount = 1;
        unsigned int m_nPlays = 0;
        png_get_acTL(png_ptr, info_ptr, &nFrameCount, &m_nPlays);
        unsigned int first = (png_get_first_frame_is_hidden(png_ptr, info_ptr) != 0) ? 1u : 0u;
        if (nFrameCount <= 1u + first)
            goto End;

        png_set_expand(png_ptr);
        png_set_strip_16(png_ptr);
        png_set_gray_to_rgb(png_ptr);
        png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
        png_set_interlace_handling(png_ptr);
        png_read_update_info(png_ptr, info_ptr);

        unsigned int m_nWidth = png_get_image_width(png_ptr, info_ptr);
        unsigned int m_nHeight = png_get_image_height(png_ptr, info_ptr);
        unsigned int m_nChannels = png_get_channels(png_ptr, info_ptr);
        size_t m_nRowSize = png_get_rowbytes(png_ptr, info_ptr);

        if (m_nWidth == 0 || m_nHeight == 0 || m_nChannels != 4 || m_nRowSize == 0 ||
            m_nWidth > static_cast<unsigned int>(INT_MAX) ||
            m_nHeight > static_cast<unsigned int>(INT_MAX) ||
            m_nRowSize < static_cast<size_t>(m_nWidth) * 4 ||
            static_cast<size_t>(m_nHeight) > SIZE_MAX / m_nRowSize)
        {
            goto End;
        }

        size_t m_nFrameSize = static_cast<size_t>(m_nHeight) * m_nRowSize;
        bufs->p_image = static_cast<unsigned char*>(calloc(1, m_nFrameSize));
        bufs->p_frame = static_cast<unsigned char*>(malloc(m_nFrameSize));
        bufs->p_temp = static_cast<unsigned char*>(malloc(m_nFrameSize));
        bufs->rows_image = static_cast<png_bytepp>(malloc(static_cast<size_t>(m_nHeight) * sizeof(png_bytep)));
        bufs->rows_frame = static_cast<png_bytepp>(malloc(static_cast<size_t>(m_nHeight) * sizeof(png_bytep)));

        if (!bufs->p_image || !bufs->p_frame || !bufs->p_temp || !bufs->rows_image || !bufs->rows_frame)
            goto End;

        for (unsigned int j = 0; j < m_nHeight; j++)
            bufs->rows_image[j] = bufs->p_image + static_cast<size_t>(j) * m_nRowSize;

        for (unsigned int j = 0; j < m_nHeight; j++)
            bufs->rows_frame[j] = bufs->p_frame + static_cast<size_t>(j) * m_nRowSize;

        for (unsigned int i = 0; i < nFrameCount; i++)
        {
            png_uint_32 x0 = 0;
            png_uint_32 y0 = 0;
            png_uint_32 w0 = m_nWidth;
            png_uint_32 h0 = m_nHeight;
            unsigned short delay_num = 1;
            unsigned short delay_den = 10;
            unsigned char dop = PNG_DISPOSE_OP_NONE;
            unsigned char bop = PNG_BLEND_OP_SOURCE;

            png_read_frame_head(png_ptr, info_ptr);
            if (i < first)
            {
                // Skip hidden default IDAT image; it is not part of the animation sequence
                png_read_image(png_ptr, bufs->rows_frame);
                continue;
            }

            if (png_get_valid(png_ptr, info_ptr, PNG_INFO_fcTL))
            {
                png_get_next_frame_fcTL(png_ptr, info_ptr, &w0, &h0, &x0, &y0, &delay_num, &delay_den, &dop, &bop);
            }

            if (w0 == 0 || h0 == 0 || x0 > m_nWidth || y0 > m_nHeight ||
                w0 > m_nWidth - x0 || h0 > m_nHeight - y0)
            {
                goto End;
            }

            if (i == first)
            {
                bop = PNG_BLEND_OP_SOURCE;
                if (dop == PNG_DISPOSE_OP_PREVIOUS)
                    dop = PNG_DISPOSE_OP_BACKGROUND;
            }

            png_read_image(png_ptr, bufs->rows_frame);

            if (dop == PNG_DISPOSE_OP_PREVIOUS)
                std::memcpy(bufs->p_temp, bufs->p_image, m_nFrameSize);

            if (bop == PNG_BLEND_OP_OVER)
            {
                BlendOver(bufs->rows_image, bufs->rows_frame, x0, y0, w0, h0);
            }
            else
            {
                for (unsigned int j = 0; j < h0; j++)
                    std::memcpy(bufs->rows_image[j + y0] + static_cast<size_t>(x0) * 4, bufs->rows_frame[j], static_cast<size_t>(w0) * 4);
            }

            // APNG spec: delay_den == 0 means 100
            int den = (delay_den == 0) ? 100 : static_cast<int>(delay_den);
            int duration_ms = static_cast<int>((static_cast<int64_t>(delay_num) * 1000) / den);

            if (!push_frame(user_ctx, m_nWidth, m_nHeight, m_nChannels, m_nRowSize, bufs->p_image, bufs->rows_image, duration_ms))
                goto End;

            if (dop == PNG_DISPOSE_OP_PREVIOUS)
            {
                std::memcpy(bufs->p_image, bufs->p_temp, m_nFrameSize);
            }
            else if (dop == PNG_DISPOSE_OP_BACKGROUND)
            {
                for (unsigned int j = 0; j < h0; j++)
                    std::memset(bufs->rows_image[j + y0] + static_cast<size_t>(x0) * 4, 0, static_cast<size_t>(w0) * 4);
            }
        }

        png_read_end(png_ptr, info_ptr);
        ok = true;
    }

End:
    if (png_ptr)
    {
        if (info_ptr)
            png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL);
        else
            png_destroy_read_struct(&png_ptr, NULL, NULL);
    }
    return ok;
}

bool load_apng(Task& v)
{
    if (!v.fileDate || v.fileSize < 8)
        return false;

    const unsigned char* raw_data = static_cast<const unsigned char*>(v.fileDate);
    if (png_sig_cmp(raw_data, 0, 8) != 0)
        return false;

    apng_read_context read_ctx = { raw_data, static_cast<size_t>(v.fileSize), 8 };
    apng_decode_buffers bufs = { NULL, NULL, NULL, NULL, NULL };

    bool ok = apng_decode_core(&read_ctx, &bufs, &apng_push_frame_cb, &v);

    if (bufs.rows_frame) free(bufs.rows_frame);
    if (bufs.rows_image) free(bufs.rows_image);
    if (bufs.p_temp) free(bufs.p_temp);
    if (bufs.p_frame) free(bufs.p_frame);
    if (bufs.p_image) free(bufs.p_image);

    if (!ok || v.inImage.empty())
    {
        v.clear_in_image();
        v.inFrame.clear();
        return false;
    }

    if (v.save_format.length() == 0) v.save_format = "apng";
    v.load_format = "apng";
    return true;
}

SR_NOINLINE static bool apng_encode_core(Task* v, int xres, int yres, int bpp,
                                         const unsigned char* const* frame_ptrs,
                                         const int* durations, unsigned int frame_count,
                                         png_bytep* row_pointers)
{
    bool ok = false;
    png_structp png_ptr = NULL;
    png_infop info_ptr = NULL;

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, &apng_silent_error_fn, &apng_silent_warning_fn);
    if (!png_ptr)
        goto End;

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
        goto End;

    if (setjmp(png_jmpbuf(png_ptr)))
        goto End;

    png_set_write_fn(png_ptr, static_cast<void*>(v), &apng_write_fn, NULL);

    png_set_IHDR(png_ptr, info_ptr, static_cast<png_uint_32>(xres), static_cast<png_uint_32>(yres), 8,
                 (bpp == 4) ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_RGB,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_set_acTL(png_ptr, info_ptr, frame_count, 0);
    png_set_first_frame_is_hidden(png_ptr, info_ptr, 0);
    png_write_info(png_ptr, info_ptr);

    {
        size_t row_stride = static_cast<size_t>(xres) * static_cast<size_t>(bpp);
        for (unsigned int idx = 0; idx < frame_count; ++idx)
        {
            int duration_ms = durations[idx];
            png_uint_16 delay_num = 0;
            png_uint_16 delay_den = 1000;
            if (duration_ms > 65535)
            {
                int centis = duration_ms / 10;
                delay_num = static_cast<png_uint_16>(centis > 65535 ? 65535 : centis);
                delay_den = 100;
            }
            else
            {
                delay_num = static_cast<png_uint_16>(duration_ms);
            }

            const unsigned char* base = frame_ptrs[idx];
            for (int k = 0; k < yres; k++)
                row_pointers[k] = const_cast<png_bytep>(base + static_cast<size_t>(k) * row_stride);

            png_write_frame_head(png_ptr, info_ptr, row_pointers,
                                 static_cast<png_uint_32>(xres), static_cast<png_uint_32>(yres),
                                 0, 0, delay_num, delay_den, PNG_DISPOSE_OP_NONE, PNG_BLEND_OP_SOURCE);
            png_write_image(png_ptr, row_pointers);
            png_write_frame_tail(png_ptr, info_ptr);
        }
    }

    png_write_end(png_ptr, info_ptr);
    ok = true;

End:
    if (png_ptr)
    {
        if (info_ptr)
            png_destroy_write_struct(&png_ptr, &info_ptr);
        else
            png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
    }
    return ok;
}

bool apng_save(Task& v)
{
    if (v.outImage.empty())
        return false;

    ncnn::Mat* first_ptr = *v.outImage.begin();
    if (!first_ptr || first_ptr->empty() || first_ptr->w <= 0 || first_ptr->h <= 0 ||
        (first_ptr->elemsize != 3 && first_ptr->elemsize != 4))
    {
        return false;
    }

    int xres = first_ptr->w;
    int yres = first_ptr->h;
    int bpp = static_cast<int>(first_ptr->elemsize);

    std::vector<const unsigned char*> frame_ptrs;
    std::vector<int> durations;
    frame_ptrs.reserve(v.outImage.size());
    durations.reserve(v.outImage.size());

    std::list<ncnn::Mat*>::iterator i = v.outImage.begin();
    std::list<int>::iterator j = v.inFrame.begin();
    for (; i != v.outImage.end(); ++i)
    {
        if (!*i || (*i)->empty() || (*i)->w != xres || (*i)->h != yres || static_cast<int>((*i)->elemsize) != bpp)
            return false;
        frame_ptrs.push_back(static_cast<const unsigned char*>((*i)->data));
        int dur = 100;
        if (j != v.inFrame.end())
        {
            dur = (*j > 0) ? *j : 0;
            ++j;
        }
        durations.push_back(dur);
    }

    std::vector<png_bytep> row_pointers(static_cast<size_t>(yres));

    v.out = NULL;
    v.outSize = 0;
    v.allOutSize = 0;

    size_t init_cap = std::min<size_t>(static_cast<size_t>(xres) * static_cast<size_t>(yres) * static_cast<size_t>(bpp), 4 * 1024 * 1024);
    if (init_cap < 4096) init_cap = 4096;
    v.out = malloc(init_cap);
    if (!v.out)
        return false;
    v.allOutSize = static_cast<int>(init_cap);

    bool ok = apng_encode_core(&v, xres, yres, bpp,
                               frame_ptrs.data(), durations.data(),
                               static_cast<unsigned int>(frame_ptrs.size()),
                               row_pointers.data());

    if (!ok)
    {
        if (v.out)
            free(v.out);
        v.out = NULL;
        v.outSize = 0;
        v.allOutSize = 0;
        return false;
    }
    return true;
}

#endif // APNG_IMAGE_H