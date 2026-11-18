"""Portable JSON, Markdown and HTML reports for the integration runner."""

import html
import json
import os
from pathlib import Path
import re
import tempfile
from urllib.parse import quote


def _text(value):
    if isinstance(value, (dict, list, tuple)):
        return json.dumps(value, ensure_ascii=False, sort_keys=True)
    return str(value)


def _markdown(value):
    # Escape HTML as well: Markdown viewers commonly allow inline HTML.
    value = html.escape(_text(value), quote=True)
    value = re.sub(r"([\\`*_{}\[\]()#+.!|>~\-])", r"\\\1", value)
    return value.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "<br>")


def _escape(value):
    return html.escape(_text(value), quote=True)


def _atomic_write(path, text):
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", newline="\n", dir=path.parent,
            prefix="." + path.name + ".", suffix=".tmp", delete=False,
        ) as stream:
            temporary = Path(stream.name)
            stream.write(text)
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def _case_files(root, result):
    """Return only real, contained case files, never links outside the report."""
    directory = root / str(result.get("directory", result.get("name", "")))
    try:
        directory.resolve().relative_to(root)
    except (ValueError, OSError):
        return {}
    if not directory.is_dir():
        return {}
    files = {}
    for path in sorted(directory.iterdir()):
        try:
            path.resolve().relative_to(root)
            if path.is_file():
                files[path.name] = quote(path.relative_to(root).as_posix(), safe="/")
        except (ValueError, OSError):
            continue
    return files


def _frames(files):
    indices = {0}
    for name in files:
        match = re.fullmatch(r"(?:reference|output)_(?:frame)?(\d+)\.png", name)
        if match:
            indices.add(int(match.group(1)))
    rows = []
    for index in sorted(indices):
        reference = "reference.png" if index == 0 else "reference_%d.png" % index
        if index and reference not in files:
            reference = "reference_frame%d.png" % index
        output = "output_%d.png" % index
        if index == 0 and output not in files:
            output = "output.png"
        suffix = "_frame%d" % index if index else ""
        rows.append((index, ["raw%s.png" % suffix, "display%s.png" % suffix, reference, output]))
    return rows


def _assets(files):
    names = []
    for name in files:
        if (name.startswith("input.") or name.startswith("output.")
                or name in ("worker.log", "worker.json", "prepared.json")):
            names.append(name)
    return names


