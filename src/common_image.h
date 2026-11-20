#ifndef COMMON_IMAGE_H
#define COMMON_IMAGE_H

static void write_jpg_to_mem(void* writeData, void* data, int size)
{
    WriteData* write = static_cast<WriteData*>(writeData);
    if (write->capacity < write->writeSize + size) {
        size_t new_capacity = write->capacity == 0 ? 4096 : write->capacity;
        
        while (new_capacity < write->writeSize + size) {
            new_capacity *= 2;
        }
        // 注意：必须先用临时指针接收 realloc 返回值！
        void* new_ptr = std::realloc(write->data, new_capacity);
        if (!new_ptr) {
            return;
        }
        write->data = static_cast<uint8_t*>(new_ptr);
        write->capacity = new_capacity;
    }
    memcpy((unsigned char*)write->data + write->writeSize, data, size);
    write->writeSize += size;
}

static int parse_tiff_exif_orientation(const uint8_t* tiff, size_t tiffLen) {
    // TIFF 头至少 8 字节 ("II*\0" 或 "MM\0*" + 4字节 IFD 偏移)
    if (!tiff || tiffLen < 8) return 1;

    // 【修正处】判断前两个字节："II" 为小端 (Little-Endian)，"MM" 为大端 (Big-Endian)
    bool isLE = (tiff[0] == 'I' && tiff[1] == 'I');
    if (!isLE && !(tiff[0] == 'M' && tiff[1] == 'M')) return 1;

    auto read16 = [&](size_t offset) -> uint16_t {
        if (offset + 2 > tiffLen) return 0;
        return isLE ? (tiff[offset] | (tiff[offset + 1] << 8))
                    : ((tiff[offset] << 8) | tiff[offset + 1]);
    };
    auto read32 = [&](size_t offset) -> uint32_t {
        if (offset + 4 > tiffLen) return 0;
        return isLE ? (tiff[offset] | (tiff[offset + 1] << 8) | (tiff[offset + 2] << 16) | (tiff[offset + 3] << 24))
                    : ((tiff[offset] << 24) | (tiff[offset + 1] << 16) | (tiff[offset + 2] << 8) | tiff[offset + 3]);
    };

    // 校验 TIFF 魔数 42 (0x002A) 并读取第一个 IFD 偏移量
    if (read16(2) != 0x002A) return 1;
    uint32_t ifdOffset = read32(4);
    if (ifdOffset + 2 > tiffLen) return 1;

    // 遍历 IFD 目录项查找 Orientation 标签 (0x0112)
    uint16_t numEntries = read16(ifdOffset);
    for (uint16_t i = 0; i < numEntries; ++i) {
        size_t entryOffset = ifdOffset + 2 + i * 12;
        if (entryOffset + 12 > tiffLen) break;

        uint16_t tag = read16(entryOffset);
        if (tag == 0x0112) { // 找到 Orientation 标签
            uint16_t val = read16(entryOffset + 8);
            return (val >= 1 && val <= 8) ? val : 1;
        }
    }
    return 1;
}

static int get_exif_orientation(const uint8_t* rawExifData, size_t len) {
    if (!rawExifData || len < 8) return 1;

    // 如果带有 JPEG 风格的 "Exif\0\0" 6字节前缀，自动跳过
    if (len >= 14 && std::memcmp(rawExifData, "Exif\0\0", 6) == 0) {
        return parse_tiff_exif_orientation(rawExifData + 6, len - 6);
    }

    // 否则（如 PNG 的 eXIf 块或标准 WebP），直接按纯 TIFF 头解析
    return parse_tiff_exif_orientation(rawExifData, len);
}

template <typename MapFunc>
static void copy_transformed_pixels(const uint8_t* src, uint8_t* dst,
    int w, int h, int nw, int channels,
    MapFunc map_coord) {
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int nx = 0;
            int ny = 0;
            map_coord(x, y, nx, ny);
            std::memcpy(dst + (static_cast<size_t>(ny) * nw + nx) * channels,
                src + (static_cast<size_t>(y) * w + x) * channels,
                static_cast<size_t>(channels));
        }
    }
}

static uint8_t* apply_orientation(const uint8_t* src, int& w, int& h, int channels, int orientation) {
    if (!src || w <= 0 || h <= 0 || channels <= 0) {
        return nullptr;
    }

    // 无需旋转
    if (orientation <= 1 || orientation > 8) {
        return nullptr;
    }

    // 5~8 号方向会导致宽高互换
    bool swapWH = (orientation >= 5 && orientation <= 8);
    int nw = swapWH ? h : w;
    int nh = swapWH ? w : h;

    size_t totalBytes = static_cast<size_t>(nw) * nh * channels;
    uint8_t* dst = static_cast<uint8_t*>(std::malloc(totalBytes));
    if (!dst) {
        return nullptr;
    }

    // 2. 垂直翻转 (4)：可以直接整行 memcpy，速度提升数倍
    if (orientation == 4) {
        size_t rowBytes = static_cast<size_t>(w) * channels;
        for (int y = 0; y < h; ++y) {
            std::memcpy(dst + static_cast<size_t>(h - 1 - y) * rowBytes,
                src + static_cast<size_t>(y) * rowBytes,
                rowBytes);
        }
        return dst;
    }

    // 3. 其余方向：直接传入具名类型参数的 Lambda
    switch (orientation) {
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

#endif // 