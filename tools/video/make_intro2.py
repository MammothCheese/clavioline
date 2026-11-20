"""The intro, take 2: a line drawing of the Clavioline sketches itself; its tone network lights up as tablets switch in;
then it becomes the FM-1 running the firmware. 960 x 540, 20 fps, animated WebP. (Helpers from make_intro.py.)"""
import math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter

import make_intro as mi
from make_intro import W, H, FPS, NAVY, CHROME, IVORY, ORANGE, TEAL, MINT, FAINT, LABELS, SW_X, ease, stars, orb, mono

HERE = Path(__file__).parent
CLEAR = (0, 0, 0, 0)


# ---- 1. the Clavioline, as line art (drawn in code: the keyboard unit under a piano, its tablet rail, the knee lever,
#         the cable to the amplifier cabinet)
def clav_strokes():
    S = []                                                     # each stroke: (kind, coords, colour, width)
    S.append(("text", (330, 66, "piano keyboard"), FAINT, 14))
    for i in range(23):                                        # the piano's keys above (a strip)
        x = 150 + i * 30
        S.append(("line", [(x, 84), (x, 124)], FAINT, 1))
    S.append(("rect", [150, 84, 840, 124], FAINT, 1))
    S.append(("line", [(170, 140), (820, 140)], TEAL, 3))      # the Clavioline: top edge
    S.append(("line", [(150, 170), (840, 170)], TEAL, 3))
    S.append(("line", [(170, 140), (150, 170)], TEAL, 3))
    S.append(("line", [(820, 140), (840, 170)], TEAL, 3))
    for i in range(22):                                        # its 21 white keys, F to E
        x0, x1 = 170 + i * (650 / 21), 150 + i * (690 / 21)
        S.append(("line", [(x0, 140), (x1, 170)], IVORY, 1))
    for i in range(21):                                        # black keys (F# G# A#, C# D#)
        if i % 7 in (0, 1, 2, 4, 5):
            xa = 170 + (i + 0.7) * (650 / 21)
            S.append(("rect", [xa, 141, xa + 14, 155], IVORY, 2))
    S.append(("rect", [150, 170, 840, 252], TEAL, 3))          # the front panel
    S.append(("text", (172, 182, "CLAVIOLINE"), IVORY, 15))
    for i in range(18):                                        # the tablet rail: 18 tablets
        x = 300 + i * 29
        S.append(("rrect", [x, 206, x + 22, 238], ORANGE if i == 13 else IVORY, 2))
    S.append(("line", [(170, 252), (170, 276)], TEAL, 2))      # the brackets that hold it under the piano
    S.append(("line", [(820, 252), (820, 276)], TEAL, 2))
    S.append(("line", [(700, 252), (700, 300), (690, 400)], CHROME, 4))   # the knee lever
    S.append(("rrect", [662, 396, 718, 430], CHROME, 3))
    S.append(("text", (582, 440, "knee lever"), FAINT, 14))
    S.append(("curve", [(840, 230), (880, 260), (868, 330), (850, 352)], TEAL, 2))   # the cable to the cabinet
    S.append(("rect", [770, 352, 920, 510], TEAL, 3))          # the amplifier cabinet
    S.append(("circle", (845, 420, 48), IVORY, 2))             # its loudspeaker
    for r in (36, 24, 12):
        S.append(("circle", (845, 420, r), FAINT, 1))
    S.append(("text", (782, 486, "amplifier"), FAINT, 14))
    return S


