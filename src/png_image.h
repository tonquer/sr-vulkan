#ifndef PNG_IMAGE_H
#define PNG_IMAGE_H

// png image decoder and encoder with libpng / libspng
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>
#include <cstring>
#include <vector>
#include <png.h>
#include "spng.h"
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

static inline bool is_png_data(const void* data, int size)
{
    if (!data || size < 8)
        return false;
    return png_sig_cmp(static_cast<const unsigned char*>(data), 0, 8) == 0;
}

struct png_read_context
{
    const unsigned char* indata;
    size_t inlen;
    size_t current;
};

static void png_read_fn(png_structp png_ptr, png_bytep dst, png_size_t nread)
{
    struct png_read_context* prc = static_cast<struct png_read_context*>(png_get_io_ptr(png_ptr));
    if (!prc || nread == 0)
        return;
    if (nread > prc->inlen - prc->current)
    {
        png_error(png_ptr, "png unexpected end of input");
        return;
    }

    std::memcpy(dst, prc->indata + prc->current, nread);
    prc->current += nread;
}

static void png_silent_error_fn(png_structp png_ptr, png_const_charp error_msg)
{
    (void)error_msg;
    png_longjmp(png_ptr, 1);
}

static void png_silent_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
{
    (void)png_ptr;
    (void)warning_msg;
}

typedef unsigned char* (*png_alloc_fn)(void* ctx, int width, int height, int channels);

struct png_decode_state
{
    png_bytepp row_pointers;
    int width;
    int height;
    int channels;
    int orientation;
};

// Pure-C noinline helper so setjmp/longjmp never crosses C++ EH frames or leaks buffers.
SR_NOINLINE static bool png_decode_core(const unsigned char* buffer, size_t len, bool auto_transfom,
                                        png_alloc_fn alloc_fn, void* alloc_ctx,
                                        png_decode_state* state)
{
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, &png_silent_error_fn, &png_silent_warning_fn);
    if (!png_ptr)
        return false;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return false;
    }

    struct png_read_context prc = { buffer, len, 0 };
    png_set_read_fn(png_ptr, &prc, png_read_fn);

    png_read_info(png_ptr, info_ptr);

    png_uint_32 raw_width = png_get_image_width(png_ptr, info_ptr);
    png_uint_32 raw_height = png_get_image_height(png_ptr, info_ptr);
    if (raw_width == 0 || raw_height == 0 ||
        raw_width > static_cast<png_uint_32>(INT_MAX) ||
        raw_height > static_cast<png_uint_32>(INT_MAX))
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return false;
    }

    int width = static_cast<int>(raw_width);
    int height = static_cast<int>(raw_height);
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

    png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    int channels = has_alpha ? 4 : 3;
    size_t row_stride = static_cast<size_t>(width) * static_cast<size_t>(channels);
    if (static_cast<size_t>(height) > SIZE_MAX / row_stride)
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return false;
    }

    unsigned char* pixeldata = alloc_fn(alloc_ctx, width, height, channels);
    if (!pixeldata)
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return false;
    }

    state->row_pointers = static_cast<png_bytepp>(malloc(static_cast<size_t>(height) * sizeof(png_bytep)));
    if (!state->row_pointers)
    {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return false;
    }

    for (int i = 0; i < height; i++)
        state->row_pointers[i] = pixeldata + static_cast<size_t>(i) * row_stride;

    png_read_image(png_ptr, state->row_pointers);
    png_read_end(png_ptr, info_ptr);

    int orientation = 1;
#ifdef PNG_eXIf_SUPPORTED
    if (auto_transfom)
    {
        png_uint_32 num_exif = 0;
        png_bytep exif_data = NULL;
        if (png_get_eXIf_1(png_ptr, info_ptr, &num_exif, &exif_data) == PNG_INFO_eXIf &&
            exif_data != NULL && num_exif > 0)
        {
            orientation = get_exif_orientation(exif_data, num_exif);
        }
    }
#else
    (void)auto_transfom;
#endif

    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    state->width = width;
    state->height = height;
    state->channels = channels;
    state->orientation = orientation;
    return true;
}

static unsigned char* png_alloc_malloc_cb(void* ctx, int width, int height, int channels)
{
    unsigned char** slot = static_cast<unsigned char**>(ctx);
    size_t buf_size = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
    *slot = static_cast<unsigned char*>(malloc(buf_size));
    return *slot;
}

