#!/usr/bin/env python3
"""The site's photo corpus: public-domain photographs from Wikimedia Commons.

The photographs are not part of the repository. tools/site/corpus_photos.tsv lists them:
for each file, the URL of the image this site measures, its SHA-256, its license and its
author, and the page on Commons where both can be checked. This script downloads them
into a cache outside the repository and checks every file against its digest, so the
charts are drawn from exactly the bytes the manifest names.

The files come from a copy of the corpus, a fixed commit of the libphash-site-corpus
repository that holds every photograph of the manifest under its SHA-256 (MIRROR), and
from Commons only for a photograph the copy lacks. Commons alone would not do: a rendition
carries the file's metadata, so an edit to a file's description on Commons changes the
bytes of its rendition while its pixels stay the same, and the download no longer matches
the manifest. A commit, once made, never changes.

    fetch_corpus.py fetch [--cache DIR] [--strict]
        Downloads what the cache lacks and prints the paths of the verified files. A
        file that cannot be downloaded, or whose bytes differ from the manifest, is left
        out with a warning: the charts say how many photographs they were drawn from, so
        a smaller corpus is visible on the page rather than silently different. With
        --strict a missing file fails the command instead; the site workflow fetches
        the corpus this way before it builds the site, so the site it builds has every photograph.

    fetch_corpus.py page --out docs/project/corpus.md
        Writes the site's attribution page from the manifest: every photograph, with a
        link to its page on Commons, its author and its license.

    fetch_corpus.py manifest
        Rebuilds the manifest from Commons (below). Run by hand, never by the build:
        the selection is a decision, recorded in the repository, not something a build
        repeats.

    fetch_corpus.py mirror --out DIR
        Writes the copy of the corpus into DIR (photos/<sha256>.<ext> and the manifest)
        from the verified files of the cache. After a new manifest, this is committed to
        libphash-site-corpus and MIRROR names the new commit.

The selection: files that Commons' reviewers rated Quality images, whose license, read
from each file's own metadata, is CC0 or public domain, spread over the subjects in
GROUPS so that no one kind of picture dominates, at most MAX_PER_AUTHOR per author, and
drawn with a fixed seed; the subjects that fall short are made up from all Quality
images. The image measured is Commons' standard 1280-pixel-wide
rendition (960 for a portrait), about a megapixel, the size a collection being
deduplicated typically holds.
"""
import argparse
import csv
import hashlib
import html
import json
import os
import random
import re
import sys
import tarfile
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MANIFEST = os.path.join(ROOT, "tools", "site", "corpus_photos.tsv")
API = "https://commons.wikimedia.org/w/api.php"
USER_AGENT = "libphash-site/1 (https://github.com/gudoshnikovn/libphash; corpus manifest)"
FIELDS = ["file", "subject", "license", "author", "sha256", "width", "height", "url", "page"]

# (subject, Commons search, how many). Every search is restricted to bitmaps under a
# CC0 license; the license is then read again from each file's metadata.
GROUPS = [
    ("people", "deepcat:Quality_images_of_people", 24),
    ("animals", "deepcat:Quality_images_of_animals", 24),
    ("architecture", "deepcat:Quality_images_of_architecture", 24),
    ("landscapes", "deepcat:Quality_images_of_landscapes", 20),
    ("nature", "deepcat:Quality_images_of_nature", 20),
    ("objects", "deepcat:Quality_images_of_objects", 24),
    ("transport", "deepcat:Quality_images_of_transport", 20),
    ("food", "deepcat:Quality_images_of_food", 12),
    ("sports", "deepcat:Quality_images_of_sports", 10),
    ("arts", "deepcat:Quality_images_of_the_arts", 10),
    ("text", "incategory:Quality_images intitle:sign", 6),
    ("text", "incategory:Quality_images intitle:inscription", 6),
    # What the subjects above could not fill, the cap on authors being reached in some,
    # comes from all Quality images, up to TOTAL.
    ("other", "incategory:Quality_images", None),
]
TOTAL = 200
SEED = 20261007
# Files the selection would draw and the site leaves out: nudity, which the site's
# attribution page would list by name.
EXCLUDE = {"File:Anterior view of human male, retouched.jpg"}
MAX_PER_AUTHOR = 3
LICENSES = {"cc0", "pd"}  # extmetadata "License" codes accepted
# The copy of the corpus every build downloads from: the commit of libphash-site-corpus
# that holds this manifest's photographs.
MIRROR_REPO = "gudoshnikovn/libphash-site-corpus"
MIRROR_COMMIT = "c625fbedd46f3303c3f4278d6e20969de369b522"
MIRROR = f"https://raw.githubusercontent.com/{MIRROR_REPO}/{MIRROR_COMMIT}/photos/"
# The whole commit as one archive: a cache missing most of the corpus downloads this
# instead of each file in turn.
MIRROR_ARCHIVE = f"https://codeload.github.com/{MIRROR_REPO}/tar.gz/{MIRROR_COMMIT}"


