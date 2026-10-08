"""The figures and measurements of docs/theory/preparation.md: what the library makes of a
file before any hash reads it. Every image shown is one the library loaded (`site_stages
loaded`), and every number is measured by `site_stages measure` and `pairs`, loading with
the settings their `--load=` option sets (take_load_settings() in tools/site/stages/util.c).

Unlike an algorithm page, this one has no hash of its own: render.py calls figures() and
nothing else.
"""
import concurrent.futures
import io
import os
import tempfile
from fractions import Fraction

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image, ImageOps

from draw.corpus_charts import LABEL_MISSING, share
from draw.markdown import table, write_text
from draw.style import hide_axes, save, style_axes
from measure import corpus
from measure.cache import ROOT, cached
from measure.metric import metric
from measure.separability import COPY_STRENGTHS, RECALL, copies_and_different, separability
from measure.tool import read_pnm, run_lines
from measure.transforms import transforms
from pages import (TITLES, ahash, bmh, color_hash, color_moments, dhash, mhash, phash,
                   radial, whash)

NAME = "preparation"
ALGORITHMS = (ahash, dhash, phash, whash, mhash, bmh, radial, color_hash, color_moments)
GRAY_ALGORITHMS = ALGORITHMS[:7]
SHORT = {"photos": "Photographs", "synthetic": "Synthetic images"}
MODULE = os.path.relpath(__file__, ROOT)
LARGE = os.path.join(ROOT, "tests", "data", "photo_large.jpeg")


def _value(mod, raw):
    """One comparison as the algorithm's page reads it: bits that differ for a bit hash,
    the algorithm's own score otherwise."""
    return metric(mod)[2](raw)


def _fmt(mod, value):
    return metric(mod)[4](value)


def _median(mod, values):
    """The median and quartiles of the values a page would chart, as one cell."""
    v = [_value(mod, x) for x in values if x is not None]
    if not v:
        return "—"
    q = np.percentile(v, [25, 50, 75])
    return f"{_fmt(mod, q[1])} ({_fmt(mod, q[0])}–{_fmt(mod, q[2])})"


def _unit(mod):
    return f"bits of {mod.BITS}" if mod.BITS else metric(mod)[0]


def _name(mod):
    return TITLES[mod.ALGO]


# ---- The orientation --------------------------------------------------------------------

# --8<-- [start:orientation]
T = Image.Transpose
# What a viewer does to the stored pixels for each value of the EXIF Orientation tag, as
# Pillow's ImageOps.exif_transpose() does it, and the transform that undoes it: a camera
# that stores the picture with value k stores UNDO[k] of what it shows.
DISPLAY = {2: T.FLIP_LEFT_RIGHT, 3: T.ROTATE_180, 4: T.FLIP_TOP_BOTTOM, 5: T.TRANSPOSE,
           6: T.ROTATE_270, 7: T.TRANSVERSE, 8: T.ROTATE_90}
UNDO = {**DISPLAY, 6: T.ROTATE_90, 8: T.ROTATE_270}
ORIENTATION_NAMES = {1: "as stored", 2: "mirrored", 3: "turned 180°", 4: "flipped",
                     5: "transposed", 6: "turned 90° right", 7: "transversed",
                     8: "turned 90° left"}


def oriented_files(base, tmp):
    """The picture stored the eight ways a camera may store it, each as a PNG whose eXIf
    chunk carries the Orientation that shows it upright; checked with Pillow's own reading
    of the tag. Lossless, so the library's result can be compared pixel for pixel."""
    files = []
    for k in range(1, 9):
        stored = base.transpose(UNDO[k]) if k > 1 else base.copy()
        exif = Image.Exif()
        exif[0x0112] = k
        files.append(os.path.join(tmp, f"orientation-{k}.png"))
        stored.save(files[-1], exif=exif)
        assert np.array_equal(np.asarray(ImageOps.exif_transpose(Image.open(files[-1]))),
                              np.asarray(base)), k
    return files


