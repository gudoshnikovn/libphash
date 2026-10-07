#!/usr/bin/env python3
"""Writes the API reference as Markdown pages of the documentation site.

Doxygen (scripts/api_docs.sh) reads the doc comments of include/libphash.h and, next to
its HTML, writes them as XML. This script turns that XML into one page per topic (the
@defgroup list at the top of the header) plus an index, so the reference has the site's
look, its dark theme and its search, and a page elsewhere on the site links straight to
a function: `[ph_compute_ahash()](../api/hash64.md#ph_compute_ahash)`, an anchor the
strict build checks.

Every declaration is written exactly once, under an anchor equal to its name, in header
order. A member Doxygen lists under two topics (a function in a `@name` block of one
topic with an `@ingroup` of another) goes on the page its `@ingroup` names. The set of
functions on the pages is checked against the functions the header exports, and an XML
element this script has no rendering for fails it rather than being dropped: the
reference never silently loses text.

Usage: scripts/api_pages.py <doxygen xml dir> <output dir>
"""
import os
import pathlib
import re
import sys
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADER = ROOT / "include" / "libphash.h"

# Signatures longer than this put one parameter per line.
SIGNATURE_WIDTH = 88


class GenError(Exception):
    pass


def collapse(text):
    return re.sub(r"\s+", " ", text)


def escape(text):
    """Plain text from a doc comment, made literal for Markdown and its extensions."""
    text = text.replace("\\", "\\\\")
    for ch in "*_[]`<$":
        text = text.replace(ch, "\\" + ch)
    return text


def plain(elem):
    """The text of an element with every tag dropped (types, initializers)."""
    return collapse("".join(elem.itertext())).strip() if elem is not None else ""


def indent(text, prefix="    "):
    return "\n".join(prefix + line if line else line for line in text.split("\n"))


class Header:
    """What the XML does not keep: exported names, PH_NODISCARD, and each @ingroup."""

    DECL = re.compile(r"^(PH_NODISCARD\s+)?PH_API\b[^;]*?\b(ph_\w+)\s*\(", re.M | re.S)

    def __init__(self, path):
        self.text = path.read_text(encoding="utf-8")
        self.lines = self.text.split("\n")
        self.exported = []
        self.nodiscard = set()
        for m in self.DECL.finditer(self.text):
            self.exported.append(m.group(2))
            if m.group(1):
                self.nodiscard.add(m.group(2))
        self.topics = re.findall(r"@defgroup\s+(\w+)\s", self.text)

    def ingroup(self, line):
        """The @ingroup of the doc comment that ends just above 1-based `line`."""
        i = line - 2
        while i >= 0 and not self.lines[i].rstrip().endswith("*/"):
            i -= 1
        end = i
        while i >= 0 and "/**" not in self.lines[i]:
            i -= 1
        found = re.findall(r"@ingroup\s+(\w+)", "\n".join(self.lines[i:end + 1]))
        return found[0] if len(found) == 1 else None


