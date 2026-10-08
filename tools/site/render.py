#!/usr/bin/env python3
"""Draws the documentation site's figures from what site_stages (tools/site/stages/)
measures.

Every figure is drawn twice, for the light and the dark theme (`name.light.svg`,
`name.dark.svg`; a page shows them with `#only-light` / `#only-dark`), and every measured
chart also gets a Markdown table of the same numbers, which a page includes under the
figure so the values are readable without the picture.

One module per algorithm page draws its own figures (pages/<algo>.py); a topic page
(pages/preparation.py) draws only its own. The figures every
page has are drawn here, once per page, from measurements made once for all of them
(measure/): the robustness of the example image and of both corpora under the edits of
measure/transforms.py, and the times. tools/site/README.md has the whole layout.

Usage: tools/site/render.py --tool build/release/site_stages --image tests/data/photo.jpeg
                            --out docs/assets/generated [--algo ahash,...,preparation|all]

With SITE_PREVIEW=<dir> in the environment, every figure is also written to <dir> as a
PNG on its theme's background (common.save), to look at before the page is built.
"""
import argparse
import sys

from draw.corpus_charts import (corpus_robustness_figure, corpus_tables, edits_examples,
                                edits_figure, edits_table, separability_figure)
from draw.robustness_charts import robustness_figure, robustness_table
from draw.timing_tables import write_timing
from measure.corpus import CORPORA, measure_corpus
from measure.robustness import measure_robustness
from measure.timing import measure_timing
from pages import PAGES, TOPICS

ALGORITHMS = {m.ALGO: m for m in PAGES}
TOPIC_PAGES = {m.NAME: m for m in TOPICS}


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--tool", required=True)
    p.add_argument("--image", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--algo", default="all",
                   help=f"comma-separated, of: {', '.join([*ALGORITHMS, *TOPIC_PAGES])}; "
                        "or all (the default)")
    args = p.parse_args()

    chosen = [*ALGORITHMS, *TOPIC_PAGES] if args.algo == "all" else args.algo.split(",")
    unknown = [n for n in chosen if n not in ALGORITHMS and n not in TOPIC_PAGES]
    if unknown:
        p.error(f"unknown page: {', '.join(unknown)}")
    names = [n for n in chosen if n in ALGORITHMS]
    topics = [n for n in chosen if n in TOPIC_PAGES]

    for name in names:
        ALGORITHMS[name].figures(args.tool, args.image, args.out)
    data = measure_robustness(args.tool, args.image)
    caption = "One image, the example above; each transform applied alone to the original."
    for name in names:
        robustness_figure(data, name, args.out, ALGORITHMS[name], caption)
        robustness_table(data, name, args.out, ALGORITHMS[name])
    datasets = [(c, measure_corpus(args.tool, c)) for c in CORPORA]
    for name in names:
        corpus_robustness_figure(datasets, name, ALGORITHMS[name], args.out)
        separability_figure(datasets, name, ALGORITHMS[name], args.out)
        corpus_tables(datasets, name, ALGORITHMS[name], args.out)
        edits_figure(datasets, name, ALGORITHMS[name], args.out)
        edits_table(datasets, name, ALGORITHMS[name], args.out)
    edits_examples(args.image, args.out)
    timing = measure_timing(args.tool)
    write_timing(timing, {n: ALGORITHMS[n] for n in names}, args.out)
    for name in topics:
        TOPIC_PAGES[name].figures(args.tool, args.image, args.out, timing)
    print(f"render: figures in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