def orientation(tool, image):
    """The stored and the loaded pixels of the eight files, and pHash's bits between each
    file and the upright picture with the tag read and with it ignored. Fails the build
    unless the library loads every file as the upright picture, exactly."""
    base = Image.open(image).convert("RGB")
    base = base.crop((0, base.height // 8, base.width, base.height - base.height // 8))
    with tempfile.TemporaryDirectory() as tmp:
        files = oriented_files(base, tmp)
        ref = os.path.join(tmp, "upright.ppm")
        base.save(ref)
        run_lines(tool, "loaded", tmp, *files, "--load=orient=off", *files)
        loaded = [read_pnm(os.path.join(tmp, f"{k:02d}.ppm")) for k in range(16)]
        on = run_lines(tool, "measure", ref, *files)
        off = run_lines(tool, "measure", ref, "--load=orient=off", *files)
    for k in range(8):
        stored = base.transpose(UNDO[k + 1]) if k else base
        if not np.array_equal(loaded[k], np.asarray(base)):
            raise SystemExit(f"preparation: orientation {k + 1} did not load upright")
        if not np.array_equal(loaded[8 + k], np.asarray(stored)):
            raise SystemExit(f"preparation: orientation {k + 1} did not load as stored")
    return loaded[8:], loaded[:8], on, off
# --8<-- [end:orientation]


def orientation_figure(tool, image, out):
    stored, upright, on, off = orientation(tool, image)

    def fig(c):
        figure, axes = plt.subplots(2, 8, figsize=(11, 3.4))
        for k in range(8):
            for row, im in ((0, stored[k]), (1, upright[k])):
                ax = axes[row, k]
                ax.imshow(im)
                hide_axes(ax)
                ax.set_xlim(-0.5, 399.5)
                ax.set_ylim(399.5, -0.5)
            axes[0, k].set_title(f"{k + 1}: {ORIENTATION_NAMES[k + 1]}", color=c["ink"],
                                 fontsize=8.5)
        axes[0, 0].set_ylabel("Stored", color=c["ink"], fontsize=10)
        axes[1, 0].set_ylabel("Loaded", color=c["ink"], fontsize=10)
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out, NAME), "orientation")
    rows = [[f"{k + 1}", ORIENTATION_NAMES[k + 1],
             f"{_fmt(phash, _value(phash, on[k]['phash']))}",
             f"{_fmt(phash, _value(phash, off[k]['phash']))}"] for k in range(8)]
    write_text(os.path.join(out, NAME), "orientation.md", table(
        ["Orientation", "The viewer shows it", "pHash bits from the upright picture, tag "
         "read", "tag ignored"], rows))


# ---- Transparency ------------------------------------------------------------------------

# --8<-- [start:cutout]
BLACK, WHITE = (0, 0, 0), (255, 255, 255)
BACKGROUNDS = ("grey", "white", "black")


def ellipse_alpha(w, h, cx, cy, rx, ry):
    """Opaque inside an ellipse (center and radii as fractions of the frame), fully
    transparent outside: the alpha of a cut-out."""
    y, x = np.mgrid[0:h, 0:w]
    inside = ((x + 0.5) / w - cx) ** 2 / rx ** 2 + ((y + 0.5) / h - cy) ** 2 / ry ** 2 <= 1
    return np.where(inside, 255, 0).astype(np.uint8)


def ellipse_of(seed):
    """The ellipse of image number `seed`: a different place and size for every image, the
    same for an image and its copies."""
    rng = np.random.default_rng(seed)
    return (*rng.uniform(0.3, 0.7, 2), *rng.uniform(0.2, 0.45, 2))


def cutout(image, ellipse, under):
    """An RGBA image: `image` inside the ellipse, and the color `under` stored beneath the
    transparent pixels outside it, as an encoder that writes black, or white, there would."""
    rgb = np.asarray(image.convert("RGB")).copy()
    alpha = ellipse_alpha(image.width, image.height, *ellipse)
    rgb[alpha == 0] = under
    return Image.fromarray(np.dstack([rgb, alpha]), "RGBA")
# --8<-- [end:cutout]


