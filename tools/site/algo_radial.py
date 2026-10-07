"""The figures of docs/theory/radial.md, from `site_stages radial`, `site_stages
radial-profiles` and `site_stages radial-variants`."""
import json
import os
import subprocess
import tempfile

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

import corpus
from common import hide_axes, run_stages, save, style_axes
from corpus_charts import copies_and_different, separability, share
from timing import measure_timing, ms

ALGO = "radial"
BITS = None  # not a bit vector: the charts show the library's own score
METRIC = "peak correlation"
LOWER_IS_CLOSER = False
METRIC_RANGE = (0.0, 1.0)
FORMAT = "{:.3f}"
REFERENCE = (0.9, "PH_RADIAL_PCC_THRESHOLD")
REFUSED = "an image with no angular structure"

SHORT = {"photos": "photographs", "synthetic": "synthetic images"}

# The settings of `site_stages radial-variants`, grouped as the page shows them.
SIGMAS = [("sigma_1", "σ 1"), ("default", "σ 3.5 (default)"), ("sigma_8", "σ 8")]
GAMMAS = [("gamma_0.5", "γ 0.5"), ("default", "γ 1 (default)"), ("gamma_2", "γ 2")]
GRIDS = [("grid_40x32", 40, 32), ("grid_90x64", 90, 64), ("default", 180, 128),
         ("grid_360x256", 360, 256), ("grid_1440x1024", 1440, 1024),
         ("grid_4096x4096", 4096, 4096)]


def _digest(hexstr):
    return np.frombuffer(bytes.fromhex(hexstr), np.uint8)


# --8<-- [start:similarity]
def similarity(a, b):
    """ph_radial_similarity() for rows of digests: a[i] against b[i], the Pearson
    correlation at every cyclic shift of b[i], the largest of them; NaN where either
    digest has all its bytes equal, the pair the library refuses."""
    a, b = np.atleast_2d(a).astype(float), np.atleast_2d(b).astype(float)
    ra = a - a.mean(axis=1, keepdims=True)
    rb = b - b.mean(axis=1, keepdims=True)
    denom = np.sqrt((ra ** 2).sum(axis=1) * (rb ** 2).sum(axis=1))
    # np.roll(rb, d)[i] is rb[(i - d) % n], the term ph_radial_similarity() pairs with ra[i].
    peak = np.max([(ra * np.roll(rb, d, axis=1)).sum(axis=1)
                   for d in range(a.shape[1])], axis=0)
    with np.errstate(invalid="ignore", divide="ignore"):
        return np.where(denom > 0, np.clip(peak / np.where(denom > 0, denom, 1), -1, 1), np.nan)


def peak_profile_correlation(p, q):
    """The same score on two variance profiles instead of two digests: the Pearson
    correlation at every cyclic shift of q, the largest, and the shift it is found at."""
    p = (p - p.mean()) / p.std()
    q = (q - q.mean()) / q.std()
    scores = [np.mean(p * np.roll(q, d)) for d in range(len(p))]
    return max(scores), int(np.argmax(scores))
# --8<-- [end:similarity]


def _profiles(tool, reference, images):
    """`site_stages radial-profiles` on PIL images: one row per image, the reference's
    first."""
    with tempfile.TemporaryDirectory() as tmp:
        files = []
        for i, im in enumerate([reference] + images):
            files.append(os.path.join(tmp, f"{i:04d}.ppm"))
            im.save(files[-1])
        out = subprocess.run([tool, "radial-profiles", *files], check=True,
                             capture_output=True, text=True).stdout
    return [json.loads(line) for line in out.splitlines()]


