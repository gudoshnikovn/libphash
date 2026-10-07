#!/usr/bin/env python3
"""The site's photo corpus: public-domain photographs from Wikimedia Commons.

The photographs are not part of the repository. tools/site/corpus_photos.tsv lists them:
for each file, the URL of the image this site measures, its SHA-256, its license and its
author, and the page on Commons where both can be checked. This script downloads them
into a cache outside the repository and checks every file against its digest, so the
charts are drawn from exactly the bytes the manifest names.

    fetch_corpus.py fetch [--cache DIR]
        Downloads what the cache lacks and prints the paths of the verified files. A
        file that cannot be downloaded, or whose bytes differ from the manifest, is left
        out with a warning: the charts say how many photographs they were drawn from, so
        a smaller corpus is visible on the page rather than silently different.

    fetch_corpus.py page --out docs/project/corpus.md
        Writes the site's attribution page from the manifest: every photograph, with a
        link to its page on Commons, its author and its license.

    fetch_corpus.py manifest
        Rebuilds the manifest from Commons (below). Run by hand, never by the build:
        the selection is a decision, recorded in the repository, not something a build
        repeats.

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


def local_name(row):
    return row["sha256"][:16] + os.path.splitext(row["url"])[1].lower()


# --8<-- [start:fetch]
def fetch(cache=None):
    """The verified local paths of the manifest's photographs, downloading what is missing.
    Returns (paths, problems); a photograph that is unavailable is a problem, not an error."""
    d = cache_dir(cache)
    os.makedirs(d, exist_ok=True)
    paths, problems, offline = [], [], 0
    for row in read_manifest():
        path = os.path.join(d, local_name(row))
        if not os.path.exists(path):
            if offline:
                offline += 1
                continue
            try:
                data = get(row["url"])
            except urllib.error.HTTPError as e:
                problems.append(f"{row['file']}: {e}")
                continue
            except OSError as e:
                # No network: nothing more will download, so the rest is not tried.
                problems.append(f"cannot reach Commons ({e})")
                offline = 1
                continue
            if hashlib.sha256(data).hexdigest() != row["sha256"]:
                problems.append(f"{row['file']}: the download differs from the manifest")
                continue
            with open(path + ".part", "wb") as f:
                f.write(data)
            os.replace(path + ".part", path)
            time.sleep(0.2)  # Commons asks clients to pace their requests
        else:
            with open(path, "rb") as f:
                if hashlib.sha256(f.read()).hexdigest() != row["sha256"]:
                    problems.append(f"{row['file']}: the cached copy differs from the manifest")
                    continue
        paths.append(path)
    if offline:
        problems.append(f"{offline} photographs are neither cached nor downloadable")
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
downloads them once into a cache and checks every file against its digest, so the charts
are drawn from exactly the files listed here.

| # | File | Subject | Author | License |
|---|---|---|---|---|
"""


def write_page(out):
    rows = read_manifest()
    with open(out, "w") as f:
        subjects = sorted({r["subject"] for r in rows} - {"other"})
        f.write(PAGE_HEAD.format(n=len(rows), per_author=MAX_PER_AUTHOR,
                                 subjects=", ".join(subjects)))
        for i, r in enumerate(rows, 1):
            name = r["file"].replace("|", "\\|").replace("[", "\\[").replace("]", "\\]")
            author = r["author"].replace("|", "\\|")
            page = r["page"].replace("(", "%28").replace(")", "%29")
            f.write(f"| {i} | [{name}]({page}) | {r['subject']} | {author} | "
                    f"{r['license']} |\n")


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("command", choices=["fetch", "manifest", "page"])
    p.add_argument("--cache")
    p.add_argument("--out", help="page: the Markdown file to write")
    args = p.parse_args()
    if args.command == "manifest":
        build_manifest()
        return 0
    if args.command == "page":
        write_page(args.out)
        return 0
    paths, problems = fetch(args.cache)
    for msg in problems:
        print(f"warning: {msg}", file=sys.stderr)
    print("\n".join(paths))
    return 0


if __name__ == "__main__":
    sys.exit(main())
