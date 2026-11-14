"""把 res/icons/*.svg 栅格化成 res/icons/*.png，供构建时嵌入 exe。

输出：128x128、白色 RGB + 覆盖率 alpha 的 PNG（预乘无关的直通 alpha）。
运行时用 WIC 解码后上传成 D3D11 纹理，按主题色染色绘制，因此这里只关心 alpha。

用法：python tools/rasterize_icons.py
依赖：svgelements、Pillow
"""

from __future__ import annotations

import pathlib
import sys
import xml.etree.ElementTree as ET

from PIL import Image, ImageDraw
from svgelements import Path

SS = 8  # 超采样倍数：1024 视图正好等于 128 输出的 8 倍
SIZE = 128
VIEW = 1024.0
CURVE_STEPS = 64

ROOT = pathlib.Path(__file__).resolve().parent.parent
ICON_DIR = ROOT / "res" / "icons"
NAMES = ["pin", "opacity", "reset", "close"]


def signed_area(points: list[tuple[float, float]]) -> float:
    total = 0.0
    for i in range(len(points)):
        x0, y0 = points[i]
        x1, y1 = points[(i + 1) % len(points)]
        total += x0 * y1 - x1 * y0
    return total * 0.5


def point_in_polygon(point: tuple[float, float], polygon: list[tuple[float, float]]) -> bool:
    x, y = point
    inside = False
    for i in range(len(polygon)):
        x0, y0 = polygon[i]
        x1, y1 = polygon[(i + 1) % len(polygon)]
        if (y0 > y) != (y1 > y):
            cross = x0 + (y - y0) * (x1 - x0) / (y1 - y0)
            if cross > x:
                inside = not inside
    return inside


def flatten(d: str) -> list[list[tuple[float, float]]]:
    """把 path 的 d 展开成若干条折线（绝对坐标，视口单位）。"""
    subpaths: list[list[tuple[float, float]]] = []
    current: list[tuple[float, float]] = []
    for segment in Path(d):
        end = segment.end
        if segment.__class__.__name__ == "Move":
            if len(current) > 1:
                subpaths.append(current)
            current = [(float(end.x), float(end.y))]
            continue
        if not current:
            current = [(float(segment.start.x), float(segment.start.y))]
        for step in range(1, CURVE_STEPS + 1):
            point = segment.point(step / CURVE_STEPS)
            current.append((float(point.x), float(point.y)))
        if segment.__class__.__name__ == "Close":
            subpaths.append(current)
            current = []
    if len(current) > 1:
        subpaths.append(current)
    return subpaths


def render(svg_file: pathlib.Path) -> Image.Image:
    root = ET.parse(svg_file).getroot()
    ns = ""
    if root.tag.startswith("{"):
        ns = root.tag.split("}")[0] + "}"
    path_element = root.find(f"{ns}path")
    if path_element is None:
        raise RuntimeError(f"{svg_file.name} 里找不到 <path>")

    d = path_element.get("d", "")
    stroke_width = float(path_element.get("stroke-width", 0) or 0)
    has_stroke = path_element.get("stroke") is not None and stroke_width > 0

    subpaths = [p for p in flatten(d) if len(p) > 2]
    if not subpaths:
        raise RuntimeError(f"{svg_file.name} 展开后没有有效轮廓")

    canvas = Image.new("L", (int(VIEW), int(VIEW)), 0)
    draw = ImageDraw.Draw(canvas)

    # 同向大轮廓填充，嵌套在里面的反向轮廓挖空（图钉的内孔、透明度图标的左半内圆）。
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

    if has_stroke:
        width = max(1, int(round(stroke_width)))
        for polygon in subpaths:
            outline = list(polygon)
            if outline[0] != outline[-1]:
                outline.append(outline[0])
            draw.line(outline, fill=255, width=width, joint="curve")

    return canvas.resize((SIZE, SIZE), Image.BOX)


def main() -> int:
    mask_sheet = Image.new("L", (SIZE * len(NAMES), SIZE), 0)
    for index, name in enumerate(NAMES):
        svg_file = ICON_DIR / f"{name}.svg"
        if not svg_file.exists():
            print(f"缺少 {svg_file}", file=sys.stderr)
            return 1
        mask = render(svg_file)
        mask_sheet.paste(mask, (SIZE * index, 0))
        white = Image.new("RGBA", mask.size, (255, 255, 255, 0))
        white.putalpha(mask)
        white.save(ICON_DIR / f"{name}.png", optimize=True)
        print(f"{name}.png 已生成：{white.size[0]}x{white.size[1]}")

    preview = Image.new("RGBA", (SIZE * len(NAMES), SIZE), (30, 32, 36, 255))
    preview.alpha_composite(Image.merge("RGBA", (
        Image.new("L", mask_sheet.size, 255),
        Image.new("L", mask_sheet.size, 255),
        Image.new("L", mask_sheet.size, 255),
        mask_sheet,
    )))
    preview.save(ICON_DIR / "sheet-preview.png")
    print("sheet-preview.png 已生成（仅供目视检查，可删除）")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
