#!/usr/bin/env python3
"""Copy dxe and map.xml into output/, build the dual-core ldr, and write map.html.

Run from anywhere:

    python post_build.py

Artifacts are taken from each core's Debug directory, or Release if Debug is
absent. They land in output/<project>/ with no configuration folder. The
loader image is one file, output/adsp21593.ldr, because both SHARC cores boot
from a single SPI stream. The init code is the EV-21593-SOM image shipped
with CCES; it sets SCLK0 to 125 MHz, which is what timer.c assumes.
"""

from __future__ import annotations

import html
import os
import re
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUTPUT = ROOT / "output"
PROJECTS = ("sharc1", "sharc2")
CONFIGS = ("Debug", "Release")
PROC = "ADSP-21593"
SI_REVISION = "0.0"


def main() -> int:
    dxe = {}
    copied = []
    for project in PROJECTS:
        src_dir = _config_dir(project)
        if src_dir is None:
            print("skip %s: no Debug or Release directory" % project, file=sys.stderr)
            continue
        artifacts = [p for p in _artifacts(src_dir) if p.suffix.lower() != ".ldr"]
        if not artifacts:
            print("skip %s: no dxe or map.xml in %s" % (project, src_dir), file=sys.stderr)
            continue
        dest_dir = OUTPUT / project
        dest_dir.mkdir(parents=True, exist_ok=True)
        for src in artifacts:
            dest = dest_dir / src.name
            shutil.copy2(src, dest)
            copied.append(dest)
            print("copy %s -> %s" % (src.relative_to(ROOT), dest.relative_to(ROOT)))
            if src.suffix.lower() == ".dxe":
                dxe[project] = dest
            if src.name.endswith(".map.xml"):
                html_path = dest.with_name(src.name[: -len(".xml")] + ".html")
                html_path.write_text(map_xml_to_html(dest), encoding="utf-8")
                print("html %s" % html_path.relative_to(ROOT))

    if "sharc1" not in dxe or "sharc2" not in dxe:
        print("需要 sharc1.dxe 和 sharc2.dxe 才能生成 ldr。", file=sys.stderr)
        return 1

    ldr = OUTPUT / "adsp21593.ldr"
    if _build_ldr(dxe["sharc1"], dxe["sharc2"], ldr) != 0:
        return 1
    copied.append(ldr)
    print("ldr  %s" % ldr.relative_to(ROOT))
    return 0 if copied else 1


def _config_dir(project: str):
    for config in CONFIGS:
        path = ROOT / project / config
        if path.is_dir():
            return path
    return None


def _artifacts(src_dir: Path):
    for pattern in ("*.dxe", "*.map.xml"):
        yield from sorted(src_dir.glob(pattern))


def _build_ldr(core1: Path, core2: Path, dest: Path) -> int:
    elfloader, init_dxe = _cces_loader()
    if elfloader is None:
        print("找不到 elfloader.exe 或 ezkit21593_initcode_core1.dxe。设置 CCES_HOME。", file=sys.stderr)
        return 1
    dest.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(elfloader),
        "-proc", PROC,
        "-si-revision", SI_REVISION,
        "-b", "SPI",
        "-f", "BINARY",
        "-Width", "8",
        "-bcode", "1",
        "-init", str(init_dxe),
        "-core1=%s" % core1,
        "-core2=%s" % core2,
        "-NoFinalTag=%s" % core1,
        "-o", str(dest),
    ]
    print(" ".join(cmd))
    result = subprocess.run(cmd)
    if result.returncode != 0 or not dest.is_file():
        print("elfloader 失败，退出码 %s" % result.returncode, file=sys.stderr)
        return result.returncode or 1
    return 0


def _cces_loader():
    roots = []
    env = os.environ.get("CCES_HOME")
    if env:
        roots.append(Path(env))
    roots.append(Path(r"E:\bin\cces\3.0.3"))
    for root in roots:
        exe = root / "elfloader.exe"
        init = root / "SHARC" / "ldr" / "ezkit21593_initcode_core1.dxe"
        if exe.is_file() and init.is_file():
            return exe, init
    return None, None


