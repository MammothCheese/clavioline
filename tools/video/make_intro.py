"""The intro: the 1952 tone network, traced, becomes the FM-1. 960 x 540, 20 fps, animated WebP."""
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H, FPS = 960, 540, 20
NAVY, PANEL, PANEL2, CHROME, IVORY, INK = (8, 16, 38), (18, 40, 62), (13, 29, 51), (150, 172, 190), (238, 230, 206), (28, 24, 22)
ORANGE, TEAL, MINT, FAINT, RED = (255, 142, 36), (56, 196, 186), (160, 244, 222), (58, 80, 104), (214, 64, 52)
HERE = Path(__file__).parent
ROOT = HERE.parent.parent
orb = lambda n: ImageFont.truetype(str(ROOT / "assets/fonts/Orbitron.ttf"), n)
mono = lambda n: ImageFont.truetype("consola.ttf", n)
LABELS = ["1", "2", "3", "4", "5", "6", "7", "8", "9", "O", "A", "B", "V"]
SW_X = [228 + i * 42 for i in range(13)]


def ease(t):
    t = min(1, max(0, t))
    return t * t * (3 - 2 * t)


def stars(d):
    for x, y in ((40, 30), (130, 500), (300, 22), (520, 512), (700, 40), (900, 470), (930, 90), (60, 300), (880, 260)):
        d.point((x, y), fill=(206, 216, 255))


def schematic():
    """the tone-forming network, drawn from the traced netlist (not the scan): coils on a shared core, the tablet row"""
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    c, w = TEAL, 3
    d.line([(250, 92), (720, 92)], fill=CHROME, width=4)                       # the shared iron core
    d.line([(250, 99), (720, 99)], fill=CHROME, width=2)
    for i, x in enumerate((300, 420, 540, 660)):                              # L1..L4
        for k in range(4):
            d.arc([x - 34 + k * 17, 104, x - 34 + k * 17 + 17, 134], 180, 360, fill=c, width=w)
        d.line([(x - 34, 119), (x - 34, 180)], fill=c, width=w)
        d.line([(x + 34, 119), (x + 34, 150), (x + 50, 150)], fill=c, width=w)
        d.text((x - 10, 140), f"L{i + 1}", font=mono(15), fill=IVORY)
    for k in range(4):                                                         # L5, standing
        d.arc([150, 190 + k * 17, 180, 207 + k * 17], 90, 270, fill=c, width=w)
    d.line([(165, 180), (165, 190)], fill=c, width=w)
    d.line([(165, 258), (165, 300)], fill=c, width=w)
    d.text((120, 222), "L5", font=mono(15), fill=IVORY)
    d.line([(165, 180), (830, 180)], fill=c, width=w)                          # the signal path
    d.text((790, 158), "OUT", font=mono(15), fill=ORANGE)
    d.text((112, 172), "IN", font=mono(15), fill=ORANGE)
    d.line([(150, 180), (165, 180)], fill=c, width=w)
    for i, x in enumerate(SW_X):                                               # the tablets
        d.line([(x, 180), (x, 226)], fill=c, width=2)
        d.line([(x - 11, 226), (x + 11, 226)], fill=IVORY, width=3)            # a capacitor
        d.line([(x - 11, 234), (x + 11, 234)], fill=IVORY, width=3)
        d.line([(x, 234), (x, 292)], fill=c, width=2)
        d.ellipse([x - 3, 289, x + 3, 295], fill=c)
        d.line([(x, 292), (x + 14, 318)], fill=ORANGE, width=3)                # the switch
        d.ellipse([x - 3, 322, x + 3, 328], fill=c)
        d.line([(x, 325), (x, 352)], fill=c, width=2)
        tw = d.textlength(LABELS[i], font=mono(20))
        d.text((x - tw / 2, 358), LABELS[i], font=mono(20), fill=IVORY)
    d.line([(165, 300), (165, 352), (SW_X[-1], 352)], fill=c, width=2)         # ground
    for k in range(3):
        d.line([(SW_X[-1] + 30 - k * 7, 352 + k * 6), (SW_X[-1] + 54 + k * 7 - 14 * k, 352 + k * 6)], fill=c, width=2)
    d.line([(SW_X[-1], 352), (SW_X[-1] + 42, 352)], fill=c, width=2)
    return im


