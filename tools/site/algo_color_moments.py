"""The figures of docs/theory/color-moments.md, from `site_stages color_moments`,
`site_stages color_moments-digests` and `site_stages measure`."""
import json
import os
import subprocess
import tempfile

import matplotlib.pyplot as plt
import numpy as np
from PIL import Image

import corpus
from common import hide_axes, run_stages, save, style_axes
from corpus_charts import COPY_STRENGTHS, copies_and_different, separability, share
from transforms import content_edits, transforms

ALGO = "color_moments"
BITS = None  # not a bit vector: the charts show the library's own distance
METRIC = "L2 distance, channel levels"
LOWER_IS_CLOSER = True
METRIC_RANGE = (0.0, None)  # no top: the axis ends just above the largest distance
FORMAT = "{:.1f}"
REFUSED = "a grayscale image"

SHORT = {"photos": "photographs", "synthetic": "synthetic images"}
CHANNELS = ("red", "green", "blue")
MOMENTS = ("mean", "standard deviation", "skewness")
MOMENT_SHORT = ("mean", "σ", "skew")

# The tonal copies and the recolorings set side by side in "Tone against color": (group,
# edit, strength) as the corpus measurement names them.
TONE_COLOR = [("robust", "Brightness", 1.15), ("robust", "Contrast", 1.15),
              ("robust", "Gamma", 1.2), ("edits", "Hue rotation", 30),
              ("edits", "Hue rotation", 90), ("edits", "Hue rotation", 180)]


# --8<-- [start:decode]
def moments(hexstr):
    """A ColorMoments digest as its nine numbers, [channel][moment]: each pair of bytes a
    signed 16-bit big-endian number, divided by 128 (PH_VECTOR16_SCALE)."""
    return (np.frombuffer(bytes.fromhex(hexstr), ">i2").astype(float) / 128.0).reshape(3, 3)


def l2(a, b):
    """ph_l2_distance() on two decoded digests."""
    return float(np.sqrt(((np.asarray(a) - np.asarray(b)) ** 2).sum()))
# --8<-- [end:decode]


def _measure(tool, ref, variants):
    """`site_stages measure`: every algorithm's score of each variant against `ref`."""
    out = subprocess.run([tool, "measure", ref, *variants], check=True, capture_output=True,
                         text=True).stdout
    return [json.loads(line) for line in out.splitlines()]


def _digests(tool, paths):
    out = subprocess.run([tool, "color_moments-digests", *paths], check=True,
                         capture_output=True, text=True).stdout
    return [moments(json.loads(line)["digest"]) for line in out.splitlines()]


