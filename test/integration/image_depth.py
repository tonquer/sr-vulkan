"""Real integer-depth encoders and independent on-disk depth inspection."""
import ctypes
import struct
import zlib
from io import BytesIO
from PIL import Image


def samples16(image, channels):
    image = image.convert('L' if channels == 1 else 'RGB')
    # Low bytes carry independent information, not an 8-bit image times 257.
    return [min(65535, value * 256 + (index * 73 + 19) % 256)
            for index, value in enumerate(image.tobytes())]


def png16(image, channels, orientation=1):
    values = samples16(image, channels)
    width, height = image.size
    stride = width * channels
    rows = b''.join(b'\0' + struct.pack('>' + 'H' * stride, *values[y * stride:(y + 1) * stride])
                    for y in range(height))
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    exif = Image.Exif()
    exif[274] = orientation
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 16, 0 if channels == 1 else 2, 0, 0, 0))
            + chunk(b'eXIf', exif.tobytes()[6:]) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def bmp16(image, variant):
    width, height = image.size
    masks = (0xF800, 0x07E0, 0x001F) if variant == 'rgb565' else (0x7C00, 0x03E0, 0x001F)
    rows = []
    for y in reversed(range(height)):
        row = bytearray()
        for x in range(width):
            r, g, b = image.getpixel((x, y))
            word = ((r >> 3) << (11 if variant == 'rgb565' else 10)) | ((g >> (2 if variant == 'rgb565' else 3)) << 5) | (b >> 3)
            row.extend(struct.pack('<H', word))
        row.extend(b'\0' * ((-len(row)) % 4))
        rows.append(bytes(row))
    pixels = b''.join(rows)
    # BITMAPINFOHEADER + explicit BI_BITFIELDS masks, bottom-up padded rows.
    offset = 14 + 40 + 12
    return (struct.pack('<2sIHHI', b'BM', offset + len(pixels), 0, 0, offset)
            + struct.pack('<IiiHHIIiiII', 40, width, height, 1, 16, 3, len(pixels), 0, 0, 0, 0)
            + struct.pack('<III', *masks) + pixels)


class _Preview(ctypes.Structure):
    _fields_ = [('xsize', ctypes.c_uint32), ('ysize', ctypes.c_uint32)]


class _Animation(ctypes.Structure):
    _fields_ = [('tps_numerator', ctypes.c_uint32), ('tps_denominator', ctypes.c_uint32),
                ('num_loops', ctypes.c_uint32), ('have_timecodes', ctypes.c_int)]


class _BasicInfo(ctypes.Structure):
    # Complete public, forwards-padded JxlBasicInfo ABI (jxl/codestream_header.h).
    # No guessed byte offsets, internal decoder structs, or plugin mode inference.
    _fields_ = [('have_container', ctypes.c_int), ('xsize', ctypes.c_uint32), ('ysize', ctypes.c_uint32),
                ('bits_per_sample', ctypes.c_uint32), ('exponent_bits_per_sample', ctypes.c_uint32),
                ('intensity_target', ctypes.c_float), ('min_nits', ctypes.c_float),
                ('relative_to_max_display', ctypes.c_int), ('linear_below', ctypes.c_float),
                ('uses_original_profile', ctypes.c_int), ('have_preview', ctypes.c_int),
                ('have_animation', ctypes.c_int), ('orientation', ctypes.c_int),
                ('num_color_channels', ctypes.c_uint32), ('num_extra_channels', ctypes.c_uint32),
                ('alpha_bits', ctypes.c_uint32), ('alpha_exponent_bits', ctypes.c_uint32),
                ('alpha_premultiplied', ctypes.c_int), ('preview', _Preview), ('animation', _Animation),
                ('intrinsic_xsize', ctypes.c_uint32), ('intrinsic_ysize', ctypes.c_uint32),
                ('padding', ctypes.c_uint8 * 100)]


