#!/usr/bin/env python3
"""Generate the open placeholder art in data/placeholder/.

The game needs the player's own Text Twist 2 install for its art (see
docs/design/import-assets.md). Until an import exists it falls back to
these files: bitmap fonts drawn from open fonts (third_party/fonts/), the
dialog frame, its centre fill and the dialog buttons. Every shape is new;
only the colour values were sampled from the original install, which
CLAUDE.md allows ("look and feel may imitate the original; assets may not").

The output keeps the original's file names and sizes, so the loader in
ui/src/main.cpp finds them with the paths it already uses. The fonts are
GF2 bitmap fonts (ui/include/tt2ui/bitmap_font.hpp): an XML of glyph
rectangles plus up to three PNG layers (face, stroke, shadow).

Run from the repository root, then commit data/placeholder/:

    python3 tools/gen_placeholders.py

Needs Pillow. The output is deterministic: two runs give identical bytes.
"""

import os
import shutil
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "data", "placeholder")
FONT_DIR = os.path.join(ROOT, "third_party", "fonts")
LUCKIEST = os.path.join(FONT_DIR, "luckiest-guy", "LuckiestGuy-Regular.ttf")
ARIMO = os.path.join(FONT_DIR, "arimo", "Arimo[wght].ttf")

# ---------------------------------------------------------------------------
# Palette, sampled from the original install (rounded to 16 levels).
# ---------------------------------------------------------------------------
ROYAL = (0x00, 0x30, 0xB0)
ROYAL_DARK = (0x00, 0x20, 0xA0)
NAVY = (0x00, 0x10, 0x80)
DEEP = (0x00, 0x10, 0x40)       # PAUSE_DARK_BG
GOLD = (0xE0, 0xC0, 0x00)
YELLOW = (0xF0, 0xE0, 0x00)
ORANGE = (0xF0, 0x70, 0x00)
CREAM = (0xF0, 0xF0, 0xC0)
WHITE = (0xF0, 0xF0, 0xF0)
CYAN = (0x20, 0xD0, 0xF0)
SKY = (0x10, 0x50, 0xD0)
SHADOW = (0x00, 0x10, 0x40, 150)

# ---------------------------------------------------------------------------
# Fonts. Header-style fonts use Luckiest Guy, text fonts Arimo Bold. The
# metrics (cap height, top offset, padding, spacing) are copied from the
# original XMLs so every label keeps its place in the layouts.
#   cap: height of "A" (sh); top: its `by`; pads: face / stroke / shadow.
# ---------------------------------------------------------------------------
FONTS = [
    # name,                 face,  cap, top, pads,       space, charsp, lineh, fill,              stroke
    ("font_uibar",          "lg",   26,  6, (4, 6, 6),     3, 0,  0, (0xF0, 0xF0, 0xA0), (0x00, 0x00, 0x70)),
    ("font_dialog_headers", "lg",   38, 10, (4, 10, 16),   8, 2, 36, (0xF0, 0xF0, 0xE0), (0x00, 0x48, 0x90)),
    ("font_dialog_headers2", "lg",  29,  7, (8, 8, 8),     6, 2,  0, (0xF0, 0xF0, 0xE0), (0x00, 0x48, 0x90)),
    ("font_uibar_labels",   "lg",   23,  5, (4, 4, 4),     3, 0,  0, (0xE0, 0xF0, 0xF0), (0x10, 0x40, 0xB0)),
    ("font_settingstext",   "ar",   16,  4, (4, 4, 8),     6, 0,  0, WHITE, (0x00, 0x30, 0xA0)),
    ("font_uibar_numbers",  "ar",   21,  5, (4, 4, 4),     3, 0,  0, (0xF0, 0xF0, 0x90), (0x00, 0x30, 0xC0)),
    ("font_nextround02",    "ar",   21,  5, (4, 8, 10),    4, 0,  0, (0x00, 0x50, 0xB0), (0xF0, 0xF0, 0xE0)),
    ("font_tutorial01",     "ar",   16,  4, (4, 4, 8),     4, 1,  0, (0xF0, 0xE0, 0x80), (0x00, 0x40, 0x80)),
    ("font_highscore_text", "ar",   12,  2, (4,),          4, 0,  0, (0xF0, 0xD0, 0x00), None),
]

