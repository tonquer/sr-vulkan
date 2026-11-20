"""Scheduling checks without initializing the native extension."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import run_tests


def options(**kwargs):
    values = dict(device='cpu', threads=0, gpu=0, timeout=1, models=None, jobs=8)
    values.update(kwargs)
    return SimpleNamespace(**values)


class RunnerTests(unittest.TestCase):
    def test_convert_matrix_and_no_initialization(self):
        import io
        import json
        import sys
        import fixtures
        from PIL import Image
        cases = run_tests.make_cases('convert', [])
        self.assertEqual(len(cases), 62)
        self.assertEqual(len({c['name'] for c in cases}), 62)
        self.assertIn('convert_animation_gif_to_jxl', {c['name'] for c in cases})
        self.assertNotIn('convert_animation_apng_to_jxl', {c['name'] for c in cases})
        image = fixtures.pattern()
        buffer = io.BytesIO()
        image.save(buffer, format='PNG')
        # A fake module with no initialization API proves this branch is independent.
        fake = SimpleNamespace(convert=lambda data, fmt: buffer.getvalue(), getLastError=lambda: 'codec error')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            image.save(root / 'reference.png')
            (root / 'input.png').write_bytes(buffer.getvalue())
            (root / 'prepared.json').write_text(json.dumps(dict(input='input.png', references=['reference.png'])))
            case = dict(suite='convert', format='png', target='png')
            with patch.dict(sys.modules, {'sr_vulkan': SimpleNamespace(sr_vulkan=fake)}):
                run_tests.worker(case, options(models='/missing'), root)
                fake.convert = lambda data, fmt: None
                with self.assertRaisesRegex(AssertionError, 'codec error'):
                    run_tests.worker(case, options(), root)

    def test_convert_jxl_animation_requires_both_correct_frames(self):
        import json
        import sys
        import fixtures
        first = fixtures.pattern()
        second = first.transpose(fixtures.Image.Transpose.FLIP_LEFT_RIGHT)
        case = dict(suite='convert', format='gif', target='jxl', animation=True)
        fake = SimpleNamespace(convert=lambda data, fmt: b'jxl')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'input.gif').write_bytes(b'gif')
            first.save(root / 'reference.png')
            second.save(root / 'reference_frame1.png')
            (root / 'prepared.json').write_text(json.dumps(dict(input='input.gif',
                references=['reference.png', 'reference_frame1.png'])))
            with patch.dict(sys.modules, {'sr_vulkan': SimpleNamespace(sr_vulkan=fake)}), \
                    patch('fixtures.detect_format', return_value='jxl'):
                for frames in ([first, second], [first], [first, first], [second, first]):
                    with patch('fixtures._decode_jxl_frames', return_value=frames):
                        if len(frames) == 2 and frames[0] is first and frames[1] is second:
                            run_tests.worker(case, options(), root)
                        else:
                            with self.assertRaises(AssertionError):
                                run_tests.worker(case, options(), root)

    def test_matrix(self):
        cases = run_tests.make_cases('all', ['MODEL_A', 'MODEL_A_TTA'])
        self.assertEqual(len({c['name'] for c in cases}), len(cases))
        self.assertEqual(len([c for c in cases if c['suite'] == 'orientation']), 64)
        self.assertGreaterEqual(len([c for c in cases if c['suite'] == 'bitdepth']), 10)
        self.assertEqual([c['model'] for c in cases if c['suite'] == 'models'], ['MODEL_A', 'MODEL_A_TTA'])

    def test_missing_source_still_creates_failure_report(self):
        import json
        import sys
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / 'report'
            with patch.object(sys, 'argv', ['run_tests.py', '--source', str(Path(tmp) / 'absent.jpg'),
                                          '--output', str(root)]):
                self.assertEqual(run_tests.main(), 1)
            summary = json.loads((root / 'summary.json').read_text(encoding='utf-8'))
            self.assertFalse(summary['successful'])
            self.assertIn('FileNotFoundError', summary['fatal_error'])
            self.assertTrue((root / 'report.html').is_file())

    def test_model_paths(self):
        self.assertEqual(run_tests.model_files('/models', 'MODEL_REALSR_DF2K_UP4X_TTA')[0],
                         Path('/models/realsr/REALSR_DF2K_UP4X.param'))

    def test_timeout_and_crash(self):
        case = run_tests.make_cases('formats', [])[0]
        for failure in (subprocess.TimeoutExpired('worker', 1, output=b'hung'), None):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                (root / case['name']).mkdir()
                with patch('run_tests.subprocess.run', side_effect=failure,
                           return_value=subprocess.CompletedProcess('worker', 99, b'crash')):
                    result = run_tests.execute_case(case, options(), root)
                self.assertEqual(result['status'], 'timeout' if failure else 'failed')
                self.assertTrue((root / case['name'] / 'worker.log').exists())

    def test_exit_zero_requires_valid_worker_result(self):
        import json
        case = run_tests.make_cases('formats', [])[0]
        for payload in (None, '{broken', '{}', json.dumps({'metrics': [{'reference_mae': 1}]})):
            with tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                folder = root / case['name']
                folder.mkdir()
                if payload is not None:
                    (folder / 'worker.json').write_text(payload, encoding='utf-8')
                with patch('run_tests.subprocess.run', return_value=subprocess.CompletedProcess('worker', 0, b'ok')):
                    result = run_tests.execute_case(case, options(), root)
                self.assertEqual(result['status'], 'passed' if payload and 'reference_mae' in payload else 'failed')

    def test_eight_native_jobs_overlap_and_callbacks_are_serial(self):
        cases = run_tests.make_cases('models', ['MODEL_' + str(i) for i in range(16)])
        barrier = threading.Barrier(8)
        lock = threading.Lock()
        active = 0
        peak = 0
        seen = []
        main_thread = threading.get_ident()

        def fake(case, opts, root):
            nonlocal active, peak
            with lock:
                active += 1
                peak = max(peak, active)
            barrier.wait(timeout=5)
            with lock:
                active -= 1
            return dict(name=case['name'], status='passed')

        def update(result):
            self.assertEqual(threading.get_ident(), main_thread)
            seen.append(result['name'])

        with patch('run_tests.execute_case', side_effect=fake):
            run_tests.schedule(cases, options(), Path('.'), update)
        self.assertEqual(peak, 8)
        self.assertEqual(len(seen), 16)
        self.assertEqual(set(seen), {c['name'] for c in cases})

    def test_jobs_one_and_other_groups_are_sequential(self):
        cases = run_tests.make_cases('formats', []) + run_tests.make_cases('models', ['MODEL_A', 'MODEL_B'])
        seen = []
        with patch('run_tests.execute_case', side_effect=lambda c, o, r: dict(name=c['name'])):
            run_tests.schedule(cases, options(jobs=1), Path('.'), lambda r: seen.append(r['name']))
        self.assertEqual(seen, [c['name'] for c in cases])


if __name__ == '__main__':
    unittest.main()
