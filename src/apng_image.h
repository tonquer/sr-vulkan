#ifndef APNG_IMAGE_H
#define APNG_IMAGE_H
// #include <pngpriv.h>
#include <png.h>
// #include <pngstruct.h>
#include "waifu2x_main.h"

//unsigned char dispose(int a, int b, unsigned char* pPrev, int n, int xres, int yres, int bpp, int w0, int h0, int x0, int y0, int* w1, int* h1, int* x1, int* y1)
//{
//    int  i, j, k, diff, area1, area2, area3;
//    int  x_min, x_max, y_min, y_max;
//    unsigned char op;
//
//    unsigned char* pImg = Frame[a].p;
//    unsigned char* pNext = Frame[b].p;
//
//    // NONE
//    x_min = xres - 1;
//    x_max = 0;
//    y_min = yres - 1;
//    y_max = 0;
//
//    for (j = 0; j < yres; j++)
//        for (i = 0; i < xres; i++)
//        {
//            diff = 0;
//            for (k = 0; k < bpp; k++)
//                if (*(pImg + (j * xres + i) * bpp + k) != *(pNext + (j * xres + i) * bpp + k))
//                    diff = 1;
//
//            if (diff == 1)
//            {
//                if (i < x_min) x_min = i;
//                if (i > x_max) x_max = i;
//                if (j < y_min) y_min = j;
//                if (j > y_max) y_max = j;
//            }
//        }
//
//    if ((x_max < x_min) || (y_max < y_min))
//    {
//        *w1 = 1; *h1 = 1;
//        *x1 = 0; *y1 = 0;
//        return PNG_DISPOSE_OP_NONE;
//    }
//
//    area1 = (x_max - x_min + 1) * (y_max - y_min + 1);
//    *w1 = x_max - x_min + 1;
//    *h1 = y_max - y_min + 1;
//    *x1 = x_min;
//    *y1 = y_min;
//    op = PNG_DISPOSE_OP_NONE;
//
//    if (a == 0)
//        return op;
//
//    // PREVIOUS
//    x_min = xres - 1;
//    x_max = 0;
//    y_min = yres - 1;
//    y_max = 0;
//
//    for (j = 0; j < yres; j++)
//        for (i = 0; i < xres; i++)
//        {
//            diff = 0;
//
//            for (k = 0; k < bpp; k++)
//                if (*(pPrev + (j * xres + i) * bpp + k) != *(pNext + (j * xres + i) * bpp + k))
//                    diff = 1;
//
//            if (diff == 1)
//            {
//                if (i < x_min) x_min = i;
//                if (i > x_max) x_max = i;
//                if (j < y_min) y_min = j;
//                if (j > y_max) y_max = j;
//            }
//        }
//
//    if ((x_max < x_min) || (y_max < y_min))
//    {
//        *w1 = 1; *h1 = 1;
//        *x1 = 0; *y1 = 0;
//        return PNG_DISPOSE_OP_PREVIOUS;
//    }
//
//    area2 = (x_max - x_min + 1) * (y_max - y_min + 1);
//    if (area2 < area1)
//    {
//        area1 = area2;
//        *w1 = x_max - x_min + 1;
//        *h1 = y_max - y_min + 1;
//        *x1 = x_min;
//        *y1 = y_min;
//        op = PNG_DISPOSE_OP_PREVIOUS;
//    }
//
//    // BACKGROUND
//    if (bpp == 4)
//    {
//        x_min = xres - 1;
//        x_max = 0;
//        y_min = yres - 1;
//        y_max = 0;
//
//        for (j = 0; j < yres; j++)
//            for (i = 0; i < xres; i++)
//            {
//                diff = 0;
//
//                if ((i >= x0) && (i < x0 + w0) && (j >= y0) && (j < y0 + h0))
//                {
//                    for (k = 0; k < bpp; k++)
//                        if (*(pNext + (j * xres + i) * bpp + k) != 0)
//                            diff = 1;
//                }
//                else
//                {
//                    for (k = 0; k < bpp; k++)
//                        if (*(pImg + (j * xres + i) * bpp + k) != *(pNext + (j * xres + i) * bpp + k))
//                            diff = 1;
//                }
//
//                if (diff == 1)
//                {
//                    if (i < x_min) x_min = i;
//                    if (i > x_max) x_max = i;
//                    if (j < y_min) y_min = j;
//                    if (j > y_max) y_max = j;
//                }
//            }
//
//        if ((x_max < x_min) || (y_max < y_min))
//        {
//            *w1 = 1; *h1 = 1;
//            *x1 = 0; *y1 = 0;
//            return PNG_DISPOSE_OP_BACKGROUND;
//        }
//
//        area3 = (x_max - x_min + 1) * (y_max - y_min + 1);
//        if (area3 < area1)
//        {
//            area1 = area3;
//            *w1 = x_max - x_min + 1;
//            *h1 = y_max - y_min + 1;
//            *x1 = x_min;
//            *y1 = y_min;
//            op = PNG_DISPOSE_OP_BACKGROUND;
//        }
//    }
//    return op;
//}
void apng_write_fn(png_structp png_ptr, unsigned char * data, size_t len)
{
    Task *v = (Task *) png_get_io_ptr(png_ptr);
    if (len <= 0) return;
    if(v->outSize + (int)len > v->allOutSize)
    {
        v->allOutSize = v->outSize + std::max(1024, (int)len*2);
        v->out = realloc(v->out, v->allOutSize);
    }
    memcpy((unsigned char *)v->out + v->outSize, data, len);
    v->outSize += (int)len;
}

