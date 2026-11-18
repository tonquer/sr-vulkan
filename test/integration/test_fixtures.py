"""From test/: python -m unittest discover -s integration -p test_fixtures.py -v"""

from io import BytesIO
import unittest
import json
from pathlib import Path
import tempfile

from PIL import Image

try:
    from . import fixtures
except ImportError:
    import fixtures


class FixtureTests(unittest.TestCase):
    def test_pattern(self):
        image = fixtures.pattern()
        self.assertEqual(image.mode, "RGB")
        self.assertEqual(image.size, (96, 64))
        self.assertEqual(len(image.getcolors()), 4)
        fixtures.assert_image(image, scale=1)

    def test_all_formats_and_directions(self):
        # BMP has no orientation field; deliberately reject unsupported cases.
        # JXL failures are errors, not skips or passes with unrotated pixels.
        for name in fixtures.FORMATS:
            for orientation in range(1, 9):
                with self.subTest(format=name, orientation=orientation):
                    if name == "bmp" and orientation != 1:
                        with self.assertRaisesRegex(ValueError, "BMP.*orientation"):
                            fixtures.generate(name, orientation)
                        continue
                    data = fixtures.generate(name, orientation)
                    fixtures.check_format(data, name)
                    frames = fixtures.decode_frames(data)
                    self.assertEqual(len(frames), 2 if name == "apng" else 1)
                    for index, frame in enumerate(frames):
                        fixtures.assert_frame(frame, index, orientation, scale=1)
                        enlarged = frame.resize((frame.width * 2, frame.height * 2), Image.Resampling.NEAREST)
                        fixtures.assert_frame(enlarged, index, orientation)

    def test_all_wrong_orientations_fail(self):
        for expected in range(1, 9):
            for actual in range(1, 9):
                if actual == expected:
                    continue
                with self.subTest(expected=expected, actual=actual):
                    with self.assertRaises(AssertionError):
                        fixtures.assert_image(fixtures.expected_pattern(actual), expected, scale=1)

    def test_wrong_pixels_inside_regions_fail(self):
        image = fixtures.pattern()
        image.putpixel((24, 16), (0, 0, 0))
        with self.assertRaises(AssertionError):
            fixtures.assert_image(image, scale=1)
        with self.assertRaises(AssertionError):
            fixtures.assert_image(Image.new("RGB", (96, 64)), scale=1)

    def test_gray_images_cannot_pass_overlapping_tolerances(self):
        for orientation in range(1, 9):
            size = fixtures.expected_pattern(orientation).size
            for gray in (128, 129):
                for frame in (0, 1):
                    with self.subTest(orientation=orientation, gray=gray, frame=frame):
                        with self.assertRaises(AssertionError):
                            fixtures.assert_frame(Image.new('RGB', size, (gray,) * 3),
                                                  frame, orientation, scale=1)

    def test_apng_real_animation_and_second_frame(self):
        data = fixtures.generate("apng")
        with Image.open(BytesIO(data)) as image:
            self.assertTrue(image.is_animated)
            self.assertEqual(image.n_frames, 2)
            self.assertFalse(image.info.get("default_image", False))
            self.assertEqual(image.info["duration"], 80)
            image.seek(1)
            self.assertEqual(image.info["duration"], 120)
        first, second = fixtures.decode_frames(data)
        fixtures.assert_frame(first, 0, scale=1)
        fixtures.assert_frame(second, 1, scale=1)
        self.assertNotEqual(first.tobytes(), second.tobytes())
        with self.assertRaises(AssertionError):
            fixtures.assert_frame(first, 1, scale=1)
        with self.assertRaises(AssertionError):
            fixtures.assert_frame(second, 0, scale=1)

    def test_orientation_is_metadata_not_prerotated(self):
        for name in ("png", "jpg", "webp", "apng"):
            for orientation in range(1, 9):
                with self.subTest(format=name, orientation=orientation):
                    with Image.open(BytesIO(fixtures.generate(name, orientation))) as image:
                        self.assertEqual(image.size, (96, 64))
                        self.assertEqual(image.getexif().get(274), orientation)
                        fixtures.assert_image(image, scale=1)

    def test_source_prepare_depth_variants(self):
        self.assertEqual(len(fixtures.bitdepth_variants()), 11)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source.png'
            fixtures.pattern().resize((150, 100)).save(source)
            for case in fixtures.bitdepth_variants():
                with self.subTest(**case):
                    folder = root / (case['format'] + case['variant'])
                    metadata = fixtures.prepare(case, source, folder)
                    json.dumps(metadata)
                    bits = metadata['actual_bits']
                    self.assertEqual(bits['bits_per_pixel'], {
                        'rgb8': 24, 'rgb16': 48, 'gray8': 8, 'gray16': 16,
                        'palette8': 8, 'rgb555': 16, 'rgb565': 16, 'rgb24': 24}[case['variant']])
                    if case['variant'].endswith('16'):
                        self.assertEqual(bits['bits_per_sample'], 16)
                        self.assertTrue(bits['low_bit_nonzero'])
                    if case['variant'] in ('rgb555', 'rgb565'):
                        self.assertEqual(bits['channel_bits'], [5, 6 if case['variant'] == 'rgb565' else 5, 5])
                    with Image.open(folder / metadata['references'][0]) as reference:
                        self.assertEqual(reference.size, (96, 64))
                        fixtures.assert_reference(reference.resize((192, 128)), reference)

    def test_source_preparation_normalizes_exif_and_clears_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source.jpg'
            exif = Image.Exif()
            exif[274] = 6
            fixtures.pattern().save(source, quality=100, subsampling=0, exif=exif)
            result = fixtures.prepare({'format': 'png'}, source, root / 'out')
            with Image.open(root / 'out' / result['references'][0]) as reference:
                self.assertEqual(reference.size, (96, 144))
                self.assertFalse(reference.getexif())
                self.assertLess(sum(abs(a - b) for a, b in zip(reference.getpixel((72, 36)), fixtures.COLORS[0])), 12)

    def test_prepare_all_orientations_and_auto(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / 'source.png'
            fixtures.pattern().save(source)
            for fmt in fixtures.FORMATS:
                for orientation in range(1, 9):
                    if fmt == 'bmp' and orientation != 1:
                        continue
                    for auto in (True, False):
                        with self.subTest(format=fmt, orientation=orientation, auto=auto):
                            folder = root / f'{fmt}-{orientation}-{auto}'
                            metadata = fixtures.prepare({'format': fmt, 'orientation': orientation, 'auto': auto}, source, folder)
                            self.assertEqual(len(metadata['references']), 2 if fmt == 'apng' else 1)
                            for index, filename in enumerate(metadata['references']):
                                with Image.open(folder / filename) as reference:
                                    fixtures.assert_frame(reference, index, orientation if auto else 1, scale=1)
                                    fixtures.assert_reference(reference.resize((reference.width * 2, reference.height * 2)), reference)

    def test_reference_wrong_orientation_gray_and_size(self):
        reference = fixtures.pattern()
        for orientation in range(1, 9):
            expected = fixtures.expected_pattern(orientation)
            for actual_orientation in range(1, 9):
                actual = fixtures.expected_pattern(actual_orientation).resize((expected.width * 2, expected.height * 2))
                if actual_orientation == orientation:
                    fixtures.assert_reference(actual, expected)
                else:
                    with self.assertRaises(AssertionError):
                        fixtures.assert_reference(actual, expected)
            with self.assertRaises(AssertionError):
                fixtures.assert_reference(expected.convert('L').resize((expected.width * 2, expected.height * 2)), expected)
        with self.assertRaises(AssertionError):
            fixtures.assert_reference(reference, reference)
        # Metadata must not cause a second transformation.
        actual = reference.resize((192, 128))
        actual.getexif()[274] = 6
        fixtures.assert_reference(actual, reference)

    def test_errors(self):
        for orientation in (0, 9, -1, None, True, 1.5, "6"):
            with self.subTest(orientation=orientation):
                with self.assertRaises(ValueError):
                    fixtures.generate("png", orientation)
        with self.assertRaises(ValueError):
            fixtures.generate("tiff")
        with self.assertRaises(AssertionError):
            fixtures.check_format(fixtures.generate("png"), "jpg")
        with self.assertRaises(Exception):
            fixtures.decode_frames(b"not an image")
        for scale in (0, -1, True, 1.5):
            with self.assertRaises(ValueError):
                fixtures.assert_image(fixtures.pattern(), scale=scale)


if __name__ == "__main__":
    unittest.main()
