"""A small loop for the guide's "Behind the sound": the Clavioline in line art beside its tone network, tablets switching
in and out. 840 x 280, 15 fps, animated WebP (drawn with make_intro2.py's line art and network)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

import make_intro2 as m2
from make_intro import NAVY, CHROME, ORANGE, ease, mono

HERE = Path(__file__).parent
OUT = HERE / "behind-the-sound.webp"
W, H, FPS, CS = 840, 280, 15, 30                 # CS: frames per tablet combination
COMBOS = m2.COMBOS


def glowing(layer):
    g = layer.filter(ImageFilter.GaussianBlur(4))
    g.alpha_composite(layer)
    return g


def main():
    strokes = [st for st in m2.clav_strokes() if not (st[2] == m2.FAINT and (st[0] in ("line", "rect") and st[1][0][1] < 130 if st[0] == "line" else st[0] == "rect" and st[1][1] < 130) or (st[0] == "text" and st[1][2] == "piano keyboard"))]
    lay = Image.new("RGBA", (m2.W, m2.H), (0, 0, 0, 0)); ld = ImageDraw.Draw(lay)
    for st in strokes:
        m2.draw_stroke(ld, st)                                   # (the Clavioline alone: no piano above it)
    clav = glowing(lay).crop((100, 120, 930, 515)).resize((300, 143), Image.LANCZOS)
    frames = []
    n = CS * len(COMBOS)
    for f in range(n):
        k, t = f // CS, (f % CS) / CS
        closed = COMBOS[k][0] if t > 0.2 else ()
        flow = ((t - 0.2) / 0.8) * 1.3 % 1.0 if t > 0.2 else None
        net = glowing(m2.network(closed, flow)).crop((100, 80, 860, 380)).resize((500, 197), Image.LANCZOS)
        im = Image.new("RGBA", (W, H), NAVY + (255,))
        im.alpha_composite(clav, (14, 62))
        im.alpha_composite(net, (326, 22))
        d = ImageDraw.Draw(im)
        d.text((20, 236), "THE CLAVIOLINE, 1947", font=mono(14), fill=CHROME)
        a = int(255 * min(ease(t / 0.2), 1 - ease((t - 0.85) / 0.15)))
        ov = Image.new("RGBA", (W, H), (0, 0, 0, 0)); od = ImageDraw.Draw(ov)
        od.text((340, 236), "TABLETS " + COMBOS[k][1], font=mono(14), fill=CHROME + (a,))
        od.text((340 + od.textlength("TABLETS " + COMBOS[k][1] + "  ", font=mono(14)), 236), COMBOS[k][2], font=mono(14), fill=ORANGE + (a,))
        im.alpha_composite(ov)
        frames.append(im.convert("RGB"))
    frames[0].save(OUT, save_all=True, append_images=frames[1:], duration=1000 // FPS, loop=0, quality=78, method=6)
    frames[CS + 20].save(HERE / "behind-preview.png")
    print(OUT, OUT.stat().st_size // 1024, "KB,", len(frames), "frames,", round(len(frames) / FPS, 1), "s loop")


if __name__ == "__main__":
    main()
