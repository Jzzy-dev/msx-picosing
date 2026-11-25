#!/usr/bin/env python3
# A cover for the test song: a night sky with a big star (drawn here, no
# outside art). usage: make_test_cover.py <out.jpg>
import sys, math, random
from PIL import Image, ImageDraw, ImageFilter
W = 300
img = Image.new("RGB", (W, W))
d = ImageDraw.Draw(img)
for y in range(W):                       # deep blue to purple
    t = y / W
    d.line([(0, y), (W, y)], fill=(int(20 + 70 * t), int(20 + 10 * t), int(90 - 20 * t)))
random.seed(7)
for _ in range(120):                     # small stars
    x, y = random.randrange(W), random.randrange(int(W * 0.8))
    r = random.choice([1, 1, 1, 2])
    d.ellipse([x - r, y - r, x + r, y + r], fill=(255, 255, 220))
glow = Image.new("RGB", (W, W)); g = ImageDraw.Draw(glow)
g.ellipse([70, 40, 230, 200], fill=(120, 100, 30))
img = Image.blend(img, Image.composite(glow.filter(ImageFilter.GaussianBlur(30)), img, glow.convert("L").filter(ImageFilter.GaussianBlur(30))), 0.6)
d = ImageDraw.Draw(img)
cx, cy, R, r = 150, 120, 70, 28           # the big star
pts = [(cx + (R if i % 2 == 0 else r) * math.sin(i * math.pi / 5), cy - (R if i % 2 == 0 else r) * math.cos(i * math.pi / 5)) for i in range(10)]
d.polygon(pts, fill=(255, 215, 40), outline=(255, 250, 200))
d.rectangle([0, 240, W, W], fill=(15, 60, 30))   # a hill
d.ellipse([-60, 215, 360, 360], fill=(25, 90, 40))
img.save(sys.argv[1], quality=85)