def inspect_jxl(data, dll, pixel_format_class):
    """Read public BASIC_INFO and decode UINT16 samples using independent libjxl."""
    dll.JxlDecoderGetBasicInfo.restype = ctypes.c_int
    dll.JxlDecoderGetBasicInfo.argtypes = [ctypes.c_void_p, ctypes.POINTER(_BasicInfo)]
    decoder = dll.JxlDecoderCreate(None)
    if not decoder:
        raise RuntimeError('Independent libjxl decoder creation failed')
    source = ctypes.create_string_buffer(data)
    info = _BasicInfo()
    output = None
    def check(status):
        if status != 0:
            raise RuntimeError(f'Independent libjxl validation failed: status={status}')
    try:
        dll.JxlDecoderSetKeepOrientation.restype = ctypes.c_int
        dll.JxlDecoderSetKeepOrientation.argtypes = [ctypes.c_void_p, ctypes.c_int]
        check(dll.JxlDecoderSetKeepOrientation(decoder, 1))
        check(dll.JxlDecoderSubscribeEvents(decoder, 0x40 | 0x1000))
        check(dll.JxlDecoderSetInput(decoder, source, len(data)))
        dll.JxlDecoderCloseInput(decoder)
        for _ in range(32):
            status = dll.JxlDecoderProcessInput(decoder)
            if status == 0x40:
                check(dll.JxlDecoderGetBasicInfo(decoder, ctypes.byref(info)))
                if info.num_color_channels not in (1, 3) or info.bits_per_sample not in (8, 16) or info.exponent_bits_per_sample:
                    raise RuntimeError('Unsupported independent libjxl BASIC_INFO; cannot verify integer depth')
                fmt = pixel_format_class(info.num_color_channels, 3, 1, 0)  # UINT16, little endian
            elif status == 5:
                length = ctypes.c_size_t()
                check(dll.JxlDecoderImageOutBufferSize(decoder, ctypes.byref(fmt), ctypes.byref(length)))
                expected = info.xsize * info.ysize * info.num_color_channels * 2
                if length.value != expected:
                    raise RuntimeError('Independent JXL dimensions/buffer disagree')
                output = ctypes.create_string_buffer(length.value)
                check(dll.JxlDecoderSetImageOutBuffer(decoder, ctypes.byref(fmt), output, length.value))
            elif status == 0x1000 and output is not None:
                values = list(struct.unpack('<' + 'H' * (len(output.raw) // 2), output.raw))
                return {'bits_per_sample': info.bits_per_sample, 'channels': info.num_color_channels,
                        'bits_per_pixel': info.bits_per_sample * info.num_color_channels,
                        'orientation': info.orientation, 'low_bit_nonzero': any(v % 257 for v in values)}, values
            else:
                raise RuntimeError(f'Independent JXL depth decode incomplete: status={status}')
        raise RuntimeError('Independent JXL depth decoder did not finish')
    finally:
        dll.JxlDecoderDestroy(decoder)


def inspect(data, format_name, jxl_decoder=None):
    if format_name in ('png', 'apng'):
        width, height, bits, color = struct.unpack('>IIBB', data[16:26])
        channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[color]
        result = {'bits_per_sample': bits, 'channels': channels, 'bits_per_pixel': bits * channels, 'color_type': color}
        if bits == 16:
            position, compressed = 8, bytearray()
            while position < len(data):
                length = struct.unpack('>I', data[position:position + 4])[0]
                if data[position + 4:position + 8] == b'IDAT':
                    compressed.extend(data[position + 8:position + 8 + length])
                position += length + 12
            rows = zlib.decompress(compressed)
            stride = width * channels * 2
            if len(rows) != height * (stride + 1) or any(rows[y * (stride + 1)] for y in range(height)):
                raise ValueError('16-bit fixture requires independently decodable unfiltered PNG rows')
            payload = b''.join(rows[y * (stride + 1) + 1:(y + 1) * (stride + 1)] for y in range(height))
            values = struct.unpack('>' + 'H' * (len(payload) // 2), payload)
            result['low_bit_nonzero'] = any(v % 257 for v in values)
        return result
    if format_name == 'bmp':
        bits = struct.unpack_from('<H', data, 28)[0]
        result = {'bits_per_pixel': bits, 'bits_per_sample': 8 if bits != 16 else None, 'channels': 3}
        if bits == 16:
            masks = list(struct.unpack_from('<III', data, 54))
            result.update(masks=masks, channel_bits=[m.bit_count() for m in masks], bits_per_sample=max(m.bit_count() for m in masks))
        return result
    if format_name == 'jxl':
        if jxl_decoder is None:
            raise RuntimeError('Independent libjxl depth validator required')
        return jxl_decoder(data)[0]
    return {'bits_per_sample': 8, 'channels': 3, 'bits_per_pixel': 24}