static unsigned char* png_alloc_mat_cb(void* ctx, int width, int height, int channels)
{
    ncnn::Mat** slot = static_cast<ncnn::Mat**>(ctx);
    ncnn::Mat* mat = new ncnn::Mat();
    mat->create(width, height, static_cast<size_t>(channels), channels);
    if (mat->empty())
    {
        delete mat;
        return NULL;
    }
    *slot = mat;
    return static_cast<unsigned char*>(mat->data);
}

unsigned char* png_load_memery(const unsigned char* buffer, int len, int* w, int* h, int* c)
{
    if (!is_png_data(buffer, len) || !w || !h || !c)
        return NULL;

    unsigned char* pixeldata = NULL;
    png_decode_state state = { NULL, 0, 0, 0, 1 };

    bool ok = png_decode_core(buffer, static_cast<size_t>(len), false, &png_alloc_malloc_cb, &pixeldata, &state);
    if (state.row_pointers)
        free(state.row_pointers);

    if (!ok)
    {
        if (pixeldata)
            free(pixeldata);
        return NULL;
    }

    *w = state.width;
    *h = state.height;
    *c = state.channels;
    return pixeldata;
}

SR_NOINLINE static bool png_encode_core(Task* v, int w, int h, int c, const unsigned char* pixeldata, png_bytep* row_pointers)
{
    png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, &png_silent_error_fn, &png_silent_warning_fn);
    if (!png_ptr)
        return false;

    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr)
    {
        png_destroy_write_struct(&png_ptr, NULL);
        return false;
    }

    if (setjmp(png_jmpbuf(png_ptr)))
    {
        png_destroy_write_struct(&png_ptr, &info_ptr);
        return false;
    }

    png_set_write_fn(png_ptr, static_cast<void*>(v), &apng_write_fn, NULL);

    png_set_IHDR(png_ptr, info_ptr, static_cast<png_uint_32>(w), static_cast<png_uint_32>(h), 8,
                 c == 3 ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

    png_write_info(png_ptr, info_ptr);

    size_t row_stride = static_cast<size_t>(w) * static_cast<size_t>(c);
    for (int i = 0; i < h; i++)
        row_pointers[i] = const_cast<png_bytep>(pixeldata + static_cast<size_t>(i) * row_stride);

    png_write_image(png_ptr, row_pointers);
    png_write_end(png_ptr, info_ptr);
    png_destroy_write_struct(&png_ptr, &info_ptr);
    return true;
}

int png_save_memery(Task& v, int w, int h, int c, const unsigned char* pixeldata)
{
    if (!pixeldata || w <= 0 || h <= 0 || (c != 3 && c != 4))
        return 0;

    v.out = NULL;
    v.outSize = 0;
    v.allOutSize = 0;

    std::vector<png_bytep> row_pointers(static_cast<size_t>(h));
    bool ok = png_encode_core(&v, w, h, c, pixeldata, row_pointers.data());
    if (!ok)
    {
        if (v.out)
            free(v.out);
        v.out = NULL;
        v.outSize = 0;
        v.allOutSize = 0;
        return 0;
    }
    return 1;
}

bool load_png(Task& v)
{
    if (!is_png_data(v.fileDate, v.fileSize))
        return false;

    ncnn::Mat* mat = nullptr;
    png_decode_state state = { NULL, 0, 0, 0, 1 };

    bool ok = png_decode_core(static_cast<const unsigned char*>(v.fileDate), static_cast<size_t>(v.fileSize),
                              v.auto_transfom, &png_alloc_mat_cb, &mat, &state);
    if (state.row_pointers)
        free(state.row_pointers);

    if (!ok || !mat)
    {
        if (mat)
            delete mat;
        return false;
    }

    if (state.orientation != 1)
    {
        int out_w = state.width;
        int out_h = state.height;
        uint8_t* rotated = apply_orientation(static_cast<const uint8_t*>(mat->data), out_w, out_h, state.channels, state.orientation);
        if (rotated != nullptr)
        {
            ncnn::Mat* rotated_mat = new ncnn::Mat();
            rotated_mat->create(out_w, out_h, static_cast<size_t>(state.channels), state.channels);
            if (rotated_mat->empty())
            {
                free(rotated);
                delete rotated_mat;
                delete mat;
                return false;
            }
            std::memcpy(rotated_mat->data, rotated,
                        static_cast<size_t>(out_w) * static_cast<size_t>(out_h) * static_cast<size_t>(state.channels));
            free(rotated);
            delete mat;
            mat = rotated_mat;
        }
    }

    v.inImage.push_back(mat);
    v.inFrame.push_back(100);
    if (v.save_format.length() == 0) v.save_format = "png";
    v.load_format = "png";
    return true;
}

