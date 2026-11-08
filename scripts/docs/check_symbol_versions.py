#!/usr/bin/env python3
"""Fails if docs/symbols-in-versions.txt does not match the public header.

docs/symbols-in-versions.txt lists every public symbol with the release that added it
and, when it applies, the release that deprecated and the release that removed it. Bindings
read it to tell what a given version of the library provides. Its symbols are the
functions, types, enumerators and macros of include/libphash.h, the fields of its
structs as `struct.field`, and the macros of include/phash_version.h.in.

A declaration added after 2.0.0 carries `@since X.Y.Z` in its doc comment, which the API
reference shows (scripts/docs/api_pages.py); a declaration without one is part of 2.0.0. What
happened to it later follows in its doc comment, oldest first: `@changed{X.Y.Z} ...` for
each change of its contract, `@deprecated{X.Y.Z} ...` for its deprecation (aliases of
docs/Doxyfile). Checked here:

  - every symbol of the header is in the file and every symbol of the file that is not
    removed is in the header;
  - the file's release for each symbol equals its @since, and its deprecated column the
    version of its @deprecated{X.Y.Z};
  - the history of a declaration is in version order, after the release that added it,
    with at most one deprecation;
  - a function is declared PH_DEPRECATED("...") exactly when it has @deprecated{X.Y.Z},
    the macro on its own line directly above its PH_API line;
  - each symbol the file dates to the latest release tag or earlier is in that tag's
    header, and each symbol dated later is not: a declaration added without @since
    would be dated 2.0.0 and is missing from the tag. The tags have to be fetched (CI:
    fetch-depth 0).

The macros of phash_version.h.in have no doc comments for Doxygen to read; their
@since is the file's column alone.

Usage: check_symbol_versions.py <doxygen xml dir>
"""
import pathlib
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parents[2]
SYMBOLS = ROOT / "docs" / "symbols-in-versions.txt"
VERSION_HEADER = "include/phash_version.h.in"
HEADERS = ("include/libphash.h", VERSION_HEADER)
FIRST = "2.0.0"  # the release of every declaration without @since
VERSION = re.compile(r"\d+\.\d+\.\d+")
# What the Doxyfile aliases @changed{X.Y.Z} and @deprecated{X.Y.Z} expand to.
CHANGED = re.compile(r"Changed in (\d+\.\d+\.\d+):")
DEPRECATED = re.compile(r"Deprecated in (\d+\.\d+\.\d+): ")


def key(version):
    return tuple(int(p) for p in version.split("."))


def text(elem):
    return " ".join("".join(elem.itertext()).split())


def descriptions(elem):
    """An element's own descriptions: a struct contains its fields, an enum its
    enumerators, and their doc comments are theirs."""
    return [d for part in ("briefdescription", "detaileddescription", "inbodydescription")
            for d in elem.findall(part)]


def since(elem):
    """The version of an element's `@since`, or None. Raises ValueError if malformed."""
    found = [text(s) for d in descriptions(elem) for s in d.iter("simplesect")
             if s.get("kind") == "since"]
    if not found:
        return None
    if len(found) > 1 or not VERSION.fullmatch(found[0]):
        raise ValueError(f"@since must be one version X.Y.Z, found {found}")
    if found[0] == FIRST:
        raise ValueError(f"@since {FIRST} is written as no @since at all")
    return found[0]


def history(elem):
    """[("changed" or "deprecated", version)] of an element, in the order written.
    Raises ValueError for a @par that is not @changed{X.Y.Z}, a @deprecated without a
    version, or another Doxygen list (@todo, @bug)."""
    out = []
    for d in descriptions(elem):
        for node in d.iter():
            if node.tag == "simplesect" and node.get("kind") == "par":
                title = text(node.find("title"))
                m = CHANGED.fullmatch(title)
                if not m:
                    raise ValueError(f"@par {title!r}: the one @par is @changed{{X.Y.Z}}")
                out.append(("changed", m.group(1)))
            elif node.tag == "xrefsect":
                if node.findtext("xreftitle") != "Deprecated":
                    raise ValueError(f"@{node.findtext('xreftitle').lower()} has no place "
                                     "in the public header")
                m = DEPRECATED.match(text(node.find("xrefdescription")))
                if not m:
                    raise ValueError("@deprecated without a version: write "
                                     "@deprecated{X.Y.Z} <what to use instead>")
                out.append(("deprecated", m.group(1)))
    return out


