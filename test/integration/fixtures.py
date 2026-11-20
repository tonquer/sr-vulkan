"""Independent image fixtures; no imports from, or calls to, the SR under test.

``generate(name, orientation=1)`` returns encoded bytes, with real orientation
metadata (BMP supports only 1). ``decode_frames(data)`` returns display-oriented,
detached RGB images; APNG must really contain two distinct animation frames.
``assert_image(image, orientation=1, scale=2)`` checks an already-oriented image,
not its metadata. JXL uses JPEG reconstruction and a public libjxl decoder API,
not Pillow's reconstruction view, to verify actual codestream orientation.
"""

import ctypes
import ctypes.util
from functools import lru_cache
from io import BytesIO
from pathlib import Path
import tempfile
import struct

try:
    from . import image_depth
except ImportError:
    import image_depth

from PIL import Image, ImageChops, ImageDraw, ImageOps
import pillow_jxl  # Registers the JXL Pillow plugin.

FORMATS = ("png", "jpg", "webp", "bmp", "apng", "jxl")
SIZE = (96, 64)
COLORS = ((178, 76, 78), (72, 165, 91), (74, 100, 181), (183, 163, 69))
TOLERANCE = 70
_TRANSPOSE = {
    2: Image.Transpose.FLIP_LEFT_RIGHT,
    3: Image.Transpose.ROTATE_180,
    4: Image.Transpose.FLIP_TOP_BOTTOM,
    5: Image.Transpose.TRANSPOSE,
    6: Image.Transpose.ROTATE_270,
    7: Image.Transpose.TRANSVERSE,
    8: Image.Transpose.ROTATE_90,
}


def _orientation(value):
    if isinstance(value, bool) or not isinstance(value, int) or value not in range(1, 9):
        raise ValueError("orientation must be an integer from 1 through 8")
    return value


def pattern():
    """Return a fresh non-square RGB image with four distinguishable quadrants."""
    image = Image.new("RGB", SIZE)
    draw = ImageDraw.Draw(image)
    for box, color in zip(((0, 0, 47, 31), (48, 0, 95, 31),
                           (0, 32, 47, 63), (48, 32, 95, 63)), COLORS):
        draw.rectangle(box, fill=color)
    return image


def expected_pattern(orientation, frame=0):
    """Return the requested fixture frame transformed by EXIF orientation."""
    _orientation(orientation)
    if frame not in (0, 1):
        raise ValueError("fixture frame must be 0 or 1")
    image = pattern()
    if frame == 1:
        image = image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
    return image.transpose(_TRANSPOSE[orientation]) if orientation != 1 else image


def assert_frame(image, frame, orientation=1, scale=2):
    """Validate an APNG frame's pixels, not just dimensions or byte inequality."""
    assert_image(image, orientation=orientation, scale=scale, frame=frame)


def assert_image(image, orientation=1, scale=2, frame=0):
    """Assert dimensions and interior quadrant pixels; ignore codec edge ringing.

    ``image`` must be a PIL image with orientation already applied. Every pixel
    in the central half of each quadrant is checked (maximum channel error 70).
    Metadata is deliberately not applied here, so missing/double rotation fails.
    """
    expected = expected_pattern(orientation, frame=frame)
    if isinstance(scale, bool) or not isinstance(scale, int) or scale < 1:
        raise ValueError("scale must be a positive integer")
    size = tuple(dimension * scale for dimension in expected.size)
    assert image.size == size, f"size {image.size}, expected {size} (orientation={orientation})"
    actual = image.convert("RGB")
    width, height = size
    for x in (0.25, 0.75):
        for y in (0.25, 0.75):
            color = expected.getpixel((int(expected.width * x), int(expected.height * y)))
            box = (int(width * (x - 0.125)), int(height * (y - 0.125)),
                   int(width * (x + 0.125)), int(height * (y + 0.125)))
            region = actual.crop(box)
            # Tolerances overlap for muted colors; require the expected color
            # class as well, so a correctly sized gray image cannot pass.
            pixels = set(zip(*[iter(region.tobytes())] * 3))
            for pixel in pixels:
                distances = [sum((a - b) ** 2 for a, b in zip(pixel, candidate))
                             for candidate in COLORS]
                expected_distance = distances[COLORS.index(color)]
                assert all(expected_distance < distance for index, distance in enumerate(distances)
                           if COLORS[index] != color), (
                    f"quadrant ({x}, {y}) wrong color class {pixel}; expected {color}"
                )
            delta = ImageChops.difference(region, Image.new("RGB", region.size, color))
            error = max(maximum for minimum, maximum in delta.getextrema())
            assert error <= TOLERANCE, (
                f"quadrant ({x}, {y}) error {error} > {TOLERANCE}; "
                f"expected {color}, orientation={orientation}"
            )