def alpha_figure(tool, image, out):
    """The example cut out, stored with black and with white under its transparent pixels:
    what the library loads with alpha ignored and on each background. Fails the build
    unless both files load the same on every background."""
    base = Image.open(image).convert("RGB")
    ellipse = (0.5, 0.5, 0.42, 0.36)
    with tempfile.TemporaryDirectory() as tmp:
        files = [os.path.join(tmp, f"{n}.png") for n in ("black", "white")]
        for f, under in zip(files, (BLACK, WHITE)):
            cutout(base, ellipse, under).save(f)
        args = ["--load=alpha=ignore", *files]
        for bg in BACKGROUNDS:
            args += [f"--load=alpha={bg}", *files]
        run_lines(tool, "loaded", tmp, *args)
        px = [read_pnm(os.path.join(tmp, f"{k:02d}.ppm")) for k in range(8)]
    for k in range(3):
        if not np.array_equal(px[2 + 2 * k], px[3 + 2 * k]):
            raise SystemExit(f"preparation: the two cut-outs differ on {BACKGROUNDS[k]}")
    panels = [("Black under alpha 0,\nalpha ignored", px[0]),
              ("White under alpha 0,\nalpha ignored", px[1]),
              ("Either file,\non gray (default)", px[2]), ("Either file,\non white", px[4]),
              ("Either file,\non black", px[6])]

    def fig(c):
        figure, axes = plt.subplots(1, len(panels), figsize=(10, 2.6))
        for ax, (title, im) in zip(axes, panels):
            ax.imshow(im)
            hide_axes(ax)
            ax.set_title(title, color=c["ink"], fontsize=9)
        return figure

    save(fig, os.path.join(out, NAME), "alpha")


def _save_png(im, path):
    im.save(path)
    return path


def _jpeg_roundtrip(im, quality):
    buf = io.BytesIO()
    im.save(buf, "JPEG", quality=quality)
    buf.seek(0)
    return Image.open(buf).convert("RGB")


# --8<-- [start:copies]
def cutout_copies(base, seed, tmp, stem):
    """An original cut out, then its nine copies (the separability chart's), each edited
    first and cut out after with the original's ellipse: black under alpha 0."""
    ellipse = ellipse_of(seed)
    files = [_save_png(cutout(base, ellipse, BLACK), os.path.join(tmp, f"{stem}.png"))]
    for k, (name, _, steps) in enumerate(transforms()):
        im, (ext, quality) = dict(steps)[COPY_STRENGTHS[name]](base)
        if ext == "jpg":
            im = _jpeg_roundtrip(im, quality)
        files.append(_save_png(cutout(im, ellipse, BLACK), os.path.join(tmp, f"{stem}-{k}.png")))
    return files


def plain_copies(base, seed, tmp, stem):
    """An original and its nine copies, as the corpus charts make them."""
    ref = os.path.join(tmp, f"{stem}.ppm")
    base.save(ref)
    return [ref] + corpus.write_copies(base, tmp, stem)
# --8<-- [end:copies]


def _copies_one(args):
    tool, make, loads, path, i, tmp = args
    files = make(Image.open(path).convert("RGB"), i, tmp, f"{i:04d}")
    rows = {load: run_lines(tool, "measure", f"--load={load}", *files) for load in loads}
    for f in files[1:]:
        os.remove(f)
    return files[0], rows


# --8<-- [start:settings]
def over_corpus(tool, which, name, make, loads):
    """{"n", "label", load: {"copies": {algo: [value]}, "different": {algo: [value]}}} for
    one corpus: every original and its copies made by `make`, each compared with its
    original, and every pair of originals, loaded with each of `loads` (a `--load=`
    setting); cached as preparation-<name>-<corpus>."""
    paths = corpus.images(tool, which)
    out = {"n": len(paths), "label": corpus.label(which, len(paths))}
    if not paths:
        return out

    def measure():
        with tempfile.TemporaryDirectory() as tmp, \
                concurrent.futures.ProcessPoolExecutor() as pool:
            done = list(pool.map(_copies_one, [(tool, make, loads, p, i, tmp)
                                               for i, p in enumerate(paths)]))
            originals = [f for f, _ in done]
            result = {}
            for load in loads:
                copies = {}
                for _, rows in done:
                    for row in rows[load]:
                        for algo, v in row.items():
                            if algo != "file":
                                copies.setdefault(algo, []).append(v)
                different = {}
                for row in run_lines(tool, "pairs", f"--load={load}", *originals):
                    for algo, v in row.items():
                        if algo not in ("a", "b"):
                            different.setdefault(algo, []).append(v)
                result[load] = {"copies": copies, "different": different}
            return result

    key = corpus.cache_key(paths, f"{name} {loads}", also=[MODULE])
    out.update(cached(f"{NAME}-{name}-{which}", key, measure,
                      f"{name}, {len(paths)} images and their copies"))
    return out