void apng_read_fn(png_structp png_ptr, unsigned char* data, size_t len)
{
    Task* v = (Task*)png_get_io_ptr(png_ptr);
    if (len <= 0) return;
    if (v->allFileSize + (int)len > v->fileSize)
    {
        return;
    }
    memcpy(data, (unsigned char *)v->fileDate + v->allFileSize, len);
    v->allFileSize += (int)len;
}

void BlendOver(unsigned char** rows_dst, unsigned char** rows_src, unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    unsigned int i, j;
    int u, v, al;

    for (j = 0; j < h; j++)
    {
        unsigned char* sp = rows_src[j];
        unsigned char* dp = rows_dst[j + y] + x * 4;

        for (i = 0; i < w; i++, sp += 4, dp += 4)
        {
            if (sp[3] == 255)
                memcpy(dp, sp, 4);
            else if (sp[3] != 0)
            {
                if (dp[3] != 0)
                {
                    u = sp[3] * 255;
                    v = (255 - sp[3]) * dp[3];
                    al = u + v;
                    dp[0] = (sp[0] * u + dp[0] * v) / al;
                    dp[1] = (sp[1] * u + dp[1] * v) / al;
                    dp[2] = (sp[2] * u + dp[2] * v) / al;
                    dp[3] = al / 255;
                }
                else
                    memcpy(dp, sp, 4);
            }
        }
    }
}

bool MakeFrame(unsigned char** rows, unsigned int w, unsigned int h, unsigned int channels, unsigned int m_nRowSize, ncnn::Mat& inimage)
{
    inimage.create(w, h, (size_t)channels, channels);

    unsigned char * pBuf = (unsigned char*)inimage.data;
    for (unsigned int i = 0; i < h; ++i)
    {
        memcpy(pBuf, rows[i], m_nRowSize);
        pBuf += m_nRowSize;
    }

    return true;
}

