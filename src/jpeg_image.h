#ifndef JPEG_IMAGE_H
#define JPEG_IMAGE_H

// jpeg image decoder and encoder with libjpeg
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <cstring>
#include "jpeglib.h"
#include "common_image.h"

#ifndef SR_NOINLINE
#if defined(_MSC_VER)
#define SR_NOINLINE __declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
#define SR_NOINLINE __attribute__((noinline))
#else
#define SR_NOINLINE
#endif
#endif

struct safe_jpeg_error_mgr
{
    struct jpeg_error_mgr pub;
    jmp_buf setjmp_buffer;
};

static void safe_jpeg_error_exit(j_common_ptr cinfo)
{
    struct safe_jpeg_error_mgr* myerr = (struct safe_jpeg_error_mgr*)(cinfo->err);
    longjmp(myerr->setjmp_buffer, 1);
}

static void safe_jpeg_emit_message(j_common_ptr cinfo, int msg_level)
{
}

static void safe_jpeg_output_message(j_common_ptr cinfo)
{
}

static inline bool is_jpeg_data(const void* data, int size)
{
    if (!data || size < 2)
        return false;
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    return bytes[0] == 0xFF && bytes[1] == 0xD8;
}

typedef unsigned char* (*jpeg_alloc_fn)(void* ctx, int width, int height);

struct jpeg_decode_out
{
    int width;
    int height;
    int orientation;
};

