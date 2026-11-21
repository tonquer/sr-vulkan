#ifndef COMMON_IMAGE_H
#define COMMON_IMAGE_H
#include <cstring>
#include <stdint.h>
#include <stdlib.h>

static void write_jpg_to_mem(void* writeData, void* data, int size)
{
    WriteData* write = static_cast<WriteData*>(writeData);
    if (!write || write->failed || !data || size <= 0)
        return;

    size_t add_size = static_cast<size_t>(size);
    if (add_size > SIZE_MAX - write->writeSize)
    {
        write->failed = true;
        return;
    }

    size_t needed = write->writeSize + add_size;
    if (!write->data || write->capacity < needed)
    {
        size_t new_capacity = write->capacity == 0 ? 4096 : write->capacity;
        while (new_capacity < needed)
        {
            if (new_capacity > SIZE_MAX / 2)
            {
                new_capacity = needed;
                break;
            }
            new_capacity *= 2;
        }
        void* new_ptr = std::realloc(write->data, new_capacity);
        if (!new_ptr)
        {
            write->failed = true;
            return;
        }
        write->data = static_cast<uint8_t*>(new_ptr);
        write->capacity = new_capacity;
    }
    std::memcpy(static_cast<unsigned char*>(write->data) + write->writeSize, data, add_size);
    write->writeSize = needed;
}

static int parse_tiff_exif_orientation(const uint8_t* tiff, size_t tiffLen)
{
    // TIFF header is at least 8 bytes ("II*\0" or "MM\0*" + 4-byte IFD offset)
    if (!tiff || tiffLen < 8)
        return 1;

    bool isLE = (tiff[0] == 'I' && tiff[1] == 'I');
    if (!isLE && !(tiff[0] == 'M' && tiff[1] == 'M'))
        return 1;

    auto read16 = [&](size_t offset) -> uint16_t {
        if (offset > tiffLen || tiffLen - offset < 2)
            return 0;
        return isLE
            ? static_cast<uint16_t>(static_cast<uint32_t>(tiff[offset]) | (static_cast<uint32_t>(tiff[offset + 1]) << 8))
            : static_cast<uint16_t>((static_cast<uint32_t>(tiff[offset]) << 8) | static_cast<uint32_t>(tiff[offset + 1]));
    };
    auto read32 = [&](size_t offset) -> uint32_t {
        if (offset > tiffLen || tiffLen - offset < 4)
            return 0;
        return isLE
            ? (static_cast<uint32_t>(tiff[offset]) |
               (static_cast<uint32_t>(tiff[offset + 1]) << 8) |
               (static_cast<uint32_t>(tiff[offset + 2]) << 16) |
               (static_cast<uint32_t>(tiff[offset + 3]) << 24))
            : ((static_cast<uint32_t>(tiff[offset]) << 24) |
               (static_cast<uint32_t>(tiff[offset + 1]) << 16) |
               (static_cast<uint32_t>(tiff[offset + 2]) << 8) |
               static_cast<uint32_t>(tiff[offset + 3]));
    };

    // Validate TIFF magic 42 (0x002A) and first IFD offset
    if (read16(2) != 0x002A)
        return 1;
    size_t ifdOffset = static_cast<size_t>(read32(4));
    if (ifdOffset > tiffLen || tiffLen - ifdOffset < 2)
        return 1;

    // Scan IFD entries for Orientation tag (0x0112)
    uint16_t numEntries = read16(ifdOffset);
    for (uint16_t i = 0; i < numEntries; ++i)
    {
        size_t entryOffset = ifdOffset + 2 + static_cast<size_t>(i) * 12;
        if (entryOffset > tiffLen || tiffLen - entryOffset < 12)
            break;

        uint16_t tag = read16(entryOffset);
        if (tag == 0x0112)
        {
            uint16_t val = read16(entryOffset + 8);
            return (val >= 1 && val <= 8) ? val : 1;
        }
    }
    return 1;
}

static int get_exif_orientation(const uint8_t* rawExifData, size_t len)
{
    if (!rawExifData || len < 8)
        return 1;

    // Skip optional JPEG-style 6-byte "Exif\0\0" prefix
    if (len >= 14 && std::memcmp(rawExifData, "Exif\0\0", 6) == 0)
        return parse_tiff_exif_orientation(rawExifData + 6, len - 6);

    // Pure TIFF header (PNG eXIf, WebP EXIF)
    return parse_tiff_exif_orientation(rawExifData, len);
}

template <typename MapFunc>
static void copy_transformed_pixels(const uint8_t* src, uint8_t* dst,
    int w, int h, int nw, int channels,
    MapFunc map_coord)
{
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            int nx = 0;
            int ny = 0;
            map_coord(x, y, nx, ny);
            std::memcpy(dst + (static_cast<size_t>(ny) * static_cast<size_t>(nw) + static_cast<size_t>(nx)) * static_cast<size_t>(channels),
                src + (static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)) * static_cast<size_t>(channels),
                static_cast<size_t>(channels));
        }
    }
}

static uint8_t* apply_orientation(const uint8_t* src, int& w, int& h, int channels, int orientation)
{
    if (!src || w <= 0 || h <= 0 || channels <= 0)
        return nullptr;

    if (orientation <= 1 || orientation > 8)
        return nullptr;

    bool swapWH = (orientation >= 5 && orientation <= 8);
    int nw = swapWH ? h : w;
    int nh = swapWH ? w : h;

    size_t rowBytes = static_cast<size_t>(nw) * static_cast<size_t>(channels);
    if (rowBytes == 0 || static_cast<size_t>(nh) > SIZE_MAX / rowBytes)
        return nullptr;

    size_t totalBytes = rowBytes * static_cast<size_t>(nh);
    uint8_t* dst = static_cast<uint8_t*>(std::malloc(totalBytes));
    if (!dst)
        return nullptr;

    // Fast path for vertical flip (4): row-wise memcpy
    if (orientation == 4)
    {
        size_t srcRowBytes = static_cast<size_t>(w) * static_cast<size_t>(channels);
        for (int y = 0; y < h; ++y)
        {
            std::memcpy(dst + static_cast<size_t>(h - 1 - y) * srcRowBytes,
                src + static_cast<size_t>(y) * srcRowBytes,
                srcRowBytes);
        }
        return dst;
    }

    switch (orientation)
    {
    case 2:
        copy_transformed_pixels(src, dst, w, h, nw, channels,
            [w](int x, int y, int& nx, int& ny) { nx = w - 1 - x; ny = y; });
        break;
    case 3:
        copy_transformed_pixels(src, dst, w, h, nw, channels,
            [w, h](int x, int y, int& nx, int& ny) { nx = w - 1 - x; ny = h - 1 - y; });
        break;
    case 5:
        copy_transformed_pixels(src, dst, w, h, nw, channels,
            [](int x, int y, int& nx, int& ny) { nx = y; ny = x; });
        break;
    case 6:
        copy_transformed_pixels(src, dst, w, h, nw, channels,
            [h](int x, int y, int& nx, int& ny) { nx = h - 1 - y; ny = x; });
        break;
    case 7:
        copy_transformed_pixels(src, dst, w, h, nw, channels,
            [w, h](int x, int y, int& nx, int& ny) { nx = h - 1 - y; ny = w - 1 - x; });
        break;
    case 8:
        copy_transformed_pixels(src, dst, w, h, nw, channels,
            [w](int x, int y, int& nx, int& ny) { nx = y; ny = w - 1 - x; });
        break;
    }

    w = nw;
    h = nh;
    return dst;
}

#endif // COMMON_IMAGE_H
