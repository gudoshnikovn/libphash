#!/usr/bin/env bash
# Compare a build's benchmark numbers against a baseline build (the PR's base
# commit, or the previous commit of a push, built in the same CI run to keep
# runner noise out of the comparison) and render a Markdown regression report.
#
# Usage:
#   bench_regression_gate.sh <pr_bench_hash_bin> <base_bench_hash_bin> \
#       [runs] [threshold_pct] [out_md]
#
# Requires jq. Runs each binary's `--json smoke` <runs> times (default 5) and
# compares the *median across runs* of each metric's *min_ms* (fastest single
# iteration within a run).
#
# Why min_ms and not avg_ms: avg_ms is a mean over the whole iteration loop, so
# one scheduler preemption inside a run shifts it by tens of percent: comparing
# a binary against *itself* on avg_ms, on an idle machine, produces false 40-45%
# "regressions" (three metrics in one of five gate runs). min_ms is the best available estimate of "how fast this
# code can run" with OS noise removed, and taking the median of those across
# runs removes the remaining outliers. Rationale and the measured noise floor:
# docs/development.md.
#
# Exit code is 0 unless STRICT=1: the gate is warning-only by default, because
# CI-runner noise exceeds the idle-machine floor (docs/development.md).
set -euo pipefail

PR_BIN="$1"
BASE_BIN="$2"
RUNS="${3:-5}"
# 10%: the measured noise floor of this harness on an idle machine is under 7%
# per metric with min_ms (above 45% with avg_ms), and CI runners are
# noisier than that. 10% leaves headroom over the noise while still catching
# the kind of regression this gate exists for -- an accidental extra decode
# pass or a lost fast path costs far more than 10%.
THRESHOLD_PCT="${4:-10}"
OUT_MD="${5:-benchmark_regression.md}"
STRICT="${STRICT:-0}"

command -v jq >/dev/null || {
    echo "jq is required" >&2
    exit 1
}

WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

collect_runs() {
    local bin="$1" out="$2"
    : >"$out"
    for _ in $(seq 1 "$RUNS"); do
        "$bin" --json smoke >>"$out"
    done
}

# Reduces N `--json smoke` docs (fixed schema: loading_grayscale/loading_rgb
# objects + a hashing array) to one object of medians, keyed by metric name.
# Falls back to avg_ms so the gate still runs against a baseline binary whose
# --json output has no min_ms (any 1.x release).
MEDIAN_JQ='
def median: sort | .[(length - 1) / 2 | floor];
def metric: (.min_ms // .avg_ms);
. as $runs |
($runs[0].hashing | map(.name)) as $names |
{
  "loading_grayscale": ($runs | map(.loading_grayscale | metric) | median),
  "loading_rgb": ($runs | map(.loading_rgb | metric) | median),
} + (
  reduce $names[] as $name ({};
    . + { ($name): ($runs | map(.hashing[] | select(.name == $name) | metric) | median) }
  )
)
'

collect_runs "$PR_BIN" "$WORK_DIR/pr_runs.jsonl"
collect_runs "$BASE_BIN" "$WORK_DIR/base_runs.jsonl"

jq -s "$MEDIAN_JQ" "$WORK_DIR/pr_runs.jsonl" >"$WORK_DIR/pr_median.json"
jq -s "$MEDIAN_JQ" "$WORK_DIR/base_runs.jsonl" >"$WORK_DIR/base_median.json"

COMPARE_JQ='
[
  ($base | keys[]) as $k |
  ($base[$k]) as $bv | ($pr[$k]) as $pv |
  (if $bv == 0 then 0 else (($pv - $bv) / $bv) * 100 end) as $pct |
  {
    metric: $k, base_ms: $bv, pr_ms: $pv, pct: $pct,
    flag: (if $pct > $threshold then "regression" else "ok" end)
  }
]
'
jq -n \
    --argjson base "$(cat "$WORK_DIR/base_median.json")" \
    --argjson pr "$(cat "$WORK_DIR/pr_median.json")" \
    --argjson threshold "$THRESHOLD_PCT" \
    "$COMPARE_JQ" \
    >"$WORK_DIR/comparison.json"

ANY_REGRESSION=$(jq '[.[] | select(.flag == "regression")] | length > 0' "$WORK_DIR/comparison.json")

{
    echo "### Benchmark regression gate"
    echo
    echo "Median across $RUNS runs of each metric's fastest iteration (min_ms),"
    echo "PR vs. baseline built in this same CI run."
    echo "Threshold: >${THRESHOLD_PCT}% slower flags a regression."
    echo
    echo "| metric | baseline (ms) | PR (ms) | change | status |"
    echo "|---|---|---|---|---|"
    jq -r '.[] | "| \(.metric) | \(.base_ms | tostring) | \(.pr_ms | tostring) | \(.pct | (.*100|round)/100)% | \(if .flag == "regression" then "⚠️ regression" else "ok" end) |"' \
        "$WORK_DIR/comparison.json"
    echo
    if [[ "$ANY_REGRESSION" == "true" ]]; then
        echo "⚠️ One or more metrics regressed by more than ${THRESHOLD_PCT}%."
    else
        echo "No metric regressed by more than ${THRESHOLD_PCT}%."
    fi
} >"$OUT_MD"

cat "$OUT_MD"

if [[ "$STRICT" == "1" && "$ANY_REGRESSION" == "true" ]]; then
    exit 1
fi
exit 0