def figures(tool, image, out_dir):
    st, px = run_stages(tool, ALGO, image, ("original.ppm", "gray.pgm", "blurred.pgm",
                                            "sigma-1.pgm", "sigma-8.pgm", "gamma-0.5.pgm",
                                            "gamma-2.pgm"))
    d = st["default"]
    out = os.path.join(out_dir, ALGO)
    var = np.array(d["variance"])
    lines = np.array(st["lines"]).reshape(d["projections"], d["samples"])
    cx, cy, radius = st["center_x"], st["center_y"], st["radius"]
    hi, lo = int(np.argmax(var)), int(np.argmin(var))

    def fan(ax, c, every=10, picked=()):
        """The blurred image with every `every`-th line and the disc the lines cover."""
        ax.imshow(px["blurred.pgm"], cmap="gray", vmin=0, vmax=255)
        for i in range(0, d["projections"], every):
            t = np.pi * i / d["projections"]
            ax.plot([cx - radius * np.cos(t), cx + radius * np.cos(t)],
                    [cy - radius * np.sin(t), cy + radius * np.sin(t)], color=c["accent"],
                    linewidth=0.8, alpha=0.75)
        for i, color in picked:
            t = np.pi * i / d["projections"]
            ax.plot([cx - radius * np.cos(t), cx + radius * np.cos(t)],
                    [cy - radius * np.sin(t), cy + radius * np.sin(t)], color=color,
                    linewidth=2.4)
        ax.add_patch(plt.Circle((cx, cy), radius, fill=False, color=c["accent"],
                                linewidth=1, linestyle=(0, (4, 3))))
        ax.set_xlim(-0.5, st["width"] - 0.5)
        ax.set_ylim(st["height"] - 0.5, -0.5)
        hide_axes(ax)

    def digest_text(fig, c, y):
        h = d["digest"]
        pad = chr(0xA0) * len("digest = ")
        fig.text(0.5, y, f"digest = {h[:40]}\n{pad}{h[40:]}", ha="center",
                 multialignment="left", color=c["ink"], family="monospace", fontsize=10)

    def pipeline(c):
        fig, axes = plt.subplots(1, 4, figsize=(10.4, 3.1))
        axes[0].imshow(px["original.ppm"])
        axes[1].imshow(px["gray.pgm"], cmap="gray", vmin=0, vmax=255)
        axes[2].imshow(px["blurred.pgm"], cmap="gray", vmin=0, vmax=255)
        fan(axes[3], c)
        titles = ["Decoded image", "Grayscale", f"Blurred, σ {d['sigma']:g}",
                  f"{d['projections']} lines, every 10th drawn"]
        for ax, t in zip(axes, titles):
            hide_axes(ax)
            ax.set_title(t, color=c["ink"], fontsize=10)
        digest_text(fig, c, -0.02)
        return fig

    def projections(c):
        fig = plt.figure(figsize=(10.4, 4.2))
        ax_im = fig.add_axes([0.0, 0.05, 0.36, 0.85])
        fan(ax_im, c, every=10, picked=((hi, c["accent2"]), (lo, c["ink"])))
        for deg in (0, 30, 60, 90, 120, 150):
            t = np.pi * deg / 180
            ax_im.annotate(f"{deg}°", (cx + radius * np.cos(t), cy + radius * np.sin(t)),
                           xytext=(4 * np.cos(t), 4 * np.sin(t)), textcoords="offset points",
                           ha="left" if np.cos(t) > 0.3 else "center",
                           va="top" if np.sin(t) > 0.3 else "center", color=c["ink"],
                           fontsize=8.5,
                           bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 0.5})
        ax_im.set_title("every 10th of the 180 lines; the dashed disc\nis all the lines read",
                        color=c["ink"], fontsize=9.5)
        ax = fig.add_axes([0.44, 0.16, 0.55, 0.68])
        pos = (np.arange(d["samples"]) - d["samples"] / 2) * radius / (d["samples"] / 2)
        for i, color in ((hi, c["accent2"]), (lo, c["ink"])):
            v = lines[i]
            ok = v >= 0
            ax.plot(pos[ok], v[ok], color=color, linewidth=1.6,
                    label=f"{i}°: variance {var[i]:,.0f}")
        ax.set_xlabel("position along the line, pixels from the center", color=c["muted"],
                      fontsize=9)
        ax.set_ylabel("gray level", color=c["muted"], fontsize=9)
        ax.set_ylim(0, 255)
        ax.legend(frameon=False, labelcolor=c["ink"], fontsize=9, loc="upper left",
                  bbox_to_anchor=(0, 1.2), ncol=2)
        style_axes(ax, c)
        ax.set_title(f"the {d['samples']} points each line reads", color=c["ink"], fontsize=9.5,
                     loc="right")
        return fig

    def profile(c):
        fig, ax = plt.subplots(figsize=(10, 3.3))
        x = np.arange(d["projections"]) * 180 / d["projections"]
        ax.plot(x, var, color=c["accent"], linewidth=1.8)
        ax.axhline(d["mean"], color=c["muted"], linewidth=1, linestyle=(0, (4, 3)))
        ax.annotate(f"mean {d['mean']:,.0f}", (178, d["mean"]), xytext=(0, 3),
                    textcoords="offset points", ha="right", va="bottom", color=c["muted"],
                    fontsize=9)
        for i, color in ((hi, c["accent2"]), (lo, c["ink"])):
            ax.plot([x[i]], [var[i]], "o", color=color, markersize=6)
        ax.set_xlim(0, 180)
        ax.set_xticks(range(0, 181, 30), [f"{t}°" for t in range(0, 181, 30)])
        ax.set_xlabel("angle of the line", color=c["muted"], fontsize=9)
        ax.set_ylabel("variance, gray levels²", color=c["muted"], fontsize=9)
        style_axes(ax, c)
        return fig

    coef = np.array(d["coefficients"])
    std = np.array(d["standardized"])

    def idct(coefficients, n):
        """The profile the first coefficients describe: the inverse of the orthonormal
        DCT-II with the coefficients past them set to zero."""
        k = np.arange(len(coefficients))
        scale = np.where(k == 0, np.sqrt(1 / n), np.sqrt(2 / n))
        basis = np.cos(np.pi * (2 * np.arange(n)[:, None] + 1) * k[None, :] / (2 * n))
        return basis @ (coefficients * scale)

    def dct(c):
        fig, axes = plt.subplots(1, 2, figsize=(10.4, 3.4),
                                 gridspec_kw={"width_ratios": [1.25, 1]})
        x = np.arange(d["projections"]) * 180 / d["projections"]
        axes[0].plot(x, std, color=c["muted"], linewidth=1.2, label="standardized profile")
        axes[0].plot(x, idct(coef, d["projections"]), color=c["accent"], linewidth=1.8,
                     label="what the 40 coefficients keep")
        axes[0].axhline(0, color=c["grid"], linewidth=1)
        axes[0].set_xlim(0, 180)
        axes[0].set_xticks(range(0, 181, 30), [f"{t}°" for t in range(0, 181, 30)])
        axes[0].set_xlabel("angle of the line", color=c["muted"], fontsize=9)
        axes[0].set_ylabel("standard deviations from the mean", color=c["muted"], fontsize=9)
        axes[0].legend(frameon=False, labelcolor=c["ink"], fontsize=8.5, loc="upper center",
                       bbox_to_anchor=(0.5, 1.2), ncol=2)
        k = np.arange(len(coef))
        axes[1].bar(k, coef, width=0.7, color=c["accent"], linewidth=0)
        axes[1].axhline(0, color=c["grid"], linewidth=1)
        axes[1].set_xlabel("coefficient k  (k/2 cycles over 180°)", color=c["muted"],
                           fontsize=9)
        axes[1].set_ylabel("DCT coefficient", color=c["muted"], fontsize=9)
        axes[1].set_xlim(-0.8, len(coef) - 0.2)
        for ax in axes:
            style_axes(ax, c)
        fig.tight_layout()
        return fig

    def layout(c):
        b = _digest(d["digest"])
        fig, axes = plt.subplots(2, 1, figsize=(10.4, 4.6), sharex=True,
                                 gridspec_kw={"height_ratios": [1, 1.2]})
        k = np.arange(len(b))
        lo_c, hi_c = coef.min(), coef.max()
        axes[0].bar(k, coef, width=0.7, color=c["muted"], linewidth=0)
        for v, name in ((lo_c, "min"), (hi_c, "max")):
            axes[0].axhline(v, color=c["accent2"], linewidth=1, linestyle=(0, (4, 3)))
            axes[0].annotate(f"{name} → {0 if name == 'min' else 255}", (len(b) - 0.5, v),
                             xytext=(0, 3 if name == "max" else -3), textcoords="offset points",
                             ha="right", va="bottom" if name == "max" else "top",
                             color=c["accent2"], fontsize=8.5,
                             bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 0.5})
        axes[0].set_ylabel("coefficient", color=c["muted"], fontsize=9)
        axes[1].bar(k, b, width=0.7, color=c["accent"], linewidth=0)
        zero = 255 * (0 - lo_c) / (hi_c - lo_c)
        axes[1].axhline(zero, color=c["grid"], linewidth=1)
        for i, v in enumerate(b):
            axes[1].text(i, int(v) + 4, f"{v:02x}", ha="center", va="bottom", fontsize=7,
                         color=c["ink"], family="monospace", rotation=90)
        axes[1].set_ylim(0, 320)
        axes[1].set_yticks([0, 64, 128, 192, 255])
        axes[1].set_ylabel("byte", color=c["muted"], fontsize=9)
        axes[1].set_xticks(range(0, 40, 5))
        axes[1].set_xlabel("byte k = coefficient k; above each, the byte in hexadecimal",
                           color=c["muted"], fontsize=9)
        for ax in axes:
            style_axes(ax, c)
        fig.tight_layout()
        return fig

    save(pipeline, out, "pipeline")
    save(projections, out, "projections")
    save(profile, out, "profile")
    save(dct, out, "dct")
    save(layout, out, "bit-order")
    _settings_figure(st, px, out)
    _write_load_grayscale(st, out)
    _write_stage_numbers(st, out)

    base = Image.open(image).convert("RGB")
    _structure_figure(tool, base, out)
    _rotation_figures(tool, base, out)
    _mirror_table(tool, base, out)
    measured = corpus.measure_settings(tool, "radial-variants")
    _settings_tables(tool, measured, out)
    _grid_figure(tool, image, out)