def cache_dir(override=None):
    if override:
        return override
    base = os.environ.get("XDG_CACHE_HOME") or os.path.join(os.path.expanduser("~"), ".cache")
    return os.path.join(base, "libphash-site", "photos")


def get(url, timeout=60):
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read()


def api(**params):
    params.update(format="json", formatversion="2")
    return json.loads(get(API + "?" + urllib.parse.urlencode(params)))


def read_manifest():
    with open(MANIFEST, newline="") as f:
        rows = [r for r in csv.DictReader((line for line in f if not line.startswith("#")),
                                          delimiter="\t")]
    return rows


def extension(row):
    return os.path.splitext(urllib.parse.urlsplit(row["url"]).path)[1].lower()


def local_name(row):
    return row["sha256"][:16] + extension(row)


# --8<-- [start:fetch]
def download(row, unreachable):
    """The photograph's bytes as the manifest names them, from the copy of the corpus or
    else from Commons; (None, why) when neither has them. A source that cannot be reached
    is added to `unreachable` and not tried again."""
    why = "no source"
    for source in (MIRROR + row["sha256"] + extension(row), row["url"]):
        host = urllib.parse.urlsplit(source).netloc
        if host in unreachable:
            continue
        try:
            data = get(source)
        except urllib.error.HTTPError as e:
            why = f"{host}: {e}"
            continue
        except OSError as e:
            unreachable[host] = str(e)
            why = f"cannot reach {host} ({e})"
            continue
        finally:
            if source == row["url"]:
                time.sleep(0.2)  # Commons asks clients to pace their requests
        if hashlib.sha256(data).hexdigest() == row["sha256"]:
            return data, None
        why = f"{host}: the download differs from the manifest"
    return None, why


def unpack_archive(rows, d):
    """Writes the rows' photographs that the copy's archive holds, verified, into d.
    An archive that cannot be downloaded or read writes nothing; the files are then
    downloaded one by one."""
    wanted = {row["sha256"] + extension(row): row for row in rows}
    try:
        req = urllib.request.Request(MIRROR_ARCHIVE, headers={"User-Agent": USER_AGENT})
        with urllib.request.urlopen(req, timeout=300) as r, \
                tarfile.open(fileobj=r, mode="r|gz") as tar:
            for member in tar:
                row = wanted.get(os.path.basename(member.name))
                if row is None or not member.isfile():
                    continue
                data = tar.extractfile(member).read()
                if hashlib.sha256(data).hexdigest() != row["sha256"]:
                    continue
                path = os.path.join(d, local_name(row))
                with open(path + ".part", "wb") as f:
                    f.write(data)
                os.replace(path + ".part", path)
    except (OSError, tarfile.TarError) as e:
        print(f"warning: photo corpus: the archive of the copy: {e}", file=sys.stderr)


_fetched = {}


