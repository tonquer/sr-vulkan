#ifndef WEBP_IMAGE_H
#define WEBP_IMAGE_H

// webp image decoder and encoder with libwebp
#include <stdio.h>
#include <stdlib.h>
#include "webp/decode.h"
#include "webp/encode.h"
#include "webp/demux.h"
#include "webp/mux.h"

bool load_webp(Task &v)
{
    unsigned char* pixeldata = 0;

    WebPDecoderConfig config;
    WebPInitDecoderConfig(&config);

    if (WebPGetFeatures((uint8_t *)v.fileDate, v.fileSize, &config.input) != VP8_STATUS_OK)
        return false;

    if (config.input.has_animation)
    {
        return false;
    }

    int width = config.input.width;
    int height = config.input.height;
    int channels = config.input.has_alpha ? 4 : 3;

    pixeldata = (unsigned char*)malloc(width * height * channels);

    config.output.colorspace = channels == 4 ? MODE_RGBA : MODE_RGB;

    config.output.u.RGBA.stride = width * channels;
    config.output.u.RGBA.size = width * height * channels;
    config.output.u.RGBA.rgba = pixeldata;
    config.output.is_external_memory = 1;
    ncnn::Mat * inimage = new ncnn::Mat();
    inimage->create(width, height, (size_t)channels, channels);

    VP8StatusCode code = WebPDecode((uint8_t *)inimage->data, inimage->total()*channels, &config);
    if (code != VP8_STATUS_OK)
    {
        free(pixeldata);
        inimage->release();
        delete inimage;
        return false;
    }
    v.inImage.push_back(inimage);
    v.inFrame.push_back(100);
    if (v.save_format.length() == 0) v.save_format = "webp";
    v.load_format = "webp";
    return true;
}

bool load_webp_ani(Task& v)
{
    bool ok = false;
    uint32_t frame_index = 0;
    int prev_frame_timestamp = 0;
    WebPAnimDecoder* dec;
    WebPAnimInfo anim_info;
    WebPAnimDecoderOptions opt;
    const int kNumChannels = 4;
    int webp_status = 0;
    WebPDecoderConfig decoder_config;
    int duration, timestamp;

    memset(&opt, 0, sizeof(opt));
    const WebPData webp_data = {(unsigned char*) v.fileDate, (size_t)v.fileSize };

    opt.color_mode = MODE_RGBA;
    opt.use_threads = 0;
    dec = WebPAnimDecoderNew(&webp_data, &opt);
    if (dec == NULL) {
        //WFPRINTF(stderr, "Error parsing image: %s\n", (const W_CHAR*)filename);
        goto End;
    }
    // Main object storing the configuration for advanced decoding
    // Initialize the configuration as empty
    // This function must always be called first, unless WebPGetFeatures() is to be called
    if (!WebPInitDecoderConfig(&decoder_config)) {
        goto End;
    }
    webp_status = WebPGetFeatures(webp_data.bytes, webp_data.size, &decoder_config.input);
    if (webp_status != VP8_STATUS_OK) {
        goto End;
    }
    if (!WebPAnimDecoderGetInfo(dec, &anim_info)) {
        fprintf(stderr, "Error getting global info about the animation\n");
        goto End;
    }
    while (WebPAnimDecoderHasMoreFrames(dec)) {
        //DecodedFrame* curr_frame;
        uint8_t* frame_rgba;

        if (!WebPAnimDecoderGetNext(dec, &frame_rgba, &timestamp)) {
            fprintf(stderr, "Error decoding frame #%u\n", frame_index);
            goto End;
        }
        // assert(frame_index < anim_info.frame_count);
        duration = timestamp - prev_frame_timestamp;
        
        ncnn::Mat* inimage = new ncnn::Mat();
        inimage->create(anim_info.canvas_width, anim_info.canvas_height, (size_t)kNumChannels, kNumChannels);
        memcpy(inimage->data, frame_rgba,
            anim_info.canvas_width * kNumChannels * anim_info.canvas_height);

        v.inImage.push_back(inimage);
        v.inFrame.push_back(duration);

        ++frame_index;
        prev_frame_timestamp = timestamp;
    }
    if (v.save_format.length() == 0) v.save_format = "webp";
    v.load_format = "webp";
    ok = true;
End:
    WebPAnimDecoderDelete(dec);
    return ok;
}

