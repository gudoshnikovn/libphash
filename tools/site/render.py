#!/usr/bin/env python3
"""Draws the documentation site's figures from what tools/site/stages.c measures.

Every figure is drawn twice, for the light and the dark theme (`name.light.svg`,
`name.dark.svg`; a page shows them with `#only-light` / `#only-dark`), and every measured
chart also gets a Markdown table of the same numbers, which a page includes under the
figure so the values are readable without the picture.

One module per algorithm page draws its figures (`algo_<name>.py`); the robustness
measurement (measure.py) runs once, over the transforms of transforms.py, and serves
every page, on the example image and over both corpora (corpus.py), whose figures
corpus_charts.py draws.

Usage: tools/site/render.py --tool build/release/site_stages --image tests/data/photo.jpeg
                            --out docs/assets/generated [--algo ahash,...|all]

With SITE_PREVIEW=<dir> in the environment, every figure is also written to <dir> as a
PNG on its theme's background (common.save), to look at before the page is built.
"""
import argparse
import sys

import algo_ahash
import algo_bmh
import algo_dhash
import algo_mhash
import algo_phash
import algo_radial
import algo_whash
from corpus import measure_corpus
from corpus_charts import (corpus_robustness_figure, corpus_tables, edits_examples, edits_figure,
                           edits_table, separability_figure)
from measure import measure_robustness, robustness_figure, robustness_table
from timing import measure_timing, write_timing

ALGORITHMS = {m.ALGO: m for m in (algo_ahash, algo_dhash, algo_phash, algo_whash, algo_mhash,
                                     algo_bmh, algo_radial)}


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--tool", required=True)
    p.add_argument("--image", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--algo", default="all",
                   help=f"comma-separated, of: {', '.join(ALGORITHMS)}; or all (the default)")
    args = p.parse_args()

    names = list(ALGORITHMS) if args.algo == "all" else args.algo.split(",")
    unknown = [n for n in names if n not in ALGORITHMS]
    if unknown:
        p.error(f"unknown algorithm: {', '.join(unknown)}")

    for name in names:
        ALGORITHMS[name].figures(args.tool, args.image, args.out)
    data = measure_robustness(args.tool, args.image)
    caption = "One image, the example above; each transform applied alone to the original."
    for name in names:
        robustness_figure(data, name, args.out, ALGORITHMS[name], caption)
        robustness_table(data, name, args.out, ALGORITHMS[name])
    datasets = [(c, measure_corpus(args.tool, c)) for c in ("synthetic", "photos")]
    for name in names:
        corpus_robustness_figure(datasets, name, ALGORITHMS[name], args.out)
        separability_figure(datasets, name, ALGORITHMS[name], args.out)
        corpus_tables(datasets, name, ALGORITHMS[name], args.out)
        edits_figure(datasets, name, ALGORITHMS[name], args.out)
        edits_table(datasets, name, ALGORITHMS[name], args.out)
    edits_examples(args.image, args.out)
    write_timing(measure_timing(args.tool), {n: ALGORITHMS[n] for n in names}, args.out)
    print(f"render: figures in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
