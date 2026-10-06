#!/usr/bin/env python3
"""Fails if a declaration of the public header belongs to no topic of the API reference.

The reference is organized by topic (the @defgroup list at the top of
include/libphash.h): a declaration without an @ingroup would still be documented, but
only on the flat page of the file, where nobody browsing by topic finds it. Doxygen
reports no warning for that, so this script reads its XML output instead.

Usage: check_api_groups.py <doxygen xml dir>
"""
import pathlib
import sys
import xml.etree.ElementTree as ET


def main():
    xml_dir = pathlib.Path(sys.argv[1])
    header = ET.parse(xml_dir / "libphash_8h.xml").getroot().find("compounddef")
    grouped_structs = set()
    for group in xml_dir.glob("group__*.xml"):
        for inner in ET.parse(group).getroot().iter("innerclass"):
            grouped_structs.add(inner.get("refid"))

    problems = []
    # A member listed under the file itself is in no group: Doxygen moves a grouped
    # member's definition to its group's page.
    for member in header.iter("memberdef"):
        problems.append(f"{member.findtext('name')} ({member.get('kind')})")
    for inner in header.iter("innerclass"):
        if inner.get("refid") not in grouped_structs:
            problems.append(f"{inner.text} (struct)")

    if problems:
        print("check_api_groups: declarations in no topic of the API reference -- add "
              "@ingroup <topic> to their doc comment (topics: the @defgroup list at the top "
              "of include/libphash.h):", file=sys.stderr)
        for p in problems:
            print(f"  - {p}", file=sys.stderr)
        return 1
    print("check_api_groups: every public declaration is in a topic")
    return 0


if __name__ == "__main__":
    sys.exit(main())
