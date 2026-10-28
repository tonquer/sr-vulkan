#ifndef JXL_IMAGE_H
#define JXL_IMAGE_H

#define JXL_STATIC_DEFINE // 必须在 include 之前定义
#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <jxl/decode.h>
#include <jxl/encode.h>


// 通用解码函数
bool load_jxl(Task& v) {
    // 1. 初始化解码器
    JxlDecoder* dec = JxlDecoderCreate(NULL);
    if (!dec) return false;


    // 3. 关键：同时订阅 BASIC_INFO、FRAME（用于获取帧时间）、FULL_IMAGE（一帧完成）
    const int events = JXL_DEC_BASIC_INFO | JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE;
    if (JxlDecoderSubscribeEvents(dec, events) != JXL_DEC_SUCCESS) {
        JxlDecoderDestroy(dec);
        return false;
    }
    JxlDecoderSetInput(dec, (const uint8_t *)v.fileDate, v.fileSize);
    JxlDecoderCloseInput(dec);
    // 固定以 4 通道 8-bit RGBA 像素格式输出
    JxlPixelFormat format = {
        4,                  // 4 通道 (RGBA)
        JXL_TYPE_UINT8,     // 8 位无符号整数
        JXL_NATIVE_ENDIAN,  // 小端序
        0                   // align
    };
    JxlBasicInfo info;
    JxlFrameHeader frame_header;
    size_t frame_buffer_size = 0;
    int duration_ms = 0;
    size_t channels = 4;
    uint32_t current_width = 0;
    uint32_t current_height = 0;
    bool current_is_animation = false;
    ncnn::Mat *current_frame = NULL;
    JxlDecoderStatus status;

    bool success = false;
    // 4. 事件状态机主循环
    for (;;) {
        status = JxlDecoderProcessInput(dec);
        if (status == JXL_DEC_ERROR) {
            break;
        } 
        else if (status == JXL_DEC_NEED_MORE_INPUT) {
            v.err = "jxl decode data fail!";
            break;
        } 
        else if (status == JXL_DEC_BASIC_INFO) {
            // 获取画面的全局信息
            if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS) break;
            current_width = info.xsize;
            current_height = info.ysize;
            current_is_animation = (info.have_animation == JXL_TRUE);


            // 计算单帧 RGBA Buffer 的字节大小 (w * h * 4)
            if (JxlDecoderImageOutBufferSize(dec, &format, &frame_buffer_size) != JXL_DEC_SUCCESS) {
                break;
            }
        } 
        else if (status == JXL_DEC_FRAME) {
            // 每次准备解码新的一帧时触发（静态图只触发 1 次，动图触发 N 次）
            if (current_is_animation) {
                if (JxlDecoderGetFrameHeader(dec, &frame_header) == JXL_DEC_SUCCESS) {
                    // JXL 规范中时间换算公式：duration (秒) = duration_ticks * (tps_denominator / tps_numerator)
                    // 换算成毫秒: duration_ticks * 1000 * tps_denominator / tps_numerator
                    duration_ms = (int)((double)frame_header.duration * 1000.0 * 
                                         (double)info.animation.tps_denominator / 
                                         (double)info.animation.tps_numerator);
                }
            } else {
                duration_ms = 100;
            }

            current_frame = new ncnn::Mat();
            current_frame->create(info.xsize, info.ysize, (size_t)channels, (int)channels);
            v.inImage.push_back(current_frame);
            v.inFrame.push_back(duration_ms);
        } 
        else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
            // 把当前帧的内存首地址提供给 libjxl 写入渲染数据
            if (JxlDecoderSetImageOutBuffer(dec, &format, (uint8_t *)current_frame->data, info.xsize * info.ysize * channels) != JXL_DEC_SUCCESS) {
                break;
            }
        } 
        else if (status == JXL_DEC_FULL_IMAGE) {
            // 当前帧解码渲染完成，放入帧列表
            current_frame = NULL; // 重置为下一帧做准备
        } 
        else if (status == JXL_DEC_SUCCESS) {
            // 所有帧全部解码完毕
            success = true;
            break;
        }
    }
    JxlDecoderDestroy(dec);
    if (success && !v.inImage.empty())
    {
        if (v.save_format.length() == 0) v.save_format = "jxl";
        v.load_format = "jxl";
        return true;
    } else
    {
        return false;
    }
}


