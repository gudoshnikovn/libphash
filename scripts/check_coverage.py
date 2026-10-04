#!/usr/bin/env python3
"""Coverage of libphash's own sources against the thresholds in scripts/coverage_thresholds.txt.

Reads an lcov tracefile captured with branch coverage (scripts/coverage_cmake.sh, or the
Makefile's `coverage` target) and reports line and branch coverage per area. Without
--report it fails when an area is below its threshold. The thresholds hold for the
native-decoder build that CI's coverage-cmake job measures; docs/development.md
("Coverage standard") says what is measured, what is excluded and why.

    scripts/check_coverage.py docs/coverage/cmake/native.info          # gate
    scripts/check_coverage.py --report docs/coverage/coverage.info     # table only
    scripts/check_coverage.py --check-docs     # the table in docs/development.md matches
"""

import re

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
THRESHOLDS = ROOT / "scripts" / "coverage_thresholds.txt"
DOCS = ROOT / "docs" / "development.md"


def read_thresholds():
    rows = []
    for line in THRESHOLDS.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            area, lines_pct, branches_pct = line.split()
            rows.append((area, float(lines_pct), float(branches_pct)))
    return rows


def read_trace(path):
    files = {}
    current = None
    for line in pathlib.Path(path).read_text(encoding="utf-8").splitlines():
        if line.startswith("SF:"):
            p = pathlib.Path(line[3:])
            try:
                current = p.resolve().relative_to(ROOT).as_posix()
            except ValueError:
                current = p.as_posix()
            files.setdefault(current, [0, 0, 0, 0])
        elif current is not None:
            for i, key in enumerate(("LF:", "LH:", "BRF:", "BRH:")):
                if line.startswith(key):
                    files[current][i] += int(line[len(key):])
    return files


def matches(area, path):
    """An area is a file path, a directory ending in '/', or 'total' for all of src/."""
    if area == "total":
        return path.startswith("src/")
    return path.startswith(area) if area.endswith("/") else path == area


def pct(hit, found):
    return 100.0 * hit / found if found else 100.0


def check_docs():
    """The "Coverage standard" table must state exactly the thresholds that are checked."""
    rows = []
    for line in DOCS.read_text(encoding="utf-8").splitlines():
        m = re.fullmatch(r"\| (?:`([^`]+)`|all of `src/`) \| (\d+)% \| (\d+)% \|", line.strip())
        if m:
            rows.append((m.group(1) or "total", float(m.group(2)), float(m.group(3))))
    want = read_thresholds()
    if rows != want:
        print("docs/development.md, \"Coverage standard\", does not match "
              "scripts/coverage_thresholds.txt:")
        print("  docs:       ", rows)
        print("  thresholds: ", want)
        return 1
    print(f"coverage thresholds: docs/development.md matches ({len(want)} areas)")
    return 0


def main(argv):
    args = argv[1:]
    if args == ["--check-docs"]:
        return check_docs()
    report_only = "--report" in args
    args = [a for a in args if a != "--report"]
    if len(args) != 1:
        print(__doc__)
        return 2
    files = read_trace(args[0])
    failed = []
    print(f"{'area':22s} {'lines':>17s} {'branches':>17s}   thresholds")
    for area, min_lines, min_branches in read_thresholds():
        lf = lh = bf = bh = 0
        for path, (f_lf, f_lh, f_bf, f_bh) in files.items():
            if matches(area, path):
                lf, lh, bf, bh = lf + f_lf, lh + f_lh, bf + f_bf, bh + f_bh
        lines, branches = pct(lh, lf), pct(bh, bf)
        low = lines < min_lines or branches < min_branches
        if low:
            failed.append(area)
        print(f"{area:22s} {lines:6.1f}% {lh:4d}/{lf:<5d} {branches:6.1f}% {bh:4d}/{bf:<5d}"
              f"   {min_lines:.0f} / {min_branches:.0f}{'   BELOW' if low else ''}")
    if failed and not report_only:
        print(f"\nBelow threshold: {', '.join(failed)}. Cover the new code, or mark a line "
              "that cannot be reached with LCOV_EXCL_START/STOP and its reason.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