def deprecated(events):
    found = [v for kind, v in events if kind == "deprecated"]
    return found[0] if found else None


def header_symbols(xml_dir):
    """Symbol name -> (its @since or None for 2.0.0, its history: see history(), whether
    it is a function)."""
    xml_dir = pathlib.Path(xml_dir)
    out = {}
    for group in sorted(xml_dir.glob("group__*.xml")):
        for m in ET.parse(group).getroot().iter("memberdef"):
            out[m.findtext("name")] = m
            for v in m.findall("enumvalue"):
                out[v.findtext("name")] = v
    for path in sorted(xml_dir.glob("struct*.xml")):
        s = ET.parse(path).getroot().find("compounddef")
        name = s.findtext("compoundname")
        out[name] = s
        for f in s.iter("memberdef"):
            out[f"{name}.{f.findtext('name')}"] = f
    result = {}
    for name, elem in out.items():
        try:
            added, events = since(elem), history(elem)
        except ValueError as e:
            raise ValueError(f"{name}: {e}")
        order = [key(added or FIRST)] + [key(v) for _, v in events]
        if order != sorted(order) or len(set(order)) != len(order):
            raise ValueError(f"{name}: @since and @changed/@deprecated are not in "
                             "increasing version order")
        if sum(kind == "deprecated" for kind, _ in events) > 1:
            raise ValueError(f"{name}: deprecated twice")
        result[name] = (added, events, elem.get("kind") == "function")
    guard = "PH_PHASH_VERSION_H"
    text = (ROOT / VERSION_HEADER).read_text(encoding="utf-8")
    for name in re.findall(r"^#\s*define\s+(PH_\w+)", text, re.M):
        if name != guard:
            result[name] = (None, [], False)
    return result


def read_file():
    """Name -> (introduced, deprecated, removed); problems found in the file itself."""
    entries, problems, names = {}, [], []
    for n, line in enumerate(SYMBOLS.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.startswith("#"):
            continue
        name, *versions = line.split()
        where = f"{SYMBOLS.name}:{n}: {name}"
        if not 1 <= len(versions) <= 3:
            problems.append(f"{where}: needs 1 to 3 versions (introduced [deprecated "
                            "[removed]]), '-' for an empty deprecated")
            continue
        versions += [None] * (3 - len(versions))
        given = [v for v in versions if v and v != "-"]
        if versions[0] == "-" or versions[2] == "-" or \
                not all(VERSION.fullmatch(v) for v in given):
            problems.append(f"{where}: a version is not X.Y.Z")
            continue
        if [key(v) for v in given] != sorted(key(v) for v in given):
            problems.append(f"{where}: introduced <= deprecated <= removed is not true")
        if name in entries:
            problems.append(f"{where}: listed twice")
        entries[name] = tuple(None if v == "-" else v for v in versions)
        names.append(name)
    if names != sorted(names, key=str.lower):
        problems.append(f"{SYMBOLS.name}: names are not in alphabetical order "
                        "(ignoring case)")
    return entries, problems


def marked_deprecated():
    """The functions declared PH_DEPRECATED, and the problems with where the macro is."""
    text = (ROOT / HEADERS[0]).read_text(encoding="utf-8")
    marked = re.findall(r'^PH_DEPRECATED\("[^"\n]+"\)\n(?:PH_NODISCARD )?PH_API\b[^;(]*?\b'
                        r'(ph_\w+)\s*\(', text, re.M)
    code = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
    uses = re.findall(r"^[^#\n]*\bPH_DEPRECATED\(", code, re.M)
    problems = []
    if len(uses) != len(marked):
        problems.append("PH_DEPRECATED(\"...\") goes on its own line directly above a "
                        "function's PH_API line, with a message")
    return set(marked), problems


def latest_tag():
    """(tag, version) of the newest release tag vX.Y.Z, pre-releases excluded."""
    tags = subprocess.run(["git", "tag", "--list", "v*"], cwd=ROOT, check=True,
                          capture_output=True, text=True).stdout.split()
    released = [t for t in tags if VERSION.fullmatch(t[1:])]
    if not released:
        raise ValueError("no release tag vX.Y.Z in this clone: fetch the tags "
                         "(git fetch --tags; in CI, checkout with fetch-depth: 0)")
    tag = max(released, key=lambda t: key(t[1:]))
    return tag, tag[1:]


def tag_symbols(tag):
    """The identifiers of the tag's public headers outside comments, and its struct
    fields as `struct.field`."""
    text = ""
    for path in HEADERS:
        text += subprocess.run(["git", "show", f"{tag}:{path}"], cwd=ROOT, check=True,
                               capture_output=True, text=True).stdout
    code = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)
    out = set(re.findall(r"\w+", code))
    structs = r"typedef\s+struct\s*\w*\s*\{([^}]*)\}\s*(\w+)\s*;"
    for body, name in re.findall(structs, code):
        for decl in body.split(";"):
            # The field is the last identifier, or for a function pointer the one in (*x).
            m = re.search(r"\(\s*\*\s*(\w+)\s*\)", decl) or \
                re.search(r"(\w+)\s*(?:\[[^\]]*\])?\s*$", decl)
            if m:
                out.add(f"{name}.{m.group(1)}")
    return out


