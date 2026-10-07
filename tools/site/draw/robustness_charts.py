"""The robustness chart and its table: one algorithm's metric against the original under
every edit of transforms.py, on the example photograph (measure/robustness.py measures
it). The helpers for the y axis and the reference line serve the corpus charts too."""
import os

import matplotlib.pyplot as plt

from draw.markdown import table, write_text
from draw.style import save, style_axes
from measure.metric import metric
from measure.transforms import transforms


def robust_limits(mod, values=()):
    """The y range of a robustness chart. For bits, half of them, where unrelated images
    land, raised if an edit goes past it; for another metric, its whole range, widened to
    any value outside it."""
    if mod.BITS:
        return 0, max([mod.BITS / 2] + [v * 1.05 for v in values])
    lo, hi = close_range(mod.METRIC_RANGE, values)
    return min([lo] + list(values)), max([hi] + list(values))


def close_range(limits, values):
    """A metric's range, with an open top (None, ColorMoments' L2 distance) closed just
    above the largest of `values`."""
    lo, hi = limits
    if hi is None:
        hi = max(values, default=1.0) * 1.05
    return lo, hi


def reference_line(ax, mod, c):
    """The module's REFERENCE, a value the metric is read against (Radial's threshold), as
    a dashed line; nothing for a module without one. reference_note() names it."""
    ref = getattr(mod, "REFERENCE", None)
    if ref:
        ax.axhline(ref[0], color=c["muted"], linewidth=1, linestyle=(0, (4, 3)))


def reference_note(mod):
    """The caption's sentence for reference_line(); empty without one."""
    ref = getattr(mod, "REFERENCE", None)
    return f" Dashed: {ref[1]}, {ref[0]:g}." if ref else ""


def robustness_figure(data, algo, out_dir, mod, caption):
    """Small multiples, one panel per transform: the algorithm's metric against the
    original (for a bit hash, the bits that differ)."""
    specs = transforms()
    label, _, convert, _, _ = metric(mod)
    limits = robust_limits(mod, [convert(v) for name, _, _ in specs for _, v in data[algo][name]
                                 if v is not None])

    def fig(c):
        figure, axes = plt.subplots(3, 3, figsize=(10, 7.2), sharey=True)
        for ax, (name, xlabel, _) in zip(axes.flat, specs):
            pts = [(s, convert(v)) for s, v in data[algo][name] if v is not None]
            xs = list(range(len(pts)))
            reference_line(ax, mod, c)
            ax.plot(xs, [p[1] for p in pts], color=c["accent"], linewidth=2, marker="o",
                    markersize=4.5, solid_capstyle="round")
            ax.set_xticks(xs, [f"{p[0]:g}" for p in pts])
            ax.set_ylim(*limits)
            ax.set_title(name, color=c["ink"], fontsize=10)
            ax.set_xlabel(xlabel, color=c["muted"], fontsize=9)
            style_axes(ax, c)
        for ax in axes[:, 0]:
            ax.set_ylabel(label, color=c["muted"], fontsize=9)
        figure.text(0.5, -0.03, caption + reference_note(mod), ha="center", color=c["muted"],
                    fontsize=9)
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out_dir, algo), "robustness")


def robustness_table(data, algo, out_dir, mod):
    """The numbers behind robustness_figure(), as the table under it."""
    label, _, convert, _, fmt = metric(mod)
    head = "Bits that differ" if mod.BITS else label[0].upper() + label[1:]
    rows = []
    for name, _, _ in transforms():
        for strength, value in data[algo][name]:
            cell = ("—" if value is None else f"{round(convert(value))}" if mod.BITS
                    else fmt(value))
            rows.append([name, f"{strength:g}", cell])
    write_text(os.path.join(out_dir, algo), "robustness-table.md",
               table(["Transform", "Strength", head], rows))