// 通用编码主函数
bool jxl_save(Task& v) {
    if (v.outImage.empty()) {
        v.err = "epmty outimage";
        return false;
    }
    JxlEncoderStatus status;
    bool is_animation = (v.outImage.size() > 1);
    // 1. 创建编码器实例
    JxlEncoder* enc = JxlEncoderCreate(NULL);
    if (!enc) return false;
    ncnn::Mat& first = **v.outImage.begin();
    // 画质参数 (Butteraugli 距离):
    // 0.0f = 数学无损 (Lossless)
    // 1.0f = 视觉无损 (Visually Lossless，体积小质量极高，推荐默认)
    // 2.0f~3.0f = 略微有损 (高压缩比)
    float distance = 1.0f;
    
    // 编码速度努力程度 (1 ~ 9): 1 最快，7 默认推荐，9 压缩率极致但较慢
    int effort = 7;

    // 3. 配置全局 BasicInfo
    JxlBasicInfo basic_info;
    JxlEncoderInitBasicInfo(&basic_info);
    basic_info.xsize = first.w;
    basic_info.ysize = first.h;
    basic_info.bits_per_sample = 8;
    basic_info.uses_original_profile = JXL_FALSE;
    basic_info.num_color_channels = 3;
    basic_info.alpha_bits = first.elemsize > 3 ? 8 : 0;
    basic_info.num_extra_channels = first.elemsize > 3 ? 1 : 0;
    // 动图特有配置
    if (is_animation) {
        basic_info.have_animation = JXL_TRUE;
        // 使用毫秒作为时钟基准: tps_numerator = 1000, tps_denominator = 1 (1 秒 = 1000 ticks)
        basic_info.animation.tps_numerator = 1000;
        basic_info.animation.tps_denominator = 1;
        basic_info.animation.num_loops = 0;
        basic_info.animation.have_timecodes = JXL_FALSE;
    } else {
        basic_info.have_animation = JXL_FALSE;
    }
    status = JxlEncoderSetBasicInfo(enc, &basic_info);
    if (status != JXL_ENC_SUCCESS) {
        v.err = "jxl set BasicInfo error  !";
        JxlEncoderDestroy(enc);
        return false;
    }
    // 4. 设置默认色彩空间 (sRGB)
    JxlColorEncoding color_encoding;
    JxlColorEncodingSetToSRGB(&color_encoding, /*is_gray=*/JXL_FALSE);
    status = JxlEncoderSetColorEncoding(enc, &color_encoding);
    if (status != JXL_ENC_SUCCESS) {
        v.err = "jxl set ColorEncoding error  !";
        JxlEncoderDestroy(enc);
        return false;
    }
    // 5. 循环写入每一帧数据
    JxlPixelFormat pixel_format = {
        first.elemsize > 3 ? 4u : 3u, // 4 = RGBA, 3 = RGB
        JXL_TYPE_UINT8,
        JXL_NATIVE_ENDIAN,
        0
    };
    
    std::list<int>::iterator j;
    std::list<ncnn::Mat*>::iterator i;

    for (i = v.outImage.begin(), j = v.inFrame.begin(); i != v.outImage.end(); i++, j++)
    {
        ncnn::Mat & inimage = **i;
        int duration_ms= *j;
        // 为当前帧创建独立的 FrameSettings
        JxlEncoderFrameSettings* frame_settings = JxlEncoderFrameSettingsCreate(enc, NULL);
        
        // 设置质量与努力程度
        JxlEncoderSetFrameDistance(frame_settings, distance);
        JxlEncoderSetFrameLossless(frame_settings, distance == 0.0f ? JXL_TRUE : JXL_FALSE);
        JxlEncoderFrameSettingsSetOption(frame_settings, JXL_ENC_FRAME_SETTING_EFFORT, effort);
        // 如果是无损，关闭色度抽样，保留原汁原味像素
        if (distance == 0.0f) {
            JxlEncoderFrameSettingsSetOption(frame_settings, JXL_ENC_FRAME_SETTING_RESAMPLING, 1);
        }
        // 动图设置当前帧时长
        if (is_animation) {
            JxlFrameHeader frame_header;
            JxlEncoderInitFrameHeader(&frame_header);
            // 因为 TPS 分子是 1000，这里的 duration 直接填毫秒数
            frame_header.duration = duration_ms;
            status = JxlEncoderSetFrameHeader(frame_settings, &frame_header);
            if (status != JXL_ENC_SUCCESS) {
                v.err = "jxl encode frame error  !";
            }
        }
        // 提交当前帧的像素数据
        status = JxlEncoderAddImageFrame(frame_settings, &pixel_format, inimage.data, inimage.cstep * inimage.elemsize);
        if (status != JXL_ENC_SUCCESS) {
            v.err = "jxl encode frame error  !";
            JxlEncoderDestroy(enc);
            return false;
        }
    }
    // 6. 关闭输入，通知编码器不再有后续帧
    JxlEncoderCloseInput(enc);
    // 7. 读取编码后的 JXL 二进制字节流
    // 初始分配 128KB 缓冲区
    v.allOutSize = 128 * 1024;
    v.out = (unsigned char*)malloc(v.allOutSize);

    uint8_t* next_out = (uint8_t* )v.out;
    size_t avail_out = v.allOutSize;

    bool success = false;
    for (;;) {
        status = JxlEncoderProcessOutput(enc, &next_out, &avail_out);
        if (status == JXL_ENC_SUCCESS) {
            // 编码成功结束，截断缓冲区到真实写入大小
            v.allOutSize = v.allOutSize - avail_out;
            v.out = realloc(v.out, v.allOutSize);
            v.outSize = v.allOutSize;
            success = true;
            break;
        } else if (status == JXL_ENC_NEED_MORE_OUTPUT) {
            // 缓冲区不足，按 2 倍扩容并继续导出
            size_t bytes_written = next_out - (uint8_t *)v.out;
            v.allOutSize = v.allOutSize*2;
            v.out = realloc(v.out, v.allOutSize);            
            next_out = (uint8_t *)v.out + bytes_written;
            avail_out = v.allOutSize - bytes_written;
        } else {
            v.err = "jxl ProcessOutput error";
            break;
        }
    }
    JxlEncoderDestroy(enc);
    if (!success) {
        if (v.out){
            free(v.out);
            v.out = NULL;
            v.outSize = 0;
            v.allOutSize = 0;
        }
    }
    return success;
}


#endif // JXL_IMAGE_H
