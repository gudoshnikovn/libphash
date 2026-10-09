"""The figures and tables of docs/guide/performance.md: how decoding and every hash grow
with the size of the image (one photograph at six sizes, measure/timing.py,
measure_sizes()), what each format costs to decode, and what the settings that cut the cost
save on a 20-megapixel JPEG (the cases of `site_stages time`, measure_timing()).
"""
import os

import matplotlib.pyplot as plt

from draw.markdown import table, write_text
from draw.style import save, style_axes
from draw.timing_tables import ms
from measure.timing import FORMATS, measure_sizes
from pages import PAGES, TITLES

NAME = "performance"
FORMAT_TITLES = {"jpeg": "JPEG", "png": "PNG", "webp": "WebP"}

# --8<-- [start:levers]
# The table of what a setting saves: each row a way to load the image, each column the hash
# computed after it, every cell a case of `site_stages time` that loads the file and
# computes that hash.
LEVER_ROWS = (
    ("In color, at full size (the default)", "scale_full"),
    ("As grayscale", "gray"),
    ("At ½ size", "scale_half"),
    ("At ¼ size", "scale_quarter"),
    ("At ⅛ size", "scale_eighth"),
)
LEVER_HASHES = ("phash", "mhash", "radial")
# The four 64-bit hashes, each the first on the image, against ph_compute_multi() with
# all four flags.
FOUR = ("ahash", "dhash", "phash", "whash")
# --8<-- [end:levers]


def _mpx(row):
    return row["width"] * row["height"] / 1e6


def _log_axes(ax, c):
    style_axes(ax, c)
    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.xaxis.set_major_formatter(plt.FuncFormatter(lambda v, _: f"{v:g}"))
    ax.yaxis.set_major_formatter(plt.FuncFormatter(lambda v, _: f"{v:g}"))
    ax.xaxis.set_minor_formatter(plt.NullFormatter())
    ax.yaxis.set_minor_formatter(plt.NullFormatter())


def sizes_figure(data, out):
    """One panel per algorithm: the time of its hash against the number of pixels, beside
    the time to decode the same JPEG, both axes logarithmic and shared."""
    rows = data["sizes"]
    x = [_mpx(r) for r in rows]
    decode = [r["jpeg"]["decode"] for r in rows]
    every = decode + [r["jpeg"][m.ALGO] for r in rows for m in PAGES]

    def fig(c):
        figure, axes = plt.subplots(3, 3, figsize=(10, 8.4), sharex=True, sharey=True)
        for ax, mod in zip(axes.flat, PAGES):
            _log_axes(ax, c)
            times = [r["jpeg"][mod.ALGO] for r in rows]
            ax.plot(x, decode, color=c["muted"], linewidth=1, linestyle=(0, (4, 3)))
            ax.plot(x, times, color=c["accent"], linewidth=1.6, marker="o", markersize=3.5)
            ax.text(x[-1], times[-1] * 0.4, f"{ms(times[-1])} ms", ha="right", va="top",
                    color=c["ink"], fontsize=8.5,
                    bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 0.5})
            ax.set_title(TITLES[mod.ALGO], color=c["ink"], fontsize=10, loc="left")
        axes[0][0].text(x[0], decode[0] * 6, "decoding, dashed", color=c["muted"],
                        fontsize=8.5, va="bottom",
                        bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 0.5})
        axes[0][0].set_ylim(min(every) / 2, max(every) * 2.5)
        for ax in axes[-1]:
            ax.set_xlabel("megapixels", color=c["muted"], fontsize=9)
        for ax in axes[:, 0]:
            ax.set_ylabel("milliseconds", color=c["muted"], fontsize=9)
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out, NAME), "sizes")


def sizes_table(data, out):
    rows = data["sizes"]
    head = ["Case"] + [f"{r['width']}×{r['height']}" for r in rows]
    body = [["decoding"] + [f"{ms(r['jpeg']['decode'])} ms" for r in rows]]
    body += [[TITLES[m.ALGO]] + [f"{ms(r['jpeg'][m.ALGO])} ms" for r in rows] for m in PAGES]
    write_text(os.path.join(out, NAME), "sizes.md",
               "One JPEG at six sizes; the minimum of the runs.\n\n" + table(head, body))