def write_reports(root, environment, results, fatal_error=""):
    """Write all report formats and return the JSON-serializable summary.

    File URLs are relative to *root*, percent-encoded, and emitted only for
    existing files inside that directory. Caller-owned result dictionaries are
    preserved in full and are not modified.
    """
    root = Path(root).resolve()
    root.mkdir(parents=True, exist_ok=True)
    results = list(results)
    summary = {
        "total": len(results),
        "passed": sum(case.get("status") == "passed" for case in results),
        "failed": sum(case.get("status") == "failed" for case in results),
        "timeouts": sum(case.get("status") == "timeout" for case in results),
        "pending": sum(case.get("status") == "pending" for case in results),
        "successful": not fatal_error and all(case.get("status") == "passed" for case in results),
        "environment": environment,
        "results": results,
        "fatal_error": fatal_error,
    }
    markdown = ["# Integration report", "", "## Summary", "", "| Statistic | Value |", "| --- | --- |"]
    body = ["<h1>Integration report</h1>", "<h2>Summary</h2>", "<table><thead><tr><th>Statistic</th><th>Value</th></tr></thead><tbody>"]
    for key in ("total", "passed", "failed", "timeouts", "pending", "successful"):
        markdown.append("| %s | %s |" % (_markdown(key), _markdown(summary[key])))
        body.append("<tr><th>%s</th><td>%s</td></tr>" % (_escape(key), _escape(summary[key])))
    body.append("</tbody></table>")
    if fatal_error:
        markdown.extend(["", "## Fatal error", "", _markdown(fatal_error)])
        body.extend(["<h2>Fatal error</h2>", "<pre>%s</pre>" % _escape(fatal_error)])
    markdown.extend(["", "## Environment", "", "| Setting | Value |", "| --- | --- |"])
    body.append("<h2>Environment</h2><table><thead><tr><th>Setting</th><th>Value</th></tr></thead><tbody>")
    for key, value in environment.items():
        markdown.append("| %s | %s |" % (_markdown(key), _markdown(value)))
        body.append("<tr><th>%s</th><td>%s</td></tr>" % (_escape(key), _escape(value)))
    body.append("</tbody></table><h2>Cases</h2>")
    markdown.extend(["", "## Cases"])
    for result in results:
        name = result.get("name", "")
        markdown.extend(["", "### " + _markdown(name), "", "| Field | Value |", "| --- | --- |"])
        body.append("<section><h3>%s</h3><table><tbody>" % _escape(name))
        for key in ("suite", "status", "seconds", "error"):
            value = result.get(key, "")
            markdown.append("| %s | %s |" % (_markdown(key), _markdown(value)))
            body.append("<tr><th>%s</th><td>%s</td></tr>" % (_escape(key), _escape(value)))
        body.append("</tbody></table>")
        if result.get("metadata") is not None:
            markdown.extend(["", "Metadata: " + _markdown(result["metadata"])])
            body.append("<h4>Metadata</h4><pre>%s</pre>" % _escape(result["metadata"]))
        files = _case_files(root, result)
        assets = _assets(files)
        if assets:
            markdown.extend(["", "Files: " + " · ".join(
                "[%s](%s)" % (_markdown(asset), files[asset]) for asset in assets)])
            body.append("<p>Files: " + " · ".join(
                '<a href="%s">%s</a>' % (_escape(files[asset]), _escape(asset)) for asset in assets) + "</p>")
        if any(image in files for _, images in _frames(files) for image in images):
            markdown.extend(["", "| Frame | Input raw | Input display | Reference | Output |", "| --- | --- | --- | --- | --- |"])
            body.append("<table class=images><thead><tr><th>Frame</th><th>Input raw</th><th>Input display</th><th>Reference</th><th>Output</th></tr></thead><tbody>")
            for index, images in _frames(files):
                md_cells, html_cells = [], []
                for image in images:
                    if image in files:
                        md_cells.append("![%s](%s)" % (_markdown(image), files[image]))
                        html_cells.append('<a href="%s"><img src="%s" alt="%s" loading="lazy"></a>' % (
                            _escape(files[image]), _escape(files[image]), _escape(image)))
                    else:
                        md_cells.append("—")
                        html_cells.append("—")
                markdown.append("| %d | %s |" % (index, " | ".join(md_cells)))
                body.append("<tr><th>%d</th>%s</tr>" % (index, "".join("<td>%s</td>" % cell for cell in html_cells)))
            body.append("</tbody></table>")
        body.append("</section>")
    document = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>Integration report</title><style>
body{font-family:system-ui,sans-serif;margin:2rem;line-height:1.5;color:#222;background:#fff}
table{border-collapse:collapse;margin:1rem 0;max-width:100%}th,td{border:1px solid #ccc;padding:.5rem;text-align:left;vertical-align:top;overflow-wrap:anywhere}
.images{table-layout:fixed;width:100%}.images img{max-width:100%;height:auto}pre{white-space:pre-wrap;overflow-wrap:anywhere}section{margin:2rem 0}a{color:#0645ad}
</style></head><body>""" + "\n".join(body) + "\n</body></html>\n"
    _atomic_write(root / "summary.json", json.dumps(summary, ensure_ascii=False, indent=2) + "\n")
    _atomic_write(root / "report.md", "\n".join(markdown) + "\n")
    _atomic_write(root / "report.html", document)
    simple = ["# 测试结果简表", "",
              "共 %d 项，通过 %d 项，未通过 %d 项。" % (
                  summary["total"], summary["passed"], summary["total"] - summary["passed"]),
              "", "| 用例 | 结果 |", "| --- | --- |"]
    for result in results:
        status = "通过" if result.get("status") == "passed" else "未通过"
        simple.append("| %s | %s |" % (_markdown(result.get("name", "")), status))
    _atomic_write(root / "简明结果.md", "\n".join(simple) + "\n")
    return summary