# --8<-- [end:settings]


def _separability(mod, copies, different):
    _, lower, convert, _, _ = metric(mod)
    c = [convert(v) for v in copies if v is not None]
    d = [convert(v) for v in different if v is not None]
    return separability(c, d, lower), len(d)


def alpha_backgrounds(tool, out):
    """d′ and the different pairs the copies' threshold accepts, for cut-outs composited
    on each background."""
    lines = []
    for which in corpus.CORPORA:
        data = over_corpus(tool, which, "alpha", cutout_copies,
                           [f"alpha={bg}" for bg in BACKGROUNDS])
        lines.append(f"**{SHORT[which]}** ({data['label']}, each cut out):\n")
        if not data["n"]:
            lines.append(LABEL_MISSING + ".\n")
            continue
        rows = []
        for mod in ALGORITHMS:
            cells = []
            for bg in BACKGROUNDS:
                r = data[f"alpha={bg}"]
                (dprime, _, fmr), n = _separability(mod, r["copies"][mod.ALGO],
                                                    r["different"][mod.ALGO])
                cells.append(f"{dprime:.2f} · {share(fmr, n)}")
            rows.append([_name(mod)] + cells)
        lines.append(table(["Algorithm", "On gray (default)", "On white", "On black"], rows))
        lines.append("")
    write_text(os.path.join(out, NAME), "alpha-backgrounds.md",
               "Each cell: *d′*, then the share of pairs of different images within the "
               f"threshold that accepts {RECALL:.0%} of the copies.\n\n" + "\n".join(lines))


def _hidden_one(args):
    tool, path, i, tmp = args
    base = Image.open(path).convert("RGB")
    ellipse = ellipse_of(i)
    files = [_save_png(cutout(base, ellipse, under), os.path.join(tmp, f"{i:04d}-{n}.png"))
             for n, under in (("black", BLACK), ("white", WHITE))]
    ignored = run_lines(tool, "measure", "--load=alpha=ignore", *files)[0]
    composited = run_lines(tool, "measure", *files)[0]
    for f in files:
        os.remove(f)
    return ignored, composited


def alpha_hidden(tool, out):
    """How far apart two copies of a cut-out land when they differ only in the color under
    alpha 0, with alpha ignored; composited, the build fails unless they are identical."""
    cols, data = [], {}
    for which in corpus.CORPORA:
        paths = corpus.images(tool, which)
        if not paths:
            continue

        def measure():
            with tempfile.TemporaryDirectory() as tmp, \
                    concurrent.futures.ProcessPoolExecutor() as pool:
                return list(pool.map(_hidden_one, [(tool, p, i, tmp)
                                                   for i, p in enumerate(paths)]))

        rows = cached(f"{NAME}-hidden-{which}", corpus.cache_key(paths, "hidden",
                                                                 also=[MODULE]),
                      measure, f"color under alpha, {len(paths)} images")
        for _, composited in rows:
            for mod in ALGORITHMS:
                # Identical images: a similarity of 1, or ColorMoments' distance of 0.
                same = 0.0 if not mod.BITS and mod.LOWER_IS_CLOSER else 1.0
                v = composited[mod.ALGO]
                if v is not None and abs(v - same) > 1e-6:
                    raise SystemExit(f"preparation: composited cut-outs differ ({mod.ALGO})")
        cols.append(corpus.label(which, len(paths)))
        data[which] = [r[0] for r in rows]
    body = [[_name(mod), _unit(mod)] + [_median(mod, [r[mod.ALGO] for r in rows])
                                        for rows in data.values()] for mod in ALGORITHMS]
    write_text(os.path.join(out, NAME), "alpha-hidden.md",
               "Median (25th–75th percentile) over the cut-outs, alpha ignored:\n\n"
               + table(["Algorithm", "Measured in"] + cols, body))


# ---- Grayscale ---------------------------------------------------------------------------

# The grayscale the library computes, (38 R + 75 G + 15 B) >> 7, and the closest 8-bit
# approximation of the same BT.601 weights, (77 R + 150 G + 29 B) >> 8, both computed by
# the tool: the first is checked against the library's own on the example.
WEIGHTS = {"library": "weights=38/75/15/7", "bt601_8bit": "weights=77/150/29/8"}