def formats_figure(data, out):
    """One panel per format: the time to load the photograph in color and as grayscale
    against the number of pixels, both axes logarithmic and shared."""
    rows = data["sizes"]
    x = [_mpx(r) for r in rows]
    every = [r[f][case] for r in rows for f in FORMATS for case in ("decode", "gray_decode")]

    def fig(c):
        figure, axes = plt.subplots(1, 3, figsize=(10, 3.6), sharex=True, sharey=True)
        for ax, fmt in zip(axes, FORMATS):
            _log_axes(ax, c)
            color = [r[fmt]["decode"] for r in rows]
            gray = [r[fmt]["gray_decode"] for r in rows]
            # Gray under color, wider, so that where the two are equal (WebP) both show.
            ax.plot(x, gray, color=c["accent2"], linewidth=3.2, marker="o", markersize=5,
                    label="as grayscale")
            ax.plot(x, color, color=c["accent"], linewidth=1.4, marker="o", markersize=3,
                    label="in color")
            ax.text(x[-1], max(color[-1], gray[-1]) * 1.35, f"{ms(color[-1])} ms",
                    ha="right", va="bottom", color=c["ink"], fontsize=8.5)
            ax.set_title(FORMAT_TITLES[fmt], color=c["ink"], fontsize=10, loc="left")
            ax.set_xlabel("megapixels", color=c["muted"], fontsize=9)
        axes[0].set_ylim(min(every) / 2, max(every) * 3)
        axes[0].set_ylabel("milliseconds", color=c["muted"], fontsize=9)
        handles, labels = axes[0].get_legend_handles_labels()
        legend = axes[0].legend(handles[::-1], labels[::-1], frameon=False, fontsize=8.5,
                                loc="upper left")
        for text in legend.get_texts():
            text.set_color(c["ink"])
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out, NAME), "formats")


def formats_table(data, out):
    rows = data["sizes"]
    head = ["Format, load"] + [f"{r['width']}×{r['height']}" for r in rows]
    body = []
    for fmt in FORMATS:
        for case, how in (("decode", "in color"), ("gray_decode", "as grayscale")):
            body.append([f"{FORMAT_TITLES[fmt]}, {how}"]
                        + [f"{ms(r[fmt][case])} ms" for r in rows])
        body.append([f"{FORMAT_TITLES[fmt]}, bytes per pixel"]
                    + [f"{r[fmt]['bytes'] / (r['width'] * r['height']):.2f}" for r in rows])
    write_text(os.path.join(out, NAME), "formats.md",
               "One photograph at six sizes, encoded as JPEG (quality 90), PNG and WebP "
               "(quality 90); the minimum of the runs.\n\n" + table(head, body))


def _change(value, base):
    return "" if value == base else f", {(value / base - 1) * 100:+.0f}\u00a0%".replace("-", "−")


def levers_table(timing, out):
    """What each way of loading costs with each hash after it, on the large JPEG."""
    large = timing["large"]
    cases = large["cases"]
    head = ["Load"] + [f"load + {TITLES[h]}" for h in LEVER_HASHES]
    body = []
    for how, prefix in LEVER_ROWS:
        cells = []
        for h in LEVER_HASHES:
            v, base = cases[f"{prefix}_{h}"]["min_ms"], cases[f"scale_full_{h}"]["min_ms"]
            cells.append(f"{ms(v)}\u00a0ms{_change(v, base)}")
        body.append([how] + cells)
    write_text(os.path.join(out, NAME), "levers.md",
               f"A {large['width']}×{large['height']} JPEG, loaded into a context kept "
               "from run to run; the minimum of the runs.\n\n" + table(head, body))


def multi_table(timing, out):
    """The four 64-bit hashes each computed as the first on the image, added up, against
    one ph_compute_multi() call; and decoding, for scale."""
    head = ["Image", "Decoding", "aHash + dHash + pHash + wHash, each the first",
            "`ph_compute_multi()`, all four"]
    body = []
    for entry in (timing["small"], timing["large"]):
        c = entry["cases"]
        apart = sum(c[h]["min_ms"] for h in FOUR)
        body.append([f"{entry['width']}×{entry['height']}", f"{ms(c['decode']['min_ms'])} ms",
                     f"{ms(apart)} ms", f"{ms(c['multi']['min_ms'])} ms"])
    write_text(os.path.join(out, NAME), "multi.md",
               "The minimum of the runs.\n\n" + table(head, body))


def decode_share(timing, out):
    """The sentence of "One image, one thread": what part of loading the large JPEG and
    computing the four 64-bit hashes is the decoding."""
    c = timing["large"]["cases"]
    dec, multi = c["decode"]["min_ms"], c["multi"]["min_ms"]
    write_text(os.path.join(out, NAME), "decode-share.md",
               f"On the {timing['large']['width']}×{timing['large']['height']} JPEG, "
               f"decoding is {dec / (dec + multi) * 100:.0f} % of a load followed by "
               "`ph_compute_multi()` with all four flags.")


def figures(tool, image, out, timing):
    data = measure_sizes(tool)
    sizes_figure(data, out)
    sizes_table(data, out)
    formats_figure(data, out)
    formats_table(data, out)
    levers_table(timing, out)
    multi_table(timing, out)
    decode_share(timing, out)