bool load_apng(Task& v)
{
    bool ok = false;
    png_structp png_ptr = 0;
    png_infop info_ptr = 0;
    unsigned int nFrameCount = 1;
    unsigned int m_nPlays = 0;
    unsigned int m_nWidth, m_nHeight, m_nChannels, m_nRowSize, m_nFrameSize;
    unsigned char*p_image, *p_frame, *p_temp;
    png_bytepp rows_image, rows_frame;
    png_byte m_bFirstFrameIsHidden;
    unsigned int i, j;
    unsigned char sig[8];


    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) goto End;

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) goto End;

    if (setjmp(png_jmpbuf(png_ptr))) goto End;

    png_set_read_fn(png_ptr, (void*)(&v), &apng_read_fn);

    apng_read_fn(png_ptr, sig, 8);
    if (png_sig_cmp(sig, 0, 8) != 0) goto End;

    png_set_sig_bytes(png_ptr, 8);
    png_read_info(png_ptr, info_ptr);

    if (!png_get_valid(png_ptr, info_ptr, PNG_INFO_acTL)) goto End;
    
    png_get_acTL(png_ptr, info_ptr, &nFrameCount, &m_nPlays);
    if (nFrameCount <= 1) goto End;

    png_set_expand(png_ptr);
    png_set_strip_16(png_ptr);
    png_set_gray_to_rgb(png_ptr);
    png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);
    png_set_interlace_handling(png_ptr);
    png_read_update_info(png_ptr, info_ptr);

    m_nWidth = png_get_image_width(png_ptr, info_ptr);
    m_nHeight = png_get_image_height(png_ptr, info_ptr);
    m_nChannels = png_get_channels(png_ptr, info_ptr);
    m_nRowSize = (int)png_get_rowbytes(png_ptr, info_ptr);
    
    m_nFrameSize = m_nHeight * m_nRowSize;
    p_image = (unsigned char*)malloc(m_nFrameSize);
    p_frame = (unsigned char*)malloc(m_nFrameSize);
    p_temp = (unsigned char*)malloc(m_nFrameSize);
    rows_image = (png_bytepp)malloc(m_nHeight * sizeof(png_bytep));
    rows_frame = (png_bytepp)malloc(m_nHeight * sizeof(png_bytep));
    m_bFirstFrameIsHidden = png_get_first_frame_is_hidden(png_ptr, info_ptr) != 0;

    if (p_image && p_frame && p_temp && rows_image && rows_frame)
    {
        png_uint_32 x0 = 0;
        png_uint_32 y0 = 0;
        png_uint_32 w0 = m_nWidth;
        png_uint_32 h0 = m_nHeight;
        unsigned short delay_num = 1;
        unsigned short delay_den = 10;
        unsigned char dop = 0;
        unsigned char bop = 0;
        unsigned int first = (m_bFirstFrameIsHidden != 0) ? 1 : 0;

        for (j = 0; j < m_nHeight; j++)
            rows_image[j] = p_image + j * m_nRowSize;

        for (j = 0; j < m_nHeight; j++)
            rows_frame[j] = p_frame + j * m_nRowSize;

        for (i = 0; i < nFrameCount; i++)
        {
            png_read_frame_head(png_ptr, info_ptr);
            png_get_next_frame_fcTL(png_ptr, info_ptr, &w0, &h0, &x0, &y0, &delay_num, &delay_den, &dop, &bop);

            if (i == first)
            {
                bop = PNG_BLEND_OP_SOURCE;
                if (dop == PNG_DISPOSE_OP_PREVIOUS)
                    dop = PNG_DISPOSE_OP_BACKGROUND;
            }

            png_read_image(png_ptr, rows_frame);

            if (dop == PNG_DISPOSE_OP_PREVIOUS)
                memcpy(p_temp, p_image, m_nFrameSize);

            if (bop == PNG_BLEND_OP_OVER)
            {
                BlendOver(rows_image, rows_frame, x0, y0, w0, h0);
            }
            else
            {
                for (j = 0; j < h0; j++)
                    memcpy(rows_image[j + y0] + x0 * 4, rows_frame[j], w0 * 4);
            }
            ncnn::Mat* inimage = new ncnn::Mat();
            inimage->create(m_nWidth, m_nHeight, (size_t)m_nChannels, m_nChannels);

            MakeFrame(rows_image, m_nWidth, m_nHeight, m_nChannels, m_nRowSize, *inimage);

            v.inImage.push_back(inimage);
            v.inFrame.push_back(delay_num * 1000 / std::max(100, (int)delay_den));

            if (dop == PNG_DISPOSE_OP_PREVIOUS)
            {
                memcpy(p_image, p_temp, m_nFrameSize);
            }
            else if (dop == PNG_DISPOSE_OP_BACKGROUND)
            {
                for (j = 0; j < h0; j++)
                    memset(rows_image[j + y0] + x0 * 4, 0, w0 * 4);
            }
        }

        png_read_end(png_ptr, info_ptr);
        free(rows_frame);
        free(rows_image);
        free(p_temp);
        free(p_frame);
        free(p_image);
    }
    ok = true;
    if (v.save_format.length() == 0) v.save_format = "apng";
    v.load_format = "apng";