def gray_weights(tool, image, out):
    check = run_lines(tool, "measure", image, f"--load={WEIGHTS['library']}", image)[0]
    if any(check[m.ALGO] != 1.0 for m in GRAY_ALGORITHMS if m.BITS):
        raise SystemExit("preparation: the tool's grayscale differs from the library's")
    lines = []
    for which in corpus.CORPORA:
        data = over_corpus(tool, which, "weights", plain_copies, [WEIGHTS["bt601_8bit"]])
        if not data["n"]:
            lines.append(f"**{SHORT[which]}**: {LABEL_MISSING}.\n")
            continue
        base = corpus.measure_corpus(tool, which)
        rows = []
        for mod in GRAY_ALGORITHMS:
            convert = metric(mod)[2]
            (d0, _, f0) = separability(*copies_and_different(base, mod.ALGO, convert),
                                       metric(mod)[1])
            r = data[WEIGHTS["bt601_8bit"]]
            (d1, _, f1), n = _separability(mod, r["copies"][mod.ALGO], r["different"][mod.ALGO])
            rows.append([_name(mod), f"{d0:.2f} · {share(f0, n)}", f"{d1:.2f} · {share(f1, n)}"])
        lines.append(f"**{SHORT[which]}** ({data['label']}):\n")
        lines.append(table(["Algorithm", "38/75/15 over 128 (the library)",
                            "77/150/29 over 256"], rows))
        lines.append("")
    write_text(os.path.join(out, NAME), "gray-weights.md",
               "Each cell: *d′*, then the share of pairs of different images within the "
               f"threshold that accepts {RECALL:.0%} of the copies.\n\n" + "\n".join(lines))


def _decoder_gray_one(args):
    tool, path, tmp, i = args
    d = os.path.join(tmp, f"{i:04d}")
    os.makedirs(d)
    run_lines(tool, "loaded", d, path, "--load=gray=decoder", path)
    diff = (read_pnm(os.path.join(d, "01-gray.pgm")).astype(np.int16)
            - read_pnm(os.path.join(d, "00-gray.pgm")))
    levels = {str(k): int(n) for k, n in zip(*np.unique(diff, return_counts=True))}
    row = run_lines(tool, "measure", path, "--load=gray=decoder", path)[0]
    for f in os.listdir(d):
        os.remove(os.path.join(d, f))
    return levels, row


def decoder_gray(tool, out):
    """Over the photographs, as JPEG files: the decoder's grayscale against the library's,
    level by level, and what it does to each grayscale hash."""
    paths = corpus.images(tool, "photos")
    if not paths:
        write_text(os.path.join(out, NAME), "decoder-gray.md", LABEL_MISSING + ".")
        return

    def measure():
        with tempfile.TemporaryDirectory() as tmp, \
                concurrent.futures.ProcessPoolExecutor() as pool:
            return list(pool.map(_decoder_gray_one, [(tool, p, tmp, i)
                                                     for i, p in enumerate(paths)]))

    rows = cached(f"{NAME}-decoder-gray", corpus.cache_key(paths, "decoder-gray",
                                                           also=[MODULE]),
                  measure, f"decoder grayscale, {len(paths)} photographs")
    levels = {}
    for lv, _ in rows:
        for k, n in lv.items():
            levels[int(k)] = levels.get(int(k), 0) + n
    total = sum(levels.values())
    widest = max(abs(k) for k in levels)

    def part(keep):
        return f"{sum(n for k, n in levels.items() if keep(k)) / total:.2%}"

    level_rows = [["−3 or less", part(lambda k: k <= -3)]]
    level_rows += [[f"{k:+d}".replace("-", "−") if k else "0", part(lambda x, k=k: x == k)]
                   for k in (-2, -1, 0, 1, 2)]
    level_rows += [["+3 or more", part(lambda k: k >= 3)]]
    hash_rows = []
    for mod in GRAY_ALGORITHMS:
        raw = np.array([r[mod.ALGO] for _, r in rows])
        v = np.array([_value(mod, x) for x in raw])
        same = (raw == 1.0).mean()  # a similarity, or Radial's correlation, of exactly 1
        hash_rows.append([_name(mod), _unit(mod), f"{same:.0%}",
                          _median(mod, [r[mod.ALGO] for _, r in rows]),
                          _fmt(mod, v.max() if metric(mod)[1] else v.min())])
    write_text(os.path.join(out, NAME), "decoder-gray.md",
               f"Over {corpus.label('photos', len(paths))}, read as the JPEG files they are. "
               "The decoder's gray level minus the library's, over every pixel "
               f"(at most {widest} levels either way):\n\n"
               + table(["Difference", "Share of pixels"], level_rows)
               + "\n\nThe hash of each photograph loaded with the decoder's grayscale against "
               "the library's:\n\n"
               + table(["Algorithm", "Measured in", "Unchanged", "Median (25th–75th)",
                        "Farthest"], hash_rows))