# Advance width of SAMPLE in each original font (spaces at spacewidth).
# A face is sized to fit both this and the cap height, so wrapped dialog
# text breaks where the original's does.
SAMPLE = "The quick brown fox jumps over the lazy dog 0123456789"
SAMPLE_WIDTH = {
    "font_uibar": 625, "font_dialog_headers": 974, "font_dialog_headers2": 703, "font_uibar_labels": 527,
    "font_settingstext": 516, "font_uibar_numbers": 632, "font_nextround02": 641, "font_tutorial01": 498,
    "font_highscore_text": 340,
}

# Printable ASCII plus Latin-1 (the GF2 `c="#HH"` codes are Latin-1).
CHARSET = [c for c in range(0x21, 0x7F)] + [c for c in range(0xA1, 0x100) if c != 0xAD]
SHEET_MAX_W = 480   # unpadded row width before wrapping


def load_face(kind, size):
    if kind == "lg":
        return ImageFont.truetype(LUCKIEST, size)
    font = ImageFont.truetype(ARIMO, size)
    font.set_variation_by_axes([700])   # Bold
    return font


def fit_size(kind, cap, width, space, extra):
    """The largest point size whose "A" is at most `cap` pixels tall and
    whose SAMPLE is at most `width` pixels wide, with `extra` pixels added
    to every glyph's advance."""
    best = 4
    for size in range(4, 120):
        font = load_face(kind, size)
        top, bottom = font.getbbox("A")[1::2]
        run = sum(space if c == " " else round(font.getlength(c)) + extra for c in SAMPLE)
        if bottom - top > cap or run > width:
            break
        best = size
    return best


def glyph_bytes(font, ch):
    l, t, r, b = font.getbbox(ch)
    if r <= l or b <= t:
        return None
    return (r - l, b - t, glyph_mask(font, ch, (-l, -t), (r - l, b - t)).tobytes())


def has_glyph(font, ch, notdef):
    """False for blanks and for code points the font draws as .notdef."""
    g = glyph_bytes(font, ch)
    return g is not None and g != notdef


def lighter(rgb, t):
    return tuple(int(c + (255 - c) * t) for c in rgb)


def gradient_fill(mask, top_rgb, bottom_rgb):
    """RGBA image: a vertical gradient shown through `mask`."""
    w, h = mask.size
    grad = Image.new("RGB", (1, max(h, 1)))
    for y in range(h):
        t = y / max(h - 1, 1)
        grad.putpixel((0, y), tuple(int(a + (b - a) * t) for a, b in zip(top_rgb, bottom_rgb)))
    img = grad.resize((w, h)).convert("RGBA")
    img.putalpha(mask)
    return img


def glyph_mask(font, ch, origin, size, stroke=0):
    """L-mode mask of `ch` with its pen origin at `origin` in a `size` canvas."""
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).text(origin, ch, font=font, fill=255, stroke_width=stroke, stroke_fill=255)
    return m