class _PixelFormat(ctypes.Structure):
    # Public JxlPixelFormat from jxl/types.h, not an internal codec structure.
    _fields_ = [("num_channels", ctypes.c_uint32), ("data_type", ctypes.c_int),
                ("endianness", ctypes.c_int), ("align", ctypes.c_size_t)]


@lru_cache(maxsize=1)
def _jxl_library():
    """Load the already-installed plugin's libjxl, never the tested SR binary."""
    root = Path(pillow_jxl.__file__).resolve().parent.parent
    candidates = sorted((root / "pillow_jxl_plugin.libs").glob("libjxl-*.dll"))
    candidates += sorted((root / "pillow_jxl_plugin.libs").glob("libjxl-*.so*"))
    library = str(candidates[0]) if candidates else ctypes.util.find_library("jxl")
    if not library:
        raise RuntimeError("Independent JXL validation requires the installed libjxl shared library")
    try:
        dll = ctypes.CDLL(library)
    except OSError as error:
        raise RuntimeError(
            f"Cannot load independent libjxl decoder {library!r}: {error}. "
            "Use the installed plugin's matching shared library; no SR fallback is allowed."
        ) from error
    signatures = {
        "JxlDecoderCreate": (ctypes.c_void_p, [ctypes.c_void_p]),
        "JxlDecoderDestroy": (None, [ctypes.c_void_p]),
        "JxlDecoderSubscribeEvents": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_int]),
        "JxlDecoderSetInput": (ctypes.c_int, [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]),
        "JxlDecoderCloseInput": (None, [ctypes.c_void_p]),
        "JxlDecoderProcessInput": (ctypes.c_int, [ctypes.c_void_p]),
        "JxlDecoderImageOutBufferSize": (ctypes.c_int, [ctypes.c_void_p, ctypes.POINTER(_PixelFormat), ctypes.POINTER(ctypes.c_size_t)]),
        "JxlDecoderSetImageOutBuffer": (ctypes.c_int, [ctypes.c_void_p, ctypes.POINTER(_PixelFormat), ctypes.c_void_p, ctypes.c_size_t]),
    }
    for name, (restype, argtypes) in signatures.items():
        function = getattr(dll, name)
        function.restype, function.argtypes = restype, argtypes
    return dll


