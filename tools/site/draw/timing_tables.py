"""The times the pages quote, as the files they include: each algorithm's Cost row and
Cost table, the footnote naming the machine, and the table of every case. The times
themselves are measured by measure/timing.py."""
import os

from draw.markdown import write_text


def ms(value):
    """A time as a page quotes it: two significant figures, at most two decimals."""
    if value < 1:
        return f"{value:.2f}"
    return f"{value:.1f}" if value < 10 else f"{value:.0f}"


def megapixels(entry):
    return round(entry["width"] * entry["height"] / 1e6)


def describe(data):
    """The machine and the build, in one sentence."""
    m, build = data["machine"], data["small"]
    decoders = dict(kv.split("=", 1) for kv in build["build_info"].split())
    return (f"{m['cpu']}, {m['cores']} cores, {m['os']}; compiled by "
            f"{build.get('compiler', 'an unnamed compiler')}, optimized, with "
            f"{decoders['jpeg']} decoding the JPEGs")


# The nine hashes at their default settings, by ph_algorithm_name(): the rows every page's
# table shows, beside decoding, so a page compares its hash with the others.
DEFAULTS = ("ahash", "dhash", "phash", "whash", "bmh", "mhash", "radial", "color_hash",
            "color_moments")


def own_cases(algo, cases):
    """A page's own rows: its hash and the variants of it that time_cases[] names after
    it (whash_full, mhash_size_*, radial_*)."""
    return [c for c in cases if c == algo or c.startswith(algo + "_")]


def write_timing(data, algorithms, out_dir):
    """The files the pages include: per algorithm the passport's Cost row and the table of
    its Cost section (decoding, every hash at its defaults, and the page's own variants,
    in bold), and the footnote; and the full table of every case."""
    small, large = data["small"], data["large"]
    size = f"{small['width']}×{small['height']}"
    mpx = f"{megapixels(large)} Mpx"
    for algo, mod in algorithms.items():
        s, l = small["cases"][algo]["min_ms"], large["cases"][algo]["min_ms"]
        text = f"{ms(s)} ms on a {size} image, {ms(l)} ms on {mpx}, after decoding"
        for case, label in getattr(mod, "COST_VARIANTS", ()):
            text += (f"; {ms(small['cases'][case]['min_ms'])} and "
                     f"{ms(large['cases'][case]['min_ms'])} ms {label}")
        write_text(os.path.join(out_dir, algo), "cost-row.md", f"| **Cost** | {text}[^cost] |")

    timing_dir = os.path.join(out_dir, "timing")
    write_text(timing_dir, "footnote.md",
               f"[^cost]: Measured when this site was built, on {describe(data)}: the "
               "minimum of at least five runs after a warm-up. What each time covers, and "
               "the code, are under “How this was measured” beside it.")

    def table(cases, bold=()):
        rows = ["| Case | " + f"{size} | {mpx} |", "|---|---|---|"]
        for case in cases:
            b = "**" if case in bold else ""
            rows.append(f"| {b}`{case}`{b} | {b}{ms(small['cases'][case]['min_ms'])} ms{b} | "
                        f"{b}{ms(large['cases'][case]['min_ms'])} ms{b} |")
        return "\n".join(rows) + "\n"

    head = f"Measured on {describe(data)}; the minimum of the runs."
    write_text(timing_dir, "table.md", f"{head}\n\n" + table(small["cases"]))
    for algo in algorithms:
        own = own_cases(algo, small["cases"])
        shown = ["decode"] + [c for c in DEFAULTS if c in small["cases"]]
        shown += [c for c in own if c not in shown]
        write_text(os.path.join(out_dir, algo), "timing-table.md",
                   f"{head} Decoding, every hash at its default settings, and in bold "
                   "this page's.\n\n" + table(shown, own))
