#!/usr/bin/env python3
"""Checks docs/hash-revisions.tsv against PH_ALGORITHM_REVISIONS in src/hashes/algorithm.c and
writes the revision section of each algorithm page.

Usage: hash_revisions.py [--src FILE] [--history FILE] [--out DIR]

Without --out only the check runs (`make docs`). With --out, DIR/<algorithm>/revisions.md is
written for every algorithm (`make site`), included by docs/theory/<algorithm>.md.

The check fails when an algorithm's revision in the code has no row in the history, when the
history has a row above the code's revision or a gap, and when a row is malformed.
"""
import argparse
import os
import re
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")


def code_revisions(src):
    text = open(src, encoding="utf-8").read()
    names = re.search(r"PH_ALGORITHM_NAMES\[\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    names = re.findall(r'"(\w+)"', names)
    revs = re.search(r"PH_ALGORITHM_REVISIONS\[\]\s*=\s*\{(.*?)\};", text, re.S).group(1)
    revs = [int(n) for n in re.findall(r"^\s*(\d+),", revs, re.M)]
    if len(names) != len(revs):
        sys.exit(f"hash_revisions: {len(names)} names but {len(revs)} revisions in {src}")
    return dict(zip(names, revs))


def history(path):
    rows = {}
    for n, line in enumerate(open(path, encoding="utf-8"), 1):
        if line.startswith("#") or not line.strip():
            continue
        f = line.rstrip("\n").split("\t")
        if len(f) != 5 or not f[1].isdigit() or not re.fullmatch(r"\d+\.\d+\.\d+", f[2]):
            sys.exit(f"hash_revisions: {path}:{n}: expected algorithm, revision, version, "
                     "change, recompute")
        rows.setdefault(f[0], []).append((int(f[1]), f[2], f[3], f[4]))
    return rows


def check(codes, rows):
    errs = []
    for algo in rows.keys() - codes.keys():
        errs.append(f"the history names '{algo}', which is not an algorithm")
    for algo, rev in codes.items():
        have = [r[0] for r in rows.get(algo, [])]
        if have != list(range(1, len(have) + 1)):
            errs.append(f"{algo}: history revisions {have} are not 1, 2, 3, ... in order")
        if rev not in have:
            errs.append(f"{algo}: revision {rev} in the code has no row in the history")
        elif max(have) != rev:
            errs.append(f"{algo}: the history goes up to {max(have)}, the code says {rev}")
    return errs


def section(rev, rows):
    out = [f"The current revision is **{rev}**; "
           "[`ph_algorithm_revision()`](../api/algorithms.md#ph_algorithm_revision) returns it. "
           "A revision changes when the values the algorithm computes change. "
           "[Compatibility and versioning](../project/compatibility.md#hash-values) has what "
           "that means for stored hashes.", "",
           "| Revision | Library version | What changed | Recompute? |", "|---|---|---|---|"]
    out += [f"| {r} | {v} | {c} | {x} |" for r, v, c, x in reversed(rows)]
    return "\n".join(out) + "\n"


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--src", default=os.path.join(ROOT, "src/hashes/algorithm.c"))
    p.add_argument("--history", default=os.path.join(ROOT, "docs/hash-revisions.tsv"))
    p.add_argument("--out")
    a = p.parse_args()
    codes, rows = code_revisions(a.src), history(a.history)
    errs = check(codes, rows)
    if errs:
        print("hash_revisions: " + a.history, file=sys.stderr)
        for e in errs:
            print("  " + e, file=sys.stderr)
        sys.exit(1)
    if a.out:
        for algo, rev in codes.items():
            os.makedirs(os.path.join(a.out, algo), exist_ok=True)
            with open(os.path.join(a.out, algo, "revisions.md"), "w", encoding="utf-8") as f:
                f.write(section(rev, rows[algo]))


if __name__ == "__main__":
    main()