def _write_stage_numbers(st, out):
    """The numbers the steps quote, for the example photograph."""
    d = st["default"]
    b = _digest(d["digest"])
    coef = np.array(d["coefficients"])
    with open(os.path.join(out, "stage-numbers.md"), "w") as f:
        f.write(f"On the example photograph, {st['width']}×{st['height']}, the lines run "
                f"{st['radius']:g} pixels each side of the center; the variance ranges from "
                f"{min(d['variance']):,.0f} to {max(d['variance']):,.0f} gray levels², the "
                f"largest coefficient is number {int(np.argmax(coef))} and the smallest "
                f"number {int(np.argmin(coef))}, and the zero of coefficient 0 lands at byte "
                f"value {b[0]}.\n")


def _write_load_grayscale(st, out):
    a = _digest(st["default"]["digest"])
    b = _digest(st["digest_load_grayscale"])
    text = ("The example photograph gives the same digest both ways." if (a == b).all() else
            f"On the example photograph the two digests differ in {int((a != b).sum())} of "
            f"40 bytes, by at most {int(np.abs(a.astype(int) - b).max())}, and "
            f"`ph_radial_similarity()` scores them {FORMAT.format(float(similarity(a, b)[0]))}.")
    with open(os.path.join(out, "load-grayscale.md"), "w") as f:
        f.write(text + "\n")