def map_xml_to_html(xml_path: Path) -> str:
    """按 CCES linker_map_ss1.xsl 的表结构把链接 map 写成独立 HTML。"""
    text = xml_path.read_text(encoding="utf-8", errors="replace")
    text = re.sub(r"<!DOCTYPE[^>]*>", "", text, count=1)
    root = ET.fromstring(text)
    parts = [
        "<!DOCTYPE html>",
        "<html lang=\"en\">",
        "<head>",
        "<meta charset=\"utf-8\">",
        "<title>%s</title>" % esc(xml_path.name),
        "<style>",
        "body { font-family: sans-serif; margin: 1.2rem; }",
        "table { border-collapse: collapse; margin: 0.4rem 0 1rem; }",
        "th, td { border: 1px solid #888; padding: 2px 6px; vertical-align: top; }",
        "thead { background: #006fff; color: #fff; }",
        "td.num { text-align: right; font-family: Consolas, monospace; }",
        "</style>",
        "</head>",
        "<body>",
    ]
    projects = list(root.findall("LINKPROJECT"))
    if len(projects) > 1:
        for project in projects:
            parts.append(
                '<p><a href="#%s">Memory map of %s</a></p>'
                % (esc(project.get("id", "")), esc(project.get("name", "")))
            )
        parts.append("<hr>")
    for project in projects:
        _link_project(parts, project)
    for overlay in root.findall("OVERLAYLINKPROJECT"):
        parts.append("<h1>Overlay <i>%s</i></h1>" % esc(_text(overlay.find("FILE_NAME"))))
        sections = overlay.find("OUTPUT_SECTIONS")
        if sections is not None:
            _output_sections(parts, sections)
    parts.append("</body></html>\n")
    return "\n".join(parts)


def _link_project(parts, project) -> None:
    parts.append(
        '<h1 id="%s">Memory map of link project <i>%s</i></h1>'
        % (esc(project.get("id", "")), esc(project.get("name", "")))
    )
    dxe = project.find("FILE_NAME")
    if dxe is not None and (dxe.text or "").strip():
        parts.append("<p>Executable: %s" % esc(dxe.text.strip()))
        arch = project.get("architecture")
        if arch:
            parts.append(" (%s)" % esc(arch))
        parts.append("</p>")

    memories = list(project.findall("MEMORY"))
    has_type = any((mem.get("type") or "") != "" for mem in memories)
    headers = ["Memory", "Start address", "End address"]
    if has_type:
        headers.append("Type")
    headers.extend(["Qualifier", "Width", "Used words[1]", "Unused words"])
    parts.append("<table><thead><tr>")
    for header in headers:
        parts.append("<th>%s</th>" % header)
    parts.append("</tr></thead><tbody>")
    for mem in memories:
        parts.append("<tr>")
        parts.append(
            '<td><a href="#%s">%s</a></td>'
            % (esc(mem.get("id", "")), esc(mem.get("name", "")))
        )
        parts.append(_num(mem.get("start_address", "")))
        parts.append(_num(mem.get("end_address", "")))
        if has_type:
            parts.append(_num(mem.get("type", "")))
        parts.append(_num(mem.get("qualifier", "")))
        parts.append(_num(mem.get("width", "")))
        parts.append(_num(mem.get("words_used", "")))
        parts.append(_num(mem.get("words_unused", "")))
        parts.append("</tr>")
    parts.append("</tbody></table>")
    parts.append("<p>Note [1]: Padding bytes for alignment gaps are not counted towards used words.</p>")
    parts.append("<hr>")

    symbols = project.find("LDF_SYMBOLS")
    if symbols is not None:
        _ldf_symbols(parts, symbols)
    for mem in memories:
        _memory(parts, mem)


def _ldf_symbols(parts, symbols) -> None:
    parts.append("<h2>LDF symbols <i>%s</i></h2>" % esc(symbols.get("name", "")))
    parts.append("<table><thead><tr><th>Symbol</th><th>Address</th></tr></thead><tbody>")
    for sym in symbols.findall("SYMBOL"):
        parts.append(
            "<tr><td>%s</td>%s</tr>"
            % (esc(sym.get("name", "")), _num(sym.get("address", "")))
        )
    parts.append("</tbody></table><hr>")


def _memory(parts, mem) -> None:
    parts.append(
        '<h2 id="%s">Memory <i>%s</i></h2>'
        % (esc(mem.get("id", "")), esc(mem.get("name", "")))
    )
    sections = mem.find("OUTPUT_SECTIONS")
    if sections is not None:
        _output_sections(parts, sections)