def fetch(cache=None):
    """The verified local paths of the manifest's photographs, downloading what is missing.
    Returns (paths, problems); a photograph that is unavailable is a problem, not an error.
    One process downloads once: a second call returns the first one's answer."""
    d = cache_dir(cache)
    if d in _fetched:
        return _fetched[d]
    os.makedirs(d, exist_ok=True)

    def cached(row):
        path = os.path.join(d, local_name(row))
        if not os.path.exists(path):
            return False
        with open(path, "rb") as f:
            return hashlib.sha256(f.read()).hexdigest() == row["sha256"]

    rows = read_manifest()
    missing = [row for row in rows if not cached(row)]
    if len(missing) > 10:
        unpack_archive(missing, d)
    paths, problems, unreachable = [], [], {}
    for row in rows:
        path = os.path.join(d, local_name(row))
        if row not in missing or cached(row):
            paths.append(path)
            continue
        data, why = download(row, unreachable)
        if data is None:
            problems.append(f"{row['file']}: {why}")
            continue
        with open(path + ".part", "wb") as f:
            f.write(data)
        os.replace(path + ".part", path)
        paths.append(path)
    _fetched[d] = (paths, problems)
    return paths, problems
# --8<-- [end:fetch]


def plain(text):
    """Commons' metadata is HTML; the manifest keeps the text."""
    text = re.sub(r"<[^>]+>", "", text or "")
    return re.sub(r"\s+", " ", html.unescape(text)).strip()


def candidates(search):
    titles, offset = [], 0
    while offset is not None and len(titles) < 1000:
        r = api(action="query", list="search", srnamespace=6, srlimit=500, sroffset=offset,
                srsort="create_timestamp_asc",
                srsearch=f"{search} incategory:CC-zero filetype:bitmap")
        titles += [s["title"] for s in r["query"]["search"]]
        offset = r.get("continue", {}).get("sroffset")
    return titles


def info(titles):
    """imageinfo for up to 50 titles: size, license, author, and the rendition's URL."""
    out = {}
    for width in (1280, 960):
        r = api(action="query", prop="imageinfo", titles="|".join(titles),
                iiprop="size|extmetadata|url|mime", iiurlwidth=width,
                iiextmetadatafilter="License|LicenseShortName|Artist")
        for p in r["query"]["pages"]:
            if "imageinfo" in p:
                out.setdefault(p["title"], {})[width] = p["imageinfo"][0]
    return out


def build_manifest():
    os.makedirs(cache_dir(), exist_ok=True)
    rng = random.Random(SEED)
    chosen, authors, seen = [], {}, set()
    for subject, search, count in GROUPS:
        if count is None:
            count = TOTAL - len(chosen)
        pool = candidates(search)
        rng.shuffle(pool)
        picked = 0
        for i in range(0, len(pool), 50):
            if picked == count:
                break
            batch = [t for t in pool[i:i + 50] if t not in seen and t not in EXCLUDE]
            meta = info(batch) if batch else {}
            for title in batch:
                if picked == count:
                    break
                ii = meta.get(title)
                if not ii or ii[1280]["mime"] not in ("image/jpeg", "image/png"):
                    continue
                em = ii[1280]["extmetadata"]
                lic = em.get("License", {}).get("value", "")
                author = plain(em.get("Artist", {}).get("value", "")) or "unknown"
                if lic not in LICENSES or authors.get(author, 0) >= MAX_PER_AUTHOR:
                    continue
                w, h = ii[1280]["width"], ii[1280]["height"]
                if max(w, h) > 3 * min(w, h) or min(w, h) < 960:
                    continue  # a panorama or a small original: no standard rendition
                r = ii[1280] if w >= h else ii[960]
                url = r.get("thumburl") or r["url"]
                try:
                    data = get(url)
                except OSError as e:
                    print(f"skip {title}: {e}", file=sys.stderr)
                    continue
                time.sleep(0.2)
                digest = hashlib.sha256(data).hexdigest()
                row = {"sha256": digest, "url": url}
                with open(os.path.join(cache_dir(), local_name(row)), "wb") as f:
                    f.write(data)
                seen.add(title)
                authors[author] = authors.get(author, 0) + 1
                picked += 1
                chosen.append({
                    "file": title[len("File:"):], "subject": subject,
                    "license": plain(em.get("LicenseShortName", {}).get("value", lic)),
                    "author": author, "sha256": digest,
                    "width": r.get("thumbwidth", w), "height": r.get("thumbheight", h),
                    "url": url, "page": r["descriptionurl"],
                })
                print(f"{subject:12} {title}", file=sys.stderr)
        if picked < count:
            print(f"warning: {subject}: {picked} of {count}", file=sys.stderr)
    with open(MANIFEST, "w", newline="") as f:
        f.write("# The site's photo corpus; written by tools/site/fetch_corpus.py manifest.\n")
        w = csv.DictWriter(f, FIELDS, delimiter="\t", lineterminator="\n")
        w.writeheader()
        w.writerows(chosen)
    print(f"manifest: {len(chosen)} photographs in {MANIFEST}", file=sys.stderr)