unsigned char* spng_load_memery(Task& v, int* out_width, int* out_height, int* channels, bool auto_transfom)
{
    if (!is_png_data(v.fileDate, v.fileSize) || !out_width || !out_height || !channels)
        return NULL;

    int ret = 0;
    spng_ctx* ctx = NULL;
    uint8_t* out_image = NULL;
    uint8_t* final_pixels = NULL;
    size_t out_size = 0;
    struct spng_ihdr ihdr;
    int has_alpha = 0;
    int output_format = 0;
    int orientation = 1;
    struct spng_exif exifChunk;

    ctx = spng_ctx_new(0);
    if (!ctx)
    {
        v.err = "create spng error";
        return NULL;
    }

    ret = spng_set_png_buffer(ctx, v.fileDate, static_cast<size_t>(v.fileSize));
    if (ret)
    {
        v.err = spng_strerror(ret);
        goto cleanup;
    }

    ret = spng_get_ihdr(ctx, &ihdr);
    if (ret)
    {
        v.err = spng_strerror(ret);
        goto cleanup;
    }

    if (ihdr.width == 0 || ihdr.height == 0 ||
        ihdr.width > static_cast<uint32_t>(INT_MAX) ||
        ihdr.height > static_cast<uint32_t>(INT_MAX))
    {
        goto cleanup;
    }

    if (ihdr.color_type == SPNG_COLOR_TYPE_TRUECOLOR_ALPHA ||
        ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE_ALPHA)
    {
        has_alpha = 1;
    }
    else
    {
        struct spng_trns trns;
        if (spng_get_trns(ctx, &trns) == 0)
        {
            has_alpha = 1;
        }
    }

    output_format = has_alpha ? SPNG_FMT_RGBA8 : SPNG_FMT_RGB8;
    *channels = has_alpha ? 4 : 3;
    *out_width = static_cast<int>(ihdr.width);
    *out_height = static_cast<int>(ihdr.height);

    ret = spng_decoded_image_size(ctx, output_format, &out_size);
    if (ret != 0 || out_size == 0)
    {
        v.err = spng_strerror(ret);
        goto cleanup;
    }

    out_image = static_cast<uint8_t*>(malloc(out_size));
    if (!out_image)
        goto cleanup;

    ret = spng_decode_image(ctx, out_image, out_size, output_format, has_alpha ? SPNG_DECODE_TRNS : 0);
    if (ret != 0)
    {
        v.err = spng_strerror(ret);
        free(out_image);
        out_image = NULL;
        goto cleanup;
    }

    if (auto_transfom && spng_get_exif(ctx, &exifChunk) == 0 && exifChunk.data != nullptr && exifChunk.length > 0)
    {
        orientation = get_exif_orientation(reinterpret_cast<const uint8_t*>(exifChunk.data), exifChunk.length);
    }

    if (orientation != 1)
    {
        final_pixels = apply_orientation(out_image, *out_width, *out_height, *channels, orientation);
    }

cleanup:
    spng_ctx_free(ctx);
    if (final_pixels != nullptr && final_pixels != out_image)
    {
        free(out_image);
        return final_pixels;
    }
    return out_image;
}