def _settings_figure(st, px, out):
    """The blurred image and its profile at each sigma, and at each gamma."""
    for group, key, files, labels in (
            ("sigmas", "sigma", ("sigma-1.pgm", "blurred.pgm", "sigma-8.pgm"),
             ("σ 1", "σ 3.5, the default", "σ 8")),
            ("gammas", "gamma", ("gamma-0.5.pgm", "blurred.pgm", "gamma-2.pgm"),
             ("γ 0.5", "γ 1, the default", "γ 2"))):
        found = {f"{r[key]:g}": r for r in st[group]}
        shown = [found[labels[0].split()[1]], st["default"], found[labels[2].split()[1]]]

        def fig(c, files=files, labels=labels, shown=shown):
            f = plt.figure(figsize=(10.4, 5.6))
            for n, (name, label) in enumerate(zip(files, labels)):
                ax = f.add_axes([0.02 + n * 0.33, 0.42, 0.29, 0.52])
                ax.imshow(px[name], cmap="gray", vmin=0, vmax=255)
                hide_axes(ax)
                ax.set_title(label, color=c["ink"], fontsize=10)
            ax = f.add_axes([0.07, 0.07, 0.9, 0.28])
            x = np.arange(180)
            styles = [(c["accent2"], 1.4), (c["accent"], 2.0), (c["muted"], 1.4)]
            for r, label, (color, lw) in zip(shown, labels, styles):
                ax.plot(x, r["standardized"], color=color, linewidth=lw, label=label)
            ax.set_xlim(0, 180)
            ax.set_xticks(range(0, 181, 30), [f"{t}°" for t in range(0, 181, 30)])
            ax.set_ylabel("standardized\nvariance", color=c["muted"], fontsize=9)
            ax.set_xlabel("angle of the line", color=c["muted"], fontsize=9)
            ax.legend(frameon=False, labelcolor=c["ink"], fontsize=9, ncol=3,
                      loc="lower center", bbox_to_anchor=(0.5, 1.0))
            style_axes(ax, c)
            return f

        save(fig, out, key)


