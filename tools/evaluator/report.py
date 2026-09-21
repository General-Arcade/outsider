# Copyright (c) 2026 General Arcade (Pte. Ltd.)
# SPDX-License-Identifier: GPL-3.0-or-later OR LicenseRef-GeneralArcade-Commercial
"""report.json / report.html / console summary for one evaluator run."""

import html
import json
import os


def write_json(path, report):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(report, f, indent=2)


def console_summary(report):
    lines = []
    lines.append("Visual evaluation: %s (scenario %s)" % (report["game"], report["scenario"]))
    lines.append("")
    lines.append("%-16s %-7s %8s %9s  %s" % ("shot", "verdict", "changed", "mean", "largest differing region"))
    for shot in report["shots"]:
        m = shot.get("metrics")
        if not m:
            lines.append("%-16s %-7s %8s %9s  %s" % (shot["name"], shot["verdict"], "-", "-", shot.get("note", "")))
            continue
        region = ""
        if m["regions"]:
            x, y, w, h, pct = m["regions"][0]
            region = "%dx%d at (%d,%d), %.0f%% of it" % (w, h, x, y, pct)
        if m.get("size_mismatch"):
            region = "size %dx%d vs %dx%d" % (m["width"], m["height"], *m["size_mismatch"])
        lines.append("%-16s %-7s %7.2f%% %9.2f  %s" % (shot["name"], shot["verdict"], m["changed_pct"],
                                                     m["mean_diff"], region))
    counts = report["summary"]
    lines.append("")
    lines.append("match %d, minor %d, differ %d, missing %d, skipped %d" % (
        counts["match"], counts["minor"], counts["differ"], counts["missing"], counts.get("skipped", 0)))
    errors = [(side, r) for side in ("original", "outsider")
              for r in report["steps"].get(side, []) if r["status"] == "error"]
    if errors:
        lines.append("")
        lines.append("Step errors:")
        for side, r in errors:
            lines.append("  %s step %d: %s" % (side, r["index"], r["detail"]))
    return "\n".join(lines)


def write_html(path, report):
    e = html.escape
    rows = []
    for shot in report["shots"]:
        m = shot.get("metrics") or {}
        regions = "<br>".join("%dx%d at (%d,%d): %.0f%%" % (w, h, x, y, p)
                              for x, y, w, h, p in m.get("regions", [])[:4])
        rows.append(
            "<tr class='%s'><td><a href='#%s'>%s</a></td><td>%s</td><td>%s</td><td>%s</td><td>%s</td></tr>" % (
                shot["verdict"], e(shot["name"]), e(shot["name"]), shot["verdict"],
                "%.2f%%" % m["changed_pct"] if m else "-",
                "%.2f" % m["mean_diff"] if m else "-",
                regions or e(shot.get("note", ""))))

    sections = []
    for shot in report["shots"]:
        name = e(shot["name"])
        parts = ["<section id='%s'><h2>%s <span class='%s'>%s</span></h2>" % (
            name, name, shot["verdict"], shot["verdict"])]
        if shot.get("note"):
            parts.append("<p>%s</p>" % e(shot["note"]))
        if shot.get("original") and shot.get("outsider"):
            parts.append(
                "<div class='flip' data-a='%s' data-b='%s'>"
                "<img src='%s' alt='original'><div class='label'>original (hover to see outsider)</div></div>"
                % (e(shot["original"]), e(shot["outsider"]), e(shot["original"])))
        if shot.get("diff"):
            parts.append("<p><img class='wide' src='%s' alt='diff'></p>" % e(shot["diff"]))
        parts.append("</section>")
        sections.append("".join(parts))

    step_tables = []
    for side in ("original", "outsider"):
        results = report["steps"].get(side)
        if not results:
            continue
        trs = "".join("<tr class='%s'><td>%d</td><td>%s</td><td>%s</td><td>%s</td></tr>" % (
            r["status"], r["index"], e(describe_step(r["step"])), r["status"], e(r["detail"] or ""))
            for r in results)
        step_tables.append("<h3>%s</h3><table><tr><th>#</th><th>step</th><th>status</th><th>detail</th></tr>%s</table>"
                           % (side, trs))

    counts = report["summary"]
    doc = """<!doctype html><html><head><meta charset='utf-8'>
<title>Visual evaluation: %(game)s</title>
<style>
body{font-family:system-ui,sans-serif;margin:20px;background:#1e1e1e;color:#ddd}
table{border-collapse:collapse;margin:10px 0}td,th{border:1px solid #444;padding:4px 8px;text-align:left;vertical-align:top}
tr.match td:nth-child(2),span.match{color:#6c6}tr.minor td:nth-child(2),span.minor{color:#dc6}
tr.differ td:nth-child(2),span.differ{color:#e66}tr.missing td:nth-child(2),span.missing{color:#e66}
tr.skipped td:nth-child(2),span.skipped{color:#999}
tr.error td:nth-child(3){color:#e66}tr.skipped td:nth-child(3){color:#999}
img{max-width:100%%;image-rendering:pixelated}.wide{width:100%%}
.flip{position:relative;display:inline-block;cursor:crosshair}.flip .label{font-size:12px;color:#999}
a{color:#8cf}section{margin-top:30px;border-top:1px solid #333;padding-top:10px}
</style></head><body>
<h1>Visual evaluation: %(game)s</h1>
<p>Scenario <b>%(scenario)s</b>, %(width)sx%(height)s, tolerance %(tolerance)s.
match %(match)d, minor %(minor)d, differ %(differ)d, missing %(missing)d, skipped %(skipped)d.</p>
<table><tr><th>shot</th><th>verdict</th><th>changed</th><th>mean diff</th><th>regions (w x h at x,y: %% changed)</th></tr>%(rows)s</table>
%(sections)s
<h2>Steps</h2>%(steps)s
<script>
document.querySelectorAll('.flip').forEach(function(el){
  var img=el.querySelector('img');
  el.addEventListener('mouseenter',function(){img.src=el.dataset.b;el.querySelector('.label').textContent='outsider';});
  el.addEventListener('mouseleave',function(){img.src=el.dataset.a;el.querySelector('.label').textContent='original (hover to see outsider)';});
});
</script></body></html>""" % {
        "game": e(report["game"]), "scenario": e(report["scenario"]),
        "width": report["width"], "height": report["height"], "tolerance": report["tolerance"],
        "match": counts["match"], "minor": counts["minor"], "differ": counts["differ"],
        "missing": counts["missing"], "skipped": counts.get("skipped", 0),
        "rows": "".join(rows), "sections": "".join(sections),
        "steps": "".join(step_tables)}
    with open(path, "w", encoding="utf-8") as f:
        f.write(doc)


def describe_step(step):
    return " ".join("%s=%s" % (k, v if isinstance(v, str) else json.dumps(v)) for k, v in step.items())