bool load_spng(Task& v)
{
    if (!is_png_data(v.fileDate, v.fileSize))
        return false;

    int ret = 0;
    spng_ctx* ctx = spng_ctx_new(0);
    if (!ctx)
    {
        v.err = "create spng error";
        return false;
    }

    ncnn::Mat* inimage = nullptr;
    size_t out_size = 0;
    struct spng_ihdr ihdr;
    int has_alpha = 0;
    int output_format = 0;
    int width = 0;
    int height = 0;
    int channels = 3;
    int orientation = 1;
    struct spng_exif exifChunk;
    bool ok = false;

    ret = spng_set_png_buffer(ctx, v.fileDate, static_cast<size_t>(v.fileSize));
    if (ret != 0)
    {
        v.err = spng_strerror(ret);
        goto cleanup;
    }

    ret = spng_get_ihdr(ctx, &ihdr);
    if (ret != 0)
    {
        v.err = spng_strerror(ret);
        goto cleanup;
    }

    if (ihdr.width == 0 || ihdr.height == 0 ||
        ihdr.width > static_cast<uint32_t>(INT_MAX) ||
        ihdr.height > static_cast<uint32_t>(INT_MAX))
    {
        goto cleanup;
    }

    if (ihdr.color_type == SPNG_COLOR_TYPE_TRUECOLOR_ALPHA ||
        ihdr.color_type == SPNG_COLOR_TYPE_GRAYSCALE_ALPHA)
    {
        has_alpha = 1;
    }
    else
    {
        struct spng_trns trns;
        if (spng_get_trns(ctx, &trns) == 0)
            has_alpha = 1;
    }

    output_format = has_alpha ? SPNG_FMT_RGBA8 : SPNG_FMT_RGB8;
    channels = has_alpha ? 4 : 3;
    width = static_cast<int>(ihdr.width);
    height = static_cast<int>(ihdr.height);

    ret = spng_decoded_image_size(ctx, output_format, &out_size);
    if (ret != 0 || out_size == 0)
    {
        v.err = spng_strerror(ret);
        goto cleanup;
    }

    {
        size_t expected_size = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
        if (out_size > expected_size)
            goto cleanup;
    }

    inimage = new ncnn::Mat();
    inimage->create(width, height, static_cast<size_t>(channels), channels);
    if (inimage->empty())
        goto cleanup;

    ret = spng_decode_image(ctx, inimage->data, out_size, output_format, has_alpha ? SPNG_DECODE_TRNS : 0);
    if (ret != 0)
    {
        v.err = spng_strerror(ret);
        goto cleanup;
    }

    if (v.auto_transfom && spng_get_exif(ctx, &exifChunk) == 0 && exifChunk.data != nullptr && exifChunk.length > 0)
    {
        orientation = get_exif_orientation(reinterpret_cast<const uint8_t*>(exifChunk.data), exifChunk.length);
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
                goto cleanup;
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
    inimage = nullptr;
    if (v.save_format.length() == 0) v.save_format = "png";
    v.load_format = "png";
    ok = true;

cleanup:
    if (inimage)
        delete inimage;
    spng_ctx_free(ctx);
    return ok;
}

int spng_save_memery(Task& v, int w, int h, int c, const unsigned char* pixeldata)
{
    if (!pixeldata || w <= 0 || h <= 0 || (c != 3 && c != 4))
        return 0;

    spng_ctx* ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    if (!ctx)
        return 0;

    struct spng_ihdr ihdr;
    std::memset(&ihdr, 0, sizeof(ihdr));
    ihdr.width = static_cast<uint32_t>(w);
    ihdr.height = static_cast<uint32_t>(h);
    ihdr.bit_depth = 8;
    ihdr.color_type = (c == 3) ? SPNG_COLOR_TYPE_TRUECOLOR : SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;

    int ret = spng_set_ihdr(ctx, &ihdr);
    if (ret != SPNG_OK)
    {
        v.err = spng_strerror(ret);
        spng_ctx_free(ctx);
        return 0;
    }

    ret = spng_set_option(ctx, SPNG_ENCODE_TO_BUFFER, 1);
    if (ret != SPNG_OK)
    {
        v.err = spng_strerror(ret);
        spng_ctx_free(ctx);
        return 0;
    }

    size_t pixel_bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * static_cast<size_t>(c);
    ret = spng_encode_image(ctx, pixeldata, pixel_bytes, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    if (ret == SPNG_OK)
    {
        size_t out_png_size = 0;
        void* png_data = spng_get_png_buffer(ctx, &out_png_size, &ret);
        if (!png_data || ret != SPNG_OK || out_png_size == 0 || out_png_size > static_cast<size_t>(INT_MAX))
        {
            if (png_data)
                free(png_data);
            v.err = spng_strerror(ret);
            spng_ctx_free(ctx);
            return 0;
        }
        v.out = png_data;
        v.outSize = static_cast<int>(out_png_size);
    }
    else
    {
        v.err = spng_strerror(ret);
    }

    spng_ctx_free(ctx);
    return (ret == SPNG_OK) ? 1 : 0;
}

#endif // PNG_IMAGE_H