def label_layer():
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    return im


SCR_X, SCR_Y = 360, 70


def tablet_xy(i):                                                              # tablet i on the FM-1's screen (1..V)
    return SCR_X + 15 + 16 * i, SCR_Y + 89


def fm1_body(alpha, lit_key=None):
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    a = int(255 * alpha)
    d.rounded_rectangle([150, 40, 810, 500], radius=34, fill=(26, 52, 80, a), outline=(44, 75, 106, a), width=2)
    for x, y, lab in ((215, 110, "MASTER"), (285, 110, "SELECT"), (215, 190, "PRESETS"), (285, 190, "ALGO")):
        d.ellipse([x - 18, y - 18, x + 18, y + 18], fill=(170, 184, 198, a), outline=(13, 29, 51, a), width=4)
        d.text((x - d.textlength(lab, font=mono(10)) / 2, y - 34), lab, font=mono(10), fill=CHROME + (a,))
    for i, lab in enumerate(("OCT-", "OCT+")):
        d.rounded_rectangle([196 + i * 58, 242, 246 + i * 58, 268], radius=8, fill=IVORY + (a,))
        d.text((204 + i * 58, 248), lab, font=mono(12), fill=INK + (a,))
    for i in range(4):
        x = 642 + i * 46
        d.ellipse([x - 16, 94, x + 16, 126], fill=(170, 184, 198, a), outline=(13, 29, 51, a), width=4)
    d.rounded_rectangle([616, 150, 790, 268], radius=14, fill=(13, 29, 51, a))
    for r, row in enumerate((("FX", "SEL", "ENV", "LFO", "EDIT", "GLO"), ("HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"))):
        for c, lab in enumerate(row):
            x, y = 626 + c * 27, 162 + r * 52
            d.rounded_rectangle([x, y, x + 23, y + 40], radius=6, fill=IVORY + (a,))
    d.rounded_rectangle([SCR_X - 10, SCR_Y - 10, SCR_X + 250, SCR_Y + 250], radius=10, fill=(5, 10, 24, a))   # bezel
    d.rounded_rectangle([170, 336, 790, 484], radius=18, fill=(10, 22, 40, a))
    ww = 620 / 16
    for i in range(16):
        x = 172 + i * ww
        on = lit_key is not None and lit_key == ("w", i)
        d.rounded_rectangle([x + 2, 392, x + ww - 2, 478], radius=12, fill=((242, 217, 180) if on else IVORY) + (a,))
    bi = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]
    for j, i in enumerate(bi):
        x = 172 + (i + 1) * ww - 13
        d.rounded_rectangle([x, 346, x + 26, 418], radius=10, fill=INK + (a,))
    return im


