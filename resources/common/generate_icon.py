"""
One-off script used to generate resources/common/kubewatch.{ico,png}.
Not part of the build; kept here for future regeneration if the design changes.
"""
import math
from PIL import Image, ImageDraw, ImageFilter

S = 4096  # supersample size, downscaled at the end for crisp anti-aliasing
img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
draw = ImageDraw.Draw(img)

cx = cy = S / 2

# ---- rounded-square background (Fluent/macOS style "squircle") ----
margin = S * 0.045
radius = S * 0.225

def vgrad_rounded_rect(size, box, radius, top, bottom):
    w, h = size
    grad = Image.new("RGBA", size, (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)
    x0, y0, x1, y1 = box
    for y in range(int(y0), int(y1)):
        t = (y - y0) / (y1 - y0)
        r = int(top[0] + (bottom[0] - top[0]) * t)
        g = int(top[1] + (bottom[1] - top[1]) * t)
        b = int(top[2] + (bottom[2] - top[2]) * t)
        gd.line([(x0, y), (x1, y)], fill=(r, g, b, 255))
    mask = Image.new("L", size, 0)
    md = ImageDraw.Draw(mask)
    md.rounded_rectangle(box, radius=radius, fill=255)
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    return out

bg = vgrad_rounded_rect(
    (S, S),
    (margin, margin, S - margin, S - margin),
    radius,
    top=(66, 133, 244),      # #4285F4 lighter azure
    bottom=(21, 67, 168),    # #1543A8 deep kubernetes blue
)

# subtle inner shadow / rim light for depth
rim = Image.new("RGBA", (S, S), (0, 0, 0, 0))
rd = ImageDraw.Draw(rim)
rd.rounded_rectangle((margin, margin, S - margin, S - margin), radius=radius,
                      outline=(255, 255, 255, 60), width=int(S * 0.006))
bg = Image.alpha_composite(bg, rim)

img = Image.alpha_composite(img, bg)
draw = ImageDraw.Draw(img)

# ---- cluster graph: 6 satellite nodes around a central "eye" hub ----
ring_r = S * 0.26
node_r = S * 0.068
hub_r = S * 0.15
line_w = int(S * 0.026)

satellites = []
for i in range(6):
    ang = math.radians(-90 + i * 60)
    nx = cx + ring_r * math.cos(ang)
    ny = cy + ring_r * math.sin(ang)
    satellites.append((nx, ny))

# connecting spokes: hub -> each satellite
line_layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
ld = ImageDraw.Draw(line_layer)
for (nx, ny) in satellites:
    ld.line([(cx, cy), (nx, ny)], fill=(255, 255, 255, 190), width=line_w)
img = Image.alpha_composite(img, line_layer)
draw = ImageDraw.Draw(img)

# satellite nodes
for (nx, ny) in satellites:
    draw.ellipse((nx - node_r, ny - node_r, nx + node_r, ny + node_r),
                 fill=(255, 255, 255, 250))

# soft glow behind the hub to make the "eye" pop
glow = Image.new("RGBA", (S, S), (0, 0, 0, 0))
gd = ImageDraw.Draw(glow)
gd.ellipse((cx - hub_r * 1.6, cy - hub_r * 1.6, cx + hub_r * 1.6, cy + hub_r * 1.6),
           fill=(255, 255, 255, 60))
glow = glow.filter(ImageFilter.GaussianBlur(S * 0.03))
img = Image.alpha_composite(img, glow)
draw = ImageDraw.Draw(img)

# hub = white eye "shell"
draw.ellipse((cx - hub_r, cy - hub_r, cx + hub_r, cy + hub_r), fill=(255, 255, 255, 255))
draw.ellipse((cx - hub_r, cy - hub_r, cx + hub_r, cy + hub_r),
             outline=(21, 67, 168, 255), width=int(S * 0.008))

# iris + pupil (the "watch")
iris_r = hub_r * 0.56
pupil_r = hub_r * 0.26
draw.ellipse((cx - iris_r, cy - iris_r, cx + iris_r, cy + iris_r), fill=(31, 111, 235, 255))
draw.ellipse((cx - pupil_r, cy - pupil_r, cx + pupil_r, cy + pupil_r), fill=(11, 33, 79, 255))
# highlight glint
gl_r = pupil_r * 0.45
gx, gy = cx - iris_r * 0.32, cy - iris_r * 0.32
draw.ellipse((gx - gl_r, gy - gl_r, gx + gl_r, gy + gl_r), fill=(255, 255, 255, 230))

# ---- export ----
sizes_ico = [16, 24, 32, 48, 64, 128, 256]
master = img.resize((1024, 1024), Image.LANCZOS)
master.save("kubewatch.png")

ico_images = [img.resize((s, s), Image.LANCZOS) for s in sizes_ico]
ico_images[-1].save(
    "kubewatch.ico",
    format="ICO",
    sizes=[(s, s) for s in sizes_ico],
    append_images=ico_images[:-1],
)

print("done")