def figures(tool, image, out_dir):
    st, px = run_stages(tool, ALGO, image, ("original.ppm",))
    out = os.path.join(out_dir, ALGO)
    hist = np.array(st["hist"], float)
    m = np.array(st["moments"]).reshape(3, 3)
    values = moments(st["digest"])
    raw = np.frombuffer(bytes.fromhex(st["digest"]), ">i2")
    original = px["original.ppm"]

    def pipeline(c):
        fig = plt.figure(figsize=(10.4, 4.6))
        a0 = fig.add_axes([0.0, 0.16, 0.34, 0.76])
        a0.imshow(original)
        hide_axes(a0)
        a0.set_title("Decoded image", color=c["ink"], fontsize=10)
        top = hist.max() * 1.08
        for ch in range(3):
            ax = fig.add_axes([0.42, 0.7 - ch * 0.27, 0.57, 0.2])
            mean, sd, skew = m[ch]
            ax.axvspan(mean - sd, mean + sd, color=c["accent"], alpha=0.22, linewidth=0)
            ax.bar(np.arange(256), hist[ch], width=1.0, color=c["muted"], linewidth=0)
            ax.axvline(mean, color=c["accent"], linewidth=1.8)
            ax.annotate("", xy=(mean + skew, top * 0.82), xytext=(mean, top * 0.82),
                        arrowprops={"arrowstyle": "-|>", "color": c["accent2"], "linewidth": 1.8})
            ax.set_xlim(-1, 256)
            ax.set_ylim(0, top)
            ax.set_yticks([])
            ax.set_xticks(range(0, 256, 32) if ch == 2 else [])
            style_axes(ax, c)
            ax.text(0.0, 1.04, f"{CHANNELS[ch]}: mean {mean:.1f}, σ {sd:.1f}, skewness "
                    f"{_minus(f'{skew:.1f}')}", transform=ax.transAxes, color=c["ink"], fontsize=9.5)
        ax.set_xlabel("channel level", color=c["muted"], fontsize=9)
        handles = [plt.Line2D([], [], color=c["accent"], linewidth=1.8),
                   plt.Rectangle((0, 0), 1, 1, color=c["accent"], alpha=0.22, linewidth=0),
                   plt.Line2D([], [], color=c["accent2"], linewidth=1.8, marker=">",
                              markersize=5)]
        fig.legend(handles, ["mean", "mean ± σ", "skewness, from the mean"], frameon=False,
                   labelcolor=c["ink"], fontsize=9, ncol=3, loc="lower center",
                   bbox_to_anchor=(0.7, -0.04))
        fig.text(0.17, 0.04, f"digest = {st['digest']}", ha="center", color=c["ink"],
                 family="monospace", fontsize=9)
        return fig

    def layout(c):
        fig, ax = plt.subplots(figsize=(10.4, 3.5))
        for ch in range(3):
            for k in range(3):
                i = ch * 3 + k
                x, y = k * 3.9, ch * 1.25
                byte_pair = st["digest"][4 * i:4 * i + 4]
                for b in range(2):
                    ax.add_patch(plt.Rectangle((x + b * 0.62, y), 0.58, 0.62,
                                               facecolor=c["off"], edgecolor=c["grid"],
                                               linewidth=0.8))
                    ax.text(x + b * 0.62 + 0.29, y + 0.31, byte_pair[2 * b:2 * b + 2],
                            ha="center", va="center", family="monospace", fontsize=10.5,
                            color=c["ink"])
                ax.text(x + 0.6, y - 0.08, f"bytes {2 * i}–{2 * i + 1}", ha="center",
                        va="bottom", fontsize=7.5, color=c["muted"])
                ax.text(x + 1.36, y + 0.16, f"{CHANNELS[ch]} {MOMENT_SHORT[k]}", ha="left",
                        va="center", fontsize=9, color=c["ink"])
                ax.text(x + 1.36, y + 0.47, _minus(f"{raw[i]:+d} ÷ 128 = {values[ch, k]:.3f}"),
                        ha="left", va="center", fontsize=8.5, color=c["muted"])
        ax.set_xlim(-0.2, 3 * 3.9 - 0.3)
        ax.set_ylim(3 * 1.25 - 0.4, -0.35)
        hide_axes(ax)
        ax.set_aspect("equal")
        return fig

    save(pipeline, out, "pipeline")
    save(layout, out, "bit-order")
    _write_stage_numbers(m, values, raw, out)
    with open(os.path.join(out, "load-grayscale.md"), "w") as f:
        f.write("With `ph_context_set_load_grayscale()` enabled, the example photograph is "
                "decoded to one channel, and "
                f"`ph_compute_color_moments_hash()` refuses it: \"{st['load_grayscale']}\".\n")

    base = Image.open(image).convert("RGB")
    _distance_figure(tool, base, values, out)
    data = {k: corpus.measure_corpus(tool, k) for k in ("photos", "synthetic")}
    present = [k for k in ("photos", "synthetic") if data[k]["n"]]
    thresholds = {k: separability(*copies_and_different(data[k], ALGO, lambda v: v), True)[1]
                  for k in present}
    _tone_color(data, present, thresholds, out)
    _skew_figure(tool, data, present, out)


def _minus(text):
    """A number as typeset: a minus sign, not a hyphen."""
    return text.replace("-", "−")


def _write_stage_numbers(m, values, raw, out):
    lines = ["| Channel | Moment | Computed | × 128, rounded | Bytes |", "|---|---|---|---|---|"]
    for ch in range(3):
        for k in range(3):
            i = ch * 3 + k
            lines.append(f"| {CHANNELS[ch]} | {MOMENTS[k]} | {m[ch, k]:.4f} | {raw[i]:+d} | "
                         f"`{int(raw[i]) & 0xFFFF:04x}` |")
    with open(os.path.join(out, "stage-numbers.md"), "w") as f:
        f.write("\n".join(lines) + "\n")