End:

    if (png_ptr)
    {
        if (info_ptr) { png_destroy_read_struct(&png_ptr, &info_ptr, (png_infopp)NULL); }
        else { png_destroy_read_struct(&png_ptr, NULL, NULL); }
    }
    return ok;
}
bool save_apng(Task& v)
{
    bool ok = false;
    int      a = 0, k;
    int      x0, y0, h0, w0;
    unsigned char dispose_op = PNG_DISPOSE_OP_NONE;
    png_structp png_ptr = 0;
    png_infop info_ptr = 0;
    std::list<int>::iterator j;
    std::list<ncnn::Mat*>::iterator i;
    ncnn::Mat& outimage = **v.outImage.begin();
    int xres = outimage.w;
    int yres = outimage.h;
    int bpp = (int)outimage.elemsize;
    int n = (int)v.inFrame.size();
    unsigned char first = 0;
    unsigned char* pDisp = 0;
    png_bytep* row_pointers = 0;

    png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

    if (!png_ptr) goto End;
    info_ptr = png_create_info_struct(png_ptr);

    if (!info_ptr) goto End;
    if (setjmp(png_jmpbuf(png_ptr))) goto End;
    v.allOutSize = xres * yres * bpp;
    v.out = (unsigned char*)malloc(v.allOutSize);

    png_set_write_fn(png_ptr, (void *)(&v), &apng_write_fn, NULL);

    //png_init_io(png_ptr, f);
    png_set_IHDR(png_ptr, info_ptr, xres, yres, 8,
        (bpp == 4) ? PNG_COLOR_TYPE_RGB_ALPHA : (bpp == 3) ? PNG_COLOR_TYPE_RGB : PNG_COLOR_TYPE_GRAY,
        PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

    png_set_acTL(png_ptr, info_ptr, n, 0);
    png_set_first_frame_is_hidden(png_ptr, info_ptr, first);
    png_write_info(png_ptr, info_ptr);

    row_pointers = (png_bytepp)png_malloc(png_ptr, sizeof(png_bytep) * yres);

    w0 = xres;
    h0 = yres;
    x0 = 0;
    y0 = 0;


    for (i = v.outImage.begin(), j = v.inFrame.begin(); i != v.outImage.end(); i++, j++)
    {
        ncnn::Mat& outmage = **i;
        //png_set_bgr(png_ptr);

        dispose_op = PNG_DISPOSE_OP_NONE;

        for (k = 0; k < h0; k++)
            row_pointers[k] = (unsigned char*)outmage.data + ((k + y0) * xres + x0) * bpp;

        png_write_frame_head(png_ptr, info_ptr, row_pointers, outimage.w, outimage.h, 0, 0,
            *j, 1000, dispose_op, PNG_BLEND_OP_SOURCE);
        png_write_image(png_ptr, row_pointers);
        png_write_frame_tail(png_ptr, info_ptr);

        a++;
    }
    png_write_end(png_ptr, info_ptr);
    ok = true;

End:
    if (pDisp) { free(pDisp); }
    if (row_pointers){ free(row_pointers); }

    if (png_ptr)
    {
        if(info_ptr) { png_destroy_write_struct(&png_ptr, &info_ptr);}
        else { png_destroy_write_struct(&png_ptr, (png_infopp)NULL); }
    }
    return ok;
        
}

#endif