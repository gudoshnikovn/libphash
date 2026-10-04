#!/usr/bin/env python3
"""Ratchet on explicit casts in libphash's own sources.

The rule (docs/development.md, "Conversions and casts"): a cast is right only when the code
knows something the type system does not, and its reason is stated next to it or evident
from the check above it. Compiler warnings see implicit conversions, not explicit ones, so
nothing else notices a cast added only to make a warning go away.

This script counts the explicit casts in every file under src/ -- the vendored stb_image
instantiations excluded, and `(void)x` too, which discards a value rather than converting
it -- and compares the counts with scripts/explicit_casts.txt. Any difference fails, in
either direction: a new cast, or a removed one, shows up in the diff of that file, so a
reviewer sees it and decides. After checking each new cast against the rule, run

    scripts/check_casts.py --update

and commit the list with the change.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASELINE = ROOT / "scripts" / "explicit_casts.txt"

_TYPE = (
    r"(?:const\s+)?(?:unsigned\s+|signed\s+)?"
    r"(?:size_t|ssize_t|ptrdiff_t|u?intptr_t|u?int(?:8|16|32|64)_t|int|unsigned|long\s+long|long"
    r"|short|char|double|float|void|png_\w+|JSAMP\w+|JDIMENSION|j_\w+|ph_\w+|WebP\w+|DWORD"
    r"|HANDLE|struct\s+\w+)"
    r"(?:\s+(?:long|int|char|const))*(?:\s*\*+(?:\s*const)?)*(?:\s*volatile)?"
)
# A parenthesised type directly followed by an operand. The look-behind keeps out calls and
# declarations: `f(int)`, `sizeof(int)` and `(*fn)(int)` all have a name or `)` before `(`.
_CAST = re.compile(r"(?<![\w\])])\(\s*(" + _TYPE + r")\s*\)\s*(?=[\w(&*!~\-\"'])")


def _strip(src):
    """Comments and string literals blanked, line structure kept."""
    src = re.sub(r"/\*.*?\*/", lambda m: re.sub(r"[^\n]", " ", m.group()), src, flags=re.S)
    src = re.sub(r"//[^\n]*", "", src)
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', src)


def _files():
    for path in sorted((ROOT / "src").rglob("*")):
        if path.suffix in (".c", ".h") and not path.name.startswith("stb_"):
            yield path


def count(path):
    n = 0
    for line in _strip(path.read_text(encoding="utf-8")).splitlines():
        for m in _CAST.finditer(line):
            if re.sub(r"\s+", " ", m.group(1)).strip() != "void":
                n += 1
    return n


def current():
    counts = {}
    for path in _files():
        n = count(path)
        if n:
            counts[path.relative_to(ROOT).as_posix()] = n
    return counts


def read_baseline():
    counts = {}
    for line in BASELINE.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            n, name = line.split(maxsplit=1)
            counts[name] = int(n)
    return counts


def write_baseline(counts):
    lines = ["# Explicit casts per file under src/ -- maintained by scripts/check_casts.py.",
             "# Edit only through `scripts/check_casts.py --update`, after reviewing each new",
             "# cast against docs/development.md, \"Conversions and casts\"."]
    lines += [f"{n} {name}" for name, n in sorted(counts.items())]
    BASELINE.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main(argv):
    if argv[1:] == ["--update"]:
        write_baseline(current())
        return 0
    if argv[1:]:
        print(__doc__)
        return 2
    now, then = current(), read_baseline()
    changed = sorted(f for f in set(now) | set(then) if now.get(f, 0) != then.get(f, 0))
    for f in changed:
        print(f"{f}: {then.get(f, 0)} explicit cast(s) in scripts/explicit_casts.txt, "
              f"{now.get(f, 0)} in the file")
    if changed:
        print("\nCheck each added cast against docs/development.md, \"Conversions and casts\";"
              "\nthen run scripts/check_casts.py --update and commit the list with the change.")
        return 1
    print(f"explicit casts: {sum(now.values())} in {len(now)} files, as recorded")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
