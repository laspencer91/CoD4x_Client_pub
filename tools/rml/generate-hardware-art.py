"""Generate shared fixed-function UI surfaces. Requires Pillow."""
from pathlib import Path
from PIL import Image

output = Path(__file__).parents[2] / 'ui/rml/images'
output.mkdir(parents=True, exist_ok=True)

gradient = Image.new('RGBA', (8, 128))
for y in range(128):
    for x in range(8):
        gradient.putpixel((x, y), (220, 220, 230, round(24 * (1 - y / 127))))
gradient.save(output / 'panel-gradient.tga', compression=None)

lines = Image.new('RGBA', (2, 4))
for x in range(2):
    lines.putpixel((x, 0), (220, 224, 218, 12))
lines.save(output / 'card-scanlines.tga', compression=None)

holo = Image.new('RGBA', (256, 64))
for y in range(64):
    for x in range(256):
        fade = max(0, 1 - abs(x / 255 - .5) * 2) ** .8
        core = max(0, 1 - abs(y / 63 - .5) * 2)
        alpha = fade * (.06 + .22 * core + (.08 if y % 4 == 0 else 0))
        holo.putpixel((x, y), (217, 186, 112, round(255 * alpha)))
holo.save(output / 'deploy-field.tga', compression=None)

shadow = Image.new('RGBA', (128, 64))
for y in range(64):
    for x in range(128):
        radius = ((x - 63.5) / 63.5) ** 2 + ((y - 31.5) / 31.5) ** 2
        shadow.putpixel((x, y), (0, 0, 0, round(140 * max(0, 1 - radius) ** 2)))
shadow.save(output / 'weapon-shadow.tga', compression=None)