def main():
    if len(sys.argv) != 2:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    try:
        header = header_symbols(sys.argv[1])
        entries, problems = read_file()
        marked, misplaced = marked_deprecated()
        problems += misplaced
        tag, released = latest_tag()
    except ValueError as e:
        print(f"check_symbol_versions: {e}", file=sys.stderr)
        return 1
    in_tag_header = tag_symbols(tag)

    for name in sorted(set(header) - set(entries)):
        problems.append(f"{name}: in the header, not in {SYMBOLS.name}")
    for name, (introduced, deprecated_in, removed) in sorted(entries.items()):
        in_tag = name in in_tag_header
        if removed is None and name not in header:
            problems.append(f"{name}: in {SYMBOLS.name} but not in the header; a removed "
                            "symbol keeps its line with the version that removed it")
        if removed is not None and name in header:
            problems.append(f"{name}: removed in {removed} by {SYMBOLS.name}, but still "
                            "in the header")
        if name in header:
            added, events, function = header[name]
            if function and (name in marked) != bool(deprecated(events)):
                problems.append(f"{name}: " + ("PH_DEPRECATED without @deprecated{X.Y.Z}"
                                               if name in marked else
                                               "@deprecated without PH_DEPRECATED(\"...\")"))
            if (added or FIRST) != introduced:
                problems.append(f"{name}: @since {added or f'absent ({FIRST})'}, "
                                f"{SYMBOLS.name} says {introduced}")
            if deprecated(events) != deprecated_in:
                doc = f"@deprecated{{{deprecated(events)}}}" if deprecated(events) else \
                    "no @deprecated"
                problems.append(f"{name}: {doc}, {SYMBOLS.name} says deprecated in "
                                f"{deprecated_in or '-'}")
        present = key(introduced) <= key(released) and \
            (removed is None or key(removed) > key(released))
        if present and not in_tag:
            problems.append(f"{name}: dated {introduced}, but not in {tag}; a "
                            "declaration added after it needs @since and its own release")
        if in_tag and key(introduced) > key(released):
            problems.append(f"{name}: dated {introduced}, but already in {tag}")

    if problems:
        print(f"check_symbol_versions: {SYMBOLS.relative_to(ROOT)} does not match "
              "include/libphash.h:", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    print(f"check_symbol_versions: {len(entries)} symbols as listed; dates checked "
          f"against {tag}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