def _structure_figure(tool, base, out):
    """Two synthetic images, one without angular structure and one just above the bound,
    and the example photograph: each profile relative to its mean, and the verdict."""
    synth = corpus.images(tool, "synthetic", os.path.join(corpus.CACHE, "work"))
    rows = _profiles(tool, base, [Image.open(p).convert("RGB") for p in synth])
    synth_rows = rows[1:]
    spread = [r["radial"]["relative_spread"] for r in synth_rows]
    flat = [i for i, r in enumerate(synth_rows) if not r["radial"]["structure"]]
    above = [i for i, r in enumerate(synth_rows) if r["radial"]["structure"]]
    picks = [flat[0] if flat else int(np.argmin(spread)),
             min(above, key=lambda i: spread[i])]
    shown = [(f"synthetic image {i}", Image.open(synth[i]).convert("RGB"), synth_rows[i])
             for i in picks] + [("the example photograph", base, rows[0])]
    bound = 1e-4

    def fig(c):
        f, axes = plt.subplots(2, 3, figsize=(10.4, 5.4),
                               gridspec_kw={"height_ratios": [1.15, 1]})
        for n, (name, im, row) in enumerate(shown):
            r = row["radial"]
            axes[0, n].imshow(np.asarray(im))
            hide_axes(axes[0, n])
            axes[0, n].set_title(name, color=c["ink"], fontsize=10)
            v = np.array(r["variance"]) / r["mean"]
            ax = axes[1, n]
            ax.axhspan(1 - np.sqrt(bound), 1 + np.sqrt(bound), color=c["accent2"], alpha=0.25,
                       linewidth=0)
            ax.plot(np.arange(180), v, color=c["accent"], linewidth=1.6)
            ax.set_xlim(0, 180)
            ax.set_xticks(range(0, 181, 60), [f"{t}°" for t in range(0, 181, 60)])
            ax.set_ylim(min(0.6, v.min() - 0.05), max(1.4, v.max() + 0.05))
            verdict = ("digest, scored" if r["structure"]
                       else "no structure: all-zero digest")
            ax.set_title(f"spread {np.sqrt(r['relative_spread']):.2%} of the mean\n{verdict}",
                         color=c["ink"] if r["structure"] else c["accent2"], fontsize=9.5)
            style_axes(ax, c)
        axes[1, 0].set_ylabel("variance / its mean", color=c["muted"], fontsize=9)
        f.tight_layout(rect=(0, 0.05, 1, 1))
        f.text(0.5, 0.0, "Orange band: within 1 % of the mean. A profile whose spread is "
               "smaller has no structure to describe.", ha="center", color=c["muted"],
               fontsize=9)
        return f

    save(fig, out, "structure")
    n_flat = len(flat)
    with open(os.path.join(out, "structure.md"), "w") as f:
        f.write(f"{n_flat} of the {len(synth)} synthetic images, both sets of concentric "
                f"rings, have no structure by this measure; the smallest spread among the "
                f"others is {np.sqrt(min(spread[i] for i in above)):.1%} of the mean"
                if n_flat == 2 else
                f"{n_flat} of the {len(synth)} synthetic images have no structure by this "
                "measure")
        f.write(", and the smallest among the photographs "
                f"{_min_photo_spread(tool):.1%}.\n")


