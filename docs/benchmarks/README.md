# Benchmark records

Measurements behind decisions about the library: which decoders it vendors, which build
options it defaults to. One file per measurement, named `YYYY-MM-DD-<subject>.md`. A
record states the question, the answer, and the exact conditions: the library commit,
the commit or tag holding the benchmark code, the CI run, every machine with its CPU, OS,
compiler and library versions, the corpus and the method, then the full tables. A record
is not edited after it is written; a new measurement is a new file, and a decision that
rests on numbers links the record that holds them.

| Record | Question |
|---|---|
| [2026-10-04 — libjpeg-turbo, libpng and zlib-ng against stb_image](2026-10-04-decoders-vs-stb.md) | What each vendored decoder buys over the `stb_image` fallback in time, memory and size |
| [2026-10-04 — PNG decoding: libpng against spng](2026-10-04-png-libpng-vs-spng.md) | Whether spng is a faster PNG backend than libpng |

## Raw data

`data/<record>/<platform>.tsv` holds every run of a record's CI legs, one line per run
and no header: file, build, `min_ms` of the load to gray, `min_ms` of the load to RGB.
The tables are medians over these lines. Local runs keep only the tables.

## Reproducing

The benchmark code a record ran is kept under a `bench/<record>` tag, so the
measurement can be rerun as it was. The decoder comparison runs from the tree:

```bash
python3 -m pip install pillow numpy
python3 scripts/gen_bench_corpus.py tests/data/photo_large.jpeg /tmp/corpus
scripts/bench_decoders.sh /tmp/corpus /tmp/bench 5 /tmp/bench/result.md
```

It needs cmake, a C compiler, jq, the zlib headers, nasm on x86, and the vendored
submodules. It builds three configurations of `bench_hash`, checks with `nm` that each
links exactly the decoders it was configured with, and times `bench_hash --json load` on
every corpus file, rotating the order of the builds every round. A cell is the median
across rounds of each run's `min_ms` — the estimator of the CI benchmark gate, for the
reason given in [`development.md`](../development.md#4-benchmarks-testssrcbench_hashc).

Other platforms than the local machine are measured on CI runners, never under
emulation: emulated timings say nothing about the native ones.
