#ifndef WIC_IMAGE_H
#define WIC_IMAGE_H

// image decoder and encoder with WIC
#include <wincodec.h>
#include <string>
#include <iostream>
#include <stdio.h>
#include "crc32.h"

template <typename T>
inline void ReverseBytes(T& val)
{
    const T copyVal = val;

    unsigned char* ptr = reinterpret_cast<unsigned char*>(&val);
    const unsigned char* copyPtr = reinterpret_cast<const unsigned char*>(&copyVal);
    for (size_t i = 0; i < sizeof(T); ++i)
        ptr[i] = copyPtr[sizeof(T) - i - 1];
}

unsigned char* wic_decode_image_by_data(const unsigned char * fileData, unsigned int fileSize, int* w, int* h, int* c)
{
    IWICImagingFactory* factory = 0;
    IWICBitmapDecoder* decoder = 0;
    IWICBitmapFrameDecode* frame = 0;
    WICPixelFormatGUID pixel_format;
    IWICFormatConverter* converter = 0;
    IWICBitmap* bitmap = 0;
    IWICBitmapLock* lock = 0;
    IWICStream* pStream;
    int width = 0;
    int height = 0;
    int channels = 0;
    WICRect rect = { 0, 0, 0, 0 };
    unsigned int datasize = 0;
    unsigned char* data = 0;
    int stride = 0;
    unsigned char* bgrdata = 0;

    if (CoCreateInstance(CLSID_WICImagingFactory1, 0, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        goto RETURN;

    if (factory->CreateStream(&pStream))
    {
        goto RETURN;
    }
    if (pStream->InitializeFromMemory((BYTE*)fileData, fileSize))
    {
        goto RETURN;
    }

    if (factory->CreateDecoderFromStream(pStream, NULL, WICMETADATACACHEOPTION_FORCE_DWORD, &decoder))
    {
        goto RETURN;
    }

    if (decoder->GetFrame(0, &frame))
        goto RETURN;

    if (factory->CreateFormatConverter(&converter))
        goto RETURN;

    if (frame->GetPixelFormat(&pixel_format))
        goto RETURN;

    if (!IsEqualGUID(pixel_format, GUID_WICPixelFormat32bppBGRA))
        pixel_format = GUID_WICPixelFormat24bppBGR;

    channels = IsEqualGUID(pixel_format, GUID_WICPixelFormat32bppBGRA) ? 4 : 3;

    if (converter->Initialize(frame, pixel_format, WICBitmapDitherTypeNone, 0, 0.0, WICBitmapPaletteTypeCustom))
        goto RETURN;

    if (factory->CreateBitmapFromSource(converter, WICBitmapCacheOnDemand, &bitmap))
        goto RETURN;

    if (bitmap->GetSize((UINT*)&width, (UINT*)&height))
        goto RETURN;

    rect.Width = width;
    rect.Height = height;
    if (bitmap->Lock(&rect, WICBitmapLockRead, &lock))
        goto RETURN;
    
    if (lock->GetDataPointer(&datasize, &data))
        goto RETURN;

    if (lock->GetStride((UINT*)&stride))
        goto RETURN;

    bgrdata = (unsigned char*)malloc(width * height * channels);
    if (!bgrdata)
        goto RETURN;

    for (int y = 0; y < height; y++)
    {
        const unsigned char* ptr = data + y * stride;
        unsigned char* bgrptr = bgrdata + y * width * channels;
        memcpy(bgrptr, ptr, width * channels);
    }

    *w = width;
    *h = height;
    *c = channels;

RETURN:
    if (lock) lock->Release();
    if (bitmap) bitmap->Release();
    if (decoder) decoder->Release();
    if (frame) frame->Release();
    if (converter) converter->Release();
    if (factory) factory->Release();

    return bgrdata;
}

int wic_encode_image_to_data(int w, int h, int c, void* bgrdata, void*& out, int& outSize, unsigned long modelIndex, unsigned long toW, unsigned long toH, unsigned long oldW, unsigned long oldH)
{
    IWICImagingFactory* factory = 0;
    IWICStream* stream = 0;
    IWICBitmapEncoder* encoder = 0;
    IWICBitmapFrameEncode* frame = 0;
    IWICBitmap *bitmap2 = 0;
    IWICBitmapScaler* scale = 0;
    WICPixelFormatGUID format = c == 4 ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR;
    int stride = (w * c * 8 + 7) / 8;
    unsigned char* data = 0;
    int ret = 0;
    IStream* streamIn;
    CreateStreamOnHGlobal(NULL, true, &streamIn);

    if (CoCreateInstance(CLSID_WICImagingFactory1, 0, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        goto RETURN;

    if (factory->CreateStream(&stream))
        goto RETURN;

    if (stream->InitializeFromIStream(streamIn))
        goto RETURN;

    if (factory->CreateEncoder(GUID_ContainerFormatPng, 0, &encoder))
        goto RETURN;

    if (encoder->Initialize(stream, WICBitmapEncoderNoCache))
        goto RETURN;

    if (encoder->CreateNewFrame(&frame, 0))
        goto RETURN;

    if (frame->Initialize(0))
        goto RETURN;

    if (factory->CreateBitmapScaler(&scale))
        goto RETURN;

    if (factory->CreateBitmapFromMemory(w, h, format, stride, stride * h, (BYTE*)bgrdata, &bitmap2))
        goto RETURN;

    if (scale->Initialize(bitmap2, toW, toH, WICBitmapInterpolationModeFant))
        goto RETURN;

    if (frame->SetSize((UINT)toW, (UINT)toH))
        goto RETURN;

    if (frame->SetPixelFormat(&format))
        goto RETURN;

    if (!IsEqualGUID(format, c == 4 ? GUID_WICPixelFormat32bppBGRA : GUID_WICPixelFormat24bppBGR))
        goto RETURN;
    
    //if (frame->WritePixels(h, stride, h * stride, (BYTE *)bgrdata))
        //goto RETURN;
    if (frame->WriteSource(scale, NULL))
        goto RETURN;

    if (frame->Commit())
        goto RETURN;

    if (encoder->Commit())
        goto RETURN;
    
    ret = 1;

RETURN:
    if (ret == 1) {
        STATSTG sts;
        stream->Stat(&sts, STATFLAG_DEFAULT);
        ULARGE_INTEGER uli = sts.cbSize;
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        outSize = (ULONG)uli.QuadPart;

        out = malloc(outSize);
        ULONG written;
        stream->Seek(zero, STREAM_SEEK_SET, NULL);
        stream->Read(out, outSize, &written);
           
    }
    if (scale) scale->Release();
    if (bitmap2) bitmap2->Release();
    if (data) free(data);
    if (encoder) encoder->Release();
    if (frame) frame->Release();
    if (stream) stream->Release();
    if (streamIn) streamIn->Release();
    if (factory) factory->Release();

    return ret;
}
int wic_encode_jpeg_image_to_data(int w, int h, int c, void* bgrdata, void *& out, int & outSize, unsigned long modelIndex, unsigned long toW, unsigned long toH, unsigned long oldW, unsigned long oldH)
{
    // assert c == 3

    IWICImagingFactory* factory = 0;
    IWICStream* stream = 0;
    IWICBitmapEncoder* encoder = 0;
    IWICBitmapFrameEncode* frame = 0;
    IPropertyBag2* propertybag = 0;
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    IStream *streamData;
    IWICBitmap* bitmap2 = 0;
    IWICBitmapScaler* scale = 0;
    CreateStreamOnHGlobal(NULL, true, &streamData);
    int stride = (w * c * 8 + 7) / 8;
    int ret = 0;

    PROPBAG2 option = { 0 };
    option.pstrName = L"ImageQuality";
    VARIANT varValue;
    VariantInit(&varValue);
    varValue.vt = VT_R4;
    varValue.fltVal = 1.0f;
    unsigned long size = h * stride;
    IStream *streamIn;
    CreateStreamOnHGlobal(NULL, true, &streamIn);

    if (CoCreateInstance(CLSID_WICImagingFactory1, 0, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))
        goto RETURN;

    if (factory->CreateStream(&stream))
        goto RETURN;

    if (stream->InitializeFromIStream(streamIn))
        goto RETURN;

    if (factory->CreateEncoder(GUID_ContainerFormatJpeg, 0, &encoder))
        goto RETURN;

    if (encoder->Initialize(stream, WICBitmapEncoderNoCache))
        goto RETURN;

    if (encoder->CreateNewFrame(&frame, &propertybag))
        goto RETURN;

    if (propertybag->Write(1, &option, &varValue))
        goto RETURN;

    if (frame->Initialize(propertybag))
        goto RETURN;
    
    if (factory->CreateBitmapScaler(&scale))
        goto RETURN;

    if (factory->CreateBitmapFromMemory(w, h, format, stride, stride * h, (BYTE*)bgrdata, &bitmap2))
        goto RETURN;

    if (scale->Initialize(bitmap2, toW, toH, WICBitmapInterpolationModeFant))
        goto RETURN;

    if (frame->SetSize((UINT)toW, (UINT)toH))
        goto RETURN;

    if (frame->SetPixelFormat(&format))
        goto RETURN;

    if (!IsEqualGUID(format, GUID_WICPixelFormat24bppBGR))
        goto RETURN;

    //if (frame->WritePixels(h, stride, h * stride, (BYTE *)bgrdata))
        //goto RETURN;
    if (frame->WriteSource(scale, NULL))
        goto RETURN;

    if (frame->Commit())
        goto RETURN;

    if (encoder->Commit())
        goto RETURN;
    
    ret = 1;

RETURN:

    if (ret == 1) {
        STATSTG sts;
        stream->Stat(&sts, STATFLAG_DEFAULT);
        ULARGE_INTEGER uli = sts.cbSize;
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        outSize = (ULONG)uli.QuadPart;
        
        out = malloc(outSize);
        ULONG written;
        stream->Seek(zero, STREAM_SEEK_SET, NULL);
        stream->Read(out, outSize, &written);
    }
    if (scale) scale->Release();
    if (bitmap2) bitmap2->Release();
    if (encoder) encoder->Release();
    if (frame) frame->Release();
    if (propertybag) propertybag->Release();
    if (stream) stream->Release();
    if (streamIn) streamIn->Release();
    if (factory) factory->Release();

    return ret;
}

unsigned char* wic_decode_bmp_by_data(unsigned char* fileDate, int fileSize, int &w, int &h, int &c)
{
    w = *reinterpret_cast<unsigned int*>(fileDate + 18);
    h = *reinterpret_cast<unsigned int*>(fileDate + 22);
    c = *reinterpret_cast<unsigned short int*>(fileDate + 28) / 8;
    int pad = 0;
    if (c <= 3) {
        pad = (-w * 3) & 3;
    }
    unsigned char* out = (unsigned char*)malloc(w * h * c);
    for (int y = 0; y < h; y++)
    {
        const unsigned char* ptr = (unsigned char*)fileDate + 54 + (y * w * c + y * pad);
        unsigned char* bgrptr = (unsigned char*)out + y * w * c;
        memcpy(bgrptr, ptr, w * c);
    }
    return out;
}

int wic_encode_bmp_image_to_data(int w, int h, int c, void* bgrdata, void*& out, int& outSize)
{
    int picSize = (w * c * 8 + 31) / 32 * 4 * h;
    outSize = picSize + 54;
    if (!bgrdata)
        return -1;

    out = malloc(outSize);
    if (!out)
        return -1;

    char head[14] = "BM";
    char head2[40];
    memset(head2, 0, 40);

    int zero = 0;
    int headTotalSize = 54;
    int head2Szie = 40;
    short int biPlanes = 1;
    short int bitNum = c * 8;

    memcpy((unsigned char*)head + 2, (unsigned char*)&outSize, 4);
    memcpy((unsigned char*)head + 6, (unsigned char*)&zero, 4);
    memcpy((unsigned char*)head + 10, (unsigned char*)&headTotalSize, 4);

    memcpy((unsigned char*)head2, (unsigned char*)&head2Szie, 4);
    memcpy((unsigned char*)head2+4, (unsigned char*)&w, 4);
    memcpy((unsigned char*)head2+8, (unsigned char*)&h, 4);
    memcpy((unsigned char*)head2+12, (unsigned char*)&biPlanes, 2);
    memcpy((unsigned char*)head2+14, (unsigned char*)&bitNum, 2);
    memcpy((unsigned char*)head2+20, (unsigned char*)&picSize, 4);

    memcpy((unsigned char*)out, &head, 14);
    memcpy((unsigned char*)out+14, &head2, 40);
    //memcpy((unsigned char*)out+ 54, bgrdata, picSize);
    int pad = 0;
    if (c <= 3) {
        pad = (-w * 3) & 3;
    }
    for (int y = 0; y < h; y++)
    {
        const unsigned char* ptr = (unsigned char *)bgrdata + y * w * c;
        unsigned char* bgrptr = (unsigned char*)out + 54 + (y * w * c + y*pad);
        memcpy(bgrptr, ptr, w * c);
        if (pad > 0)
        {
            memcpy(bgrptr + w*c, &zero, pad);
        }
    }
    return 1;
}

#endif // WIC_IMAGE_H
