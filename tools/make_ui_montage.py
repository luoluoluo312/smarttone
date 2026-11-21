from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
names = [
    "main", "perform", "perform_highlight", "perform_synced",
    "octave", "transpose", "metronome", "library",
    "playback", "paused", "settings", "popup",
    "record", "recording", "library_rec", "library_deleted",
]
imgs = []
for name in names:
    p = ROOT / "build" / f"ui_{name}.ppm"
    im = Image.open(p).convert("RGB")
    im.save(ROOT / "docs" / f"ui_{name}.png")
    imgs.append((name, im))

sheet = Image.new("RGB", (4 * 140 + 10, 4 * 190 + 10), (22, 22, 26))
draw = ImageDraw.Draw(sheet)
for i, (name, im) in enumerate(imgs):
    x = 10 + (i % 4) * 140
    y = 10 + (i // 4) * 190
    sheet.paste(im, (x, y))
    draw.text((x, y + 162), name, fill=(220, 220, 220))
sheet.resize((sheet.width * 2, sheet.height * 2), Image.Resampling.NEAREST).save(
    ROOT / "docs" / "ui_pages.png"
)
print("wrote", ROOT / "docs" / "ui_pages.png")