def _decode_jxl_frames(data):
    # The plugin's info dimensions follow codestream orientation, even when its
    # Pillow image instead exposes the *unrotated reconstructed JPEG*.
    _, info, _, _, _ = pillow_jxl.Decoder(num_threads=0)(data)
    size = (info.width, info.height)
    dll = _jxl_library()
    decoder = dll.JxlDecoderCreate(None)
    if not decoder:
        raise RuntimeError("JxlDecoderCreate failed")
    source = ctypes.create_string_buffer(data)
    pixels = None
    pixel_format = _PixelFormat(3, 2, 0, 0)  # RGB, UINT8, native endian, no padding.

    def check(status, operation):
        if status != 0:
            raise RuntimeError(f"{operation} failed: libjxl status {status}")

    try:
        check(dll.JxlDecoderSubscribeEvents(decoder, 0x1000), "subscribe FULL_IMAGE")
        check(dll.JxlDecoderSetInput(decoder, source, len(data)), "set input")
        dll.JxlDecoderCloseInput(decoder)
        frames = []
        while True:
            status = dll.JxlDecoderProcessInput(decoder)
            if status == 5:  # JXL_DEC_NEED_IMAGE_OUT_BUFFER
                length = ctypes.c_size_t()
                check(dll.JxlDecoderImageOutBufferSize(decoder, ctypes.byref(pixel_format), ctypes.byref(length)), "output size")
                if length.value != size[0] * size[1] * 3:
                    raise RuntimeError("JXL decoder dimensions and RGB buffer disagree")
                pixels = ctypes.create_string_buffer(length.value)
                check(dll.JxlDecoderSetImageOutBuffer(decoder, ctypes.byref(pixel_format), pixels, length.value), "set output")
            elif status == 0x1000:  # JXL_DEC_FULL_IMAGE; default API applies orientation.
                if pixels is None:
                    raise RuntimeError("JXL decoder returned a frame without pixels")
                frames.append(Image.frombytes("RGB", size, pixels.raw))
                pixels = None
            elif status == 0:  # SUCCESS means the complete animation was decoded.
                if not frames:
                    raise RuntimeError("JXL decoder returned no frames")
                return frames
            else:
                raise RuntimeError(f"JXL decode failed/incomplete: libjxl status {status}")
    finally:
        dll.JxlDecoderDestroy(decoder)


def _decode_jxl(data):
    return _decode_jxl_frames(data)[0]


def detect_format(data):
    """Return png/jpg/webp/bmp/apng/jxl, rejecting unsupported or broken data."""
    with Image.open(BytesIO(data)) as image:
        name = image.format
        if name == "PNG":
            position = 8
            while position + 12 <= len(data):
                length = int.from_bytes(data[position:position + 4], 'big')
                if data[position + 4:position + 8] == b'acTL':
                    return 'apng'
                position += length + 12
            return 'png'
        mapping = {"JPEG": "jpg", "WEBP": "webp", "BMP": "bmp", "JXL": "jxl", "GIF": "gif"}
        if name not in mapping:
            raise ValueError(f"Unsupported image format: {name}")
        return mapping[name]


def check_format(data, format_name):
    """Assert encoded format, plus APNG's real two-frame/different-pixel contract."""
    if format_name not in FORMATS:
        raise ValueError(f"Unsupported fixture format: {format_name}")
    actual = detect_format(data)
    assert actual == format_name, f"encoded format {actual}, expected {format_name}"
    if actual == "apng":
        frames = decode_frames(data)
        assert len(frames) == 2, f"APNG must have two frames, got {len(frames)}"
        assert ImageChops.difference(*frames).getbbox(), "APNG frames must have different pixels"


def decode_frames(data):
    """Decode bytes to detached display-oriented RGB frames without using SR.

    JXL is decoded as pixels via libjxl (JPEG reconstruction is not requested).
    Other formats use Pillow plus EXIF transpose exactly once. APNG's optional
    non-animation default image is excluded from the returned animation frames.
    """
    if detect_format(data) == "jxl":
        return _decode_jxl_frames(data)
    frames = []
    with Image.open(BytesIO(data)) as image:
        for index in range(int(image.info.get("default_image", False)), getattr(image, "n_frames", 1)):
            image.seek(index)
            frame = ImageOps.exif_transpose(image.copy())
            if frame.mode in ('I', 'I;16', 'I;16L', 'I;16B'):
                frame = Image.frombytes('L', frame.size, frame.point(lambda value: value / 256).convert('L').tobytes())
            frames.append(frame.convert("RGB"))
    return frames


