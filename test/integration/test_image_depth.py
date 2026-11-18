"""Independent fixture encoding regressions (no native SR import)."""
from io import BytesIO
import struct
import unittest
from PIL import Image
try:
    from . import fixtures, image_depth
except ImportError:
    import fixtures
    import image_depth


class ImageDepthTests(unittest.TestCase):
    def test_bmp_odd_width_padding_and_masks(self):
        source = fixtures.pattern().resize((97, 65))
        for variant, masks in [('rgb555', [0x7C00, 0x03E0, 0x001F]), ('rgb565', [0xF800, 0x07E0, 0x001F])]:
            with self.subTest(variant=variant):
                data = image_depth.bmp16(source, variant)
                metadata = image_depth.inspect(data, 'bmp')
                self.assertEqual(metadata['masks'], masks)
                self.assertEqual(struct.unpack_from('<I', data, 34)[0], 196 * 65)
                self.assertEqual(len(data), 66 + 196 * 65)
                with Image.open(BytesIO(data)) as decoded:
                    self.assertEqual(decoded.size, source.size)
                    self.assertLess(max(abs(a-b) for a,b in zip(decoded.convert('RGB').tobytes(), source.tobytes())), 9)

    def test_png16_header_and_low_samples(self):
        for channels in (1, 3):
            with self.subTest(channels=channels):
                data = image_depth.png16(fixtures.pattern(), channels, 6)
                metadata = image_depth.inspect(data, 'png')
                self.assertEqual(metadata['bits_per_sample'], 16)
                self.assertEqual(metadata['channels'], channels)
                self.assertTrue(metadata['low_bit_nonzero'])
                with Image.open(BytesIO(data)) as decoded:
                    self.assertEqual(decoded.getexif()[274], 6)
                    self.assertEqual(decoded.size, (96, 64))

    def test_jxl16_independent_exact_samples(self):
        source = fixtures.pattern()
        data = fixtures._encode_source(source, 'jxl', 1, 'gray16')
        metadata, values = fixtures._inspect_jxl(data)
        self.assertEqual(metadata['bits_per_sample'], 16)
        self.assertEqual(metadata['channels'], 1)
        self.assertEqual(values, image_depth.samples16(source, 1))
        self.assertTrue(metadata['low_bit_nonzero'])


if __name__ == '__main__':
    unittest.main()
