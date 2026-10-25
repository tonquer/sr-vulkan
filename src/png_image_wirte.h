#ifndef PNG_IMAGE_WRITE_H
#define PNG_IMAGE_WRITE_H

// png image decoder and encoder with libpng
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include "spng.h"

int save_png_file(const char* filename, const unsigned char* image_data, uint32_t width, uint32_t height) {
    int ret = 0;
    FILE* fp = NULL;
    spng_ctx* ctx = NULL;
    struct spng_ihdr ihdr = { 0 };

    // 1. 打开要写入的文件
    fp = fopen(filename, "wb");
    if (!fp) {
        printf("无法打开文件进行写入: %s\n", filename);
        return -1;
    }

    // 2. 创建编码器上下文
    ctx = spng_ctx_new(SPNG_CTX_ENCODER);
    if (!ctx) {
        printf("创建 spng 上下文失败\n");
        fclose(fp);
        return -1;
    }

    // 3. 设置输出文件流
    ret = spng_set_png_file(ctx, fp);
    if (ret) {
        printf("spng_set_png_file 错误: %s\n", spng_strerror(ret));
        goto cleanup;
    }

    // 4. 设置 PNG 头信息 (IHDR)
    ihdr.width = width;
    ihdr.height = height;
    ihdr.bit_depth = 8;                      // 每个通道 8 位
    ihdr.color_type = SPNG_COLOR_TYPE_TRUECOLOR_ALPHA;
    ihdr.interlace_method = SPNG_INTERLACE_NONE; // 不隔行扫描

    ret = spng_set_ihdr(ctx, &ihdr);
    if (ret) {
        printf("spng_set_ihdr 错误: %s\n", spng_strerror(ret));
        goto cleanup;
    }

    // 5. 编码并写入图像数据
    // SPNG_FMT_RGBA8 表示输入的原始数据格式是 RGBA 每一个字节一个通道
    // SPNG_ENCODE_FINALIZE 表示编码完成后直接结束并写入尾部标志
    ret = spng_encode_image(ctx, image_data, (size_t)width * height * 4, SPNG_FMT_PNG, SPNG_ENCODE_FINALIZE);
    if (ret) {
        printf("spng_encode_image 错误: %s\n", spng_strerror(ret));
        goto cleanup;
    }

    printf("成功保存 PNG 图片: %s\n", filename);

cleanup:
    // 6. 释放资源
    spng_ctx_free(ctx);
    fclose(fp);
    return ret;
}

#endif // PNG_IMAGE_WRITE_H