def _distance_figure(tool, base, values, out):
    """The example against itself with its hue turned 30°: the nine differences and the
    L2 distance they add up to; the distance computed here checked against the library's."""
    turned, _ = dict(content_edits()[1][2])[30](base)
    with tempfile.TemporaryDirectory() as tmp:
        ref, var = os.path.join(tmp, "a.ppm"), os.path.join(tmp, "b.ppm")
        base.save(ref)
        turned.save(var)
        other = _digests(tool, [var])[0]
        lib = _measure(tool, ref, [var])[0][ALGO]
    mine = l2(values, other)
    if abs(mine - lib) > 1e-6:
        raise SystemExit(f"render: color_moments: L2 {mine:.9f} here, {lib:.9f} from the library")
    diff = (other - values).ravel()

    def fig(c):
        f = plt.figure(figsize=(10.4, 3.9))
        for n, (im, t) in enumerate(((base, "the example"), (turned, "its hue turned 30°"))):
            ax = f.add_axes([0.0, 0.52 - n * 0.48, 0.17, 0.4])
            ax.imshow(np.asarray(im))
            hide_axes(ax)
            ax.set_title(t, color=c["ink"], fontsize=9)
        ax = f.add_axes([0.25, 0.2, 0.74, 0.68])
        x = np.arange(9) + np.repeat([0, 0.6, 1.2], 3)
        ax.bar(x, diff, width=0.7, color=[c["accent"] if d >= 0 else c["accent2"] for d in diff])
        ax.axhline(0, color=c["muted"], linewidth=0.8)
        for xi, d in zip(x, diff):
            ax.text(xi, d + (0.4 if d >= 0 else -0.4), _minus(f"{d:+.1f}"), ha="center",
                    va="bottom" if d >= 0 else "top", fontsize=8.5, color=c["ink"])
        ax.set_xticks(x, [s for _ in CHANNELS for s in MOMENT_SHORT], fontsize=8.5)
        for ch, name in enumerate(CHANNELS):
            ax.text(x[3 * ch + 1], -0.2, name, transform=ax.get_xaxis_transform(),
                    ha="center", va="top", color=c["ink"], fontsize=9)
        lo, hi = min(diff.min(), 0), max(diff.max(), 0)
        ax.set_ylim(lo - 0.15 * (hi - lo), hi + 0.15 * (hi - lo))
        ax.set_ylabel("turned − original, levels", color=c["muted"], fontsize=9)
        style_axes(ax, c)
        ax.set_title(f"L2 = √(sum of the nine squares) = {FORMAT.format(lib)}",
                     color=c["ink"], fontsize=10)
        return f

    save(fig, out, "distance")


def _values(data, group, name, strength):
    for s, values in data[group][ALGO][name]:
        if s == strength:
            return np.array([v for v in values if v is not None])
    raise KeyError((group, name, strength))


def _label(group, name, strength):
    return (f"hue {strength}°" if name == "Hue rotation"
            else f"{name.lower()} {strength:g}")