def draw_stroke(d, s, a=255, part=1.0):
    kind, c, col, w = s
    col = col + (a,)
    if kind == "line":
        (x0, y0), (x1, y1) = c[0], c[-1]
        if len(c) > 2:
            pts = c[:max(2, int(len(c) * part + 0.999))]
            d.line(pts, fill=col, width=w)
        else:
            d.line([(x0, y0), (x0 + (x1 - x0) * part, y0 + (y1 - y0) * part)], fill=col, width=w)
    elif kind == "rect":
        d.rectangle(c, outline=col, width=w)
    elif kind == "rrect":
        d.rounded_rectangle(c, radius=4, outline=col, width=w)
    elif kind == "circle":
        x, y, r = c
        d.arc([x - r, y - r, x + r, y + r], -90, -90 + 360 * part, fill=col, width=w)
    elif kind == "curve":
        pts = [bez(c, t / 30) for t in range(31)]
        d.line(pts[:max(2, int(31 * part))], fill=col, width=w)
    elif kind == "text":
        x, y, t = c
        d.text((x, y), t, font=mono(w), fill=col)


def bez(p, t):
    (x0, y0), (x1, y1), (x2, y2), (x3, y3) = p
    u = 1 - t
    return (u ** 3 * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t ** 3 * x3,
            u ** 3 * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t ** 3 * y3)


def clav_layer(progress, alpha=1.0):
    im = Image.new("RGBA", (W, H), CLEAR)
    d = ImageDraw.Draw(im)
    S = clav_strokes()
    n = progress * len(S)
    for i, s in enumerate(S):
        if i + 1 <= n:
            draw_stroke(d, s, int(255 * alpha))
        elif i < n:
            draw_stroke(d, s, int(255 * alpha), n - i)
    return im


# ---- 2. the tone network, with tablets switching in
def network(closed, flow, reveal=1.0, alpha=1.0):
    """closed: tablet indices switched in (their branch lit); flow: 0..1, a pulse along the signal path"""
    im = Image.new("RGBA", (W, H), CLEAR)
    d = ImageDraw.Draw(im)
    a = int(255 * alpha)
    T, I, O, M = TEAL + (a,), IVORY + (a,), ORANGE + (a,), MINT + (a,)
    d.line([(250, 92), (720, 92)], fill=CHROME + (a,), width=4)
    d.line([(250, 99), (720, 99)], fill=CHROME + (a,), width=2)
    for i, x in enumerate((300, 420, 540, 660)):
        for k in range(4):
            d.arc([x - 34 + k * 17, 104, x - 34 + k * 17 + 17, 134], 180, 360, fill=T, width=3)
        d.line([(x - 34, 119), (x - 34, 180)], fill=T, width=3)
        d.line([(x + 34, 119), (x + 34, 150), (x + 50, 150)], fill=T, width=3)
        d.text((x - 10, 140), f"L{i + 1}", font=mono(15), fill=I)
    for k in range(4):
        d.arc([150, 190 + k * 17, 180, 207 + k * 17], 90, 270, fill=T, width=3)
    d.line([(165, 180), (165, 190)], fill=T, width=3); d.line([(165, 258), (165, 300)], fill=T, width=3)
    d.text((120, 222), "L5", font=mono(15), fill=I)
    d.line([(150, 180), (830, 180)], fill=T, width=3)
    d.text((790, 158), "OUT", font=mono(15), fill=O); d.text((112, 172), "IN", font=mono(15), fill=O)
    for i, x in enumerate(SW_X):
        on = i in closed
        cb = M if on else T
        d.line([(x, 180), (x, 226)], fill=cb, width=3 if on else 2)
        d.line([(x - 11, 226), (x + 11, 226)], fill=I, width=3); d.line([(x - 11, 234), (x + 11, 234)], fill=I, width=3)
        d.line([(x, 234), (x, 292)], fill=cb, width=3 if on else 2)
        d.ellipse([x - 3, 289, x + 3, 295], fill=cb)
        if on:
            d.line([(x, 292), (x, 322)], fill=M, width=3)                     # switched in: the contact closed
        else:
            d.line([(x, 292), (x + 14, 318)], fill=O, width=3)
        d.ellipse([x - 3, 322, x + 3, 328], fill=cb)
        d.line([(x, 325), (x, 352)], fill=cb, width=3 if on else 2)
        tw = d.textlength(LABELS[i], font=mono(20))
        d.text((x - tw / 2, 358), LABELS[i], font=mono(20), fill=(M if on else I))
    d.line([(165, 300), (165, 352), (SW_X[-1] + 40, 352)], fill=T, width=2)
    for k in range(3):
        d.line([(SW_X[-1] + 40 - (12 - 4 * k), 360 + k * 6), (SW_X[-1] + 40 + (12 - 4 * k), 360 + k * 6)], fill=T, width=2)
    d.line([(SW_X[-1] + 40, 352), (SW_X[-1] + 40, 360)], fill=T, width=2)
    if flow is not None and closed:                                           # the signal: along the path, down the branches
        x = 150 + (830 - 150) * flow
        d.ellipse([x - 6, 174, x + 6, 186], fill=M)
        for i in closed:
            if SW_X[i] < x:
                y = 180 + min(1, (x - SW_X[i]) / 120) * 172
                d.ellipse([SW_X[i] - 5, y - 5, SW_X[i] + 5, y + 5], fill=O)
    if reveal < 1:                                                            # traced in, left to right
        m = Image.new("L", (W, H), 0)
        ImageDraw.Draw(m).rectangle([0, 0, reveal * (W + 40), H], fill=255)
        im.putalpha(Image.composite(im.getchannel("A"), Image.new("L", (W, H), 0), m))
    return im


