"""Render the SVG logo into Windows icon and preview PNG assets.

Requires Pillow and PyMuPDF; neither is needed to build or run the app.
"""

from pathlib import Path

import fitz
from PIL import Image


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "assets" / "logo.svg"
SIZES = (16, 24, 32, 48, 64, 128, 256)


def render(page, size):
    scale = size * 4 / 64
    pixmap = page.get_pixmap(matrix=fitz.Matrix(scale, scale), alpha=True)
    image = Image.frombytes("RGBA", (pixmap.width, pixmap.height), pixmap.samples)
    return image.resize((size, size), Image.Resampling.LANCZOS)


with fitz.open(stream=SOURCE.read_bytes(), filetype="svg") as document:
    page = document[0]
    frames = [render(page, size) for size in SIZES]
    frames[-1].save(
        ROOT / "src" / "TaskbarMonitor.ico",
        format="ICO",
        sizes=[(size, size) for size in SIZES],
        append_images=frames[:-1],
    )
    render(page, 512).save(ROOT / "assets" / "logo.png")
