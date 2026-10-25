#ifndef PNG_IMAGE_H
#define PNG_IMAGE_H

// png image decoder and encoder with libpng
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <png.h>
#include "spng.h"

struct png_read_context
{
    const unsigned char* indata;
    unsigned int inlen;
    unsigned int current;
};

static void png_read_fn(png_structp png_ptr, png_bytep dst, png_size_t nread)
{
    struct png_read_context* prc = (struct png_read_context*)(png_get_io_ptr(png_ptr));
    if (prc->current + nread > prc->inlen)
        return;

    memcpy(dst, prc->indata + prc->current, nread);
    prc->current += nread;
}

unsigned char* png_load_memery(const unsigned char* buffer, int len, int* w, int* h, int* c)
{
    if (len < 8 || png_sig_cmp(buffer, 0, 8))
        return NULL;

    unsigned char* pixeldata = 0;

    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
        return NULL;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return NULL;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return NULL;
    }

    struct png_read_context prc = { buffer, (unsigned int)len, 0 };
    png_set_read_fn(png_ptr, &prc, png_read_fn);

    png_read_info(png_ptr, info_ptr);

    int width = png_get_image_width(png_ptr, info_ptr);
    int height = png_get_image_height(png_ptr, info_ptr);

    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);

    // always output RGB(A) 8bit
    if (bit_depth == 16)
        png_set_strip_16(png_ptr);

    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png_ptr);

    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png_ptr);

    int has_alpha = 0;
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        has_alpha = 1;
    if (color_type == PNG_COLOR_TYPE_RGBA || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        has_alpha = 1;

    if (has_alpha)
        png_set_tRNS_to_alpha(png_ptr);

    png_read_update_info(png_ptr, info_ptr);

    int channels = has_alpha ? 4 : 3;

    pixeldata = (unsigned char*)malloc(width * height * channels);

    png_bytepp row_pointers = new png_bytep[height];
    for (png_uint_32 i = 0; i < height; i++)
        row_pointers[i] = pixeldata + (size_t)i * width * channels;

    png_read_image(png_ptr, row_pointers);

    png_read_end(png_ptr, info_ptr);

    delete[] row_pointers;

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    *w = width;
    *h = height;
    *c = channels;

    return pixeldata;
}

int png_save_memery(Task& v, int w, int h, int c, const unsigned char* pixeldata)
{
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr)
        return 0;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_write_struct(&png_ptr, NULL);
        return 0;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return 0;
    }
    png_set_write_fn(png_ptr, (void*)(&v), &apng_write_fn, NULL);

    png_set_IHDR(png_ptr, info_ptr, w, h, 8, c == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    // png_set_compression_level(png_ptr, 9);

    png_write_info(png_ptr, info_ptr);

    png_bytepp row_pointers = new png_bytep[h];
    for (int i = 0; i < h; i++)
        row_pointers[i] = (const png_bytep)(pixeldata + (size_t)i * w * c);

    png_write_image(png_ptr, row_pointers);

    png_write_end(png_ptr, info_ptr);

    delete[] row_pointers;

    png_destroy_write_struct(&png_ptr, &info_ptr);

    return 1;
}

bool load_png(Task& v)
{
    unsigned char* pixeldata = 0;
    bool ok = true;
    void* result = 0;
    int w = 0, h = 0, c = 0;
    pixeldata = png_load_memery((unsigned char*)v.fileDate, v.fileSize, &w, &h, &c);
    if (!pixeldata)
    {
        return false;
    }
    ncnn::Mat* inimage = new ncnn::Mat();
    inimage->create(w, h, (size_t)c, c);
    memcpy(inimage->data, pixeldata, w * h * c);
    v.inImage.push_back(inimage);
    v.inFrame.push_back(100);
    if (pixeldata) stbi_image_free(pixeldata);
    if (v.save_format.length() == 0) v.save_format = "png";
    v.load_format = "png";
    return true;
}