# ---- The area average --------------------------------------------------------------------

# --8<-- [start:area]
def area_weights(sw, sh, dw, dh, col, row):
    """How much of each source pixel the cell (col, row) of a dw×dh grid covers, exactly:
    the grid divides the image into equal areas whatever its size."""
    def axis(n, m, k):
        lo, hi = Fraction(k * n, m), Fraction((k + 1) * n, m)
        return [max(Fraction(0), min(hi, i + 1) - max(lo, Fraction(i))) for i in range(n)]
    return np.outer(axis(sh, dh, row), axis(sw, dw, col))


def area_mean(pixels, weights):
    """The cell's value: the covered pixels averaged by their coverage, rounded half up."""
    total = sum(w * int(p) for w, p in zip(weights.ravel(), pixels.ravel()))
    return int(total / weights.sum() + Fraction(1, 2))
# --8<-- [end:area]


def area_figure(tool, image, out):
    """A small image reduced onto a 3×2 grid by the library, beside the coverage of one cell
    and the mean it gives; fails the build unless every cell is what the weights give."""
    sw, sh, dw, dh = 10, 7, 3, 2
    gray = Image.open(image).convert("L").crop((120, 150, 280, 262)).resize((sw, sh),
                                                                             Image.BOX)
    px = np.asarray(gray)
    with tempfile.TemporaryDirectory() as tmp:
        path = os.path.join(tmp, "small.pgm")
        gray.save(path)
        grid = run_lines(tool, "area", str(dw), str(dh), path)[0]["grid"]
    for r in range(dh):
        for c in range(dw):
            if area_mean(px, area_weights(sw, sh, dw, dh, c, r)) != grid[r * dw + c]:
                raise SystemExit(f"preparation: area cell ({c}, {r}) differs from the library")
    col, row = 1, 0
    weights = area_weights(sw, sh, dw, dh, col, row)

    def frac(w):
        if w == 0:
            return ""
        return "1" if w == 1 else f"{w.numerator}/{w.denominator}"

    def fig(c):
        figure, (a, b) = plt.subplots(1, 2, figsize=(9, 3.4),
                                      gridspec_kw={"width_ratios": [sw, dw * sw / dw * 0.6]})
        a.imshow(px, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
        for (i, j), w in np.ndenumerate(weights):
            if w:
                a.text(j, i - 0.15, frac(w), ha="center", va="center", fontsize=8.5,
                       color=c["ink"], bbox=dict(boxstyle="round,pad=0.15", lw=0,
                                                 fc=c["surface"], alpha=0.85))
                a.text(j, i + 0.3, str(px[i, j]), ha="center", va="center", fontsize=7,
                       color=c["muted"], bbox=dict(boxstyle="round,pad=0.1", lw=0,
                                                   fc=c["surface"], alpha=0.85))
        for k in range(1, dw):
            a.axvline(k * sw / dw - 0.5, color=c["accent"], linewidth=2)
        for k in range(1, dh):
            a.axhline(k * sh / dh - 0.5, color=c["accent"], linewidth=2)
        x0, y0 = col * sw / dw - 0.5, row * sh / dh - 0.5
        a.add_patch(plt.Rectangle((x0, y0), sw / dw, sh / dh, fill=False, linewidth=2.5,
                                  edgecolor=c["accent2"]))
        hide_axes(a)
        a.set_title(f"A {sw}×{sh} image and a {dw}×{dh} grid: how much of each pixel "
                    "the outlined cell covers", color=c["ink"], fontsize=9.5)
        b.imshow(np.array(grid).reshape(dh, dw), cmap="gray", vmin=0, vmax=255,
                 interpolation="nearest")
        for k, v in enumerate(grid):
            i, j = divmod(k, dw)
            b.text(j, i, str(v), ha="center", va="center", fontsize=11, color=c["ink"],
                   bbox=dict(boxstyle="round,pad=0.2", lw=0, fc=c["surface"], alpha=0.85))
        b.add_patch(plt.Rectangle((col - 0.5, row - 0.5), 1, 1, fill=False, linewidth=2.5,
                                  edgecolor=c["accent2"]))
        hide_axes(b)
        b.set_title("The grid the library computes", color=c["ink"], fontsize=9.5)
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out, NAME), "area")
    total = sum(w * int(p) for w, p in zip(weights.ravel(), px.ravel()))
    write_text(os.path.join(out, NAME), "area.md",
               f"The outlined cell covers {frac(weights.sum())} pixels; its pixels weighted "
               f"by coverage sum to {float(total):.2f}, and the mean, "
               f"{float(total / weights.sum()):.2f}, rounds to {grid[row * dw + col]}.")