def _output_sections(parts, sections) -> None:
    blocks = list(sections.findall("OUTPUT_SECTION"))
    has_mem_type = any(block.get("memory_type") is not None for block in blocks)
    has_reserved = any((block.get("word_size_reserved") or "0x0") != "0x0" for block in blocks)
    headers = ["Output section", "Type"]
    if has_mem_type:
        headers.extend(["Memory type", "Memory width"])
    headers.extend(["Start address", "Size in words"])
    if has_reserved:
        headers.append("Reserved size in words")
    parts.append("<table><thead><tr>")
    for header in headers:
        parts.append("<th>%s</th>" % header)
    parts.append("</tr></thead><tbody>")
    for block in blocks:
        inputs = block.find("INPUT_SECTIONS")
        has_inputs = inputs is not None and list(inputs.findall("INPUT_SECTION"))
        name = esc(block.get("name", ""))
        if has_inputs:
            name = '<a href="#%s">%s</a>' % (esc(block.get("id", "")), name)
        parts.append("<tr><td>%s</td>" % name)
        parts.append(_num(block.get("type", "")))
        if has_mem_type:
            parts.append(_num(block.get("memory_type", "")))
            parts.append(_num(block.get("memory_width", "")))
        parts.append(_num(block.get("start_address", "")))
        parts.append(_num(block.get("word_size", "")))
        if has_reserved:
            parts.append(_num(block.get("word_size_reserved", "")))
        parts.append("</tr>")
    parts.append("</tbody></table><hr>")
    for block in blocks:
        inputs = block.find("INPUT_SECTIONS")
        if inputs is None or not list(inputs.findall("INPUT_SECTION")):
            continue
        _output_section_detail(parts, block, inputs)


def _output_section_detail(parts, block, inputs) -> None:
    parts.append(
        '<h3 id="%s">Output section <i>%s</i></h3>'
        % (esc(block.get("id", "")), esc(block.get("name", "")))
    )
    parts.append(
        "<table><thead><tr><th>Input section</th><th>Start address</th>"
        "<th>Size</th><th>Input file</th></tr></thead><tbody>"
    )
    input_sections = list(inputs.findall("INPUT_SECTION"))
    for section in input_sections:
        parts.append(
            '<tr><td><a href="#%s">%s</a></td>%s%s<td>%s</td></tr>'
            % (
                esc(section.get("id", "")),
                esc(section.get("name", "")),
                _num(section.get("start_address", "")),
                _num(section.get("size", "")),
                esc(_text(section.find("INPUT_FILE"))),
            )
        )
    parts.append("</tbody></table>")
    for section in input_sections:
        extra = ""
        if section.get("split") is not None:
            extra += ":<small>%s</small>" % esc(section.get("element_at", ""))
        if section.get("abs_placed") is not None:
            extra += "<small> (absolutely placed) </small>"
        parts.append(
            '<h3 id="%s">Input section <i>%s(%s)</i>%s</h3>'
            % (
                esc(section.get("id", "")),
                esc(_text(section.find("INPUT_FILE"))),
                esc(section.get("name", "")),
                extra,
            )
        )
        parts.append(
            "<table><thead><tr><th>Symbol</th><th>Demangled name</th>"
            "<th>Address</th><th>Size</th><th>Binding</th></tr></thead><tbody>"
        )
        for sym in section.findall("SYMBOL"):
            name = sym.get("name", "")
            if name.startswith("."):
                continue
            parts.append(
                "<tr><td>%s</td><td>%s</td>%s%s%s</tr>"
                % (
                    esc(name),
                    esc(_text(sym.find("DEMANGLED_NAME"))),
                    _num(sym.get("address", "")),
                    _num(sym.get("size", "")),
                    _num(sym.get("binding", "")),
                )
            )
        parts.append("</tbody></table>")
    parts.append("<hr>")


def _num(value: str) -> str:
    return '<td class="num">%s</td>' % esc(value)


def _text(node) -> str:
    if node is None or node.text is None:
        return ""
    return node.text.strip()


def esc(value: str) -> str:
    return html.escape(value, quote=True)


if __name__ == "__main__":
    sys.exit(main())