def build_font(spec):
    name, kind, cap, top, pads, space, charsp, lineh, fill, stroke_rgb = spec
    # Luckiest Guy draws lowercase as small capitals, so it runs wider than
    # the original's mixed case; 15 % extra keeps the headers legible.
    width = SAMPLE_WIDTH[name] * (115 if kind == "lg" else 100) // 100
    stroke_w = 0
    if len(pads) > 1:
        stroke_w = min(max(1, round(cap / 14)), pads[1] - 1)
    # The stroke layer grows each glyph by stroke_w on both sides. Without
    # room for it in the advance, neighbouring outlines run into each other.
    gap = stroke_w   # the side bearings cover the other side
    font = load_face(kind, fit_size(kind, cap, width, space, gap))
    a_bottom = font.getbbox("A")[3]   # the baseline: kept where the original's is
    notdef = glyph_bytes(font, chr(0xE000))   # a private-use code point: .notdef
    shadow_d = 0
    if len(pads) > 2:
        shadow_d = max(0, min(max(1, round(cap / 14)), pads[2] - stroke_w))

    # Glyph boxes in font units relative to the pen origin.
    glyphs = []
    for code in CHARSET:
        ch = chr(code)
        if not has_glyph(font, ch, notdef):
            continue
        l, t, r, b = font.getbbox(ch)
        glyphs.append((code, ch, l, t, r - l, b - t, round(font.getlength(ch)) + gap))

    # Pack into rows of an unpadded sheet.
    placed, x, y, row_h, ix, iy = [], 0, 0, 0, 0, 0
    for g in glyphs:
        sw, sh = g[4], g[5]
        if x + sw > SHEET_MAX_W and ix > 0:
            x, y, ix, iy = 0, y + row_h, 0, iy + 1
            row_h = 0
        placed.append((g, x, y, ix, iy))
        x += sw
        ix += 1
        row_h = max(row_h, sh)
    rows = iy + 1
    unpadded_w = max(px + g[4] for g, px, _, _, _ in placed)
    # Each row's width differs; the sheet is as wide as the widest row
    # including its per-glyph padding.
    def layer_size(p):
        w = 0
        for g, px, _, gix, _ in placed:
            w = max(w, px + 2 * p * gix + g[4] + 2 * p)
        return w, y + row_h + 2 * p * rows

    layer_names = [name + ".png", name + "_stroke.png", name + "_shadow.png"][: len(pads)]
    layers = [Image.new("RGBA", layer_size(p), (0, 0, 0, 0)) for p in pads]
    for (code, ch, l, t, sw, sh, adv), px, py, gix, giy in placed:
        for li, p in enumerate(pads):
            cell_w, cell_h = sw + 2 * p, sh + 2 * p
            origin = (p - l, p - t)
            if li == 0:
                mask = glyph_mask(font, ch, origin, (cell_w, cell_h))
                cell = gradient_fill(mask, lighter(fill, 0.45), fill)
            elif li == 1:
                mask = glyph_mask(font, ch, origin, (cell_w, cell_h), stroke_w)
                cell = Image.new("RGBA", (cell_w, cell_h), stroke_rgb + (255,))
                cell.putalpha(mask)
            else:
                mask = glyph_mask(font, ch, (origin[0] + shadow_d, origin[1] + shadow_d), (cell_w, cell_h), stroke_w)
                mask = mask.point(lambda v: v * SHADOW[3] // 255)
                cell = Image.new("RGBA", (cell_w, cell_h), SHADOW[:3] + (255,))
                cell.putalpha(mask)
            layers[li].alpha_composite(cell, (px + 2 * p * gix, py + 2 * p * giy))

    lines = [
        "<root>",
        '  <Font id="font" spacewidth="%d" charspacing="%d" lineheight="%d">' % (space, charsp, lineh),
        '    <Text id="description" value="Generated placeholder (%s), tools/gen_placeholders.py" />'
        % ("Luckiest Guy, Apache 2.0" if kind == "lg" else "Arimo Bold, SIL OFL 1.1"),
        '    <Vector id="layers">',
    ]
    for img_name, p in zip(layer_names, pads):
        lines.append('      <Layer image="%s" padding="%d" color="#000000" />' % (img_name, p))
    lines += ["    </Vector>", '    <Vector id="chars">']
    for (code, ch, l, t, sw, sh, adv), px, py, gix, giy in placed:
        lines.append(
            '      <Char c="#%02X" bx="%d" by="%d" ix="%d" iy="%d" sx="%d" sy="%d" sw="%d" sh="%d" width="%d" />'
            % (code, l + gap // 2, t - a_bottom + top + cap, gix, giy, px, py, sw, sh, adv)
        )
    lines += ["    </Vector>", "  </Font>", "</root>", ""]

    font_out = os.path.join(OUT, "fonts")
    with open(os.path.join(font_out, name + ".xml"), "w", newline="\n") as f:
        f.write("\n".join(lines))
    for img, img_name in zip(layers, layer_names):
        save(img, os.path.join(font_out, img_name))
    return len(placed)


# ---------------------------------------------------------------------------
# Dialog frame: one 400x200 dialog drawn whole, then cut into the eight
# pieces DEFAULT_PAINTER stretches (corners 37x86 / 37x53, edges 4 px).
# The geometry honours FRAME_FILL_* in ui/include/tt2ui/profile_dialogs.hpp:
# opaque from x=6 (left glow), y=1, 7 px right glow, 16 px bottom shadow,
# 16 px rounded corners. The inside is transparent so the centre image
# (background.png) shows through.
# ---------------------------------------------------------------------------
SIDE_W, TOP_H, BOTTOM_H = 37, 86, 53
DLG_W, DLG_H = 400, 200


def rounded(size, box, radius, fill):
    m = Image.new("L", size, 0)
    ImageDraw.Draw(m).rounded_rectangle(box, radius=radius, fill=fill)
    return m


def vertical(size, stops):
    """RGB image with a vertical gradient through (t, rgb) stops."""
    w, h = size
    col = Image.new("RGB", (1, h))
    for y in range(h):
        t = y / max(h - 1, 1)
        for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
            if t0 <= t <= t1:
                k = (t - t0) / max(t1 - t0, 1e-9)
                col.putpixel((0, y), tuple(int(a + (b - a) * k) for a, b in zip(c0, c1)))
                break
    return col.resize((w, h))


def build_dialog():
    size = (DLG_W, DLG_H)
    body = (6, 1, DLG_W - 8, DLG_H - 17)           # opaque frame, inclusive box
    inner = (22, 70, DLG_W - 24, DLG_H - 33)        # the window onto the centre

    img = Image.new("RGBA", size, (0, 0, 0, 0))
    # Outer glow (left/top/right) and the drop shadow (bottom).
    glow = rounded(size, (3, 0, DLG_W - 4, DLG_H - 11), 18, 90).filter(ImageFilter.GaussianBlur(2))
    shade = rounded(size, (8, 8, DLG_W - 6, DLG_H - 2), 18, 120).filter(ImageFilter.GaussianBlur(4))
    img.alpha_composite(Image.merge("RGBA", (*Image.new("RGB", size, DEEP).split(), ImageChops.lighter(glow, shade))))

    # Gold trim, then the blue body inside it.
    trim = vertical(size, [(0, YELLOW), (1, GOLD)]).convert("RGBA")
    trim.putalpha(rounded(size, body, 16, 255))
    img.alpha_composite(trim)
    blue = vertical(size, [(0, (0x18, 0x48, 0xC8)), (0.35, ROYAL), (1, ROYAL_DARK)]).convert("RGBA")
    blue.putalpha(rounded(size, (body[0] + 3, body[1] + 3, body[2] - 3, body[3] - 3), 13, 255))
    img.alpha_composite(blue)
    # A soft highlight along the top of the header band.
    hi = Image.new("RGBA", size, (255, 255, 255, 0))
    hi.putalpha(rounded(size, (body[0] + 8, body[1] + 6, body[2] - 8, body[1] + 26), 10, 38))
    img.alpha_composite(hi)

    # Cut the window, then paint its inner shadow (dark at the rim,
    # transparent within 8 px) so the centre looks set in.
    hole = rounded(size, inner, 12, 255)
    a = ImageChops.subtract(img.getchannel("A"), hole)
    img.putalpha(a)
    rim = rounded(size, inner, 12, 255)
    inset = ImageChops.subtract(rim, rounded(size, (inner[0] + 2, inner[1] + 2, inner[2] - 2, inner[3] - 2), 10, 255))
    soft = ImageChops.subtract(rim, rounded(size, (inner[0] + 8, inner[1] + 8, inner[2] - 8, inner[3] - 6), 8, 255))
    soft = soft.filter(ImageFilter.GaussianBlur(3))
    soft = ImageChops.multiply(soft, rim).point(lambda v: v * 110 // 255)
    shadow = Image.new("RGBA", size, NAVY + (0,))
    shadow.putalpha(ImageChops.lighter(soft, inset.point(lambda v: v * 200 // 255)))
    img.alpha_composite(shadow)

    gui = os.path.join(OUT, "images", "gui", "dialogdefault")
    cx = DLG_W // 2
    my = (TOP_H + DLG_H - BOTTOM_H) // 2
    pieces = {
        "top_left": (0, 0, SIDE_W, TOP_H),
        "top": (cx, 0, cx + 4, TOP_H),
        "top_right": (DLG_W - SIDE_W, 0, DLG_W, TOP_H),
        "left": (0, my, SIDE_W, my + 4),
        "right": (DLG_W - SIDE_W, my, DLG_W, my + 4),
        "bottom_left": (0, DLG_H - BOTTOM_H, SIDE_W, DLG_H),
        "bottom": (cx, DLG_H - BOTTOM_H, cx + 4, DLG_H),
        "bottom_right": (DLG_W - SIDE_W, DLG_H - BOTTOM_H, DLG_W, DLG_H),
    }
    for piece, box in pieces.items():
        save(img.crop(box), os.path.join(gui, piece + ".png"))


def build_background():
    """BORDER_CENTER (images/gui/background.png): soft rings, cyan to blue."""
    w, h = 800, 600
    img = Image.new("RGB", (w, h))
    px = img.load()
    cx, cy, far = w / 2, h / 2, ((w / 2) ** 2 + (h / 2) ** 2) ** 0.5
    for y in range(h):
        for x in range(w):
            d = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            t = min(d / far, 1.0)
            band = 0.06 if int(d / 34) % 2 else 0.0
            k = min(1.0, t * 1.15 + band)
            px[x, y] = tuple(int(a + (b - a) * k) for a, b in zip(CYAN, SKY))
    save(img.convert("RGBA"), os.path.join(OUT, "images", "gui", "background.png"))


# ---------------------------------------------------------------------------
# Dialog buttons: a 1x4 sheet (normal / hover / pressed / disabled), each a
# pill with a gold rim and the label baked in, as in the original sheets.
# ---------------------------------------------------------------------------
BUTTONS = [
    # file, frame w, frame h, label
    ("button_ok", 119, 50, "OK"),
    ("button_cancel", 119, 50, "CANCEL"),
    ("button_yes", 119, 50, "YES"),
    ("button_no", 119, 50, "NO"),
    ("button_add", 119, 50, "ADD"),
    ("button_remove", 119, 50, "REMOVE"),
    ("button_end_menu", 157, 52, "MAIN MENU"),
    ("button_end_playagain", 157, 52, "PLAY AGAIN"),
    ("button_readmore", 157, 52, "READ MORE"),
    ("button_play", 157, 52, "PLAY"),
    ("button_pause_resume", 181, 52, "RESUME GAME"),
    ("button_pause_settings", 181, 52, "SETTINGS"),
    ("button_pause_endgame", 181, 52, "MAIN MENU"),
    ("button_pause_help", 181, 52, "HOW TO PLAY"),
]
# body top, body bottom, rim, label fill, label outline
STATES = [
    ((0x20, 0x50, 0xD0), NAVY, GOLD, WHITE, NAVY),               # normal
    (YELLOW, ORANGE, (0xF0, 0xF0, 0x80), ROYAL, CREAM),          # hover
    (WHITE, (0xA0, 0xE0, 0xF0), CYAN, (0x10, 0x90, 0xE0), WHITE),  # pressed
    ((0x60, 0x68, 0x88), (0x40, 0x48, 0x68), (0x70, 0x78, 0x90), (0x90, 0x98, 0xB0), (0x40, 0x48, 0x68)),  # disabled
]


def text_width(font, text, spacing):
    return sum(round(font.getlength(c)) for c in text) + spacing * (len(text) - 1)


def build_button(name, fw, fh, label):
    sheet = Image.new("RGBA", (fw, fh * 4), (0, 0, 0, 0))
    pill = (4, 6, fw - 5, fh - 7)
    for row, (top_rgb, bottom_rgb, rim, ink, outline) in enumerate(STATES):
        frame = Image.new("RGBA", (fw, fh), (0, 0, 0, 0))
        r = (pill[3] - pill[1]) // 2
        drop = Image.new("RGBA", (fw, fh), DEEP + (0,))
        drop.putalpha(rounded((fw, fh), (pill[0] + 1, pill[1] + 2, pill[2] + 1, pill[3] + 2), r, 110)
                      .filter(ImageFilter.GaussianBlur(1.5)))
        frame.alpha_composite(drop)
        rim_img = Image.new("RGBA", (fw, fh), rim + (255,))
        rim_img.putalpha(rounded((fw, fh), pill, r, 255))
        frame.alpha_composite(rim_img)
        inner = (pill[0] + 2, pill[1] + 2, pill[2] - 2, pill[3] - 2)
        body = vertical((fw, fh), [(0, top_rgb), (1, bottom_rgb)]).convert("RGBA")
        body.putalpha(rounded((fw, fh), inner, r - 2, 255))
        frame.alpha_composite(body)
        gloss = Image.new("RGBA", (fw, fh), (255, 255, 255, 0))
        gloss.putalpha(rounded((fw, fh), (inner[0] + 6, inner[1] + 2, inner[2] - 6, inner[1] + 10), 5, 60))
        frame.alpha_composite(gloss)

        size = 17
        font = load_face("ar", size)
        while size > 9 and text_width(font, label, 1) > inner[2] - inner[0] - 14:
            size -= 1
            font = load_face("ar", size)
        tw = text_width(font, label, 1)
        a_top, a_bottom = font.getbbox("A")[1::2]
        x = (fw - tw) // 2
        y = (inner[1] + inner[3]) // 2 - (a_top + a_bottom) // 2
        draw = ImageDraw.Draw(frame)
        for c in label:
            draw.text((x, y), c, font=font, fill=ink + (255,), stroke_width=1, stroke_fill=outline + (255,))
            x += round(font.getlength(c)) + 1
        sheet.alpha_composite(frame, (0, row * fh))
    save(sheet, os.path.join(OUT, "images", "gui", name + ".png"))


# ---------------------------------------------------------------------------

def save(img, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img.save(path, "PNG", optimize=True)   # no text chunks, no timestamps


LICENSE_MD = """# Generated placeholder art

Everything here is written by `tools/gen_placeholders.py`; do not edit it by hand. The game
loads these files only when the player has not imported their own Text Twist 2 install.

- **Shapes:** drawn by the script. No original GameHouse pixels; only colour values were sampled.
- **Fonts:** bitmap renderings of open fonts from `third_party/fonts/`:
  - Luckiest Guy (Apache 2.0, `LICENSE-LuckiestGuy.txt`): font_uibar, font_dialog_headers,
    font_dialog_headers2, font_uibar_labels.
  - Arimo Bold (SIL OFL 1.1, `OFL-Arimo.txt`): font_settingstext, font_uibar_numbers,
    font_nextround02, font_tutorial01, font_highscore_text, and the button labels.
- **License of the rest:** MIT, like the code.
"""


def main():
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(os.path.join(OUT, "fonts"))
    for spec in FONTS:
        n = build_font(spec)
        print("fonts/%s.xml: %d glyphs" % (spec[0], n))
    build_dialog()
    build_background()
    for name, fw, fh, label in BUTTONS:
        build_button(name, fw, fh, label)
    print("images/gui: dialog frame, background, %d buttons" % len(BUTTONS))
    shutil.copyfile(os.path.join(FONT_DIR, "luckiest-guy", "LICENSE.txt"), os.path.join(OUT, "LICENSE-LuckiestGuy.txt"))
    shutil.copyfile(os.path.join(FONT_DIR, "arimo", "OFL.txt"), os.path.join(OUT, "OFL-Arimo.txt"))
    with open(os.path.join(OUT, "README.md"), "w", newline="\n") as f:
        f.write(LICENSE_MD)
    return 0


if __name__ == "__main__":
    sys.exit(main())
