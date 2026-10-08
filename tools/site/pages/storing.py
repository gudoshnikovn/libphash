"""The tables of docs/guide/storing.md: what each algorithm's hash takes to store, what one
comparison of a linear search costs (`site_stages scan`, timed with the other times in
measure/timing.py), and how far a multi-index lookup has to reach for the thresholds
measured on the corpora (the 64-bit hashes' thresholds of docs/theory/comparing.md, from
the cached corpus measurements). It measures nothing else of its own.
"""
import os
from math import comb

from draw.markdown import table, write_text
from measure import corpus
from measure.metric import metric
from measure.separability import copies_and_different, separability
from measure.tool import run_lines
from pages import PAGES, TITLES

NAME = "storing"

# The scan cases of timing.c, in the order of the table: what is compared, and with what.
SCAN_ROWS = (
    ("hamming", "64-bit hash", "`ph_hamming_distance()`"),
    ("popcount", "64-bit hash", "`__builtin_popcountll(a ^ b)` in the search loop"),
    ("phash", "aHash, dHash, pHash, wHash digest", "`ph_hamming_distance_digest()`"),
    ("bmh", "BMH digest", "`ph_hamming_distance_digest()`"),
    ("mhash", "mHash digest", "`ph_hamming_distance_digest()`"),
    ("radial", "Radial digest", "`ph_radial_similarity()`"),
    ("color_hash", "ColorHash digest", "`ph_histogram_intersection()`"),
    ("color_moments", "ColorMoments digest", "`ph_l2_distance()`"),
)
# One query against a stored collection of this many, and every pair of a collection of
# this many.
QUERY_AGAINST = 1_000_000
ALL_PAIRS_OF = 100_000
# Multi-index hashing: a 64-bit hash cut into this many blocks of 64 / BLOCKS bits.
BLOCKS = 4


def _duration(ns):
    """A duration in nanoseconds, in the unit that reads best."""
    for unit, scale in (("min", 60e9), ("s", 1e9), ("ms", 1e6), ("µs", 1e3)):
        if ns >= scale:
            v = ns / scale
            return f"{v:.0f} {unit}" if v >= 100 else f"{v:.1f} {unit}" if v >= 10 else f"{v:.2f} {unit}"
    return f"{ns:.0f} ns" if ns >= 100 else f"{ns:.2f} ns"


def formats_table(tool, image, out):
    """One row per algorithm: its digest's kind and size, and the length of its text."""
    sizes = {r["algorithm"]: r for r in run_lines(tool, "sizes", image)}
    rows = []
    for mod in PAGES:
        s = sizes[mod.ALGO]
        as_int = ("`uint64_t`, or 16 hex digits"
                  if mod.ALGO in ("ahash", "dhash", "phash", "whash") else "—")
        rows.append([TITLES[mod.ALGO], f"`{s['kind']}`", str(s["size"]), str(s["text"]), as_int])
    write_text(os.path.join(out, NAME), "formats.md",
               table(["Algorithm", "Kind", "Bytes", "Characters of its text", "Also as"], rows)
               + "\n\nBMH's size is for its default 16×16 grid; every other size is fixed.")


def scan_table(timing, out):
    """The cost of one comparison in a linear search, and what it adds up to."""
    scan = timing["scan"]["cases"]
    pairs = ALL_PAIRS_OF * (ALL_PAIRS_OF - 1) // 2
    rows = []
    for case, what, how in SCAN_ROWS:
        if case not in scan:
            continue
        ns = scan[case]["min_ns"]
        rows.append([how, f"{what}, {scan[case]['size']} bytes", _duration(ns),
                     _duration(ns * QUERY_AGAINST), _duration(ns * pairs)])
    write_text(os.path.join(out, NAME), "scan.md",
               "The minimum of the runs, on the machine named at the foot of the page:\n\n"
               + table(["Comparison", "Stored value", "One comparison",
                        f"One query, {QUERY_AGAINST:,} stored",
                        f"Every pair of {ALL_PAIRS_OF:,}"], rows))


def index_table(tool, out):
    """For each 64-bit hash and corpus, the threshold that accepts 95 % of the copies, and
    what a multi-index lookup at that threshold looks up in each block."""
    datasets = [(k, corpus.measure_corpus(tool, k)) for k in corpus.CORPORA]
    datasets = sorted([(k, d) for k, d in datasets if d["n"]], key=lambda p: p[0] != "photos")
    bits = 64 // BLOCKS
    rows = []
    for mod in PAGES:
        if mod.BITS != 64:
            continue
        _, lower, convert, _, _ = metric(mod)
        row = [TITLES[mod.ALGO]]
        for _, data in datasets:
            copies, different = copies_and_different(data, mod.ALGO, convert)
            t = int(separability(copies, different, lower)[1])
            r = t // BLOCKS
            looked_up = sum(comb(bits, k) for k in range(r + 1))
            row += [f"≤ {t} bits", f"{looked_up:,} values, within {r} bit{'s' * (r != 1)}"]
        rows.append(row)
    head = ["Hash"]
    for k, _ in datasets:
        name = corpus.SHORT[k].capitalize()
        head += [f"{name}: threshold", "looked up per block"]
    write_text(os.path.join(out, NAME), "index.md", table(head, rows))


def figures(tool, image, out, timing):
    formats_table(tool, image, out)
    scan_table(timing, out)
    index_table(tool, out)