def _min_photo_spread(tool):
    paths = corpus.images(tool, "photos", os.path.join(corpus.CACHE, "work"))
    if not paths:
        return float("nan")
    out = subprocess.run([tool, "radial-profiles", *paths], check=True, capture_output=True,
                         text=True).stdout
    return min(np.sqrt(json.loads(line)["radial"]["relative_spread"])
               for line in out.splitlines())


# --8<-- [start:rotation]
def _rotate(im, degrees):
    """The rotation of transforms.py: about the center, bicubic, the corners filled with
    the image's mean color (the lines never read them: they stay inside the disc)."""
    fill = tuple(int(v) for v in np.asarray(im).reshape(-1, 3).mean(axis=0))
    return im.rotate(degrees, resample=Image.BICUBIC, fillcolor=fill)


def rotation_sweep(tool, base, angles):
    """For each angle, the library's score for the rotated image's digest against the
    original's, and the same score computed on the two variance profiles instead, with
    the shift it is found at."""
    rows = _profiles(tool, base, [_rotate(base, a) for a in angles])
    ref = np.array(rows[0]["radial"]["variance"])
    out = []
    for a, row in zip(angles, rows[1:]):
        p, shift = peak_profile_correlation(ref, np.array(row["radial"]["variance"]))
        out.append({"angle": a, "digest": row["similarity"], "profile": p, "shift": shift,
                    "variance": row["radial"]["variance"]})
    return rows[0], out
# --8<-- [end:rotation]


TABLE_ANGLES = (1, 2, 3, 5, 10, 15, 30, 45, 90, 135, 180)


