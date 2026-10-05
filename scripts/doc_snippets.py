#!/usr/bin/env python3
"""Writes every ```c block of README.md and MIGRATION.md out as a C file that compiles.

A block with its own main() is written as it is. Any other block is a fragment: it is
placed in a function body, inside its own braces, after declarations of the names the
fragments use without declaring (ctx, path, hash, ...), so that a fragment declaring one
of them itself simply shadows it. In a block that contrasts the two versions, the part
from "/* Before (1.x)" up to "/* After" is 1.x code and is dropped.

Usage: doc_snippets.py OUT_DIR. Prints the files written, one per line, as
"<source>:<line> <file>".
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCES = ["README.md", "MIGRATION.md"]

PRELUDE = """\
#include <libphash.h>
#include <stdint.h>
#include <stdio.h>

void log_skip(const char *p);
void handle_bad_config(void);

void snippet(void);
void snippet(void) {
    ph_context_t *ctx = NULL, *ctx_a = NULL, *ctx_b = NULL;
    const char *path = "";
    uint64_t hash = 0;
    ph_digest_t other = {0};
    int dct_size = 32, reduction_size = 8;
    (void)ctx; (void)ctx_a; (void)ctx_b; (void)path; (void)hash; (void)other; (void)dct_size; (void)reduction_size;
    {
"""
EPILOGUE = """\
    }
}
"""


def blocks(text):
    lines = text.split("\n")
    i = 0
    while i < len(lines):
        if lines[i].strip() == "```c":
            indent = len(lines[i]) - len(lines[i].lstrip())  # a block inside a list item
            start = i + 1
            j = start
            while j < len(lines) and not lines[j].lstrip().startswith("```"):
                j += 1
            yield start, [line[indent:] for line in lines[start:j]]
            i = j
        i += 1


def drop_before(body):
    out, skipping = [], False
    for line in body:
        if line.lstrip().startswith("/* Before (1.x)"):
            skipping = True
            continue
        if line.lstrip().startswith("/* After"):
            skipping = False
            continue
        if not skipping:
            out.append(line)
    return out


def main():
    out_dir = pathlib.Path(sys.argv[1])
    out_dir.mkdir(parents=True, exist_ok=True)
    for name in SOURCES:
        text = (ROOT / name).read_text(encoding="utf-8")
        for line_no, body in blocks(text):
            stem = re.sub(r"\W", "_", name) + f"_{line_no}"
            if any(re.search(r"\bmain\s*\(", line) for line in body):
                code = "\n".join(body) + "\n"
            else:
                code = PRELUDE + "\n".join(drop_before(body)) + "\n" + EPILOGUE
            path = out_dir / f"{stem}.c"
            path.write_text(code, encoding="utf-8")
            print(f"{name}:{line_no} {path}")


if __name__ == "__main__":
    main()