class Reference:
    def __init__(self, xml_dir, header):
        self.xml_dir = pathlib.Path(xml_dir)
        self.header = header
        self.groups = {}  # topic -> compounddef
        for topic in header.topics:
            path = self.xml_dir / f"group__{topic}.xml"
            if not path.exists():
                raise GenError(f"{path}: missing; @defgroup {topic} has no Doxygen output")
            self.groups[topic] = ET.parse(path).getroot().find("compounddef")
        self.structs = {}
        self.links = {}  # refid -> (page, anchor or None)
        self.names = {}  # symbol name -> refid, for code spans Doxygen left unlinked
        self.home = {}  # memberdef id -> topic
        self.where = ""
        self._place()

    # -- where everything goes ---------------------------------------------------

    def _place(self):
        seen = {}
        for topic, group in self.groups.items():
            self.links[group.get("id")] = (f"{topic}.md", None)
            for m in group.iter("memberdef"):
                seen.setdefault(m.get("id"), (m, []))[1].append(topic)
            for inner in group.iter("innerclass"):
                refid = inner.get("refid")
                struct = ET.parse(self.xml_dir / f"{refid}.xml").getroot().find("compounddef")
                name = struct.findtext("compoundname")
                self.structs[refid] = (topic, struct)
                self._link(refid, topic, name)
                for f in struct.iter("memberdef"):
                    self._link(f.get("id"), topic, f"{name}.{f.findtext('name')}", name=False)
        for mid, (m, topics) in seen.items():
            name = m.findtext("name")
            if len(topics) == 1:
                topic = topics[0]
            else:
                line = int(m.find("location").get("line"))
                topic = self.header.ingroup(line)
                if topic not in topics:
                    raise GenError(f"{name}: listed under {', '.join(topics)}; its doc "
                                   "comment needs one @ingroup naming one of them")
            self.home[mid] = topic
            self._link(mid, topic, name)
            for v in m.findall("enumvalue"):
                self._link(v.get("id"), topic, v.findtext("name"))
        self.links["indexpage"] = ("index.md", None)

    def _link(self, refid, topic, anchor, name=True):
        self.links[refid] = (f"{topic}.md", anchor)
        if name:
            self.names[anchor] = refid

    def href(self, refid, page):
        if refid not in self.links:
            return None
        target, anchor = self.links[refid]
        if target == page:
            return f"#{anchor}" if anchor else "#"
        return target + (f"#{anchor}" if anchor else "")

    # -- doc comment markup ------------------------------------------------------

    def fail(self, elem):
        raise GenError(f"{self.where}: no rendering for <{elem.tag}> "
                       f"{dict(elem.attrib) or ''}".rstrip())

    def inline(self, elem, page, code=False):
        """Inline content of `elem` (its text, children and their tails) as Markdown."""
        out = [self._text(elem.text, code)]
        for child in elem:
            out.append(self._inline_child(child, page, code))
            out.append(self._text(child.tail, code))
        return "".join(out)

    def _text(self, text, code):
        if not text:
            return ""
        text = collapse(text)
        return f"\x00{text}\x01" if code else escape(text)

    def _inline_child(self, child, page, code):
        tag = child.tag
        if tag == "computeroutput":
            return self._code(self.inline(child, page, code=True))
        if tag == "ref":
            label = collapse("".join(child.itertext()))
            link = self.href(child.get("refid"), page)
            if code:
                return f"\x02{label}\x03{link or ''}\x04"
            if not link:
                return escape(label)
            # A declaration reads as code, as it does everywhere else on the site; a
            # topic reads as its title.
            if self.links[child.get("refid")][1]:
                return f"[`{label}`]({link})"
            return f"[{escape(label)}]({link})"
        if tag in ("bold", "emphasis"):
            mark = "**" if tag == "bold" else "*"
            return mark + self.inline(child, page, code).strip() + mark
        if tag == "ulink":
            return f"[{self.inline(child, page, code)}]({child.get('url')})"
        if tag == "ndash":
            return "–"
        if tag == "mdash":
            return "—"
        if tag == "anchor":
            return ""
        if tag == "linebreak":
            return "<br>"
        self.fail(child)

    def _code(self, marked):
        """A code span; `marked` holds code text and refs from inline(code=True)."""
        parts = re.findall(r"\x00(.*?)\x01|\x02(.*?)\x03(.*?)\x04", marked, re.S)
        out = []
        for text, label, link in parts:
            if label:
                out.append(f"[`{label}`]({link})" if link else f"`{label}`")
            elif text.strip():
                out.append(f"`{text}`")
        if len(parts) == 1 and not parts[0][1]:
            return self._autolink(parts[0][0])
        return "".join(out)

    def _autolink(self, text):
        """A code span naming a declaration, linked as Doxygen links a @ref."""
        bare = text.strip()
        name = bare[:-2] if bare.endswith("()") else bare
        refid = self.names.get(name)
        link = self.href(refid, self.page) if refid else None
        return f"[`{bare}`]({link})" if link else f"`{text}`"

    def blocks(self, elem, page, extra):
        """Block content of a description: a list of Markdown blocks.

        Parameter lists and @return go into `extra` ("params", "returns") so the member
        lays them out in fixed places; notes and warnings stay where they are written.
        """
        out = []
        for child in elem:
            if child.tag == "para":
                out.extend(self._para(child, page, extra))
            elif child.tag == "title":
                continue
            else:
                self.fail(child)
        return out

    def _para(self, para, page, extra):
        out = []
        run = [self._text(para.text, False)]

        def flush():
            text = "".join(run).strip()
            if text:
                out.append(text)
            run.clear()

        for child in para:
            block = self._block(child, page, extra)
            if block is None:
                run.append(self._inline_child(child, page, False))
            else:
                flush()
                out.extend(block)
            run.append(self._text(child.tail, False))
        flush()
        return out

    def _block(self, child, page, extra):
        """Markdown blocks for a block element inside a para, or None if it is inline."""
        tag = child.tag
        if tag in ("itemizedlist", "orderedlist"):
            items = []
            for n, item in enumerate(child, 1):
                if item.tag != "listitem":
                    self.fail(item)
                body = "\n\n".join(self.blocks(item, page, extra))
                mark = "- " if tag == "itemizedlist" else f"{n}. "
                items.append(mark + indent(body, " " * 4)[4:])
            return ["\n".join(items) if all("\n\n" not in i for i in items)
                    else "\n\n".join(items)]
        if tag == "simplesect":
            kind = child.get("kind")
            body = self.blocks(child, page, extra)
            if kind == "return":
                extra.setdefault("returns", []).extend(body)
                return []
            if kind in ("note", "warning"):
                return [f"!!! {kind}\n\n" + indent("\n\n".join(body))]
            self.fail(child)
        if tag == "parameterlist":
            if child.get("kind") != "param":
                self.fail(child)
            for item in child.findall("parameteritem"):
                names = item.find("parameternamelist").findall("parametername")
                desc = " ".join(self.blocks(item.find("parameterdescription"), page, extra))
                for name in names:
                    extra.setdefault("params", []).append(
                        (name.text, name.get("direction", ""), desc))
            return []
        if tag == "table":
            return [self._table(child, page, extra)]
        if tag == "programlisting":
            lines = []
            for line in child.findall("codeline"):
                lines.append(self._codeline(line))
            return ["```c\n" + "\n".join(lines) + "\n```"]
        return None

    def _codeline(self, line):
        out = []

        def walk(e):
            if e.tag == "sp":
                out.append(" ")
            elif e.tag in ("codeline", "highlight", "ref"):
                out.append(e.text or "")
                for c in e:
                    walk(c)
                    out.append(c.tail or "")
            else:
                self.fail(e)

        walk(line)
        return "".join(out)

    def _table(self, table, page, extra):
        rows = []
        for row in table.findall("row"):
            cells = []
            for entry in row.findall("entry"):
                text = " ".join(self.blocks(entry, page, extra))
                cells.append(text.replace("|", "\\|"))
            rows.append((row.find("entry").get("thead") == "yes", cells))
        if not rows or not rows[0][0]:
            raise GenError(f"{self.where}: a table without a header row")
        lines = ["| " + " | ".join(rows[0][1]) + " |",
                 "|" + "---|" * len(rows[0][1])]
        lines += ["| " + " | ".join(cells) + " |" for _, cells in rows[1:]]
        return "\n".join(lines)

    def description(self, elem, page, extra):
        """Brief and detailed description of a member or compound, as blocks."""
        out = []
        for part in ("briefdescription", "detaileddescription", "inbodydescription"):
            node = elem.find(part)
            if node is not None:
                out.extend(self.blocks(node, page, extra))
        return out

    def one_line(self, elem, page):
        """A description that must fit in a table cell."""
        extra = {}
        body = self.description(elem, page, extra)
        if extra or any("\n" in b for b in body):
            raise GenError(f"{self.where}: a description with lists, notes or parameters "
                           "cannot go in a table cell")
        return " ".join(body).replace("|", "\\|")

    # -- members -----------------------------------------------------------------

    MEMBER_TAGS = {"type", "definition", "argsstring", "name", "qualifiedname", "param",
                   "briefdescription", "detaileddescription", "inbodydescription",
                   "location", "initializer", "enumvalue"}

    def member(self, m, page):
        name = m.findtext("name")
        self.where = f"{page}: {name}"
        for child in m:
            if child.tag not in self.MEMBER_TAGS:
                self.fail(child)
        kind = m.get("kind")
        if kind == "function":
            return self.function(m, name, page)
        if kind == "enum":
            return self.enum(m, name, page)
        if kind == "typedef":
            return self.typedef(m, name, page)
        if kind == "define":
            return self.define(m, name, page)
        raise GenError(f"{self.where}: no rendering for a {kind}")

    def signature(self, m, name):
        ret = plain(m.find("type"))
        params = []
        for p in m.findall("param"):
            ptype = plain(p.find("type"))
            pname = (p.findtext("declname") or "") + (p.findtext("array") or "")
            sep = "" if ptype.endswith("*") or not pname else " "
            params.append(f"{ptype}{sep}{pname}")
        head = f"{ret}{'' if ret.endswith('*') else ' '}{name}("
        line = head + ", ".join(params) + ");"
        if len(line) <= SIGNATURE_WIDTH:
            return line
        return head + "\n" + ",\n".join("    " + p for p in params) + ");"

    def function(self, m, name, page):
        extra = {}
        body = self.description(m, page, extra)
        out = [f"### `{name}()` {{ #{name} }}",
               "```c\n" + self.signature(m, name) + "\n```"]
        out += body
        if "params" in extra:
            rows = extra["params"]
            declared = [p.findtext("declname") for p in m.findall("param")]
            if [r[0] for r in rows] != [d for d in declared if d]:
                raise GenError(f"{self.where}: documented parameters do not match the "
                               "declaration")
            directed = any(d for _, d, _ in rows)
            head = "| Parameter | Direction | Description |" if directed else \
                "| Parameter | Description |"
            lines = ["**Parameters**", "", head, "|---|---|---|" if directed else "|---|---|"]
            for pname, direction, desc in rows:
                cells = [f"`{pname}`"] + ([direction or "in"] if directed else []) + \
                    [desc.replace("|", "\\|")]
                lines.append("| " + " | ".join(cells) + " |")
            out.append("\n".join(lines))
        if "returns" in extra:
            first, *rest = extra["returns"]
            if re.match(r"(- |\d+\. )", first):
                out += ["**Returns**", first]
            else:
                out.append("**Returns** " + first)
            out += rest
        if name in self.header.nodiscard:
            out.append("Declared `PH_NODISCARD`: the compiler warns when the returned "
                       "value is ignored.")
        return out

    def enum(self, m, name, page):
        extra = {}
        out = [f"### `{name}` {{ #{name} }}"] + self.description(m, page, extra)
        self._no_extra(extra)
        lines = ["| Name | Value | Description |", "|---|---|---|"]
        for v in m.findall("enumvalue"):
            vname = v.findtext("name")
            value = plain(v.find("initializer")).lstrip("=").strip()
            self.where = f"{page}: {name}::{vname}"
            lines.append(f"| <span id=\"{vname}\"></span>`{vname}` | `{value}` | "
                         f"{self.one_line(v, page)} |")
        out.append("\n".join(lines))
        return out

    def typedef(self, m, name, page):
        definition = collapse(m.findtext("definition"))
        ptr = re.fullmatch(r"typedef (.+?)\(\*\) (\w+)(\(.*\))", definition)
        if ptr:
            definition = f"typedef {ptr.group(1).strip()} (*{ptr.group(2)}){ptr.group(3)}"
        extra = {}
        out = [f"### `{name}` {{ #{name} }}", f"```c\n{definition};\n```"]
        out += self.description(m, page, extra)
        self._no_extra(extra)
        return out

    def define(self, m, name, page):
        extra = {}
        value = plain(m.find("initializer"))
        out = [f"### `{name}` {{ #{name} }}", f"```c\n#define {name} {value}\n```"]
        out += self.description(m, page, extra)
        self._no_extra(extra)
        return out

    def struct(self, refid, page):
        _, s = self.structs[refid]
        name = s.findtext("compoundname")
        self.where = f"{page}: {name}"
        for child in s:
            if child.tag not in ("compoundname", "includes", "sectiondef", "briefdescription",
                                 "detaileddescription", "location", "listofallmembers"):
                self.fail(child)
        extra = {}
        out = [f"### `{name}` {{ #{name} }}"] + self.description(s, page, extra)
        self._no_extra(extra)
        lines = ["| Type | Field | Description |", "|---|---|---|"]
        for f in s.iter("memberdef"):
            fname = f.findtext("name")
            self.where = f"{page}: {name}.{fname}"
            ftype = plain(f.find("type"))
            lines.append(f"| `{ftype}` | <span id=\"{name}.{fname}\"></span>"
                         f"`{fname}{f.findtext('argsstring') or ''}` | "
                         f"{self.one_line(f, page)} |")
        out.append("\n".join(lines))
        return out

    def _no_extra(self, extra):
        if extra:
            raise GenError(f"{self.where}: @param or @return on a declaration that is "
                           "not a function")

    # -- pages -------------------------------------------------------------------

    SECTIONS = [("Types", ("typedef", "enum", "struct")), ("Constants", ("define",)),
                ("Functions", ("function",))]

    def topic_page(self, topic):
        group = self.groups[topic]
        page = f"{topic}.md"
        self.page = page
        self.where = page
        for child in group:
            if child.tag not in ("compoundname", "title", "sectiondef", "innerclass",
                                 "briefdescription", "detaileddescription"):
                self.fail(child)
        extra = {}
        out = [f"# {group.findtext('title')}"] + self.description(group, page, extra)
        self._no_extra(extra)

        # Members by kind, each in header order; a @name block is a section of its own.
        by_kind = {}
        named = []
        for sect in group.findall("sectiondef"):
            members = [m for m in sect.findall("memberdef") if self.home[m.get("id")] == topic]
            if sect.get("kind") == "user-defined":
                # A @name block is a section of functions; a type or a constant declared
                # inside it still goes with the other types and constants.
                named.append((sect, [m for m in members if m.get("kind") == "function"]))
                members = [m for m in members if m.get("kind") != "function"]
            for m in members:
                by_kind.setdefault(m.get("kind"), []).append((self._line(m), "member", m))
        for inner in group.findall("innerclass"):
            _, s = self.structs[inner.get("refid")]
            by_kind.setdefault("struct", []).append((self._line(s), "struct", inner.get("refid")))

        # Types and constants first; then the functions, where each @name block is a
        # section of its own, and the sections follow the header's order.
        sections = []
        for title, kinds in self.SECTIONS:
            items = sorted((i for k in kinds for i in by_kind.pop(k, [])), key=lambda i: i[0])
            if items:
                sections.append((title, items))
        functions = sections.pop() if sections and sections[-1][0] == "Functions" else None
        chunks = [(min(self._line(m) for m in members), "named", (sect, members))
                  for sect, members in named]
        if functions:
            chunks.append((functions[1][0][0], "plain", functions))
        for title, items in sections:
            out += self.section(title, items, page)
        for _, what, obj in sorted(chunks, key=lambda c: c[0]):
            out += self.named_section(*obj, page) if what == "named" else \
                self.section(*obj, page)
        if by_kind:
            raise GenError(f"{page}: no section for {', '.join(by_kind)}")
        return out

    def section(self, title, items, page):
        out = [f"## {title}"]
        for _, what, obj in items:
            out += self.struct(obj, page) if what == "struct" else self.member(obj, page)
        return out

    def named_section(self, sect, members, page):
        header = sect.findtext("header")
        slug = re.sub(r"[^a-z0-9]+", "-", header.lower()).strip("-")
        out = [f"## {escape(header)} {{ #{slug} }}"]
        first = sect.find("memberdef").get("id")
        owner = re.match(r"group__(\w+?)_1", first).group(1)
        self.where = f"{page}: @name {header}"
        if owner == page[:-3]:
            extra = {}
            desc = sect.find("description")
            if desc is not None:
                out += self.blocks(desc, page, extra)
            self._no_extra(extra)
        else:
            out.append(f"These functions follow the rules given under "
                       f"[{escape(header)}]({owner}.md#{slug}).")
        for m in sorted(members, key=self._line):
            out += self.member(m, page)
        return out

    @staticmethod
    def _line(elem):
        return int(elem.find("location").get("line"))

    def index_page(self):
        root = ET.parse(self.xml_dir / "indexpage.xml").getroot().find("compounddef")
        self.page = "index.md"
        self.where = "index.md"
        extra = {}
        out = [f"# {root.findtext('title')}"] + self.description(root, "index.md", extra)
        self._no_extra(extra)
        out.append("## Topics")
        lines = []
        for topic, group in self.groups.items():
            brief = " ".join(self.blocks(group.find("briefdescription"), "index.md", extra))
            lines.append(f"- [{group.findtext('title')}]({topic}.md) — {brief}")
        out.append("\n".join(lines))
        return out

    def check(self, pages):
        """Every exported function exactly once, and nothing on a page it is not."""
        text = "\n".join("\n\n".join(p) for p in pages.values())
        shown = re.findall(r"^### `(\w+)\(\)` \{ #\1 \}$", text, re.M)
        problems = []
        for name in sorted(set(shown) | set(self.header.exported)):
            n = shown.count(name)
            if name not in self.header.exported:
                problems.append(f"{name}: on the pages but not exported by the header")
            elif n != 1:
                problems.append(f"{name}: on the pages {n} times")
        if len(self.header.exported) != len(set(self.header.exported)):
            problems.append("the header declares a function twice")
        if problems:
            raise GenError("API pages do not match the header's exports:\n  - "
                           + "\n  - ".join(problems))
        return len(shown)


GENERATED = ("<!-- Generated by scripts/api_pages.py from the doc comments of "
             "include/libphash.h. Do not edit. -->")


def main():
    if len(sys.argv) != 3:
        print(__doc__.strip().splitlines()[-1], file=sys.stderr)
        return 2
    xml_dir, out_dir = sys.argv[1], pathlib.Path(sys.argv[2])
    try:
        ref = Reference(xml_dir, Header(HEADER))
        pages = {f"{t}.md": ref.topic_page(t) for t in ref.groups}
        pages["index.md"] = ref.index_page()
        functions = ref.check(pages)
    except GenError as e:
        print(f"api_pages: {e}", file=sys.stderr)
        return 1
    out_dir.mkdir(parents=True, exist_ok=True)
    for name in os.listdir(out_dir):
        if name.endswith(".md") and name not in pages:
            os.remove(out_dir / name)
    for name, blocks in pages.items():
        (out_dir / name).write_text(GENERATED + "\n\n" + "\n\n".join(blocks) + "\n",
                                    encoding="utf-8")
    print(f"api_pages: {len(pages)} pages, {functions} functions into {out_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