def generate(format_name, orientation=1):
    """Encode a fixture with raw 96x64 pixels and requested EXIF orientation.

    BMP cannot represent EXIF: a non-1 orientation raises ValueError, never a
    silently pre-rotated or metadata-free stand-in. JXL generation fails loudly
    if JPEG reconstruction does not synchronize actual codestream orientation.
    """
    _orientation(orientation)
    if format_name not in FORMATS:
        raise ValueError(f"Unsupported fixture format: {format_name}")
    if format_name == "bmp" and orientation != 1:
        raise ValueError("BMP cannot encode EXIF orientation; only orientation=1 is supported")
    image = pattern()
    exif = Image.Exif()
    exif[274] = orientation
    output = BytesIO()
    if format_name == "jxl":
        # The plugin requires a real JPEG filename for its documented lossless
        # JPEG reconstruction route; metadata-only RGB JXL save is insufficient.
        with tempfile.TemporaryDirectory(prefix="sr-jxl-fixture-") as directory:
            filename = Path(directory) / "source.jpg"
            image.save(filename, format="JPEG", quality=100, subsampling=0, exif=exif)
            with Image.open(filename) as jpeg:
                jpeg.save(output, format="JXL", lossless_jpeg=True, num_threads=0)
    elif format_name == "apng":
        second = image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)
        image.save(output, format="PNG", save_all=True, append_images=[second],
                   duration=[80, 120], loop=0, disposal=[0, 0], blend=[0, 0], exif=exif)
    elif format_name == "jpg":
        image.save(output, format="JPEG", quality=100, subsampling=0, exif=exif)
    elif format_name == "webp":
        image.save(output, format="WEBP", lossless=True, exif=exif)
    elif format_name == "png":
        image.save(output, format="PNG", exif=exif)
    else:
        image.save(output, format="BMP")
    data = output.getvalue()
    check_format(data, format_name)
    # Validate the encoded result independently, especially actual JXL pixels.
    assert_image(decode_frames(data)[0], orientation, scale=1)
    return data


def bitdepth_variants():
    """Input compatibility cases; native output remains eight bits per channel."""
    return [{'format': fmt, 'variant': variant} for fmt, variants in (
        ('png', ('rgb8', 'rgb16', 'gray8', 'gray16')),
        ('bmp', ('palette8', 'rgb555', 'rgb565', 'rgb24')),
        ('jxl', ('rgb8', 'gray8', 'gray16'))) for variant in variants]


def _inspect_jxl(data):
    return image_depth.inspect_jxl(data, _jxl_library(), _PixelFormat)


def _encode_source(image, fmt, orientation, variant):
    exif = Image.Exif()
    exif[274] = orientation
    output = BytesIO()
    if variant in ('gray8', 'gray16'):
        image = image.convert('L')
    if fmt == 'png' and variant in ('rgb16', 'gray16'):
        return image_depth.png16(image, 1 if variant == 'gray16' else 3, orientation)
    if fmt == 'bmp' and variant in ('rgb555', 'rgb565'):
        return image_depth.bmp16(image, variant)
    if fmt == 'jxl' and variant == 'gray16':
        values = image_depth.samples16(image, 1)
        wide = Image.frombytes('I;16', image.size, struct.pack('<' + 'H' * len(values), *values))
        wide.save(output, format='JXL', lossless=True, num_threads=0)
        data = output.getvalue()
        actual, decoded = _inspect_jxl(data)
        if actual['bits_per_sample'] != 16 or decoded != values:
            raise AssertionError('JXL gray16 must preserve actual UINT16 samples losslessly')
        return data
    if fmt == 'jxl' and orientation != 1:
        if variant is not None:
            raise ValueError('JXL depth variants require orientation=1; JPEG reconstruction is eight-bit RGB')
        with tempfile.TemporaryDirectory(prefix='sr-jxl-source-') as directory:
            filename = Path(directory) / 'source.jpg'
            image.save(filename, format='JPEG', quality=100, subsampling=0, exif=exif)
            with Image.open(filename) as jpeg:
                jpeg.save(output, format='JXL', lossless_jpeg=True, num_threads=0)
    elif fmt == 'jxl':
        image.save(output, format='JXL', lossless=True, num_threads=0)
    elif fmt == 'apng':
        image.save(output, format='PNG', save_all=True,
                   append_images=[image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)],
                   duration=[80, 120], loop=0, disposal=[0, 0], blend=[0, 0], exif=exif)
    elif fmt == 'jpg':
        image.save(output, format='JPEG', quality=100, subsampling=0, exif=exif)
    elif fmt == 'webp':
        image.save(output, format='WEBP', lossless=True, exif=exif)
    elif fmt == 'png':
        image.save(output, format='PNG', exif=exif)
    elif fmt == 'bmp':
        if variant == 'palette8':
            image = image.quantize(colors=256)
        image.save(output, format='BMP')
    return output.getvalue()