# ---- Decoding at a reduced scale --------------------------------------------------------

SCALES = (("half", "½"), ("quarter", "¼"), ("eighth", "⅛"))


def _scale_one(args):
    tool, path, tmp, i = args
    jpeg = path
    with Image.open(path) as im:
        if im.format != "JPEG":
            jpeg = os.path.join(tmp, f"{i:04d}.jpg")
            im.convert("RGB").save(jpeg, quality=95)
    args = [jpeg]
    for name, _ in SCALES:
        args += [f"--load=scale={name}", jpeg]
    return run_lines(tool, "measure", *args)


# --8<-- [start:scale]
def decode_scales(tool, which):
    """Every image of a corpus as a JPEG (the synthetic images encoded at quality 95),
    decoded at each reduced scale and compared with the same file decoded in full: one
    row per scale, per image."""
    paths = corpus.images(tool, which)
    if not paths:
        return {"n": 0, "label": corpus.label(which, 0), "rows": []}

    def measure():
        with tempfile.TemporaryDirectory() as tmp, \
                concurrent.futures.ProcessPoolExecutor() as pool:
            return list(pool.map(_scale_one, [(tool, p, tmp, i) for i, p in enumerate(paths)]))

    rows = cached(f"{NAME}-scale-{which}", corpus.cache_key(paths, "scale", also=[MODULE]),
                  measure, f"decode scales, {len(paths)} images")
    return {"n": len(paths), "label": corpus.label(which, len(paths)), "rows": rows}
# --8<-- [end:scale]


