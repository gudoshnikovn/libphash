#!/usr/bin/env bash
# Task 16: compare a PR build's benchmark numbers against a baseline build
# (normally main, built in the same CI run to keep runner noise out of the
# comparison) and render a Markdown regression report.
#
# Usage:
#   bench_regression_gate.sh <pr_test_benchmark_bin> <base_test_benchmark_bin> \
#       [runs] [threshold_pct] [out_md]
#
# Requires jq. Runs each binary's `--json smoke` <runs> times (default 5) and
# compares the *median* avg_ms per metric -- median instead of mean/min
# because GitHub Actions runners are noisy and a single unlucky run
# shouldn't swing the result.
#
# Exit code is always 0: this is a warning-only gate for now (see
# tasks/16_benchmark_regression_gate.md) -- flip STRICT=1 to make it fail the
# job once the signal has been observed to be stable across a few real PRs.
set -euo pipefail

PR_BIN="$1"
BASE_BIN="$2"
RUNS="${3:-5}"
THRESHOLD_PCT="${4:-25}"
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
MEDIAN_JQ='
def median: sort | .[(length - 1) / 2 | floor];
. as $runs |
($runs[0].hashing | map(.name)) as $names |
{
  "loading_grayscale": ($runs | map(.loading_grayscale.avg_ms) | median),
  "loading_rgb": ($runs | map(.loading_rgb.avg_ms) | median),
} + (
  reduce $names[] as $name ({};
    . + { ($name): ($runs | map(.hashing[] | select(.name == $name) | .avg_ms) | median) }
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
  ((($pv - $bv) / $bv) * 100) as $pct |
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
    echo "Median of $RUNS runs each, PR vs. baseline (built in this same CI run)."
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
