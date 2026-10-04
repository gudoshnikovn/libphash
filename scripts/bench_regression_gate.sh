#!/usr/bin/env bash
# Compare a build's benchmark numbers against a baseline build (the PR's base
# commit, or the previous commit of a push, built in the same CI run to keep
# runner noise out of the comparison) and render a Markdown regression report.
#
# Usage:
#   bench_regression_gate.sh <pr_bench_hash_bin> <base_bench_hash_bin> \
#       [runs] [threshold_pct] [out_md]
#
# Environment:
#   AGG_THRESHOLD_PCT  aggregate threshold, default 5 (see below)
#   STRICT=1           exit 1 when the report flags a regression
#
# Requires jq. Runs each binary's `--json smoke` <runs> times (default 5),
# alternating between the two, and compares the *median across runs* of each
# metric's *min_ms* (fastest single iteration within a run).
#
# Why min_ms and not avg_ms: avg_ms is a mean over the whole iteration loop, so
# one scheduler preemption inside a run shifts it by tens of percent, and a
# binary compared against itself shows false regressions of 40% and more.
# min_ms is the best available estimate of "how fast this code can run" with OS
# noise removed, and the median of those across runs removes the remaining
# outliers. Rationale and the measured noise floor: docs/development.md.
#
# Two rules flag a regression:
#   - one metric slower than threshold_pct (default 10%): a lost fast path or an
#     extra pass over the image;
#   - the median change over all metrics both sides have slower than
#     AGG_THRESHOLD_PCT: the whole pipeline slowing down evenly, each metric
#     staying under the per-metric threshold. A median over a dozen metrics is
#     unmoved by one noisy one, so this threshold can sit lower.
# A metric present on one side only is reported (`new`, `gone`) and is not a
# regression. Runs whose JSON reports different `schema` numbers time different
# work under the same names; the report says so and compares nothing.
set -euo pipefail

PR_BIN="$1"
BASE_BIN="$2"
RUNS="${3:-5}"
# Both thresholds sit well above the measured noise floor (docs/development.md)
# and well below what this gate exists to catch: an accidental extra decode pass
# or a lost fast path costs far more than 10%.
THRESHOLD_PCT="${4:-10}"
OUT_MD="${5:-benchmark_regression.md}"
AGG_THRESHOLD_PCT="${AGG_THRESHOLD_PCT:-5}"
STRICT="${STRICT:-0}"

command -v jq >/dev/null || {
    echo "jq is required" >&2
    exit 1
}

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

# The two binaries take turns, one run each, so both sample the same stretches of
# time: a drift in the machine's speed while the gate runs (thermal throttling, a
# neighbour on a shared runner, a clock settling) lands on both sides instead of
# reading as a difference between them, which no number of runs would average out.
run_smoke() {
    local bin="$1" out="$2"
    if ! "$bin" --json smoke >>"$out"; then
        echo "bench_regression_gate: '$bin --json smoke' failed" >&2
        exit 1
    fi
}

collect_runs() {
    : >"$WORK_DIR/pr_runs.jsonl"
    : >"$WORK_DIR/base_runs.jsonl"
    for _ in $(seq 1 "$RUNS"); do
        run_smoke "$PR_BIN" "$WORK_DIR/pr_runs.jsonl"
        run_smoke "$BASE_BIN" "$WORK_DIR/base_runs.jsonl"
    done
}