def _rotation_figures(tool, base, out):
    angles = list(range(1, 181))
    ref, sweep = rotation_sweep(tool, base, angles)
    by = {r["angle"]: r for r in sweep}
    v0 = np.array(ref["radial"]["variance"])
    turn = 15
    v15 = np.array(by[turn]["variance"])

    def shifted(c):
        f, ax = plt.subplots(figsize=(10, 3.3))
        x = np.arange(180)
        ax.plot(x, v0, color=c["accent"], linewidth=1.8, label="original")
        ax.plot(x, v15, color=c["accent2"], linewidth=1.6, label=f"turned {turn}°")
        ax.plot(x, np.roll(v15, by[turn]["shift"]), color=c["ink"], linewidth=1,
                linestyle=(0, (2, 2)),
                label=f"turned {turn}°, its profile moved {by[turn]['shift']}° along")
        ax.set_xlim(0, 180)
        ax.set_xticks(range(0, 181, 30), [f"{t}°" for t in range(0, 181, 30)])
        ax.set_xlabel("angle of the line", color=c["muted"], fontsize=9)
        ax.set_ylabel("variance, gray levels²", color=c["muted"], fontsize=9)
        ax.legend(frameon=False, labelcolor=c["ink"], fontsize=9, ncol=3, loc="lower center",
                  bbox_to_anchor=(0.5, 1.0))
        style_axes(ax, c)
        return f

    def sweep_fig(c):
        f, ax = plt.subplots(figsize=(10, 3.8))
        xs = [0] + angles
        ax.axhline(REFERENCE[0], color=c["muted"], linewidth=1, linestyle=(0, (4, 3)))
        ax.annotate(f"{REFERENCE[1]}, {REFERENCE[0]:g}", (180, REFERENCE[0]), xytext=(0, -3),
                    textcoords="offset points", ha="right", va="top", color=c["muted"],
                    fontsize=8.5, bbox={"facecolor": c["surface"], "edgecolor": "none",
                                        "pad": 1})
        ax.plot(xs, [1.0] + [by[a]["profile"] for a in angles], color=c["accent2"],
                linewidth=1.8, label="the variance profiles, best shift")
        ax.plot(xs, [1.0] + [by[a]["digest"] for a in angles], color=c["accent"],
                linewidth=2, label="the digests, ph_radial_similarity()")
        ax.set_xlim(0, 180)
        ax.set_ylim(0, 1.02)
        ax.set_xticks(range(0, 181, 15), [f"{t}°" for t in range(0, 181, 15)])
        ax.set_xlabel("rotation of the image", color=c["muted"], fontsize=9)
        ax.set_ylabel(METRIC, color=c["muted"], fontsize=9)
        ax.legend(frameon=False, labelcolor=c["ink"], fontsize=9, ncol=2, loc="lower center",
                  bbox_to_anchor=(0.5, 1.0))
        style_axes(ax, c)
        f.text(0.5, -0.04, "One image, the example photograph, turned by every whole degree.",
               ha="center", color=c["muted"], fontsize=9)
        return f

    save(shifted, out, "rotation-profile")
    save(sweep_fig, out, "rotation")
    lines = ["| Rotation | Digests: `ph_radial_similarity()` | Profiles: best shift | "
             "Shift found |", "|---|---|---|---|"]
    for a in TABLE_ANGLES:
        r = by[a]
        lines.append(f"| {a}° | {FORMAT.format(r['digest'])} | {FORMAT.format(r['profile'])} | "
                     f"{r['shift']} |")
    with open(os.path.join(out, "rotation-table.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    first = next(a for a in angles if by[a]["digest"] < REFERENCE[0])
    with open(os.path.join(out, "rotation-numbers.md"), "w") as f:
        f.write(f"On this photograph the digests stay at or above {REFERENCE[0]:g} up to "
                f"{first - 1}°; at a quarter turn they score "
                f"{FORMAT.format(by[90]['digest'])}, and at a half turn "
                f"{FORMAT.format(by[180]['digest'])}. The profiles never fall below "
                f"{FORMAT.format(min(r['profile'] for r in sweep))}.\n")


def _mirror_table(tool, base, out):
    """A mirror image runs the profile backwards: line θ becomes line 180° − θ. Checked on
    the example photograph: the mirrored image's profile against the original's read
    backwards, and their DCTs."""
    rows = _profiles(tool, base, [base.transpose(Image.Transpose.FLIP_LEFT_RIGHT)])
    p0 = np.array(rows[0]["radial"]["standardized"])
    pm = np.array(rows[1]["radial"]["standardized"])
    back = np.roll(p0[::-1], 1)  # index i -> (180 - i) mod 180
    c0 = np.array(rows[0]["radial"]["coefficients"])
    cm = np.array(rows[1]["radial"]["coefficients"])
    k = np.arange(len(c0))
    same = np.corrcoef(pm, back)[0, 1]
    odd = np.corrcoef(cm[k % 2 == 1], -c0[k % 2 == 1])[0, 1]
    even = np.corrcoef(cm[k % 2 == 0][1:], c0[k % 2 == 0][1:])[0, 1]
    with open(os.path.join(out, "mirror.md"), "w") as f:
        f.write(f"On the example photograph the mirrored image's profile correlates with the "
                f"original's read backwards at {same:.3f}; its odd coefficients correlate "
                f"with the original's turned in sign at {odd:.2f}, its even ones with the "
                f"original's unchanged at {even:.2f}, and `ph_radial_similarity()` scores the "
                f"two digests {FORMAT.format(rows[1]['similarity'])}.\n")


def _variant_scores(images, variant):
    """(copies, different) scores for one setting: each original against its copies, and
    every pair of distinct originals; refused pairs (NaN) dropped."""
    dig = np.array([[_digest(row[variant]) for row in img] for img in images])
    n, k = dig.shape[0], dig.shape[1] - 1
    copies = similarity(np.repeat(dig[:, 0], k, axis=0), dig[:, 1:].reshape(n * k, -1))
    i, j = np.triu_indices(n, 1)
    different = similarity(dig[i, 0], dig[j, 0])
    return copies[~np.isnan(copies)], different[~np.isnan(different)]


def _settings_tables(tool, measured, out):
    """Per group of settings, d′ and the threshold over both corpora; the default's d′
    checked against the corpus charts, which take the library's own scores."""
    rows = {}
    for key, data in measured.items():
        if not data["n"]:
            continue
        rows[key] = {}
        names = {v for group in (SIGMAS, GAMMAS) for v, _ in group} | {g[0] for g in GRIDS}
        for v in sorted(names):
            c, d = _variant_scores(data["images"], v)
            rows[key][v] = (*separability(c, d, False), len(d))
        cached = corpus.measure_corpus(tool, key)
        cc, cd = copies_and_different(cached, ALGO, lambda x: x)
        theirs = separability(cc, cd, False)[0]
        mine = rows[key]["default"][0]
        # The library prints its score to six decimals; computed here it is exact.
        if abs(mine - theirs) > 1e-3:
            raise SystemExit(f"render: radial: the default over the {key} corpus gives d′ "
                             f"{mine:.4f} here and {theirs:.4f} in the corpus charts")

    grids = [(g, f"{p} × {s}" + (" (default)" if g == "default" else "")) for g, p, s in GRIDS]
    for name, group in (("sigma", SIGMAS), ("gamma", GAMMAS), ("grid", grids)):
        what = {"sigma": "Blur", "gamma": "Gamma", "grid": "Lines × points"}[name]
        lines = [f"| Corpus | {what} | d′ | Threshold | Different pairs within it |",
                 "|---|---|---|---|---|"]
        for key in ("photos", "synthetic"):
            if key not in rows:
                lines.append(f"| {measured[key]['label']}: not available to this build "
                             "| | | | |")
                continue
            for v, label in group:
                dp, t, fmr, npairs = rows[key][v]
                lines.append(f"| {SHORT[key]} | {label} | {dp:.2f} | {FORMAT.format(t)} | "
                             f"{share(fmr, npairs)} |")
        with open(os.path.join(out, f"{name}-corpus.md"), "w") as f:
            f.write("\n".join(lines) + "\n")


def _grid_figure(tool, image, out):
    """For each number of lines and points, the time on both images of the timing table
    and how close the example photograph's digest comes to the finest grid's."""
    with tempfile.TemporaryDirectory() as tmp:
        ppm = os.path.join(tmp, "image.ppm")
        Image.open(image).convert("RGB").save(ppm)
        row = json.loads(subprocess.run([tool, "radial-variants", ppm], check=True,
                                        capture_output=True, text=True).stdout)
    finest = _digest(row["grid_4096x4096"])
    timing = measure_timing(tool)
    pts = []
    for g, p, s in GRIDS:
        case = "radial" if g == "default" else f"radial_{g}"
        pts.append((p, s, timing["small"]["cases"][case]["min_ms"],
                    timing["large"]["cases"][case]["min_ms"],
                    float(similarity(_digest(row[g]), finest)[0])))

    def fig(c):
        f, axes = plt.subplots(1, 2, figsize=(10.4, 3.6))
        labels = [f"{p}×{s}" for p, s, *_ in pts]
        x = np.arange(len(pts))
        for key, color, name in (("small", c["accent"], "400×400"),
                                 ("large", c["accent2"], "20 Mpx")):
            col = 2 if key == "small" else 3
            axes[0].plot(x, [r[col] for r in pts], color=color, linewidth=2, marker="o",
                         markersize=4.5, label=name)
        axes[0].set_yscale("log")
        axes[0].set_ylabel("time, ms (log scale)", color=c["muted"], fontsize=9)
        axes[0].legend(frameon=False, labelcolor=c["ink"], fontsize=9)
        axes[1].plot(x[:-1], [1 - r[4] for r in pts[:-1]], color=c["accent"], linewidth=2,
                     marker="o", markersize=4.5)
        axes[1].set_yscale("log")
        axes[1].set_ylabel("1 − peak correlation with 4096×4096\n(log scale)",
                           color=c["muted"], fontsize=9)
        for ax in axes:
            ax.set_xticks(x, labels, rotation=30)
            ax.set_xlabel("lines × points per line", color=c["muted"], fontsize=9)
            style_axes(ax, c)
            ax.axvline(2, color=c["muted"], linewidth=1, linestyle=(0, (4, 3)))
        f.tight_layout()
        return f

    save(fig, out, "grid")
    lines = ["| Lines × points | 400×400 | 20 Mpx | Peak correlation with 4096 × 4096 |",
             "|---|---|---|---|"]
    for p, s, t_small, t_large, sim in pts:
        lines.append(f"| {p} × {s} | {ms(t_small)} ms | {ms(t_large)} ms | "
                     f"{FORMAT.format(sim)} |")
    with open(os.path.join(out, "grid-table.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
