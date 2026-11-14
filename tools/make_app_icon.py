"""把 res/logo.svg（原稿）打包成 res/wind.ico。

- 所有尺寸都用原稿：三条渐变弧线，保留 0.9 不透明度，透明底。
- ICO 内每一档都存 PNG 压缩帧，编译进 exe 后 LoadImageW 直接可读。

用法：python tools/make_app_icon.py
依赖：numpy、Pillow、svgelements（与 rasterize_icons.py 一致）
"""

from __future__ import annotations

import io
import pathlib
import re
import struct
import sys
import xml.etree.ElementTree as ET

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
from rasterize_icons import flatten, point_in_polygon, signed_area  # noqa: E402

VIEW = 1024
SIZES = [256, 128, 96, 64, 48, 40, 32, 24, 20, 16]

ROOT = pathlib.Path(__file__).resolve().parent.parent
RES = ROOT / "res"
LOGO_SVG = RES / "logo.svg"
ICO_FILE = RES / "wind.ico"
PREVIEW_FILE = ROOT / "build" / "wind-ico-preview.png"


# --------------------------------------------------------------------------- 变换与渐变


def mat_mul(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return a @ b


def parse_transform(text: str | None) -> np.ndarray:
    matrix = np.eye(3)
    if not text:
        return matrix
    for name, args in re.findall(r"(\w+)\s*\(([^)]*)\)", text):
        numbers = [float(v) for v in re.split(r"[\s,]+", args.strip()) if v]
        step = np.eye(3)
        if name == "translate":
            step[0, 2] = numbers[0]
            step[1, 2] = numbers[1] if len(numbers) > 1 else 0.0
        elif name == "scale":
            step[0, 0] = numbers[0]
            step[1, 1] = numbers[1] if len(numbers) > 1 else numbers[0]
        elif name == "rotate":
            angle = np.radians(numbers[0])
            step[0, 0], step[0, 1] = np.cos(angle), -np.sin(angle)
            step[1, 0], step[1, 1] = np.sin(angle), np.cos(angle)
            if len(numbers) == 3:
                cx, cy = numbers[1], numbers[2]
                step = mat_mul(mat_mul(
                    np.array([[1, 0, cx], [0, 1, cy], [0, 0, 1]], float),
                    step,
                ), np.array([[1, 0, -cx], [0, 1, -cy], [0, 0, 1]], float))
        elif name == "matrix":
            step = np.array([[numbers[0], numbers[2], numbers[4]],
                             [numbers[1], numbers[3], numbers[5]],
                             [0, 0, 1]], float)
        matrix = mat_mul(matrix, step)
    return matrix


def parse_css(root: ET.Element, ns: str) -> dict[str, dict[str, str]]:
    """解析 Illustrator 导出的 <style>，取出 .cls-n { fill: ...; opacity: ... }。"""
    rules: dict[str, dict[str, str]] = {}
    style = next(root.iter(f"{ns}style"), None)
    if style is None or not style.text:
        return rules
    for selectors, body in re.findall(r"([^{}]+)\{([^}]*)\}", style.text):
        props = {}
        for declaration in body.split(";"):
            if ":" in declaration:
                key, value = declaration.split(":", 1)
                props[key.strip()] = value.strip()
        for selector in selectors.split(","):
            selector = selector.strip()
            if selector.startswith("."):
                rules.setdefault(selector[1:], {}).update(props)
    return rules


def parse_gradients(root: ET.Element, ns: str) -> dict[str, dict]:
    gradients: dict[str, dict] = {}
    for element in root.iter(f"{ns}linearGradient"):
        p1 = (float(element.get("x1", 0)), float(element.get("y1", 0)))
        p2 = (float(element.get("x2", 0)), float(element.get("y2", 0)))
        stops = []
        for stop in element.findall(f"{ns}stop"):
            offset = stop.get("offset", "0")
            offset = float(offset[:-1]) / 100 if offset.endswith("%") else float(offset)
            color = stop.get("stop-color", "#000000").lstrip("#")
            stops.append((offset, tuple(int(color[i:i + 2], 16) for i in (0, 2, 4))))
        stops.sort(key=lambda s: s[0])
        gradients[element.get("id")] = {
            "p1": p1,
            "p2": p2,
            # 渐变坐标 → 用户坐标；取逆后即可把用户坐标投到渐变轴上。
            "inverse": np.linalg.inv(parse_transform(element.get("gradientTransform"))),
            "stops": stops,
        }
    return gradients


# --------------------------------------------------------------------------- 光栅化


def element_mask(subpaths: list[list[tuple[float, float]]], stroke_width: float) -> np.ndarray:
    canvas = Image.new("L", (VIEW, VIEW), 0)
    draw = ImageDraw.Draw(canvas)
    for index in sorted(range(len(subpaths)), key=lambda i: -abs(signed_area(subpaths[i]))):
        polygon = subpaths[index]
        depth = sum(
            1
            for other in range(len(subpaths))
            if other != index
            and abs(signed_area(subpaths[other])) > abs(signed_area(polygon))
            and point_in_polygon(polygon[0], subpaths[other])
        )
        draw.polygon(polygon, fill=255 if depth % 2 == 0 else 0)
    if stroke_width > 0:
        width = max(1, int(round(stroke_width)))
        for polygon in subpaths:
            outline = list(polygon)
            if outline[0] != outline[-1]:
                outline.append(outline[0])
            draw.line(outline, fill=255, width=width, joint="curve")
    return np.asarray(canvas, dtype=np.float64) / 255.0


def gradient_rgb(gradient: dict) -> np.ndarray:
    inverse, p1, p2 = gradient["inverse"], gradient["p1"], gradient["p2"]
    y, x = np.mgrid[0:VIEW, 0:VIEW]
    gx = inverse[0, 0] * x + inverse[0, 1] * y + inverse[0, 2]
    gy = inverse[1, 0] * x + inverse[1, 1] * y + inverse[1, 2]
    vx, vy = p2[0] - p1[0], p2[1] - p1[1]
    length_sq = vx * vx + vy * vy
    t = np.clip(((gx - p1[0]) * vx + (gy - p1[1]) * vy) / length_sq, 0.0, 1.0)
    offsets = np.array([stop[0] for stop in gradient["stops"]])
    colors = np.array([stop[1] for stop in gradient["stops"]], dtype=np.float64) / 255.0
    return np.stack([np.interp(t, offsets, colors[:, channel]) for channel in range(3)], axis=-1)


def resolve_style(element: ET.Element, css: dict, inherited: dict) -> dict:
    """SVG 的 fill/stroke 会从 <g> 继承下来，这里按 CSS 类 → 行内属性的顺序解析。"""
    style = dict(inherited)
    style.update(css.get(element.get("class", ""), {}))
    for key in ("fill", "stroke", "stroke-width"):
        if key in element.attrib:
            style[key] = element.attrib[key]
    if "opacity" in element.attrib:
        style["opacity"] = inherited.get("opacity", 1.0) * float(element.get("opacity"))
    return style


def paint_of(style: dict, gradients: dict) -> tuple[np.ndarray | str, float]:
    fill = style.get("fill", "#000000")
    opacity = float(style.get("opacity", 1.0))
    match = re.fullmatch(r"url\(#(.+)\)", fill)
    if match:
        return gradient_rgb(gradients[match.group(1)]), opacity
    return parse_color(fill), opacity


def parse_color(value: str) -> np.ndarray:
    value = value.strip().lstrip("#")
    if len(value) == 3:
        value = "".join(channel * 2 for channel in value)
    return np.array([int(value[i:i + 2], 16) for i in (0, 2, 4)], dtype=np.float64) / 255.0


def walk(element: ET.Element, ns: str, parent_ctm: np.ndarray, css: dict, gradients: dict,
         layers: list[tuple[np.ndarray, float, float]], inherited: dict | None = None) -> None:
    """按文档顺序收集 (颜色, alpha, 覆盖率) 三层信息，稍后统一合成。"""
    tag = element.tag.replace(ns, "")
    inherited = inherited or {"opacity": 1.0}

    if tag == "defs":
        return
    style = resolve_style(element, css, inherited)
    if tag == "g":
        ctm = mat_mul(parent_ctm, parse_transform(element.get("transform")))
        for child in element:
            walk(child, ns, ctm, css, gradients, layers, style)
        return
    if tag not in ("path", "rect"):
        return

    ctm = mat_mul(parent_ctm, parse_transform(element.get("transform")))
    color, opacity = paint_of(style, gradients)

    if tag == "rect":
        width = float(element.get("width", 0))
        height = float(element.get("height", 0))
        radius = float(element.get("rx", 0))
        canvas = Image.new("L", (VIEW, VIEW), 0)
        ImageDraw.Draw(canvas).rounded_rectangle(
            [_apply(ctm, 0, 0), _apply(ctm, width, height)], radius=radius, fill=255)
        coverage = np.asarray(canvas, dtype=np.float64) / 255.0
    else:
        subpaths = [polygon for polygon in flatten(element.get("d", "")) if len(polygon) > 2]
        if not subpaths:
            return
        mapped = [[_apply(ctm, x, y) for x, y in polygon] for polygon in subpaths]
        stroke_width = 0.0
        if style.get("stroke", "none") != "none":
            stroke_width = float(style.get("stroke-width", 0) or 0)
        scale = float(np.sqrt(abs(np.linalg.det(ctm[:2, :2]))))
        coverage = element_mask(mapped, stroke_width * scale)

    layers.append((color, opacity, coverage))


def _apply(ctm: np.ndarray, x: float, y: float) -> tuple[float, float]:
    return (ctm[0, 0] * x + ctm[0, 1] * y + ctm[0, 2],
            ctm[1, 0] * x + ctm[1, 1] * y + ctm[1, 2])


def render_svg(svg_file: pathlib.Path) -> tuple[np.ndarray, np.ndarray]:
    """返回预乘 RGB（0..1）与 alpha（0..1），尺寸均为 1024x1024。"""
    root = ET.parse(svg_file).getroot()
    ns = root.tag.split("}")[0] + "}" if root.tag.startswith("{") else ""
    css = parse_css(root, ns)
    gradients = parse_gradients(root, ns)

    layers: list[tuple[np.ndarray, float, float]] = []
    for child in root:
        walk(child, ns, np.eye(3), css, gradients, layers)

    premultiplied = np.zeros((VIEW, VIEW, 3))
    alpha = np.zeros((VIEW, VIEW))
    for color, opacity, coverage in layers:
        source_alpha = coverage * opacity
        weighted = color * source_alpha[..., None]
        premultiplied = weighted + premultiplied * (1 - source_alpha[..., None])
        alpha = source_alpha + alpha * (1 - source_alpha)
    return premultiplied, alpha


def downscale(rgb: np.ndarray, alpha: np.ndarray, size: int) -> Image.Image:
    """在预乘空间缩放，避免透明边缘出现暗边，再反预乘回直通 alpha。"""
    channels = np.concatenate([rgb, alpha[..., None]], axis=-1)
    raw = np.clip(channels * 255.0 + 0.5, 0, 255).astype(np.uint8)
    small = np.asarray(Image.fromarray(raw, "RGBA").resize((size, size), Image.LANCZOS), dtype=np.float64)
    small_alpha = small[..., 3:4] / 255.0
    unweighted = np.divide(small[..., :3], small_alpha, out=np.zeros_like(small[..., :3]), where=small_alpha > 0)
    straight = np.clip(unweighted + 0.5, 0, 255).astype(np.uint8)
    return Image.fromarray(np.concatenate([straight, small[..., 3:4]], axis=-1).astype(np.uint8), "RGBA")


# --------------------------------------------------------------------------- ICO 打包


def write_ico(path: pathlib.Path, frames: list[Image.Image]) -> None:
    payloads = []
    for frame in sorted(frames, key=lambda f: -f.width):
        buffer = io.BytesIO()
        frame.save(buffer, format="PNG", optimize=True)
        payloads.append((frame.width, frame.height, buffer.getvalue()))

    offset = 6 + 16 * len(payloads)
    header = struct.pack("<HHH", 0, 1, len(payloads))
    entries = bytearray()
    blob = bytearray()
    for width, height, data in payloads:
        entries += struct.pack("<BBBBHHII", width % 256, height % 256, 0, 0, 1, 32, len(data), offset)
        blob += data
        offset += len(data)
    path.write_bytes(header + bytes(entries) + bytes(blob))


def main() -> int:
    rgb, alpha = render_svg(LOGO_SVG)
    frames = [downscale(rgb, alpha, size) for size in SIZES]
    write_ico(ICO_FILE, frames)
    print(f"{ICO_FILE.name} 已生成：{', '.join(str(f.width) for f in frames)}")

    sheet = Image.new("RGBA", (sum(f.width + 8 for f in frames), max(f.width for f in frames)), (0, 0, 0, 0))
    x = 0
    for frame in frames:
        sheet.alpha_composite(frame, (x, 0))
        x += frame.width + 8
    PREVIEW_FILE.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(PREVIEW_FILE)
    print(f"{PREVIEW_FILE.name} 已生成（仅供目视检查，位于 build/ 下，不进仓库）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