COMBOS = [((2, 3, 5), "3 · 4 · 6", "CLAVIOLINE TONE"), ((0, 9), "1 · O", "VIOLIN"),
          ((3, 5), "4 · 6", "CHURCH ORGAN"), ((11,), "B", "REED PIPE")]


def caption(ov, text, sub, a):
    d = ImageDraw.Draw(ov)
    d.text((112, 430), text, font=mono(17), fill=CHROME + (a,))
    if sub:
        d.text((112, 456), sub, font=mono(15), fill=ORANGE + (a,))


def main():
    screens = sorted(HERE.glob("m-*.ppm"), key=lambda p: int(p.stem.split("-")[1]))
    scr = [Image.open(p).convert("RGB") for p in screens]
    melody = [(7, 1800), (11, 2150), (14, 2500), (16, 2850), (14, 3200), (11, 3550)]
    white_of = {7: 4, 11: 6, 14: 8, 16: 9}
    A1, A2 = 64, 80                       # the Clavioline sketches itself; holds
    B1 = 96                               # crossfade into the circuit
    C0 = 112                              # circuit traced in
    CS = 24                               # frames per tablet combination
    C1 = C0 + CS * len(COMBOS)            # 208
    D1 = C1 + 32                          # into the FM-1: 240
    E1 = D1 + 42                          # the firmware playing: 282
    F1 = E1 + 40                          # title: 322
    frames = []
    for f in range(F1):
        im = Image.new("RGBA", (W, H), NAVY + (255,))
        d = ImageDraw.Draw(im); stars(d)
        ov = Image.new("RGBA", (W, H), CLEAR)
        if f < B1:                                                            # 1. the Clavioline
            fade = 1 - ease((f - A2) / (B1 - A2)) if f >= A2 else 1
            lay = clav_layer(ease(f / A1), fade)
            im.alpha_composite(lay.filter(ImageFilter.GaussianBlur(4)))
            im.alpha_composite(lay)
            caption(ov, "1947 · THE CLAVIOLINE", "a keyboard that clips under a piano, its amplifier on the floor", int(255 * min(fade, ease(f / 16))))
        if A2 <= f < C1 + 16:                                                 # 2. its tone network
            ain = ease((f - A2) / (B1 - A2))
            aout = 1 - ease((f - C1) / 16) if f >= C1 else 1
            reveal = ease((f - A2) / (C0 - A2))
            k = (f - C0) // CS if f >= C0 else -1
            closed, flow, cap = (), None, None
            if 0 <= k < len(COMBOS):
                t = (f - C0 - k * CS) / CS
                if t > 0.25:
                    closed = COMBOS[k][0]
                    flow = ((t - 0.25) / 0.75) * 1.25 % 1.0
                cap = COMBOS[k]
            elif f >= C1:
                closed, cap = COMBOS[-1][0], COMBOS[-1]
            lay = network(closed, flow, reveal, ain * aout)
            im.alpha_composite(lay.filter(ImageFilter.GaussianBlur(4)))
            im.alpha_composite(lay)
            if f >= B1:
                a = int(255 * ain * aout)
                if cap:
                    caption(ov, "1952 · THE TONE-FORMING NETWORK: TABLETS " + cap[1], cap[2], a)
                else:
                    caption(ov, "1952 · THE TONE-FORMING NETWORK", None, a)
        if f >= C1:                                                           # 3. it becomes the FM-1
            t = ease((f - C1) / (D1 - C1))
            lit = None
            if f >= D1:
                ms = 1800 + (f - D1) * 50
                for kk, t0 in melody:
                    if t0 <= ms < t0 + 300:
                        lit = ("w", white_of[kk])
            im.alpha_composite(mi.fm1_body(t, lit))
            if f >= D1:
                im.paste(scr[min(len(scr) - 1, f - D1)], (mi.SCR_X, mi.SCR_Y))
            elif t > 0.75:
                im.paste(Image.blend(Image.new("RGB", (240, 240), (5, 10, 24)), scr[0], ease((t - 0.75) / 0.25)), (mi.SCR_X, mi.SCR_Y))
            if f < D1:
                od = ImageDraw.Draw(ov)
                for i, lab in enumerate(LABELS):
                    sx, sy = SW_X[i], 370
                    tx, ty = mi.tablet_xy(i)
                    x, y = sx + (tx - sx) * t, sy + (ty - sy) * t
                    size = int(20 - 9 * t)
                    a = int(255 * (1 - ease((t - 0.8) / 0.2)))
                    tw = od.textlength(lab, font=mono(size))
                    od.text((x - tw / 2, y - size / 2), lab, font=mono(size), fill=ORANGE + (a,))
        im.alpha_composite(ov)
        if f >= E1:                                                           # 4. the title
            t = ease((f - E1) / 18)
            im.alpha_composite(Image.new("RGBA", (W, H), NAVY + (int(200 * t),)))
            tv = Image.new("RGBA", (W, H), CLEAR); td = ImageDraw.Draw(tv)
            a = int(255 * t)
            tt, fs = "THE CLAVIOLINE, 1947", 52
            while td.textlength(tt, font=orb(fs)) > W - 160:
                fs -= 2
            tw = td.textlength(tt, font=orb(fs))
            td.text(((W - tw) / 2, 254 - fs), tt, font=orb(fs), fill=IVORY + (a,))
            td.line([((W - tw) / 2, 272), ((W + tw) / 2, 272)], fill=TEAL + (a,), width=3)
            td.rectangle([(W + tw) / 2 + 6, 269, (W + tw) / 2 + 12, 275], fill=ORANGE + (a,))
            st = "firmware for the M-VAVE FM-1"
            td.text(((W - td.textlength(st, font=mono(24))) / 2, 296), st, font=mono(24), fill=ORANGE + (a,))
            im.alpha_composite(tv)
        frames.append(im.convert("RGB"))
    dur = [1000 // FPS] * len(frames)
    dur[-1] = 2500
    out = HERE / "clavioline-intro.webp"
    frames[0].save(out, save_all=True, append_images=frames[1:], duration=dur, loop=0, quality=80, method=4)
    for name, i in (("a-clav", 70), ("b-circuit", C0 + 20), ("c-combo", C0 + CS + 18), ("d-morph", C1 + 20), ("e-fm1", D1 + 20), ("f-title", len(frames) - 1)):
        frames[i].save(HERE / f"f2-{name}.png")
    print(out, out.stat().st_size // 1024, "KB,", len(frames), "frames,", round(len(frames) / FPS + 2.45, 1), "s")


if __name__ == "__main__":
    main()
