"""Render PortBridge's authored SVG and multi-size Windows ICO. Requires PySide6 and Pillow."""
from pathlib import Path
import os
import io
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
from PySide6.QtCore import QByteArray, QBuffer, QIODevice, QRectF
from PySide6.QtGui import QImage, QPainter
from PySide6.QtSvg import QSvgRenderer
from PySide6.QtWidgets import QApplication
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
ASSETS = ROOT / 'assets/app-icon'
app = QApplication.instance() or QApplication([])
source = (ASSETS / 'portbridge.svg').read_bytes()

def render(size):
    # Render from the vector at 4x so every ICO entry has crisp antialiasing.
    svg = source
    if size <= 32:
        svg = svg.replace(b'stroke-width="22"', b'stroke-width="26"')
    renderer = QSvgRenderer(QByteArray(svg))
    assert renderer.isValid()
    image = QImage(size * 4, size * 4, QImage.Format_ARGB32_Premultiplied)
    image.fill(0)
    painter = QPainter(image)
    renderer.render(painter, QRectF(0, 0, size * 4, size * 4))
    painter.end()
    data = QByteArray()
    buffer = QBuffer(data)
    buffer.open(QIODevice.WriteOnly)
    assert image.save(buffer, 'PNG')
    return Image.open(io.BytesIO(bytes(data))).convert('RGBA').resize((size, size), Image.Resampling.LANCZOS)

sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
images = {size: render(size) for size in sizes}
# Pillow's append_images lets each small entry retain its stronger vector strokes.
images[256].save(ASSETS / 'portbridge.ico', sizes=[(size, size) for size in sizes],
                 append_images=[images[size] for size in sizes if size != 256])
render(1024).save(ASSETS / 'portbridge.png')
preview = Image.new('RGB', (1060, 590), '#edf1f1')
draw = ImageDraw.Draw(preview)
font = ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf', 17)
bold = ImageFont.truetype('C:/Windows/Fonts/segoeuib.ttf', 25)
draw.rectangle((530, 0, 1060, 590), fill='#101416')
for left, ink in [(0, '#213336'), (530, '#e5edee')]:
    draw.text((left + 38, 28), 'PortBridge', fill=ink, font=bold)
    draw.text((left + 38, 67), 'Two ports. One bridge.', fill=ink, font=font)
    preview.paste(images[256], (left + 137, 114), images[256])
    x = left + 40
    for size in [16, 24, 32, 48, 64, 128]:
        icon = images[size]
        preview.paste(icon, (x + (size < 48) * 10, 418), icon)
        draw.text((x, 418 + size + 12), str(size) + ' px', fill=ink, font=font)
        x += max(65, size + 15)
preview.save(ASSETS / 'preview.png')
with Image.open(ASSETS / 'portbridge.ico') as ico:
    assert ico.ico.sizes() == {(size, size) for size in sizes}
print('Generated SVG-based PNG, preview, and 9-size ICO:', ASSETS)
