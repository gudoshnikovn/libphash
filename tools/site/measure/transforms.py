"""The edits the site applies to an image: the one registry every page measures.

Two groups. transforms() are the edits a copy of a picture goes through, which a hash
should survive: they make the robustness charts and the copies of the separability
measurement. content_edits() change what the picture shows, its content, its colors or
its orientation, and whether a hash should notice them depends on what it is used for:
they make the chart of edits that change the picture. An edit added to either group
appears on every algorithm's page, so the pages always compare the algorithms under the
same edits.
"""
import numpy as np
from PIL import Image, ImageEnhance, ImageFilter


def write_edit(op, image, stem):
    """Applies one edit of either group to a Pillow image and saves the result as the
    edit says (PPM, or JPEG at the edit's quality) at <stem>.<extension>; the path."""
    im, (ext, quality) = op(image)
    path = f"{stem}.{ext}"
    im.save(path, **({"quality": quality} if ext == "jpg" else {}))
    return path


# --8<-- [start:transforms]
def transforms():
    """(name, x-axis label, [(strength, PIL image -> (image, (file extension, quality)))]).

    Each edit is applied alone to the original. A variant is saved as PPM, which is
    lossless (and quick to write for a corpus of thousands of variants), so the edit is
    the only difference; JPEG quality is the one edit that is the saving itself.
    """
    def jpeg(q):
        return lambda im: (im, ("jpg", q))

    def scale(s):
        return lambda im: (im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))),
                                     Image.LANCZOS), ("ppm", None))

    def rotate(deg):
        # The corners the turn uncovers are filled with the image's mean color, so they
        # add no edge of their own.
        def f(im):
            fill = tuple(int(v) for v in np.asarray(im).reshape(-1, 3).mean(axis=0))
            return im.rotate(deg, resample=Image.BICUBIC, fillcolor=fill), ("ppm", None)
        return f

    def brightness(k):
        return lambda im: (ImageEnhance.Brightness(im).enhance(k), ("ppm", None))

    def contrast(k):
        return lambda im: (ImageEnhance.Contrast(im).enhance(k), ("ppm", None))

    def gamma(g):
        def f(im):
            a = (np.asarray(im).astype(np.float64) / 255.0) ** g
            return Image.fromarray((a * 255.0).round().astype(np.uint8)), ("ppm", None)
        return f

    def blur(r):
        return lambda im: (im.filter(ImageFilter.GaussianBlur(r)), ("ppm", None))

    def noise(sigma):
        # A fixed seed: the same noise on every build, so the chart does not move.
        def f(im):
            rng = np.random.default_rng(1)
            a = np.asarray(im).astype(np.float64) + rng.normal(0, sigma, (im.height, im.width, 3))
            return Image.fromarray(np.clip(a, 0, 255).round().astype(np.uint8)), ("ppm", None)
        return f

    def crop(p):
        def f(im):
            dx, dy = round(im.width * p / 2), round(im.height * p / 2)
            return im.crop((dx, dy, im.width - dx, im.height - dy)), ("ppm", None)
        return f

    return [
        ("JPEG quality", "quality", [(q, jpeg(q)) for q in (95, 75, 50, 30, 15, 5)]),
        ("Downscale", "scale", [(s, scale(s)) for s in (0.75, 0.5, 0.25, 0.125)]),
        ("Rotation", "degrees", [(d, rotate(d)) for d in (1, 2, 5, 10, 20, 45)]),
        ("Brightness", "factor", [(k, brightness(k)) for k in (0.5, 0.7, 0.85, 1.15, 1.3, 1.5)]),
        ("Contrast", "factor", [(k, contrast(k)) for k in (0.5, 0.7, 0.85, 1.15, 1.3, 1.5)]),
        ("Gamma", "exponent", [(g, gamma(g)) for g in (0.5, 0.7, 0.85, 1.2, 1.5, 2.0)]),
        ("Gaussian blur", "radius, px", [(r, blur(r)) for r in (0.5, 1, 2, 4, 8)]),
        ("Noise", "σ, gray levels", [(s, noise(s)) for s in (2, 5, 10, 20, 40)]),
        ("Crop", "border removed, %", [(p, crop(p / 100)) for p in (5, 10, 20, 30)]),
    ]
# --8<-- [end:transforms]


# --8<-- [start:content_edits]
def content_edits():
    """The edits that change the picture, in the shape of transforms(); a strength is a
    number, or a word where the edit has no scale.

    A patch replaces a square at the center, covering the given share of the frame, with
    the square of the same size at the top-left corner of the same image: a local edit
    with content the image itself supplies. A hue rotation turns every pixel's hue by the
    given angle and keeps its saturation and value, a recoloring. The turns and the mirror
    change only the orientation.
    """
    def patch(area):
        def f(im):
            side = area ** 0.5
            w, h = round(im.width * side), round(im.height * side)
            x0, y0 = (im.width - w) // 2, (im.height - h) // 2
            out = im.copy()
            out.paste(im.crop((0, 0, w, h)), (x0, y0))
            return out, ("ppm", None)
        return f

    def hue(degrees):
        def f(im):
            hsv = np.asarray(im.convert("HSV")).copy()
            hsv[..., 0] = (hsv[..., 0].astype(int) + round(degrees * 256 / 360)) % 256
            return Image.fromarray(hsv, "HSV").convert("RGB"), ("ppm", None)
        return f

    def turn(method):
        return lambda im: (im.transpose(method), ("ppm", None))

    return [
        ("Local patch", "share of the frame, %", [(a, patch(a / 100)) for a in (1, 4, 9, 16)]),
        ("Hue rotation", "degrees", [(d, hue(d)) for d in (30, 90, 180)]),
        ("Turn and mirror", "", [("90°", turn(Image.Transpose.ROTATE_90)),
                                 ("180°", turn(Image.Transpose.ROTATE_180)),
                                 ("mirror", turn(Image.Transpose.FLIP_LEFT_RIGHT))]),
    ]
# --8<-- [end:content_edits]
