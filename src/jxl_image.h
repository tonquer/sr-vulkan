#ifndef JXL_IMAGE_H
#define JXL_IMAGE_H

#define JXL_STATIC_DEFINE
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <climits>
#include <jxl/decode.h>
#include <jxl/encode.h>

bool load_jxl(Task& v)
{
    if (!v.fileDate || v.fileSize <= 0)
        return false;

    const uint8_t* raw_data = static_cast<const uint8_t*>(v.fileDate);
    size_t raw_size = static_cast<size_t>(v.fileSize);
    JxlSignature sig = JxlSignatureCheck(raw_data, raw_size);
    if (sig != JXL_SIG_CODESTREAM && sig != JXL_SIG_CONTAINER)
        return false;

    JxlDecoder* dec = JxlDecoderCreate(NULL);
    if (!dec)
        return false;

    const int events = JXL_DEC_BASIC_INFO | JXL_DEC_FRAME | JXL_DEC_FULL_IMAGE;
    if (JxlDecoderSubscribeEvents(dec, events) != JXL_DEC_SUCCESS)
    {
        JxlDecoderDestroy(dec);
        return false;
    }

    if (JxlDecoderSetKeepOrientation(dec, v.auto_transfom ? JXL_FALSE : JXL_TRUE) != JXL_DEC_SUCCESS)
    {
        JxlDecoderDestroy(dec);
        return false;
    }

    if (JxlDecoderSetInput(dec, raw_data, raw_size) != JXL_DEC_SUCCESS)
    {
        JxlDecoderDestroy(dec);
        return false;
    }
    JxlDecoderCloseInput(dec);

    JxlPixelFormat format = {
        3,
        JXL_TYPE_UINT8,
        JXL_NATIVE_ENDIAN,
        0
    };
    JxlBasicInfo info;
    std::memset(&info, 0, sizeof(info));
    JxlFrameHeader frame_header;
    size_t frame_buffer_size = 0;
    size_t channels = 3;
    int duration_ms = 100;
    bool has_basic_info = false;
    bool is_animation = false;
    ncnn::Mat* current_frame = NULL;
    bool success = false;

    for (;;)
    {
        JxlDecoderStatus status = JxlDecoderProcessInput(dec);
        if (status == JXL_DEC_ERROR)
        {
            break;
        }
        else if (status == JXL_DEC_NEED_MORE_INPUT)
        {
            v.err = "jxl decode data fail!";
            break;
        }
        else if (status == JXL_DEC_BASIC_INFO)
        {
            if (JxlDecoderGetBasicInfo(dec, &info) != JXL_DEC_SUCCESS)
                break;

            if (info.xsize == 0 || info.ysize == 0 ||
                info.xsize > static_cast<uint32_t>(INT_MAX) ||
                info.ysize > static_cast<uint32_t>(INT_MAX))
            {
                break;
            }

            is_animation = (info.have_animation == JXL_TRUE);
            channels = (info.alpha_bits > 0) ? 4 : 3;
            format.num_channels = static_cast<uint32_t>(channels);

            if (JxlDecoderImageOutBufferSize(dec, &format, &frame_buffer_size) != JXL_DEC_SUCCESS)
                break;

            size_t expected_size = static_cast<size_t>(info.xsize) * static_cast<size_t>(info.ysize) * channels;
            if (frame_buffer_size == 0 || frame_buffer_size > expected_size)
                break;

            has_basic_info = true;
        }
        else if (status == JXL_DEC_FRAME)
        {
            if (!has_basic_info)
                break;

            if (is_animation)
            {
                if (JxlDecoderGetFrameHeader(dec, &frame_header) == JXL_DEC_SUCCESS &&
                    info.animation.tps_numerator > 0)
                {
                    duration_ms = static_cast<int>(
                        static_cast<double>(frame_header.duration) * 1000.0 *
                        static_cast<double>(info.animation.tps_denominator) /
                        static_cast<double>(info.animation.tps_numerator));
                    if (duration_ms < 0)
                        duration_ms = 0;
                }
                else
                {
                    duration_ms = 100;
                }
            }
            else
            {
                duration_ms = 100;
            }

            if (current_frame)
            {
                delete current_frame;
                current_frame = NULL;
            }

            current_frame = new ncnn::Mat();
            current_frame->create(static_cast<int>(info.xsize), static_cast<int>(info.ysize), channels, static_cast<int>(channels));
            if (current_frame->empty())
            {
                delete current_frame;
                current_frame = NULL;
                break;
            }
        }
        else if (status == JXL_DEC_NEED_IMAGE_OUT_BUFFER)
        {
            if (!current_frame || current_frame->empty())
                break;

            if (JxlDecoderSetImageOutBuffer(dec, &format, current_frame->data, frame_buffer_size) != JXL_DEC_SUCCESS)
                break;
        }
        else if (status == JXL_DEC_FULL_IMAGE)
        {
            if (current_frame)
            {
                v.inImage.push_back(current_frame);
                v.inFrame.push_back(duration_ms);
                current_frame = NULL;
            }
        }
        else if (status == JXL_DEC_SUCCESS)
        {
            success = true;
            break;
        }
    }

    if (current_frame)
    {
        delete current_frame;
        current_frame = NULL;
    }
    JxlDecoderDestroy(dec);

    if (success && !v.inImage.empty())
    {
        if (v.save_format.length() == 0) v.save_format = "jxl";
        v.load_format = "jxl";
        return true;
    }

    v.clear_in_image();
    v.inFrame.clear();
    return false;
}

