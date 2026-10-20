"""Generate the DNS Mania application icon.

Concept: the letter "M" (Mania) rendered as a DNS network path — a chain of
nodes connected by thick links, with a glowing core node at the central dip.
Dark warm background, golden-yellow accent.
"""
import math
from PIL import Image, ImageDraw, ImageFilter

SS = 4              # supersampling factor for anti-aliasing
BASE = 1024         # master render size
OUT_DIR = "resources"

# Palette (golden-yellow on warm dark)
BG_TOP = (24, 18, 10)
BG_BOTTOM = (48, 34, 12)
YELLOW = (255, 196, 0)
YELLOW_SOFT = (255, 226, 110)
YELLOW_DEEP = (214, 146, 10)


def lerp(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


def rounded_rect_mask(size, radius):
    m = Image.new("L", (size, size), 0)
    d = ImageDraw.Draw(m)
    d.rounded_rectangle([0, 0, size - 1, size - 1], radius=radius, fill=255)
    return m


def build_background(size):
    """Vertical gradient with a soft radial golden glow behind the center."""
    img = Image.new("RGB", (size, size))
    d = ImageDraw.Draw(img)
    for y in range(size):
        d.line([(0, y), (size, y)], fill=lerp(BG_TOP, BG_BOTTOM, y / (size - 1)))

    # Radial glow
    glow = Image.new("L", (size, size), 0)
    gd = ImageDraw.Draw(glow)
    cx = cy = size // 2
    r = int(size * 0.42)
    for i in range(r, 0, -2):
        a = int(70 * (1 - i / r) ** 2)
        gd.ellipse([cx - i, cy - i, cx + i, cy + i], fill=a)
    glow = glow.filter(ImageFilter.GaussianBlur(size * 0.03))
    tint = Image.new("RGB", (size, size), YELLOW_DEEP)
    img = Image.composite(tint, img, glow.point(lambda p: p))
    # blend tint subtly
    img = Image.blend(Image.new("RGB", (size, size), BG_BOTTOM), img, 1.0)
    return img


def m_path_nodes(size):
    """Return the polyline points and node circles for the 'M' network path."""
    c = size / 2
    s = size / 1024.0  # scale factor from the 1024 design grid

    left_x = c - 180 * s
    right_x = c + 180 * s
    top_y = c - 200 * s   # the two peaks of the M
    dip_y = c + 40 * s    # the central dip (glowing core)
    bot_y = c + 200 * s   # the two feet of the M

    A = (left_x, bot_y)   # bottom-left foot
    B = (left_x, top_y)   # top-left peak
    C = (c, dip_y)        # central dip (core)
    D = (right_x, top_y)  # top-right peak
    E = (right_x, bot_y)  # bottom-right foot

    poly = [A, B, C, D, E]

    # node radii: peaks/feet medium, larger glowing core at the center dip
    nodes = [
        (A, int(52 * s)),
        (B, int(52 * s)),
        (C, int(92 * s)),  # core
        (D, int(52 * s)),
        (E, int(52 * s)),
    ]
    return poly, nodes


def draw_network(size, glow_pass=False):
    """Draw the M path + nodes on a transparent layer."""
    layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)

    poly, nodes = m_path_nodes(size)
    link_w = int(54 * size / 1024.0)

    color = YELLOW if not glow_pass else YELLOW_SOFT
    alpha = 255 if not glow_pass else 200

    # Thick rounded polyline for the M path.
    pts = [(p[0], p[1]) for p in poly]
    d.line(pts, fill=color + (alpha,), width=link_w, joint="curve")
    # Round caps at the two ends.
    for p in (pts[0], pts[-1]):
        r = link_w // 2
        d.ellipse([p[0] - r, p[1] - r, p[0] + r, p[1] + r], fill=color + (alpha,))

    # Node circles on top (slightly larger than the links).
    for (cx, cy), r in nodes:
        d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=color + (alpha,))

    # Dark inner dots on the non-core nodes for a "node" look.
    if not glow_pass:
        for (cx, cy), r in nodes:
            if r < int(90 * size / 1024.0):  # skip the big core
                ir = max(2, int(r * 0.42))
                d.ellipse([cx - ir, cy - ir, cx + ir, cy + ir],
                          fill=BG_TOP + (255,))

    return layer


def build_master(size):
    bg = build_background(size).convert("RGBA")

    # Soft glow layer under the crisp network.
    glow = draw_network(size, glow_pass=True)
    glow = glow.filter(ImageFilter.GaussianBlur(size * 0.02))
    bg = Image.alpha_composite(bg, glow)

    # Crisp network on top.
    net = draw_network(size, glow_pass=False)
    bg = Image.alpha_composite(bg, net)

    return bg


def main():
    import os
    os.makedirs(OUT_DIR, exist_ok=True)

    master = build_master(BASE * SS)

    # Full-size PNG (256) for the Qt resource / window icon.
    png256 = master.resize((256, 256), Image.LANCZOS)
    png256.save(os.path.join(OUT_DIR, "dnsmania_256.png"))

    # Also a larger 512 PNG (handy for high-DPI / previews).
    master.resize((512, 512), Image.LANCZOS).save(
        os.path.join(OUT_DIR, "dnsmania_512.png"))

    # Multi-size .ico for the executable / taskbar.
    sizes = [(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)]
    ico_img = master.resize((256, 256), Image.LANCZOS)
    ico_img.save(os.path.join(OUT_DIR, "dnsmania.ico"), sizes=sizes)

    # A big preview to eyeball the design.
    master.resize((512, 512), Image.LANCZOS).save(
        os.path.join(OUT_DIR, "preview.png"))

    print("Icons written to", OUT_DIR)


if __name__ == "__main__":
    main()