def main():
    sch = schematic()
    glow = sch.filter(ImageFilter.GaussianBlur(5))
    screens = sorted(HERE.glob("m-*.ppm"), key=lambda p: int(p.stem.split("-")[1]))
    scr_imgs = [Image.open(p).convert("RGB") for p in screens]
    melody = [(7, 1800), (11, 2150), (14, 2500), (16, 2850), (14, 3200), (11, 3550)]
    white_of = {7: 4, 11: 6, 14: 8, 16: 9}                                     # key index -> white key number
    frames = []
    N1, N2, N3, N4, N5 = 60, 80, 112, 152, 196
    for f in range(N5):
        im = Image.new("RGBA", (W, H), NAVY + (255,))
        d = ImageDraw.Draw(im)
        stars(d)
        ov = Image.new("RGBA", (W, H), (0, 0, 0, 0)); od = ImageDraw.Draw(ov)   # (fading text: blended)
        if f < N3:                                                             # the circuit, traced left to right
            wipe = ease(f / N1) * (W + 40)
            fade = 1 - ease((f - N2) / (N3 - N2 - 8)) if f >= N2 else 1
            mask = Image.new("L", (W, H), 0)
            ImageDraw.Draw(mask).rectangle([0, 0, wipe, H], fill=int(255 * fade))
            layer = Image.composite(sch, Image.new("RGBA", (W, H), (0, 0, 0, 0)), mask)
            g = Image.composite(glow, Image.new("RGBA", (W, H), (0, 0, 0, 0)), mask)
            im.alpha_composite(g); im.alpha_composite(layer)
            if f < N1:
                d.line([(wipe, 60), (wipe, 400)], fill=MINT, width=2)
            cap_a = int(255 * (fade ** 3) * ease(f / 20)) if f < N2 + 8 else 0
            od.text((112, 430), "1952 · THE TONE-FORMING NETWORK, TRACED", font=mono(16), fill=CHROME + (cap_a,))
        if f >= N2:                                                            # it becomes the FM-1
            t = ease((f - N2) / (N3 - N2))
            body_a = t
            lit = None
            if f >= N3:
                ms = 1800 + (f - N3) * 50
                for k, t0 in melody:
                    if t0 <= ms < t0 + 300:
                        lit = ("w", white_of[k])
            im.alpha_composite(fm1_body(body_a, lit))
            if f >= N3:
                idx = min(len(scr_imgs) - 1, f - N3)
                im.paste(scr_imgs[idx], (SCR_X, SCR_Y))
            elif t > 0.75:
                s = scr_imgs[0].copy()
                im.paste(Image.blend(Image.new("RGB", (240, 240), (5, 10, 24)), s, ease((t - 0.75) / 0.25)), (SCR_X, SCR_Y))
            if f < N3:                                                         # the tablet letters fly to the screen
                for i, lab in enumerate(LABELS):
                    sx, sy = SW_X[i], 370
                    tx, ty = tablet_xy(i)
                    x, y = sx + (tx - sx) * t, sy + (ty - sy) * t
                    size = int(20 - 9 * t)
                    a = int(255 * (1 - ease((t - 0.8) / 0.2)))
                    tw = od.textlength(lab, font=mono(size))
                    od.text((x - tw / 2, y - size / 2), lab, font=mono(size), fill=ORANGE + (a,))
        im.alpha_composite(ov)
        if f >= N4:                                                            # the title
            t = ease((f - N4) / 18)
            veil = Image.new("RGBA", (W, H), NAVY + (int(200 * t),))
            im.alpha_composite(veil)
            tv = Image.new("RGBA", (W, H), (0, 0, 0, 0)); d = ImageDraw.Draw(tv)
            a = int(255 * t)
            tt = "THE CLAVIOLINE, 1947"
            fs = 52
            while d.textlength(tt, font=orb(fs)) > W - 160: fs -= 2
            tw = d.textlength(tt, font=orb(fs))
            d.text(((W - tw) / 2, 268 - fs - 14), tt, font=orb(fs), fill=IVORY + (a,))
            d.line([((W - tw) / 2, 272), ((W + tw) / 2, 272)], fill=TEAL + (a,), width=3)
            d.rectangle([(W + tw) / 2 + 6, 269, (W + tw) / 2 + 12, 275], fill=ORANGE + (a,))
            st = "firmware for the M-VAVE FM-1"
            sw = d.textlength(st, font=mono(24))
            d.text(((W - sw) / 2, 296), st, font=mono(24), fill=ORANGE + (a,))
            im.alpha_composite(tv)
        frames.append(im.convert("RGB"))
    durations = [1000 // FPS] * len(frames)
    durations[-1] = 2500                                                       # hold the title
    out = HERE / "clavioline-intro.webp"
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=durations, loop=0, quality=82, method=4)
    frames[70].save(HERE / "frame-circuit.png"); frames[100].save(HERE / "frame-morph.png"); frames[130].save(HERE / "frame-fm1.png"); frames[-1].save(HERE / "frame-title.png")
    print(out, out.stat().st_size // 1024, "KB,", len(frames), "frames")


if __name__ == "__main__":
    main()
