#!/usr/bin/env bash
# Runs the commands read from stdin, one per line, up to <jobs> at a time, then prints
# each command's output in order, folded into a group in a GitHub Actions log. Exits 1
# if any command failed, after printing them all, so one failure does not hide another.
#
# Usage: printf '%s\n' "./build/test_a" "valgrind ./build/test_b" | run_parallel.sh [jobs]
#
# For test binaries that run outside ctest (the subset under Valgrind, which runs each
# process on one core): they share no files and do not depend on timing, so running them
# side by side checks exactly what running them one after another does.
set -euo pipefail

JOBS="${1:-4}"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

n=0
while IFS= read -r cmd; do
    [[ -n "$cmd" ]] || continue
    printf '%s\n' "$cmd" >"$WORK_DIR/$n.cmd"
    n=$((n + 1))
done
if [[ "$n" -eq 0 ]]; then
    echo "run_parallel: no commands on stdin" >&2
    exit 1
fi

# Each command's exit status goes to a file: xargs reports only that one failed.
# shellcheck disable=SC2016 # expanded by the inner bash, not this one
seq 0 $((n - 1)) | xargs -P "$JOBS" -I{} bash -c \
    'bash "$1/$2.cmd" >"$1/$2.log" 2>&1; echo $? >"$1/$2.rc"' _ "$WORK_DIR" {}

status=0
for i in $(seq 0 $((n - 1))); do
    cmd="$(cat "$WORK_DIR/$i.cmd")"
    echo "::group::$cmd"
    cat "$WORK_DIR/$i.log"
    echo "::endgroup::"
    rc="$(cat "$WORK_DIR/$i.rc")"
    if [[ "$rc" != "0" ]]; then
        echo "::error::exit status $rc: $cmd"
        status=1
    fi
done
exit "$status"
