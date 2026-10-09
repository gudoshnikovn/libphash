"""The figure and tables of docs/guide/batch.md: how many images a batch hashes per second
and how much memory it holds against its number of workers, and what one worker holds for
each format (`site_stages batch`, measure/timing.py, measure_batch()).
"""
import os

import matplotlib.pyplot as plt

from draw.markdown import table, write_text
from draw.style import save, style_axes
from measure.timing import FORMATS, measure_batch, measure_sizes

NAME = "batch"
FORMAT_TITLES = {"jpeg": "JPEG", "png": "PNG", "webp": "WebP"}
MB = 1e6


# --8<-- [start:estimate]
# What one worker holds, as the code allocates it: the decoded RGB pixels (3 bytes each)
# and their grayscale copy (1 byte), plus the encoded file, mapped or read while it is
# decoded.
def estimate(pixels, file_bytes):
    return 4 * pixels + file_bytes
# --8<-- [end:estimate]


def _rate(run):
    return run["items"] / run["seconds"]


def scaling_figure(data, pixels, out):
    """Two panels against the number of workers: images per second as a multiple of one
    worker's, for both sizes, beside the line where every worker adds one worker's worth;
    and the peak memory of the 20-megapixel batch, beside the estimate."""
    cpus = data["cpus"]
    big, small = data["scaling"]["20"], data["scaling"]["0.25"]
    workers = [r["workers"] for r in big]
    jpeg_bytes = data["memory"]["jpeg"]["bytes"]

    def fig(c):
        figure, (left, right) = plt.subplots(1, 2, figsize=(10, 3.9))
        for ax in (left, right):
            style_axes(ax, c)
            ax.axvline(cpus, color=c["muted"], linewidth=0.9, linestyle=(0, (1, 2)))
            ax.set_xlabel("workers", color=c["muted"], fontsize=9)
            ax.set_xlim(0, workers[-1] + 1)
            ax.set_xticks([1, cpus // 2, cpus, workers[-1]])
        left.plot([1, cpus], [1, cpus], color=c["muted"], linewidth=1, linestyle=(0, (4, 3)))
        for runs, color, label in ((big, c["accent"], "20 Mpx"), (small, c["accent2"], "¼ Mpx")):
            one = _rate(runs[0])
            ys = [_rate(r) / one for r in runs]
            left.plot([r["workers"] for r in runs], ys, color=color, linewidth=1.6, marker="o",
                      markersize=3.5)
            left.text(workers[-1], ys[-1] + (0.5 if color == c["accent"] else -0.5), label,
                      ha="right", va="bottom" if color == c["accent"] else "top",
                      color=color, fontsize=8.5)
        left.text(cpus - 0.7, cpus + 0.2, "each worker adds\none worker's rate",
                  color=c["muted"], fontsize=8.5, ha="right", va="bottom")
        left.set_ylim(0, cpus + 1.5)
        left.set_ylabel("images per second, × one worker", color=c["muted"], fontsize=9)
        left.set_title("Throughput", color=c["ink"], fontsize=10, loc="left")

        peak = [r["peak_after"] / MB for r in big]
        base = big[0]["peak_before"] / MB
        guess = [base + w * estimate(pixels[20], jpeg_bytes) / MB for w in workers]
        right.plot(workers, guess, color=c["muted"], linewidth=1, linestyle=(0, (4, 3)))
        right.plot(workers, peak, color=c["accent"], linewidth=1.6, marker="o", markersize=3.5)
        right.text(workers[-1] - 0.3, peak[-1] * 0.94, f"{peak[-1] / 1000:.2f} GB",
                   ha="right", va="top", color=c["ink"], fontsize=8.5,
                   bbox={"facecolor": c["surface"], "edgecolor": "none", "pad": 0.5})
        right.text(workers[-1] - 1.5, guess[-1] * 1.01, "4 bytes per pixel + the file,\n"
                   "per worker", color=c["muted"], fontsize=8.5, ha="right", va="bottom")
        right.set_ylim(0, max(peak + guess) * 1.12)
        right.set_ylabel("peak memory, MB", color=c["muted"], fontsize=9)
        right.set_title("Memory, 20 Mpx", color=c["ink"], fontsize=10, loc="left")
        for ax in (left, right):
            ax.text(cpus + 0.3, ax.get_ylim()[1] * 0.04, "one per CPU", color=c["muted"],
                    fontsize=8, ha="left", va="bottom")
        figure.tight_layout()
        return figure

    save(fig, os.path.join(out, NAME), "scaling")


def scaling_table(data, sizes, out):
    big, small = data["scaling"]["20"], data["scaling"]["0.25"]
    one_big, one_small = _rate(big[0]), _rate(small[0])
    head = ["Workers", f"{sizes[20]}: images per second", "× one worker",
            f"{sizes[0.25]}: images per second", "× one worker", f"{sizes[20]}: peak memory"]
    body = []
    for b, s in zip(big, small):
        body.append([str(b["workers"]), f"{_rate(b):.1f}", f"{_rate(b) / one_big:.2f}",
                     f"{_rate(s):.0f}", f"{_rate(s) / one_small:.2f}",
                     f"{b['peak_after'] / MB:.0f} MB"])
    write_text(os.path.join(out, NAME), "scaling-table.md",
               f"`ph_hash_files()` with the four 64-bit hashes; {big[0]['items']} items of the "
               f"{sizes[20]} JPEG and {small[0]['items']} of the {sizes[0.25]} one (at least "
               "two per worker), the shortest of the runs. The peak is the process's highest "
               f"resident memory; {big[0]['peak_before'] / MB:.1f} MB of it is there before "
               "the batch.\n\n" + table(head, body))


def scaling_sentence(data, out):
    """The sentence under the figure: the multiples at one worker per CPU and at twice that."""
    cpus = data["cpus"]

    def times(key, workers):
        runs = data["scaling"][key]
        run = next(r for r in runs if r["workers"] == workers)
        return f"{_rate(run) / _rate(runs[0]):.1f}"

    write_text(os.path.join(out, NAME), "scaling.md",
               f"Here {cpus} workers, one per CPU, hash {times('20', cpus)} times as many "
               f"20-megapixel images per second as one worker, and {2 * cpus} workers "
               f"{times('20', 2 * cpus)} times; on quarter-megapixel images, "
               f"{times('0.25', cpus)} and {times('0.25', 2 * cpus)} times.[^cost]")


def memory_table(data, pixels, sizes, out):
    """One row per format: the file, the estimate, and the measured share of one worker at
    one worker and at one per CPU."""
    cpus = data["cpus"]
    head = ["Format", "File", "4 bytes per pixel + the file", "Per worker, 1 worker",
            f"Per worker, {cpus} workers"]
    body = []
    for fmt in FORMATS:
        m = data["memory"][fmt]
        one, every = m["runs"]
        body.append([FORMAT_TITLES[fmt], f"{m['bytes'] / MB:.1f} MB",
                     f"{estimate(pixels[20], m['bytes']) / MB:.0f} MB",
                     f"{one['per_worker'] / MB:.0f} MB", f"{every['per_worker'] / MB:.0f} MB"])
    write_text(os.path.join(out, NAME), "memory.md",
               f"The {sizes[20]} photograph, two items per worker. A worker's share is the "
               "growth of the process's peak resident memory over the batch, divided by the "
               "workers.[^cost]\n\n" + table(head, body))


def figures(tool, image, out, timing):
    data = measure_batch(tool)
    rows = {row["mpx"]: row for row in measure_sizes(tool)["sizes"]}
    pixels = {mpx: r["width"] * r["height"] for mpx, r in rows.items()}
    sizes = {mpx: f"{r['width']}×{r['height']}" for mpx, r in rows.items()}
    scaling_figure(data, pixels, out)
    scaling_table(data, sizes, out)
    scaling_sentence(data, out)
    memory_table(data, pixels, sizes, out)