def prepare(case, source, directory):
    """Prepare source-derived files before any native call; return JSON metadata.

    All paths are filenames relative to directory. References are base-resolution
    encoded-input pixels, raw when auto is False and display-oriented otherwise.
    The second APNG frame uses raw_frame1/display_frame1/reference_frame1.png.
    """
    fmt = case['format']
    variant = case.get('variant')
    orientation = _orientation(case.get('orientation', 1))
    auto = case.get('auto', True)
    if not isinstance(auto, bool):
        raise ValueError('auto must be a boolean')
    if fmt not in FORMATS:
        raise ValueError(f'Unsupported fixture format: {fmt}')
    if variant is not None and {'format': fmt, 'variant': variant} not in bitdepth_variants():
        raise ValueError(f'Unsupported depth variant: {fmt}/{variant}')
    if fmt == 'bmp' and orientation != 1:
        raise ValueError('BMP cannot encode EXIF orientation; only orientation=1 is supported')
    if fmt == 'jxl' and variant is not None and orientation != 1:
        raise ValueError('JXL depth variants require orientation=1')
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    with Image.open(source) as opened:
        normalized = ImageOps.exif_transpose(opened).convert('RGB')
        normalized = normalized.resize((96, max(1, round(normalized.height * 96 / normalized.width))), Image.Resampling.LANCZOS)
        image = Image.frombytes('RGB', normalized.size, normalized.tobytes())
    if case.get('markers', False):
        draw = ImageDraw.Draw(image)
        for index, color in enumerate(COLORS):
            x = (index % 2) * (image.width - 9)
            y = (index // 2) * (image.height - 9)
            draw.rectangle((x, y, x + 5 + index % 2, y + 4 + index // 2), fill=color)
    # Save source stage even if encoding/independent validation fails.
    image.save(directory / 'raw.png')
    display = image.transpose(_TRANSPOSE[orientation]) if orientation != 1 else image.copy()
    display.save(directory / 'display.png')
    (image if auto is False else display).save(directory / 'reference.png')
    data = _encode_source(image, fmt, orientation, variant)
    filename = f'input.{fmt}'
    (directory / filename).write_bytes(data)
    check_format(data, fmt)
    actual_bits = image_depth.inspect(data, fmt, _inspect_jxl)
    expected_depth = 16 if variant in ('rgb16', 'gray16') else 8
    if fmt != 'bmp' and actual_bits['bits_per_sample'] != expected_depth:
        raise AssertionError(f'Encoded depth mismatch: {actual_bits}, expected {expected_depth}')
    if expected_depth == 16 and not actual_bits.get('low_bit_nonzero'):
        raise AssertionError('16-bit input has no independently verified low-bit information')
    displays = decode_frames(data)
    if fmt == 'jxl' and actual_bits['orientation'] != orientation:
        raise AssertionError('JXL codestream orientation differs from requested orientation')
    references = []
    for index, displayed in enumerate(displays):
        # EXIF orientations 6/8 are inverse; the others are self-inverse.
        inverse = {6: 8, 8: 6}.get(orientation, orientation)
        raw = displayed.transpose(_TRANSPOSE[inverse]) if orientation != 1 else displayed.copy()
        suffix = '' if index == 0 else f'_frame{index}'
        raw.save(directory / f'raw{suffix}.png')
        displayed.save(directory / f'display{suffix}.png')
        name = f'reference{suffix}.png'
        (raw if auto is False else displayed).save(directory / name)
        references.append(name)
    return {'input': filename, 'references': references, 'raw': 'raw.png', 'display': 'display.png',
            'format': fmt, 'variant': variant, 'orientation': orientation, 'auto': auto,
            'size': list(displays[0].size if auto else raw.size), 'actual_bits': actual_bits}


def prepare_convert(case, source, directory):
    """Prepare single-frame matrix inputs or explicit two-frame animation inputs."""
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    with Image.open(source) as opened:
        image = ImageOps.exif_transpose(opened).convert('RGB')
        image = image.resize((96, max(1, round(image.height * 96 / image.width))), Image.Resampling.LANCZOS)
        image = Image.frombytes('RGB', image.size, image.tobytes())
    fmt = case['format']
    animation = case.get('animation', False)
    buffer = BytesIO()
    if fmt in ('gif', 'apng'):
        frames = [image.transpose(Image.Transpose.FLIP_LEFT_RIGHT)] if animation else []
        image.save(buffer, format='GIF' if fmt == 'gif' else 'PNG', save_all=True,
                   append_images=frames, duration=100, loop=0)
        data = buffer.getvalue()
    else:
        data = _encode_source(image, fmt, 1, None)
    filename = 'input.' + fmt
    (directory / filename).write_bytes(data)
    decoded = decode_frames(data)
    assert len(decoded) == (2 if animation else 1), 'Wrong input frame count'
    references = []
    for index, frame in enumerate(decoded):
        suffix = '_frame%d' % index if index else ''
        for prefix in ('raw', 'display', 'reference'):
            frame.save(directory / (prefix + suffix + '.png'))
        references.append('reference' + suffix + '.png')
    if case['target'] in ('jpg', 'png', 'bmp'):
        references = references[:1]
    return dict(input=filename, references=references, format=fmt, target=case['target'],
                animation=animation, size=list(decoded[0].size))


def assert_reference(image, reference, scale=2):
    """Require 2x size and source structure, rejecting rotation/mirror/gray output.

    No output EXIF is read or applied. Low-resolution comparisons tolerate SR
    sharpening but require the identity orientation to beat all seven alternatives.
    """
    if not isinstance(reference, Image.Image):
        with Image.open(reference) as opened:
            expected = opened.convert('RGB')
    else:
        expected = reference.convert('RGB')
    assert image.size == (expected.width * scale, expected.height * scale), (
        f'size {image.size}, expected {(expected.width * scale, expected.height * scale)}')
    actual = image.convert('RGB').resize(expected.size, Image.Resampling.LANCZOS)
    # Blur-scale structure dampens ringing without throwing away source layout.
    size = (min(40, expected.width), min(40, expected.height))
    observed = actual.resize(size, Image.Resampling.BOX)
    target = expected.resize(size, Image.Resampling.BOX)
    def error(candidate):
        delta = ImageChops.difference(observed, candidate.resize(size, Image.Resampling.BOX))
        return sum(sum(value * count for value, count in enumerate(channel.histogram()))
                   for channel in delta.split()) / (size[0] * size[1] * 3)
    correct = error(target)
    assert correct <= 28, f'reference structure/color error {correct:.2f} > 28'
    for orientation, operation in _TRANSPOSE.items():
        wrong = error(expected.transpose(operation))
        assert correct + 0.25 < wrong, (
            f'orientation ambiguous/wrong: identity error {correct:.2f}, orientation {orientation} error {wrong:.2f}')
    # Specifically reject loss of chroma even when luminance structure survives.
    def chroma(pixels):
        return sum(abs(r - g) + abs(g - b) for r, g, b in zip(*[iter(pixels.tobytes())] * 3))
    target_chroma = chroma(target)
    actual_chroma = chroma(observed)
    if target_chroma > size[0] * size[1] * 6:
        assert actual_chroma >= target_chroma * 0.45, 'color reference became grayscale/desaturated'
    return {'reference_mae': correct, 'chroma_ratio': actual_chroma / max(1, target_chroma)}