def _tone_color(data, present, thresholds, out):
    """Tonal copies against recolorings, per corpus, against the copies' threshold; and
    the share of recolorings each color hash takes for a copy."""
    names = [_label(*e) for e in TONE_COLOR]

    def fig(c):
        colors = {"photos": c["accent"], "synthetic": c["accent2"]}
        f, ax = plt.subplots(figsize=(10, 3.8))
        xs = np.arange(len(TONE_COLOR))
        for n, key in enumerate(present):
            x = xs + (n - (len(present) - 1) / 2) * 0.22
            q = np.array([np.percentile(_values(data[key], *e), [10, 25, 50, 75, 90])
                          for e in TONE_COLOR])
            ax.vlines(x, q[:, 0], q[:, 4], color=colors[key], linewidth=1.2, alpha=0.6)
            ax.vlines(x, q[:, 1], q[:, 3], color=colors[key], linewidth=6, alpha=0.35)
            ax.plot(x, q[:, 2], linestyle="none", marker="o", markersize=5,
                    color=colors[key], label=data[key]["label"])
            ax.axhline(thresholds[key], color=colors[key], linewidth=1.1,
                       linestyle=(0, (4, 3)))
        ax.axvline(2.5, color=c["grid"], linewidth=1)
        ax.text(1, 1.0, "copies: tonal edits", transform=ax.get_xaxis_transform(),
                ha="center", va="bottom", color=c["muted"], fontsize=9)
        ax.text(4, 1.0, "recolorings", transform=ax.get_xaxis_transform(), ha="center",
                va="bottom", color=c["muted"], fontsize=9)
        ax.set_xticks(xs, names)
        ax.set_xlim(-0.5, len(names) - 0.5)
        ax.set_ylim(0, None)
        ax.set_ylabel(METRIC, color=c["muted"], fontsize=9)
        ax.legend(frameon=False, labelcolor=c["ink"], fontsize=9, loc="upper left")
        style_axes(ax, c)
        f.text(0.5, -0.04, "Dot: median; thick bar: the middle half; thin bar: 10th to 90th "
               "percentile. Dashed: the threshold that accepts 95 % of the corpus's copies, "
               "in its color.", ha="center", color=c["muted"], fontsize=9)
        f.tight_layout()
        return f

    save(fig, out, "tone-color")
    hash_thresholds = {k: separability(*copies_and_different(data[k], "color_hash",
                                                             lambda v: v), False)[1]
                       for k in present}
    lines = ["| Corpus | Edit | ColorMoments: median (25th–75th) | Taken for a copy by "
             "ColorMoments | by ColorHash |", "|---|---|---|---|---|"]
    for key in present:
        for e, name in zip(TONE_COLOR, names):
            v = _values(data[key], *e)
            q = np.percentile(v, [25, 50, 75])
            h = np.array([x for s, vals in data[key][e[0]]["color_hash"][e[1]] if s == e[2]
                          for x in vals if x is not None])
            lines.append(f"| {SHORT[key]} | {name} | {FORMAT.format(q[1])} "
                         f"({FORMAT.format(q[0])}–{FORMAT.format(q[2])}) | "
                         f"{np.mean(v <= thresholds[key]):.0%} | "
                         f"{np.mean(h >= hash_thresholds[key]):.0%} |")
    with open(os.path.join(out, "tone-color-table.md"), "w") as f:
        f.write("\n".join(lines) + "\n")


# --8<-- [start:skew]
def standardized(v):
    """Per channel, the third central moment over σ³ (the standardized skewness, which
    does not depend on the channel's spread), from the decoded skewness s = cbrt(m3);
    `v` is [...][channel][moment]."""
    with np.errstate(divide="ignore", invalid="ignore"):
        return (v[..., 2] / v[..., 1]) ** 3


def skew_measurement(tool):
    """Per corpus: (originals [image][channel][moment], copies [image][edit][channel]
    [moment]), from the digests the library computes for every original and its nine
    copies (corpus.measure_settings)."""
    measured = corpus.measure_settings(tool, "color_moments-digests")
    out = {}
    for key, d in measured.items():
        if d["n"]:
            v = np.array([[moments(row["digest"]) for row in img] for img in d["images"]])
            out[key] = (v[:, 0], v[:, 1:])
    return out
# --8<-- [end:skew]