# Reduces N `--json smoke` docs to {schema, metrics: {name: {ms, spread}}}:
#   ms     -- median across runs of the metric's min_ms;
#   spread -- (max - min) / median of those min_ms values, in percent: how far
#             apart the runs landed, i.e. how noisy this side's measurement was.
# Metrics are every top-level object with timings (the loading_* decodes) and
# every entry of the hashing array. A doc without `schema` is schema 1; one
# without min_ms falls back to avg_ms.
# shellcheck disable=SC2016 # jq variables, not shell ones
REDUCE_JQ='
def median: sort | .[(length - 1) / 2 | floor];
def metric: (.min_ms // .avg_ms);
def stats: (median) as $m |
  { ms: $m, spread: (if $m == 0 then 0 else ((max - min) / $m * 100) end) };
. as $runs |
{
  schema: ($runs[0].schema // 1),
  metrics: (
    [ $runs[] | to_entries[] | select(.value | type == "object" and has("iterations"))
      | { key: .key, ms: (.value | metric) } ]
    + [ $runs[] | (.hashing // [])[] | { key: .name, ms: metric } ]
    | group_by(.key)
    | map({ key: .[0].key, value: (map(.ms) | stats) })
    | from_entries
  )
}
'

collect_runs

jq -s "$REDUCE_JQ" "$WORK_DIR/pr_runs.jsonl" >"$WORK_DIR/pr.json"
jq -s "$REDUCE_JQ" "$WORK_DIR/base_runs.jsonl" >"$WORK_DIR/base.json"

PR_SCHEMA=$(jq '.schema' "$WORK_DIR/pr.json")
BASE_SCHEMA=$(jq '.schema' "$WORK_DIR/base.json")

if [[ "$PR_SCHEMA" != "$BASE_SCHEMA" ]]; then
    {
        echo "### Benchmark regression gate"
        echo
        echo "Not compared: the baseline measures benchmark schema $BASE_SCHEMA, this build"
        echo "schema $PR_SCHEMA. The two time different work under the same metric names"
        echo "(tests/src/bench_hash.c, PH_BENCH_SCHEMA), so a difference between them says"
        echo "nothing about the code. The next comparison of two schema-$PR_SCHEMA builds will."
    } >"$OUT_MD"
    cat "$OUT_MD"
    exit 0
fi

# One row per metric in either side: status is regression / ok for metrics both
# sides have, new / gone for the rest; pct is null for new and gone.
# shellcheck disable=SC2016 # jq variables, not shell ones
COMPARE_JQ='
($base.metrics) as $b | ($pr.metrics) as $p |
[ (($b | keys) + ($p | keys) | unique)[] as $k |
  if ($b | has($k)) and ($p | has($k)) then
    (if $b[$k].ms == 0 then 0 else (($p[$k].ms - $b[$k].ms) / $b[$k].ms * 100) end) as $pct |
    { metric: $k, base_ms: $b[$k].ms, pr_ms: $p[$k].ms, pct: $pct,
      spread: ([$b[$k].spread, $p[$k].spread] | max),
      status: (if $pct > $threshold then "regression" else "ok" end) }
  elif ($p | has($k)) then
    { metric: $k, base_ms: null, pr_ms: $p[$k].ms, pct: null, spread: $p[$k].spread,
      status: "new" }
  else
    { metric: $k, base_ms: $b[$k].ms, pr_ms: null, pct: null, spread: $b[$k].spread,
      status: "gone" }
  end
]
'
jq -n \
    --slurpfile base "$WORK_DIR/base.json" \
    --slurpfile pr "$WORK_DIR/pr.json" \
    --argjson threshold "$THRESHOLD_PCT" \
    '$base[0] as $base | $pr[0] as $pr | '"$COMPARE_JQ" \
    >"$WORK_DIR/comparison.json"

# Median change over the metrics both sides have; null when there are none.
AGG_PCT=$(jq '[.[] | select(.pct != null) | .pct] | sort
    | if length == 0 then null else .[(length - 1) / 2 | floor] end' "$WORK_DIR/comparison.json")
ANY_METRIC=$(jq '[.[] | select(.status == "regression")] | length > 0' "$WORK_DIR/comparison.json")
AGG_REGRESSION=$(jq -n --argjson a "$AGG_PCT" --argjson t "$AGG_THRESHOLD_PCT" \
    'if $a == null then false else $a > $t end')

fmt_pct() { jq -rn --argjson v "$1" 'if $v == null then "-" else "\($v * 100 | round / 100)%" end'; }

{
    echo "### Benchmark regression gate"
    echo
    echo "Median across $RUNS runs of each metric's fastest iteration (min_ms),"
    echo "this build vs. a baseline built in the same job. *spread* is how far apart"
    echo "the runs of the noisier side landed, (max - min) / median."
    echo
    echo "| metric | baseline (ms) | this build (ms) | change | spread | status |"
    echo "|---|---|---|---|---|---|"
    jq -r '.[] |
        def n: if . == null then "-" else (. * 1000000 | round / 1000000 | tostring) end;
        def p: if . == null then "-" else "\(. * 100 | round / 100)%" end;
        "| \(.metric) | \(.base_ms | n) | \(.pr_ms | n) | \(.pct | p) | \(.spread | p) | \(
            {regression: "⚠️ regression", ok: "ok", new: "new", gone: "gone"}[.status]) |"' \
        "$WORK_DIR/comparison.json"
    echo
    echo "Median change over the metrics both sides have: $(fmt_pct "$AGG_PCT")"
    echo "(threshold ${AGG_THRESHOLD_PCT}%)."
    echo
    if [[ "$ANY_METRIC" == "true" ]]; then
        echo "⚠️ One or more metrics regressed by more than ${THRESHOLD_PCT}%."
    fi
    if [[ "$AGG_REGRESSION" == "true" ]]; then
        echo "⚠️ The metrics as a whole regressed by more than ${AGG_THRESHOLD_PCT}%."
    fi
    if [[ "$ANY_METRIC" != "true" && "$AGG_REGRESSION" != "true" ]]; then
        echo "No metric regressed by more than ${THRESHOLD_PCT}%, and the median change"
        echo "is within ${AGG_THRESHOLD_PCT}%."
    fi
    if jq -e 'any(.[]; .status == "new" or .status == "gone")' "$WORK_DIR/comparison.json" >/dev/null; then
        echo
        echo "Metrics marked *new* or *gone* exist on one side only and are not compared."
    fi
} >"$OUT_MD"

cat "$OUT_MD"

if [[ "$STRICT" == "1" && ("$ANY_METRIC" == "true" || "$AGG_REGRESSION" == "true") ]]; then
    exit 1
fi
exit 0