PAGE_HEAD = """# Photo corpus

The robustness and separability charts on the algorithm pages are drawn over two corpora.
One is the synthetic corpus the tests use ([verification methodology](../methodology.md#the-corpus)).
The other is this one: {n} photographs from [Wikimedia Commons](https://commons.wikimedia.org/),
each released by its author under [CC0](https://creativecommons.org/publicdomain/zero/1.0/)
or into the public domain, as its own page on Commons states.

They were drawn from the files Commons' reviewers rated
[Quality images](https://commons.wikimedia.org/wiki/Commons:Quality_images), spread over
subjects ({subjects}) so that no one kind of picture dominates, with at most
{per_author} from any one author. The site measures Commons' standard rendition of each, 1280 pixels wide
(960 for a portrait), about a megapixel.

The files are not part of the repository and not part of the library's tests.
`tools/site/corpus_photos.tsv` lists them with the SHA-256 of each, and the site's build
downloads them once into a cache, from
[a copy kept for the site](https://github.com/{mirror}), and checks every file against its
digest, so the charts are drawn from exactly the files listed here.

| # | File | Subject | Author | License |
|---|---|---|---|---|
"""


def write_page(out):
    rows = read_manifest()
    with open(out, "w") as f:
        subjects = sorted({r["subject"] for r in rows} - {"other"})
        f.write(PAGE_HEAD.format(n=len(rows), per_author=MAX_PER_AUTHOR, mirror=MIRROR_REPO,
                                 subjects=", ".join(subjects)))
        for i, r in enumerate(rows, 1):
            name = r["file"].replace("|", "\\|").replace("[", "\\[").replace("]", "\\]")
            author = r["author"].replace("|", "\\|")
            page = r["page"].replace("(", "%28").replace(")", "%29")
            f.write(f"| {i} | [{name}]({page}) | {r['subject']} | {author} | "
                    f"{r['license']} |\n")


def write_mirror(out):
    """The copy of the corpus, from the cache: every photograph, verified, under its digest."""
    paths, problems = fetch()
    if problems:
        for msg in problems:
            print(f"error: {msg}", file=sys.stderr)
        return 1
    os.makedirs(os.path.join(out, "photos"), exist_ok=True)
    for row in read_manifest():
        with open(os.path.join(cache_dir(), local_name(row)), "rb") as f:
            data = f.read()
        with open(os.path.join(out, "photos", row["sha256"] + extension(row)), "wb") as f:
            f.write(data)
    with open(MANIFEST, "rb") as src, open(os.path.join(out, "corpus_photos.tsv"), "wb") as dst:
        dst.write(src.read())
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("command", choices=["fetch", "manifest", "page", "mirror"])
    p.add_argument("--cache")
    p.add_argument("--out", help="page: the Markdown file to write; mirror: the directory")
    p.add_argument("--strict", action="store_true",
                   help="fetch: fail unless every photograph is available")
    args = p.parse_args()
    if args.command == "manifest":
        build_manifest()
        return 0
    if args.command == "page":
        write_page(args.out)
        return 0
    if args.command == "mirror":
        return write_mirror(args.out)
    paths, problems = fetch(args.cache)
    for msg in problems:
        print(f"warning: {msg}", file=sys.stderr)
    print("\n".join(paths))
    return 1 if args.strict and problems else 0


if __name__ == "__main__":
    sys.exit(main())