bool jxl_save(Task& v)
{
    if (v.outImage.empty())
    {
        v.err = "empty outimage";
        return false;
    }

    ncnn::Mat* first_ptr = *v.outImage.begin();
    if (!first_ptr || first_ptr->empty() || first_ptr->w <= 0 || first_ptr->h <= 0 ||
        (first_ptr->elemsize != 3 && first_ptr->elemsize != 4))
    {
        v.err = "invalid outimage";
        return false;
    }

    const int width = first_ptr->w;
    const int height = first_ptr->h;
    const size_t channels = first_ptr->elemsize;

    for (std::list<ncnn::Mat*>::iterator it = v.outImage.begin(); it != v.outImage.end(); ++it)
    {
        if (!*it || (*it)->empty() || (*it)->w != width || (*it)->h != height || (*it)->elemsize != channels)
        {
            v.err = "inconsistent jxl frame dimensions";
            return false;
        }
    }

    bool is_animation = (v.outImage.size() > 1);
    JxlEncoder* enc = JxlEncoderCreate(NULL);
    if (!enc)
        return false;

    const float distance = 1.0f;
    const int effort = 7;

    JxlBasicInfo basic_info;
    JxlEncoderInitBasicInfo(&basic_info);
    basic_info.xsize = static_cast<uint32_t>(width);
    basic_info.ysize = static_cast<uint32_t>(height);
    basic_info.bits_per_sample = 8;
    basic_info.uses_original_profile = JXL_FALSE;
    basic_info.num_color_channels = 3;
    basic_info.alpha_bits = (channels == 4) ? 8 : 0;
    basic_info.num_extra_channels = (channels == 4) ? 1 : 0;
    if (is_animation)
    {
        basic_info.have_animation = JXL_TRUE;
        basic_info.animation.tps_numerator = 1000;
        basic_info.animation.tps_denominator = 1;
        basic_info.animation.num_loops = 0;
        basic_info.animation.have_timecodes = JXL_FALSE;
    }
    else
    {
        basic_info.have_animation = JXL_FALSE;
    }

    if (JxlEncoderSetBasicInfo(enc, &basic_info) != JXL_ENC_SUCCESS)
    {
        v.err = "jxl set BasicInfo error!";
        JxlEncoderDestroy(enc);
        return false;
    }

    JxlColorEncoding color_encoding;
    JxlColorEncodingSetToSRGB(&color_encoding, JXL_FALSE);
    if (JxlEncoderSetColorEncoding(enc, &color_encoding) != JXL_ENC_SUCCESS)
    {
        v.err = "jxl set ColorEncoding error!";
        JxlEncoderDestroy(enc);
        return false;
    }

    JxlEncoderFrameSettings* frame_settings = JxlEncoderFrameSettingsCreate(enc, NULL);
    if (!frame_settings)
    {
        v.err = "jxl create frame settings error!";
        JxlEncoderDestroy(enc);
        return false;
    }

    JxlEncoderSetFrameDistance(frame_settings, distance);
    JxlEncoderSetFrameLossless(frame_settings, (distance == 0.0f) ? JXL_TRUE : JXL_FALSE);
    JxlEncoderFrameSettingsSetOption(frame_settings, JXL_ENC_FRAME_SETTING_EFFORT, effort);
    if (distance == 0.0f)
    {
        JxlEncoderFrameSettingsSetOption(frame_settings, JXL_ENC_FRAME_SETTING_RESAMPLING, 1);
    }

    JxlPixelFormat pixel_format = {
        static_cast<uint32_t>(channels),
        JXL_TYPE_UINT8,
        JXL_NATIVE_ENDIAN,
        0
    };
    const size_t frame_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * channels;

    std::list<ncnn::Mat*>::iterator i = v.outImage.begin();
    std::list<int>::iterator j = v.inFrame.begin();
    for (; i != v.outImage.end(); ++i)
    {
        ncnn::Mat& inimage = **i;
        int duration_ms = 100;
        if (j != v.inFrame.end())
        {
            duration_ms = (*j > 0) ? *j : 0;
            ++j;
        }

        if (is_animation)
        {
            JxlFrameHeader frame_header;
            JxlEncoderInitFrameHeader(&frame_header);
            frame_header.duration = static_cast<uint32_t>(duration_ms);
            if (JxlEncoderSetFrameHeader(frame_settings, &frame_header) != JXL_ENC_SUCCESS)
            {
                v.err = "jxl set frame header error!";
                JxlEncoderDestroy(enc);
                return false;
            }
        }

        if (JxlEncoderAddImageFrame(frame_settings, &pixel_format, inimage.data, frame_bytes) != JXL_ENC_SUCCESS)
        {
            v.err = "jxl encode frame error!";
            JxlEncoderDestroy(enc);
            return false;
        }
    }

    JxlEncoderCloseInput(enc);

    size_t out_cap = 128 * 1024;
    uint8_t* out_buf = static_cast<uint8_t*>(malloc(out_cap));
    if (!out_buf)
    {
        v.err = "jxl alloc output buffer error";
        JxlEncoderDestroy(enc);
        return false;
    }

    uint8_t* next_out = out_buf;
    size_t avail_out = out_cap;
    size_t out_size = 0;
    bool success = false;

    for (;;)
    {
        JxlEncoderStatus status = JxlEncoderProcessOutput(enc, &next_out, &avail_out);
        if (status == JXL_ENC_SUCCESS)
        {
            out_size = static_cast<size_t>(next_out - out_buf);
            if (out_size == 0 || out_size > static_cast<size_t>(INT_MAX))
            {
                v.err = "jxl invalid output size";
                break;
            }
            uint8_t* shrunk = static_cast<uint8_t*>(realloc(out_buf, out_size));
            if (shrunk)
            {
                out_buf = shrunk;
                out_cap = out_size;
            }
            success = true;
            break;
        }
        else if (status == JXL_ENC_NEED_MORE_OUTPUT)
        {
            size_t bytes_written = static_cast<size_t>(next_out - out_buf);
            if (out_cap >= static_cast<size_t>(INT_MAX))
            {
                v.err = "jxl output buffer too large";
                break;
            }
            size_t new_cap = (out_cap > static_cast<size_t>(INT_MAX) / 2) ? static_cast<size_t>(INT_MAX) : (out_cap * 2);
            uint8_t* new_buf = static_cast<uint8_t*>(realloc(out_buf, new_cap));
            if (!new_buf)
            {
                v.err = "jxl realloc output buffer error";
                break;
            }
            out_buf = new_buf;
            out_cap = new_cap;
            next_out = out_buf + bytes_written;
            avail_out = out_cap - bytes_written;
        }
        else
        {
            v.err = "jxl ProcessOutput error";
            break;
        }
    }

    JxlEncoderDestroy(enc);

    if (!success)
    {
        free(out_buf);
        v.out = NULL;
        v.outSize = 0;
        v.allOutSize = 0;
        return false;
    }

    v.out = out_buf;
    v.outSize = static_cast<int>(out_size);
    v.allOutSize = static_cast<int>(out_cap);
    return true;
}

#endif // JXL_IMAGE_H