// Pure-C noinline helper so setjmp/longjmp never crosses C++ EH frames or inlined destructors.
SR_NOINLINE static bool jpeg_decompress_core(const unsigned char* buffer, int len, bool auto_transfom,
                                             jpeg_alloc_fn alloc_fn, void* alloc_ctx,
                                             jpeg_decode_out* out)
{
    struct jpeg_decompress_struct cinfo;
    struct safe_jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    cinfo.err->error_exit = safe_jpeg_error_exit;
    cinfo.err->emit_message = safe_jpeg_emit_message;
    cinfo.err->output_message = safe_jpeg_output_message;

    if (setjmp(jerr.setjmp_buffer))
    {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    jpeg_create_decompress(&cinfo);

    if (auto_transfom)
    {
        jpeg_save_markers(&cinfo, JPEG_APP0 + 1, 0xFFFF);
    }

    jpeg_mem_src(&cinfo, buffer, static_cast<unsigned long>(len));

    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
    {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    int orientation = 1;
    if (auto_transfom)
    {
        for (jpeg_saved_marker_ptr m = cinfo.marker_list; m != NULL; m = m->next)
        {
            if (m->marker == JPEG_APP0 + 1)
            {
                orientation = get_exif_orientation(m->data, m->data_length);
                if (orientation != 1)
                    break;
            }
        }
    }

    cinfo.out_color_space = JCS_RGB;
    cinfo.output_components = 3;
    cinfo.data_precision = 8;

    jpeg_start_decompress(&cinfo);

    if (cinfo.output_width == 0 || cinfo.output_height == 0 ||
        cinfo.output_width > static_cast<JDIMENSION>(INT_MAX) ||
        cinfo.output_height > static_cast<JDIMENSION>(INT_MAX))
    {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    int width = static_cast<int>(cinfo.output_width);
    int height = static_cast<int>(cinfo.output_height);
    unsigned char* dst = alloc_fn(alloc_ctx, width, height);
    if (!dst)
    {
        jpeg_destroy_decompress(&cinfo);
        return false;
    }

    size_t row_stride = static_cast<size_t>(width) * 3;
    while (cinfo.output_scanline < cinfo.output_height)
    {
        unsigned char* ptr = dst + static_cast<size_t>(cinfo.output_scanline) * row_stride;
        jpeg_read_scanlines(&cinfo, &ptr, 1);
    }

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);

    out->width = width;
    out->height = height;
    out->orientation = orientation;
    return true;
}

SR_NOINLINE static bool jpeg_compress_core(int w, int h, int c, const unsigned char* pixeldata,
                                           unsigned char** outbuf, unsigned long* outlen)
{
    struct jpeg_compress_struct cinfo;
    struct safe_jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr.pub);
    cinfo.err->error_exit = safe_jpeg_error_exit;
    cinfo.err->emit_message = safe_jpeg_emit_message;
    cinfo.err->output_message = safe_jpeg_output_message;

    if (setjmp(jerr.setjmp_buffer))
    {
        jpeg_destroy_compress(&cinfo);
        return false;
    }

    jpeg_create_compress(&cinfo);

    jpeg_mem_dest(&cinfo, outbuf, outlen);
    cinfo.image_width = static_cast<JDIMENSION>(w);
    cinfo.image_height = static_cast<JDIMENSION>(h);

    if (c == 4)
    {
        cinfo.in_color_space = JCS_EXT_RGBA;
        cinfo.input_components = 4;
    }
    else
    {
        cinfo.in_color_space = JCS_RGB;
        cinfo.input_components = 3;
    }

    cinfo.data_precision = 8;

    jpeg_set_defaults(&cinfo);

    // no downsampling
    cinfo.comp_info[0].h_samp_factor = 1;
    cinfo.comp_info[0].v_samp_factor = 1;
    cinfo.comp_info[1].h_samp_factor = 1;
    cinfo.comp_info[1].v_samp_factor = 1;
    cinfo.comp_info[2].h_samp_factor = 1;
    cinfo.comp_info[2].v_samp_factor = 1;

    jpeg_set_quality(&cinfo, 98, TRUE);

    jpeg_start_compress(&cinfo, TRUE);

    size_t row_stride = static_cast<size_t>(w) * static_cast<size_t>(c);
    for (int y = 0; y < h; y++)
    {
        const unsigned char* ptr = pixeldata + static_cast<size_t>(y) * row_stride;
        jpeg_write_scanlines(&cinfo, const_cast<JSAMPARRAY>(&ptr), 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    return true;
}

static unsigned char* jpeg_alloc_malloc_cb(void* ctx, int width, int height)
{
    unsigned char** slot = static_cast<unsigned char**>(ctx);
    size_t buf_size = static_cast<size_t>(width) * static_cast<size_t>(height) * 3;
    *slot = static_cast<unsigned char*>(malloc(buf_size));
    return *slot;
}

static unsigned char* jpeg_alloc_mat_cb(void* ctx, int width, int height)
{
    ncnn::Mat** slot = static_cast<ncnn::Mat**>(ctx);
    ncnn::Mat* mat = new ncnn::Mat();
    mat->create(width, height, static_cast<size_t>(3), 3);
    if (mat->empty())
    {
        delete mat;
        return NULL;
    }
    *slot = mat;
    return static_cast<unsigned char*>(mat->data);
}

unsigned char* jpeg_load_memery(const unsigned char* buffer, int len, int* w, int* h, int* c, bool auto_transfom)
{
    if (!is_jpeg_data(buffer, len) || !w || !h || !c)
        return NULL;

    unsigned char* pixeldata = NULL;
    jpeg_decode_out out = { 0, 0, 1 };

    if (!jpeg_decompress_core(buffer, len, auto_transfom, &jpeg_alloc_malloc_cb, &pixeldata, &out))
    {
        if (pixeldata)
            free(pixeldata);
        return NULL;
    }

    int width = out.width;
    int height = out.height;
    uint8_t* outPixels = NULL;
    if (out.orientation != 1)
    {
        outPixels = apply_orientation(pixeldata, width, height, 3, out.orientation);
    }

    *w = width;
    *h = height;
    *c = 3;
    if (outPixels != nullptr && outPixels != pixeldata)
    {
        free(pixeldata);
        return outPixels;
    }
    return pixeldata;
}

int jpeg_save_memery(Task& v, int w, int h, int c, const unsigned char* pixeldata)
{
    if (!pixeldata || w <= 0 || h <= 0 || (c != 3 && c != 4))
        return 0;

    unsigned char* outbuf = NULL;
    unsigned long outlen = 0;

    if (!jpeg_compress_core(w, h, c, pixeldata, &outbuf, &outlen))
    {
        if (outbuf)
            free(outbuf);
        return 0;
    }

    if (!outbuf || outlen == 0 || outlen > static_cast<unsigned long>(INT_MAX))
    {
        if (outbuf)
            free(outbuf);
        return 0;
    }

    v.out = outbuf;
    v.outSize = static_cast<int>(outlen);
    return 1;
}

bool load_jpeg(Task& v)
{
    if (!is_jpeg_data(v.fileDate, v.fileSize))
        return false;

    ncnn::Mat* mat = nullptr;
    jpeg_decode_out out = { 0, 0, 1 };

    if (!jpeg_decompress_core(static_cast<const unsigned char*>(v.fileDate), v.fileSize,
                              v.auto_transfom, &jpeg_alloc_mat_cb, &mat, &out))
    {
        if (mat)
            delete mat;
        return false;
    }

    if (out.orientation != 1)
    {
        int out_w = out.width;
        int out_h = out.height;
        uint8_t* rotated = apply_orientation(static_cast<const uint8_t*>(mat->data), out_w, out_h, 3, out.orientation);
        if (rotated != nullptr)
        {
            ncnn::Mat* rotated_mat = new ncnn::Mat();
            rotated_mat->create(out_w, out_h, static_cast<size_t>(3), 3);
            if (rotated_mat->empty())
            {
                free(rotated);
                delete rotated_mat;
                delete mat;
                return false;
            }
            std::memcpy(rotated_mat->data, rotated, static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * 3);
            free(rotated);
            delete mat;
            mat = rotated_mat;
        }
    }

    v.inImage.push_back(mat);
    v.inFrame.push_back(100);
    if (v.save_format.length() == 0) v.save_format = "jpg";
    v.load_format = "jpg";
    return true;
}

#endif // JPEG_IMAGE_H
