"""Source-derived image tests, persistent visual reports and isolated native workers."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import traceback
from uuid import uuid4

FORMATS = ('png', 'jpg', 'webp', 'bmp', 'apng', 'jxl')
BASE_MODEL = 'MODEL_WAIFU2X_CUNET_UP2X_DENOISE3X'


def model_files(root, name):
    base = name.removeprefix('MODEL_').removesuffix('_TTA')
    return [Path(root) / base.split('_', 1)[0].lower() / (base + ext) for ext in ('.param', '.bin')]


def make_cases(suite, names):
    cases = []
    if suite in ('all', 'formats'):
        cases += [dict(name='format_' + fmt, suite='formats', format=fmt, model=BASE_MODEL) for fmt in FORMATS]
    if suite in ('all', 'bitdepth'):
        import fixtures
        cases += [dict(name='depth_' + v['format'] + '_' + v['variant'], suite='bitdepth', model=BASE_MODEL, **v)
                  for v in fixtures.bitdepth_variants()]
    if suite in ('all', 'orientation'):
        for fmt in ('png', 'jpg', 'webp', 'jxl'):
            for orientation in range(1, 9):
                for auto in (True, False):
                    cases.append(dict(name='orientation_{}_{}_{}'.format(fmt, orientation, 'on' if auto else 'off'),
                                      suite='orientation', format=fmt, orientation=orientation, auto=auto, model=BASE_MODEL))
    if suite in ('all', 'models'):
        cases += [dict(name='model_' + name, suite='models', format='png', model=name) for name in names]
    return cases


def worker(case, options, directory):
    import fixtures
    from PIL import Image
    from sr_vulkan import sr_vulkan as sr
    prepared = json.loads((directory / 'prepared.json').read_text(encoding='utf-8'))
    if options.models:
        missing = [str(p) for p in model_files(options.models, case['model']) if not p.is_file()]
        assert not missing, 'Missing model files: ' + ', '.join(missing)
    if case.get('auto') is False:
        assert hasattr(sr, 'setAutoTransform'), 'Loaded extension has no setAutoTransform: ' + sr.__file__
    initialized = False
    try:
        status = sr.init()
        initialized = True
        infos = sr.getGpuInfo()
        device = options.device
        if device == 'auto':
            device = 'cpu' if status < 0 or not infos or (len(infos) == 1 and 'LLVM' in infos[0]) else 'gpu'
        if device == 'gpu':
            assert status >= 0 and infos and 0 <= options.gpu < len(infos), 'Requested GPU unavailable'
        if options.models:
            assert sr.setModelPath(options.models) == 0
        threads = options.threads or (1 if case['suite'] == 'models' else min(4, os.cpu_count() or 1))
        code = sr.initSet(-1, threads) if device == 'cpu' else sr.initSet(options.gpu)
        assert code == 0, 'initSet failed: {} {}'.format(code, sr.getLastError())
        if case['suite'] == 'orientation' and hasattr(sr, 'setAutoTransform'):
            assert sr.setAutoTransform(case['auto']) == 0
        print('extension={} device={} threads={} GPUs={}'.format(sr.__file__, device, threads, infos), flush=True)
        data = (directory / prepared['input']).read_bytes()
        expected = case['format'] if case['suite'] == 'formats' else 'png'
        fmt_option = ('apng' if expected == 'apng' else '') if case['suite'] == 'formats' else 'png'
        task = sr.add(data, getattr(sr, case['model']), 123, scale=2, format=fmt_option)
        assert task > 0, 'add failed: ' + sr.getLastError()
        result = sr.load(0)
        assert result and len(result) == 4, 'load failed: ' + sr.getLastError()
        output, fmt, back_id, tick = result
        assert isinstance(output, bytes) and output, 'Empty output: ' + sr.getLastError()
        assert back_id == 123, 'Wrong backId'
        (directory / ('output.' + expected)).write_bytes(output)
        assert fmt.lower() in ({'jpg', 'jpeg'} if expected == 'jpg' else {expected}), 'Wrong returned format: ' + fmt
        metrics = []
        with Image.open(io.BytesIO(output)) as image:
            container = {'jpg': 'JPEG', 'apng': 'PNG'}.get(expected, expected.upper())
            assert image.format == container, 'Wrong output container'
            count = getattr(image, 'n_frames', 1)
            references = prepared['references']
            assert count == len(references), 'Wrong frame count: {}'.format(count)
            for index, reference in enumerate(references):
                image.seek(index)
                # Output EXIF is intentionally ignored: test the stored pixels only.
                pixels = image.convert('RGB')
                pixels.save(directory / ('output_{}.png'.format(index)))
                with Image.open(directory / reference) as target:
                    metrics.append(fixtures.assert_reference(pixels, target))
        details = dict(native_tick=tick, device=device, threads=threads, metrics=metrics,
                       output_format=expected, output_size=list(pixels.size))
        # PNG IHDR/BMP DIB describe the real output bit depth, not Pillow mode.
        if expected == 'png':
            details['output_bits_per_channel'] = output[24]
        elif expected == 'bmp':
            details['output_bits_per_pixel'] = int.from_bytes(output[28:30], 'little')
        (directory / 'worker.json').write_text(json.dumps(details, indent=2), encoding='utf-8')
    finally:
        if initialized:
            sr.stop()
            time.sleep(5)  # Native stop is asynchronous; bounded by parent timeout.


def execute_case(case, options, root):
    directory = root / case['name']
    result = dict(name=case['name'], suite=case['suite'], directory=case['name'],
                  status='failed', seconds=0, error='')
    start = time.monotonic()
    command = [sys.executable, '-B', str(Path(__file__).resolve()), '--worker', json.dumps(case),
               '--device', options.device, '--threads', str(options.threads), '--gpu', str(options.gpu),
               '--output', str(directory)]
    if options.models:
        command += ['--models', options.models]
    try:
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=options.timeout)
        (directory / 'worker.log').write_bytes(process.stdout)
        log = process.stdout.decode('utf-8', errors='replace')
        if process.returncode:
            result['error'] = 'Worker exit {}\n{}'.format(process.returncode, log[-6000:])
        else:
            details = json.loads((directory / 'worker.json').read_text(encoding='utf-8'))
            if not isinstance(details, dict) or not details.get('metrics'):
                raise ValueError('Worker did not produce validated frame metrics')
            result.update(status='passed', metadata=details)
    except subprocess.TimeoutExpired as error:
        (directory / 'worker.log').write_bytes(error.stdout or b'')
        result.update(status='timeout', error='Worker timed out after {}s'.format(options.timeout))
    except Exception:
        result.update(status='failed', error=traceback.format_exc())
    result['seconds'] = time.monotonic() - start
    return result


def schedule(cases, options, root, update):
    """Only model cases overlap; callbacks/report writes always run on main thread."""
    for case in cases:
        if case['suite'] != 'models':
            update(execute_case(case, options, root))
    models = [case for case in cases if case['suite'] == 'models']
    with ThreadPoolExecutor(max_workers=options.jobs) as pool:
        futures = [pool.submit(execute_case, case, options, root) for case in models]
        for future in as_completed(futures):
            update(future.result())


def main():
    if sys.flags.optimize:
        print('Do not disable assertions with -O/PYTHONOPTIMIZE.', file=sys.stderr)
        return 1
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--suite', choices=('all', 'formats', 'bitdepth', 'orientation', 'models'), default='all')
    parser.add_argument('--source', default=str(Path(__file__).resolve().parent.parent / '0.jpg'))
    parser.add_argument('--models')
    parser.add_argument('--device', choices=('auto', 'cpu', 'gpu'), default='auto')
    parser.add_argument('--threads', type=int, default=0, help='0: models use 1 CPU thread, others up to 4')
    parser.add_argument('--jobs', type=int, default=8, help='Maximum simultaneous native model processes')
    parser.add_argument('--gpu', type=int, default=0)
    parser.add_argument('--timeout', type=float, default=300)
    parser.add_argument('--output', help='New or empty persistent results directory')
    parser.add_argument('--worker', help=argparse.SUPPRESS)
    options = parser.parse_args()
    if options.jobs < 1 or options.threads < 0 or options.timeout <= 0:
        parser.error('jobs/timeout must be positive; threads must be nonnegative')
    if options.models:
        options.models = str(Path(options.models).resolve())
    if options.worker:
        try:
            worker(json.loads(options.worker), options, Path(options.output))
            return 0
        except Exception:
            traceback.print_exc()
            return 1
    root = Path(options.output).resolve() if options.output else Path(__file__).resolve().parent / 'results' / (
        datetime.now().strftime('%Y%m%d_%H%M%S') + '_' + uuid4().hex[:8])
    if root.exists() and (not root.is_dir() or any(root.iterdir())):
        parser.error('--output must be new or empty')
    root.mkdir(parents=True, exist_ok=True)
    from report import write_reports
    environment = dict(python=sys.executable, source=str(Path(options.source).resolve()),
                       device=options.device, jobs=options.jobs, threads=options.threads, models=options.models)
    results = []
    fatal = ''
    print('Results: ' + str(root), flush=True)
    try:
        source = Path(options.source).resolve()
        environment['source_sha256'] = hashlib.sha256(source.read_bytes()).hexdigest()
        shutil.copyfile(source, root / ('source' + source.suffix))
        from PIL import Image, ImageOps
        with Image.open(source) as original:
            base = ImageOps.exif_transpose(original).convert('RGB')
            base.resize((96, max(1, round(base.height * 96 / base.width))), Image.Resampling.LANCZOS).save(root / 'base.png')
        import fixtures
        # Model enumeration is pure Python; do not initialize native in parent.
        from sr_vulkan import enum
        cases = make_cases(options.suite, enum.AllModelNames)
        for case in cases:
            results.append(dict(name=case['name'], suite=case['suite'], directory=case['name'], status='pending',
                                seconds=0, error='', case=case))
        write_reports(root, environment, results)
        runnable = []
        for case, entry in zip(cases, results):
            directory = root / case['name']
            directory.mkdir()
            try:
                metadata = fixtures.prepare(case, source, directory)
                (directory / 'prepared.json').write_text(json.dumps(metadata, indent=2), encoding='utf-8')
                entry['metadata'] = metadata
                runnable.append(case)
            except Exception:
                entry.update(status='failed', error='Fixture preparation failed:\n' + traceback.format_exc())
        from sr_vulkan import sr_vulkan as sr
        environment['extension'] = sr.__file__
        environment['extension_sha256'] = hashlib.sha256(Path(sr.__file__).read_bytes()).hexdigest()
        environment['has_auto_transform'] = hasattr(sr, 'setAutoTransform')
        by_name = {entry['name']: entry for entry in results}

        def update(result):
            entry = by_name[result['name']]
            metadata = dict(entry.get('metadata', {}))
            metadata.update(result.pop('metadata', {}))
            entry.update(result, metadata=metadata)
            print('{} {} {:.2f}s'.format(entry['status'], entry['name'], entry['seconds']), flush=True)
            write_reports(root, environment, results)

        schedule(runnable, options, root, update)
    except Exception:
        fatal = traceback.format_exc()
        print(fatal, file=sys.stderr)
    finally:
        summary = write_reports(root, environment, results, fatal)
    print('Report: ' + str(root / 'report.html'), flush=True)
    return 0 if summary['successful'] else 1


if __name__ == '__main__':
    sys.exit(main())