//bool webp_save_ani2(Task& v)
//{
//    const char* output = NULL;
//    WebPAnimEncoder* enc = NULL;
//    int verbose = 0;
//    int pic_num = 0;
//    int duration = 100;
//    int timestamp_ms = 0;
//    int loop_count = 0;
//    int width = 0, height = 0;
//    WebPAnimEncoderOptions anim_config;
//    WebPConfig config;
//    WebPPicture pic;
//    WebPData webp_data;
//    int c;
//    int have_input = 0;
//    CommandLineArguments cmd_args;
//    int ok;
//
//    config.lossless = 1;
//    enc = WebPAnimEncoderNew(width, height, &anim_config);
//    ok = WebPAnimEncoderAdd(enc, &pic, timestamp_ms, &config);
//}
unsigned int webp_encode(ncnn::Mat& outimage, unsigned int toW, unsigned int toH, int webp_quality, uint8_t** out)
{
    unsigned int len = 0;
    if (outimage.elemsize != 3 && outimage.elemsize != 4)
    {
        goto RETURN;
    }
    if (outimage.w != toW || outimage.h != toH)
    {
        WebPConfig config;
        if (!WebPConfigPreset(&config, WEBP_PRESET_PHOTO, webp_quality)) {
            return 0;   // version error
        }
        // ... additional tuning
        config.lossless = !!0;

        // Setup the input data
        WebPPicture pic;
        if (!WebPPictureInit(&pic)) {
            return 0;  // version error
        }
        pic.use_argb = !!0;
        pic.width = outimage.w;
        pic.height = outimage.h;
        // allocated picture of dimension width x height
        if (!WebPPictureAlloc(&pic)) {
            goto RETURN;
        }

        WebPMemoryWriter wrt;
        WebPMemoryWriterInit(&wrt);     // initialize 'wrt'

        pic.writer = WebPMemoryWrite;
        pic.custom_ptr = &wrt;

        // Compress!
        if (outimage.elemsize == 3)
        {
            WebPPictureImportRGB(&pic, (unsigned char*)outimage.data, outimage.w * (int)outimage.elemsize);
        }
        else if (outimage.elemsize == 4) {
            WebPPictureImportRGBA(&pic, (unsigned char*)outimage.data, outimage.w * (int)outimage.elemsize);
        }
        WebPPictureRescale(&pic, toW, toH);

        int ok = WebPEncode(&config, &pic);   // ok = 0 => error occurred!
        WebPPictureFree(&pic);  // must be called independently of the 'ok' result.

        if (!ok) {
            WebPMemoryWriterClear(&wrt);
            goto RETURN;
        }
        *out = wrt.mem;
        len = wrt.size;
    }
    else {
        if (outimage.elemsize == 3)
        {
            len = WebPEncodeRGB((unsigned char*)outimage.data, outimage.w, outimage.h, outimage.w * (int)outimage.elemsize, webp_quality, out);
        }
        else if (outimage.elemsize == 4)
        {
            len = WebPEncodeRGBA((unsigned char*)outimage.data, outimage.w, outimage.h, outimage.w * (int)outimage.elemsize, webp_quality, out);
        }
    }
RETURN:
    return len;
}

bool webp_save_ani(Task &v)
{
    bool ok = false;
    WebPMuxError err;

    WebPMuxFrameInfo frame;
    frame.x_offset = 0;
    frame.y_offset = 0;
    frame.id = WEBP_CHUNK_ANMF;
    frame.dispose_method = WEBP_MUX_DISPOSE_NONE;
    frame.blend_method = WEBP_MUX_NO_BLEND;
    
    uint8_t* outb = 0;
    size_t size = 0;
    WebPMuxAnimParams params;
    WebPData outputData;
    std::list<int>::iterator j;
    std::list<ncnn::Mat*>::iterator i;
    WebPMux* mux = WebPMuxNew();
    if (!mux) {
        goto End;
    }
    for (i = v.outImage.begin(), j = v.inFrame.begin(); i != v.outImage.end(); i++, j++)
    {
        ncnn::Mat & inimage = **i;
        int duration= *j;
        size = webp_encode(inimage, v.toW, v.toH, v.webp_quality, &outb);

        const WebPData webp_data = { outb, size };
        frame.duration = duration;
        frame.bitstream = webp_data;
        err = WebPMuxPushFrame(mux, &frame, 0);
        if(err != WEBP_MUX_OK)
        {
            goto End;
        }
    }
    err = WebPMuxSetCanvasSize(mux, v.toW, v.toH);
    if (err != WEBP_MUX_OK) goto End;

    params.bgcolor = 0;
    params.loop_count = 0;
    WebPMuxSetAnimationParams(mux, &params);
    err = WebPMuxAssemble(mux, &outputData);
    if (err != WEBP_MUX_OK) {
        goto End;
    }

    v.out = (void *)outputData.bytes;
    v.outSize = (int)outputData.size;
    ok = true;
End:
    WebPMuxDelete(mux);
    return ok;
}

bool webp_save(Task &v)
{
    bool ok = false;

    size_t length = 0;
    unsigned char* output = 0;
    ncnn::Mat& outimage = **v.outImage.begin();
    if (outimage.elemsize != 3 && outimage.elemsize != 4)
    {
        goto RETURN;
    }
    length = webp_encode(outimage, v.toW, v.toH, v.webp_quality, &output);
    if (length == 0)
        goto RETURN;

    v.out = (void*)output;
    v.outSize = (int)length;
    ok = true;
    
RETURN:
    return ok;
}

#endif // WEBP_IMAGE_H
