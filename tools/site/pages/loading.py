"""The measured table of docs/guide/loading.md: what a load costs in color, as grayscale,
and in a context created for it, from the cases of `site_stages time` (timing.c,
time_load()). It measures nothing of its own.
"""
import os

from draw.markdown import table, write_text

NAME = "loading"

# --8<-- [start:loads]
# Each row: how the image is loaded, the case that only loads it, the case that loads it
# and computes pHash. "decode" and "scale_full_phash" load into a context kept across runs.
LOADS = (
    ("In color, the default", "decode", "scale_full_phash"),
    ("As grayscale, `ph_context_set_load_grayscale(ctx, 1)`", "gray_decode", "gray_phash"),
    ("In color, into a context created and freed for it", "fresh_decode", "fresh_phash"),
)
# --8<-- [end:loads]


def load_times(timing, out):
    def ms(entry, case):
        v = entry["cases"][case]["min_ms"]
        return f"{v:.2f} ms" if v < 1 else f"{v:.1f} ms" if v < 100 else f"{v:.0f} ms"

    small, large = timing["small"], timing["large"]
    rows = [[how] + [ms(e, c) for e in (small, large) for c in (load, phash)]
            for how, load, phash in LOADS]
    sizes = [f"{e['width']}×{e['height']}" for e in (small, large)]
    write_text(os.path.join(out, NAME), "load-times.md",
               "Two JPEG files; the minimum of the runs, on the machine named at the foot of "
               "the page:\n\n"
               + table(["Load", f"{sizes[0]}: load", f"{sizes[0]}: load + pHash",
                        f"{sizes[1]}: load", f"{sizes[1]}: load + pHash"], rows))


def figures(tool, image, out, timing):
    load_times(timing, out)