unsigned char* spng_load_memery(Task& v, int *out_width, int *out_height, int* channels) {
    int ret = 0;
    spng_ctx *ctx = NULL;
    unsigned char *image_out = NULL;
    unsigned char *out_image = NULL;
    size_t out_size = 0;
    struct spng_ihdr ihdr;
    int has_alpha = 0;
    int output_format = 0;
    ctx = spng_ctx_new(0);
    if (!ctx) {
        v.err = "create spng error";
        return NULL;
    }
    ret = spng_set_png_buffer(ctx, v.fileDate, v.fileSize);
    if (ret) {
        v.err = spng_strerror(ret);
        goto cleanup;
    }
    ret = spng_get_ihdr(ctx, &ihdr);
    if (ret) {
        v.err = spng_strerror(ret);
        goto cleanup;
    }
    if (ihdr.color_type == SPNG_COLOR_TYPE_TRUECOLOR_ALPHA ||
        ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE_ALPHA) {
        has_alpha = 1;
    } else {
        struct spng_trns trns;
        if (spng_get_trns(ctx, &trns) == 0) {
            has_alpha = 1;
        }
    }
    output_format = has_alpha ? SPNG_FMT_RGBA8 : SPNG_FMT_RGB8;
    *channels = has_alpha ? 4 : 3;
    *out_width = ihdr.width;
    *out_height = ihdr.height;

    spng_decoded_image_size(ctx, output_format, &out_size);
    out_image = (unsigned char *)malloc(out_size);
    if (!out_image) {
        goto cleanup;
    }
    // 执行解码（一步到位完成所有像素转换和标准化）
    ret = spng_decode_image(ctx, out_image, out_size, output_format, 0);
    if (ret != 0) {
        v.err = spng_strerror(ret);
        free(out_image);
        goto cleanup;
    }
cleanup:
    spng_ctx_free(ctx);
    return image_out;
}

bool load_spng(Task& v)
{
    unsigned char* pixeldata = 0;
    bool ok = true;
    void* result = 0;
    int w = 0, h = 0, c = 0;
    pixeldata = spng_load_memery(v, &w, &h, &c);
    if (!pixeldata)
    {
        return false;
    }
    ncnn::Mat* inimage = new ncnn::Mat();
    inimage->create(w, h, (size_t)c, c);
    memcpy(inimage->data, pixeldata, w * h * c);
    v.inImage.push_back(inimage);
    v.inFrame.push_back(100);
    if (pixeldata) stbi_image_free(pixeldata);
    if (v.save_format.length() == 0) v.save_format = "png";
    v.load_format = "png";
    return true;
}

int spng_save_memery(Task& v, int w, int h, int c, const unsigned char* pixeldata)
{
    if (!pixeldata || w <= 0 || h <= 0 || (c != 3 && c != 4))
        return 0;
    spng_ctx *ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    if (!ctx)
        return 0;
    struct spng_ihdr ihdr = {0};
    ihdr.width = static_cast<uint32_t>(w);
    ihdr.height = static_cast<uint32_t>(h);
    ihdr.bit_depth = 8;
    ihdr.color_type = (c == 3) ? SPNG_COLOR_TYPE_TRUECOLOR : SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    int ret = spng_set_ihdr(ctx, &ihdr);
    if (ret != SPNG_OK)
    {
        spng_ctx_free(ctx);
        return 0;
    }
    spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);
    size_t pixel_bytes =  static_cast<size_t>(w) * h * c;
    ret = spng_encode_image(ctx, pixeldata, pixel_bytes, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    if (ret == SPNG_OK) {
        size_t out_png_size = 0;
        void * png_data = spng_get_png_buffer(ctx, &out_png_size, &ret);
        if (!png_data || ret != SPNG_OK) {
            spng_ctx_free(ctx);
            v.err = spng_strerror(ret);
            return NULL;
        }
        v.out = png_data;
        v.outSize = out_png_size;

    } else {
        v.err = spng_strerror(ret);
    }
    spng_ctx_free(ctx);
    return (ret == SPNG_OK) ? 1 : 0;
}

#endif // PNG_IMAGE_H
