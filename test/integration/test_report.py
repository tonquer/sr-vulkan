"""Standard-library tests for the integration report writer."""

import importlib.util
from html.parser import HTMLParser
import json
from pathlib import Path
import re
import tempfile
import unittest
from urllib.parse import unquote, urlsplit


_spec = importlib.util.spec_from_file_location("integration_report_under_test", Path(__file__).with_name("report.py"))
_report = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_report)
write_reports = _report.write_reports


class _Links(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
        self.tags = []

    def handle_starttag(self, tag, attrs):
        self.tags.append(tag)
        for name, value in attrs:
            if name in ("href", "src"):
                self.links.append(value)


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.environment = {
            "source": "test source", "source_sha256": "abcdef", "python": "3.x",
            "extension": "sr_vulkan.pyd", "device": "test device", "jobs": 2, "threads": 4,
        }

    def case(self, name="case", status="passed"):
        return {"name": name, "suite": "formats", "status": status, "seconds": 1.25,
                "error": "", "directory": name, "metadata": {"width": 20}}

    def test_mixed_outcomes_and_full_json(self):
        results = [self.case(str(index), status) for index, status in enumerate(
            ("passed", "passed", "failed", "timeout", "pending"))]
        results[2]["error"] = "failure details"
        summary = write_reports(self.root, self.environment, results)
        for key, expected in {"total": 5, "passed": 2, "failed": 1, "timeouts": 1, "pending": 1}.items():
            self.assertEqual(summary[key], expected)
        self.assertIs(summary["successful"], False)
        stored = json.loads((self.root / "summary.json").read_text(encoding="utf-8"))
        self.assertEqual(stored, summary)
        self.assertEqual(stored["environment"], self.environment)
        self.assertEqual(stored["results"], results)
        self.assertEqual(stored["fatal_error"], "")
        for filename in ("report.md", "report.html"):
            self.assertTrue((self.root / filename).is_file())

    def test_fatal_initialization_without_cases(self):
        summary = write_reports(self.root, self.environment, [], fatal_error="initialization failed")
        self.assertEqual(summary["total"], 0)
        self.assertIs(summary["successful"], False)
        for filename in ("report.md", "report.html", "summary.json"):
            self.assertIn("initialization failed", (self.root / filename).read_text(encoding="utf-8"))

    def test_pending_is_not_success_and_reports_are_replaced(self):
        self.assertTrue(write_reports(self.root, self.environment, [self.case()])["successful"])
        pending = self.case(status="pending")
        pending["error"] = "not started"
        summary = write_reports(self.root, self.environment, [pending])
        self.assertIs(summary["successful"], False)
        self.assertEqual(summary["pending"], 1)
        self.assertEqual(summary["passed"], 0)
        self.assertEqual(json.loads((self.root / "summary.json").read_text(encoding="utf-8")), summary)
        self.assertIn("not started", (self.root / "report.html").read_text(encoding="utf-8"))
        self.assertFalse(list(self.root.glob("*.tmp")))

    def test_html_and_markdown_escape_external_strings(self):
        payload = '<script>alert("x")</script><img src=x onerror=alert(1)> [link](javascript:bad) | *bold*'
        result = self.case(payload, "failed")
        result.update(suite=payload, error=payload, metadata={payload: payload})
        environment = dict(self.environment)
        environment[payload] = payload
        write_reports(self.root, environment, [result], fatal_error=payload)
        document = (self.root / "report.html").read_text(encoding="utf-8")
        parser = _Links()
        parser.feed(document)
        self.assertNotIn("script", parser.tags)
        self.assertNotIn("img", parser.tags)
        self.assertIn("&lt;script&gt;", document)
        self.assertIn("&quot;x&quot;", document)
        markdown = (self.root / "report.md").read_text(encoding="utf-8")
        self.assertNotIn("<script>", markdown)
        self.assertIn(r"\[link\]\(javascript:bad\)", markdown)
        self.assertIn(r"\|", markdown)
        self.assertIn(r"\*bold\*", markdown)

    def test_only_existing_relative_quoted_file_links_and_frames(self):
        name = "case space # [x] (test) ' unicode_图"
        directory = self.root / name
        directory.mkdir()
        filenames = ("input.apng", "raw.png", "display.png", "reference.png",
                     "reference_1.png", "output.apng", "output_0.png", "output_1.png",
                     "worker.log", "worker.json", "prepared.json")
        for filename in filenames:
            (directory / filename).write_bytes(b"fixture")
        write_reports(self.root, self.environment, [self.case(name)])
        parser = _Links()
        parser.feed((self.root / "report.html").read_text(encoding="utf-8"))
        self.assertTrue(parser.links)
        linked = set()
        for link in parser.links:
            self.assertFalse(urlsplit(link).scheme)
            self.assertFalse(urlsplit(link).netloc)
            self.assertFalse(link.startswith("/"))
            self.assertNotIn(" ", link)
            self.assertNotIn("#", link)
            self.assertNotIn("'", link)
            path = self.root / unquote(link)
            self.assertTrue(path.is_file(), link)
            linked.add(path.name)
        self.assertEqual(linked, set(filenames))
        markdown = (self.root / "report.md").read_text(encoding="utf-8")
        for link in re.findall(r"\]\(([^)]+)\)", markdown):
            self.assertTrue((self.root / unquote(link)).is_file(), link)
        self.assertIn("| 1 |", markdown)
        self.assertNotIn("output_2.png", markdown)

    def test_actual_apng_stage_filenames(self):
        directory = self.root / 'animation'
        directory.mkdir()
        names = ('raw.png', 'display.png', 'reference.png', 'output_0.png',
                 'raw_frame1.png', 'display_frame1.png', 'reference_frame1.png', 'output_1.png')
        for name in names:
            (directory / name).write_bytes(b'fixture')
        write_reports(self.root, self.environment, [self.case('animation')])
        document = (self.root / 'report.html').read_text(encoding='utf-8')
        for name in names:
            self.assertIn(name, document)

    def test_missing_files_and_outside_directory_have_no_links(self):
        write_reports(self.root, self.environment, [self.case("missing"), self.case("../outside")])
        parser = _Links()
        parser.feed((self.root / "report.html").read_text(encoding="utf-8"))
        self.assertEqual(parser.links, [])


if __name__ == "__main__":
    unittest.main()
