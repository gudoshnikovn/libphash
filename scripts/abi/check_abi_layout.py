#!/usr/bin/env python3
"""Fails if the public header's binary layout differs from the latest release's.

Every struct and enum the latest release tag's include/libphash.h defines is compiled into
a probe twice, once against that header and once against the working tree's, with the same
compiler, and the probe prints:

  - sizeof of each struct, and offsetof and sizeof of each of its fields;
  - sizeof of each enum, and the value of each of its enumerators.

The two outputs must be equal, with two exceptions the compatibility policy allows
(docs/project/compatibility.md):

  - a struct with a `struct_size` field is extensible: fields appended at its end make it
    larger, so its size may grow;
  - a `reserved*` field may be shortened or taken by new fields in a minor release, as long
    as the struct's size stays the same, so its own offset and size are not compared.

What appears only in the working tree (a new struct, field or enumerator) is an addition
and is not compared. Function signatures and removed symbols are abidiff's part
(scripts/abi/check_abidiff.sh, Linux); this check runs wherever CMake and a C compiler do,
MSVC included. The tags have to be fetched (CI: fetch-depth 0).

Usage: check_abi_layout.py [tag]     (default: the latest release tag vX.Y.Z)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "docs"))
from check_symbol_versions import ROOT, latest_tag

HEADER = "include/libphash.h"
TYPEDEF = re.compile(r"typedef\s+(struct|enum)\s*\{(.*?)\}\s*(\w+)\s*;", re.S)


def definitions(header):
    """[(kind, name, [member names])] of the typedef'd structs and enums in a header."""
    text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", header, flags=re.S)
    out = []
    for kind, body, name in TYPEDEF.findall(text):
        if kind == "struct":
            members = [re.search(r"(\w+)\s*(\[[^\]]*\]\s*)*$", d.strip()).group(1)
                       for d in body.split(";") if d.strip()]
        else:
            members = [re.match(r"\s*(\w+)", e).group(1) for e in body.split(",") if e.strip()]
        out.append((kind, name, members))
    return out


def probe_source(defs):
    lines = ["#include <stddef.h>", "#include <stdio.h>", '#include "libphash.h"',
             "int main(void) {"]

    def emit(label, expr):
        lines.append(f'    printf("{label} %lld\\n", (long long)({expr}));')

    for kind, name, members in defs:
        emit(f"sizeof {name}", f"sizeof({name})")
        for m in members:
            if kind == "struct":
                emit(f"offsetof {name}.{m}", f"offsetof({name}, {m})")
                emit(f"sizeof {name}.{m}", f"sizeof((({name} *)0)->{m})")
            else:
                emit(f"value {m}", m)
    lines += ["    return 0;", "}", ""]
    return "\n".join(lines)


def run_probe(work, label, header, source):
    """Builds the probe against `header` with CMake's default C compiler and returns
    its output as {label: value}."""
    src = work / label
    (src / "inc").mkdir(parents=True)
    (src / "inc" / "libphash.h").write_text(header, encoding="utf-8")
    # libphash.h includes it for the version macros, which the layout does not use.
    (src / "inc" / "phash_version.h").write_text("", encoding="utf-8")
    (src / "probe.c").write_text(source, encoding="utf-8")
    (src / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.21)\nproject(probe C)\n"
        "add_executable(probe probe.c)\ntarget_include_directories(probe PRIVATE inc)\n",
        encoding="utf-8")
    build = src / "build"
    subprocess.run(["cmake", "-S", src, "-B", build], check=True, stdout=subprocess.DEVNULL)
    built = subprocess.run(["cmake", "--build", build, "--config", "Release"],
                           capture_output=True, text=True)
    if built.returncode:
        # A struct, field or enumerator of the release that the header lacks.
        sys.exit(f"{built.stdout}{built.stderr}\ncheck_abi_layout: the probe of the "
                 f"release's structs and enums does not compile against the {label} "
                 "header: a public name is missing (docs/project/compatibility.md)")
    exe = next(p for p in build.rglob("probe*") if p.is_file() and p.suffix in ("", ".exe"))
    out = subprocess.run([exe], check=True, capture_output=True, text=True).stdout
    return dict(line.rsplit(" ", 1) for line in out.splitlines())


def compare(defs, old, new):
    problems = []
    extensible = {name for kind, name, members in defs if "struct_size" in members}
    for label, value in old.items():
        what, symbol = label.split(" ", 1)
        name, _, field = symbol.partition(".")
        if field.startswith("reserved"):
            continue
        if label not in new:
            problems.append(f"{label}: {value} in the release, gone from the header")
        elif new[label] != value:
            grown = int(new[label]) > int(value)
            if what == "sizeof" and not field and name in extensible and grown:
                continue
            problems.append(f"{label}: {value} in the release, {new[label]} in the header")
    return problems


def main():
    tag = sys.argv[1] if len(sys.argv) > 1 else latest_tag()[0]
    released = subprocess.run(["git", "show", f"{tag}:{HEADER}"], cwd=ROOT, check=True,
                              capture_output=True, text=True).stdout
    current = (ROOT / HEADER).read_text(encoding="utf-8")
    defs = definitions(released)
    source = probe_source(defs)
    with tempfile.TemporaryDirectory() as tmp:
        work = pathlib.Path(tmp)
        old = run_probe(work, "release", released, source)
        new = run_probe(work, "current", current, source)
    problems = compare(defs, old, new)
    for p in problems:
        print(f"check_abi_layout: {p}", file=sys.stderr)
    if problems:
        print(f"check_abi_layout: the layout of {HEADER} breaks the ABI of {tag} "
              "(docs/project/compatibility.md)", file=sys.stderr)
        return 1
    print(f"check_abi_layout: {len(defs)} structs and enums, {len(old)} values, "
          f"as in {tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
