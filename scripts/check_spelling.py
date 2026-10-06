#!/usr/bin/env python3
"""Fails if the documentation or the public header uses a British spelling.

The project writes American English (CONTRIBUTING.md, "Commit and PR conventions").
Checked: the top-level Markdown files, every Markdown file under docs/ (the site's
pages), examples/README.md and include/libphash.h, the text the API reference is generated
from. Code in backticks, quoted text, URLs and
identifiers are not prose and are skipped: a quotation keeps its source's spelling, and
an identifier such as PH_ALPHA_BLEND_GREY is part of the API.
"""
import glob
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
FILES = ["README.md", "MIGRATION.md", "CONTRIBUTING.md", "SECURITY.md",
         "THIRD-PARTY-NOTICES.md", "CHANGELOG.md", "examples/README.md",
         "include/libphash.h"] + sorted(
             str(pathlib.Path(p).relative_to(ROOT)) for p in glob.glob(str(ROOT / "docs/**/*.md"), recursive=True)
             if not pathlib.Path(p).relative_to(ROOT).parts[1:2] == ("api",))

BRITISH = re.compile(
    r"\b(?:colour[a-z]*|behaviour[a-z]*|neighbour[a-z]*|catalogue|analysing|centres?|"
    r"licences?|normalis[a-z]*|quantis[a-z]*|initialis[a-z]*|optimis[a-z]*|equalis[a-z]*|"
    r"maximis[a-z]*|minimis[a-z]*|parameteris[a-z]*|serialis[a-z]*|standardis[a-z]*|"
    r"recognis[a-z]*|summaris[a-z]*|grey|greys|greyscale)\b",
    re.IGNORECASE)
NOT_PROSE = re.compile(r"`[^`]*`|\"[^\"\n]*\"|“[^”\n]*”|https?://\S+")


def main():
    found = 0
    for name in FILES:
        for line_no, line in enumerate((ROOT / name).read_text(encoding="utf-8").splitlines(), 1):
            prose = NOT_PROSE.sub(" ", line)
            for m in BRITISH.finditer(prose):
                if m.group(0).isupper():
                    continue  # an identifier or a macro name, not a word
                print(f"{name}:{line_no}: British spelling '{m.group(0)}'")
                found += 1
    if found:
        print(f"check_spelling: {found} British spelling(s); the project writes American English",
              file=sys.stderr)
        return 1
    print("check_spelling: American spelling throughout")
    return 0


if __name__ == "__main__":
    sys.exit(main())