def scale_figure(tool, out):
    datasets = [(w, decode_scales(tool, w)) for w in corpus.CORPORA]
    present = [(w, d) for w, d in datasets if d["n"]]
    thresholds = {}
    for w, _ in present:
        base = corpus.measure_corpus(tool, w)
        for mod in ALGORITHMS:
            _, lower, convert, _, _ = metric(mod)
            thresholds[w, mod.ALGO] = separability(
                *copies_and_different(base, mod.ALGO, convert), lower)[1]

    def quart(mod, d, s):
        v = [_value(mod, r[s][mod.ALGO]) for r in d["rows"] if r[s][mod.ALGO] is not None]
        return np.percentile(v, [25, 50, 75]) if v else [np.nan] * 3

    def fig(c):
        colors = {"photos": c["accent"], "synthetic": c["accent2"]}
        figure, axes = plt.subplots(3, 3, figsize=(10, 8.2))
        for ax, mod in zip(axes.ravel(), ALGORITHMS):
            label, lower, _, limits, _ = metric(mod)
            for w, d in present:
                q = np.array([quart(mod, d, s) for s in range(len(SCALES))])
                xs = np.arange(len(SCALES))
                ax.fill_between(xs, q[:, 0], q[:, 2], color=colors[w], alpha=0.18, linewidth=0)
                ax.plot(xs, q[:, 1], color=colors[w], linewidth=2, marker="o", markersize=4)
                ax.axhline(thresholds[w, mod.ALGO], color=colors[w], linewidth=1.2,
                           linestyle=(0, (4, 3)))
            ax.set_xticks(range(len(SCALES)), [s for _, s in SCALES])
            if limits[1] is not None:
                ax.set_ylim(*limits)
            ax.set_title(_name(mod), color=c["ink"], fontsize=10)
            ax.set_ylabel(label, color=c["muted"], fontsize=8.5)
            style_axes(ax, c)
        figure.tight_layout(rect=(0, 0, 1, 0.94))
        handles = [plt.Line2D([], [], color=colors[w], linewidth=2, marker="o", markersize=4)
                   for w, _ in present]
        names = [d["label"] + (", as JPEG at quality 95" if w == "synthetic" else "")
                 for w, d in present]
        handles.append(plt.Line2D([], [], color=c["ink"], linewidth=1.2, linestyle=(0, (4, 3))))
        names.append(f"threshold accepting {RECALL:.0%} of copies, in the corpus's color")
        figure.legend(handles, names, loc="upper center", ncol=2, frameon=False,
                      labelcolor=c["ink"], fontsize=9, bbox_to_anchor=(0.5, 1.0))
        figure.text(0.5, -0.01, "Each image decoded at ½, ¼ and ⅛ of its size against the "
                    "same file decoded in full. Line: median; band: the middle half.",
                    ha="center", color=c["muted"], fontsize=9)
        return figure

    save(fig, os.path.join(out, NAME), "decode-scale")

    lines = []
    for w, d in present:
        rows = []
        for mod in ALGORITHMS:
            lower = metric(mod)[1]
            cells = [_unit(mod)]
            for s in range(len(SCALES)):
                v = [_value(mod, r[s][mod.ALGO]) for r in d["rows"]
                     if r[s][mod.ALGO] is not None]
                t = thresholds[w, mod.ALGO]
                within = np.mean([(x <= t) if lower else (x >= t) for x in v])
                cells.append(f"{_median(mod, [r[s][mod.ALGO] for r in d['rows']])}, "
                             f"{within:.0%}")
            rows.append([_name(mod)] + cells)
        lines.append(f"**{SHORT[w]}** ({d['label']}"
                     + (", encoded as JPEG at quality 95" if w == "synthetic" else "") + "):\n")
        lines.append(table(["Algorithm", "Measured in"] + [s for _, s in SCALES], rows))
        lines.append("")
    if len(present) < len(datasets):
        lines.append(LABEL_MISSING + ".")
    write_text(os.path.join(out, NAME), "decode-scale.md",
               "Each cell: the median (25th–75th percentile) against the full decode, then "
               f"the share of images within the threshold that accepts {RECALL:.0%} of the "
               "copies.\n\n" + "\n".join(lines))

    large = run_lines(tool, "measure", LARGE,
                      *[a for name, _ in SCALES for a in (f"--load=scale={name}", LARGE)])
    with Image.open(LARGE) as im:
        size = f"{im.width}×{im.height}"
    rows = [[_name(mod), _unit(mod)] + [_fmt(mod, _value(mod, r[mod.ALGO])) for r in large]
            for mod in ALGORITHMS]
    write_text(os.path.join(out, NAME), "decode-scale-large.md",
               f"One {size} photograph, each scale against the full decode:\n\n"
               + table(["Algorithm", "Measured in"] + [s for _, s in SCALES], rows))


def scale_times(timing, out):
    """Decoding the 20-megapixel photograph at each scale, alone and with a hash."""
    large = timing["large"]["cases"]

    def ms(case):
        v = large[case]["min_ms"]
        return f"{v:.1f} ms" if v < 100 else f"{v:.0f} ms"

    rows = []
    for name, sym in (("full", "1 (full)"),) + SCALES:
        decode = "decode" if name == "full" else f"scale_{name}_decode"
        rows.append([sym, ms(decode)] + [ms(f"scale_{name}_{a}") for a in
                                         ("phash", "mhash", "radial")])
    w, h = timing["large"]["width"], timing["large"]["height"]
    write_text(os.path.join(out, NAME), "decode-scale-times.md",
               f"A {w}×{h} JPEG; the minimum of the runs, on the machine named at the foot "
               "of the page:\n\n"
               + table(["Scale", "Decode", "Decode + pHash", "Decode + mHash",
                        "Decode + Radial"], rows))


def figures(tool, image, out, timing):
    orientation_figure(tool, image, out)
    alpha_figure(tool, image, out)
    alpha_hidden(tool, out)
    alpha_backgrounds(tool, out)
    gray_weights(tool, image, out)
    decoder_gray(tool, out)
    area_figure(tool, image, out)
    scale_figure(tool, out)
    scale_times(timing, out)