def _skew_figure(tool, data, present, out):
    """How steep the cube root is near a symmetric channel, and what that does to copies:
    the change of each channel's skewness against how symmetric the channel was."""
    measured = skew_measurement(tool)
    edits = [name for name, _, _ in transforms()]
    # The distances recomputed from the digests are those of the corpus charts.
    for key in present:
        orig, cps = measured[key]
        copies = np.sqrt(((cps - orig[:, None]) ** 2).sum(axis=(2, 3))).ravel()
        lib, _ = copies_and_different(data[key], ALGO, lambda v: v)
        if not np.allclose(np.sort(copies), np.sort(lib), atol=1e-9):
            raise SystemExit(f"render: color_moments: {key}: the distances of the copies "
                             "recomputed from the digests differ from the library's")

    sigma = 60.0
    g = np.linspace(-0.3, 0.3, 601)

    def fig(c):
        f, (a0, a1) = plt.subplots(1, 2, figsize=(10.4, 3.9),
                                   gridspec_kw={"width_ratios": [1, 1.25]})
        a0.plot(g, sigma * np.cbrt(g), color=c["accent"], linewidth=2)
        a0.axhline(0, color=c["grid"], linewidth=0.8)
        a0.axvline(0, color=c["grid"], linewidth=0.8)
        for s in (-1, 1):
            y = sigma * np.cbrt(0.01 * s)
            a0.plot([0.01 * s], [y], marker="o", color=c["accent2"], markersize=5)
        a0.annotate(f"±0.01: ±{sigma * np.cbrt(0.01):.1f} levels", (0.01, sigma * np.cbrt(0.01)),
                    xytext=(10, -14), textcoords="offset points", color=c["ink"], fontsize=9,
                    bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 1})
        a0.set_xlabel("third central moment ÷ σ³", color=c["muted"], fontsize=9)
        a0.set_ylabel(f"skewness in the digest, levels (σ = {sigma:g})", color=c["muted"],
                      fontsize=9)
        a0.set_title("The cube root, for a channel of σ 60", color=c["ink"], fontsize=10)
        style_axes(a0, c)
        key = "photos" if "photos" in measured else present[0]
        orig, cps = measured[key]
        gamma = np.abs(np.repeat(standardized(orig)[:, None, :], len(edits), axis=1))
        ds = np.abs(cps[:, :, :, 2] - orig[:, None, :, 2])
        flip = np.sign(cps[:, :, :, 2]) != np.sign(orig[:, None, :, 2])
        tonal = np.isin(np.arange(len(edits)), [edits.index(n) for n in
                                                ("Brightness", "Contrast", "Gamma")])
        keep = np.broadcast_to(~tonal[None, :, None], ds.shape)
        for mask, color, lab in ((keep & ~flip, c["muted"], "same sign"),
                                 (keep & flip, c["accent2"], "sign changed")):
            a1.scatter(gamma[mask], np.maximum(ds[mask], 1 / 128), s=6, color=color,
                       alpha=0.6 if color == c["muted"] else 0.9, linewidths=0, label=lab)
        a1.set_xscale("log")
        a1.set_yscale("log")
        a1.set_xlabel("the channel's |third central moment ÷ σ³| in the original",
                      color=c["muted"], fontsize=9)
        a1.set_ylabel("change of its skewness, levels", color=c["muted"], fontsize=9)
        a1.set_title(f"Each channel of {len(orig)} {SHORT[key]}, under the six copies\n"
                     "that keep the tone", color=c["ink"], fontsize=10)
        a1.legend(frameon=False, labelcolor=c["ink"], fontsize=9, loc="upper right",
                  markerscale=2.5)
        style_axes(a1, c)
        f.tight_layout()
        return f

    save(fig, out, "skew")
    _write_skew_table(measured, edits, out)


def _write_skew_table(measured, edits, out):
    lines = ["| Corpus | Copy | Share of the squared distance from the skewness | Channels "
             "whose skewness changes sign |", "|---|---|---|---|"]
    for key in ("photos", "synthetic"):
        if key not in measured:
            continue
        orig, cps = measured[key]
        d2 = (cps - orig[:, None]) ** 2
        for e, name in enumerate(edits):
            sq = d2[:, e].sum(axis=1)  # [image][moment]
            flips = np.sign(cps[:, e, :, 2]) != np.sign(orig[:, :, 2])
            lines.append(f"| {SHORT[key]} | {name} {COPY_STRENGTHS[name]:g} | "
                         f"{sq[:, 2].sum() / sq.sum():.0%} | "
                         f"{share(float(flips.mean()), flips.size)} |")
    lines += ["", "*d′* of the copies against different images, from the same digests, with "
              "all nine numbers and without the three skewnesses:", "",
              "| Corpus | All nine | Without the skewness |", "|---|---|---|"]
    for key in ("photos", "synthetic"):
        if key not in measured:
            continue
        orig, cps = measured[key]
        iu = np.triu_indices(len(orig), 1)
        cells = []
        for keep in (slice(0, 3), slice(0, 2)):
            copies = np.sqrt(((cps - orig[:, None])[..., keep] ** 2).sum(axis=(2, 3))).ravel()
            pair = np.sqrt(((orig[:, None] - orig[None])[..., keep] ** 2).sum(axis=(2, 3)))[iu]
            cells.append(f"{separability(copies, pair, True)[0]:.2f}")
        lines.append(f"| {SHORT[key]} | " + " | ".join(cells) + " |")
    with open(os.path.join(out, "skew-table.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
