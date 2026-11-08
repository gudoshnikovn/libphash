#!/usr/bin/env python3
"""The site's product-photo corpus: catalog images from Amazon Berkeley Objects (ABO).

ABO is a collection of Amazon product listings and their catalog images, released by
Amazon.com under CC BY 4.0 (https://amazon-berkeley-objects.s3.amazonaws.com/index.html).
The images are not part of the repository. tools/site/corpus_products.tsv lists the
sample: for each image, its product, the group of products it belongs to, the URL of the
file, its SHA-256 and its credit. This script downloads the images into a cache outside
the repository and checks every file against its digest, as fetch_corpus.py does for the
photographs of Wikimedia Commons, and reuses its helpers.

The corpus answers two questions about product photos. Images of one product are the
pictures of one listing (the main image and the others: another angle, a detail, the
product in use), "the same product, another photograph". These are different views, not
copies; telling them apart is the limit of what a perceptual hash can do, so they are
measured as a case of their own and not as copies. Byte-identical images that several
listings share are left out. Products of one group are
listings of one model in another color, "similar but different": same shop, same brand,
same model number and name, different color.

    fetch_products.py fetch [--cache DIR] [--strict]
        Downloads what the cache lacks (two connections at most, each file retried with
        a growing pause) and prints the paths of the verified files. A file that cannot
        be downloaded, or whose bytes differ from the manifest, is left out with a
        warning; with --strict it fails the command. A run stopped half-way continues
        where it stopped: a file is kept only once complete and verified.

    fetch_products.py page --out docs/project/products.md
        Writes the attribution page from the manifest.

    fetch_products.py manifest [--out FILE] [--shards HEX] [--products N] [--groups N]
        Rebuilds the manifest from the ABO metadata (below). Run by hand, never by the
        build: the selection is a decision, recorded in the repository.

    fetch_products.py mirror --out DIR
        Writes the copy of the corpus into DIR (products/<sha256>.<ext> and the
        manifest) from the verified files of the cache. After a new manifest, this is
        committed to libphash-site-corpus and PRODUCTS_MIRROR_COMMIT names the commit.

The metadata: ABO's listings (listings/metadata/listings_<0-9a-f>.json.gz, 16 files of
about 5.4 MB, one JSON object per listing) and images/metadata/images.csv.gz (6.4 MB: the
size of each original image and the path of its file), downloaded once into the cache.
The shards are read in the order of --shards, each only when the quota is not filled
yet; a quota of this size is filled by the first.

The rendition is ABO's images/original/, unchanged: 1 to 6 megapixels, about 300 KB, the
size of a product photograph in a catalog. A smaller rendition cut here would carry bytes
ABO does not publish, and images/small/ (256 pixels) is smaller than every other corpus
of the site. A listing's images are taken only when their original is at least MIN_SIDE
pixels on its shorter side, which leaves out icons and banners, and at most MAX_PIXELS,
which keeps a file near 150 KB, the megapixel class of the site's other corpora.

The selection, with a fixed seed. A listing is one product, (item_id, domain_name). It
needs at least two usable images: its main image and its other images, in the order of
the listing, at most IMAGES (3 for every other product and 2 for the rest, so about
2.5 on average and 500 images for 200 products). Listings that share the same brand, model number, model
name, product type and shop form a group; the group's products are its listings of
different colors, one per color (the sizes of one color show the same photographs), at
most GROUP_MAX. Of the groups with at least two colors, GROUPS are drawn (15% of the
PRODUCTS are in a group); the rest of the PRODUCTS are products of other groups, one
listing per group. No type of product
takes more than MAX_SHARE of the corpus (a third of ABO is phone cases). An image whose
id, or whose bytes, another product already has is skipped, so that two products never
share a photograph.
"""
import argparse
import collections
import concurrent.futures
import csv
import gzip
import hashlib
import json
import os
import random
import re
import sys
import time
import urllib.error
import urllib.parse

import fetch_corpus

ROOT = fetch_corpus.ROOT
MANIFEST = os.path.join(ROOT, "tools", "site", "corpus_products.tsv")
FIELDS = ["file", "product", "group", "role", "title", "color", "license", "author", "sha256",
          "url", "page"]
BUCKET = "https://amazon-berkeley-objects.s3.amazonaws.com/"
RENDITION = "images/original/"
LICENSE = "CC BY 4.0"
AUTHOR = "Amazon.com"
SEED = 20261108
PRODUCTS = 200
GROUPS = 15           # groups of two colors: 15% of the PRODUCTS
GROUP_MAX = 2         # colors taken from one group
IMAGES = 3            # images of one product at most: the main image and two others
MIN_SIDE = 500        # pixels, of the original image
MAX_PIXELS = 2_500_000  # of the original image: about 150 KB, the site's ~1 MP class
MAX_SHARE = 0.2       # of the corpus, per product type
CONNECTIONS = 2       # at most, against S3
RETRIES = 5
# The copy of the corpus every build downloads from: the commit of libphash-site-corpus
# that holds this manifest's images under products/; ABO's bucket is the fallback.
PRODUCTS_MIRROR_COMMIT = "8969aa7af09b3e400395669ae35d3936fb446a90"
MIRROR = (f"https://raw.githubusercontent.com/{fetch_corpus.MIRROR_REPO}/"
          f"{PRODUCTS_MIRROR_COMMIT}/products/")


def cache_dir(override=None):
    return override or os.path.join(os.path.dirname(fetch_corpus.cache_dir()), "products")


def extension(row):
    return os.path.splitext(urllib.parse.urlsplit(row["url"]).path)[1].lower()


def local_name(row):
    return row["sha256"][:16] + extension(row)


def read_manifest(path=MANIFEST):
    with open(path, newline="") as f:
        return list(csv.DictReader((line for line in f if not line.startswith("#")),
                                   delimiter="\t"))


def get(url):
    """The bytes of url; a missing file (404) at once, anything else retried after
    1, 2, 4 ... seconds."""
    for attempt in range(RETRIES):
        try:
            return fetch_corpus.get(url)
        except urllib.error.HTTPError as e:
            if e.code in (403, 404) or attempt == RETRIES - 1:
                raise
        except OSError:
            if attempt == RETRIES - 1:
                raise
        time.sleep(2 ** attempt)


def save(path, data):
    with open(path + ".part", "wb") as f:
        f.write(data)
    os.replace(path + ".part", path)


# --8<-- [start:fetch]
def download(row):
    """The image's bytes as the manifest names them, from the copy of the corpus when
    there is one, else from ABO's bucket; (None, why) when neither has them."""
    why = "no source"
    sources = [row["url"]]
    if PRODUCTS_MIRROR_COMMIT:
        sources.insert(0, MIRROR + row["sha256"] + extension(row))
    for source in sources:
        try:
            data = get(source)
        except OSError as e:
            why = f"{urllib.parse.urlsplit(source).netloc}: {e}"
            continue
        if hashlib.sha256(data).hexdigest() == row["sha256"]:
            return data, None
        why = f"{urllib.parse.urlsplit(source).netloc}: the download differs from the manifest"
    return None, why


def fetch(cache=None, manifest=MANIFEST):
    """The verified local paths of the manifest's images, downloading what is missing,
    CONNECTIONS at a time. Returns (paths, problems); an image that is unavailable is a
    problem, not an error. Returns also the number of images downloaded in this call."""
    d = cache_dir(cache)
    os.makedirs(d, exist_ok=True)
    rows = read_manifest(manifest)

    def one(row):
        path = os.path.join(d, local_name(row))
        if os.path.exists(path):
            with open(path, "rb") as f:
                if hashlib.sha256(f.read()).hexdigest() == row["sha256"]:
                    return path, None, False
        data, why = download(row)
        if data is None:
            return None, f"{row['file']}: {why}", False
        save(path, data)
        return path, None, True

    with concurrent.futures.ThreadPoolExecutor(CONNECTIONS) as pool:
        results = list(pool.map(one, rows))
    paths = [p for p, _, _ in results if p]
    problems = [w for _, w, _ in results if w]
    return paths, problems, sum(n for _, _, n in results)
# --8<-- [end:fetch]


def first(listing, key):
    """The first value of a metadata field of the form [{"value": ...}, ...]."""
    values = listing.get(key)
    return str(values[0]["value"]).strip() if values else ""


def cached(d, name):
    """The path of a metadata file in d, downloaded first if d lacks it."""
    os.makedirs(d, exist_ok=True)
    path = os.path.join(d, name)
    if not os.path.exists(path):
        print(f"downloading {name}", file=sys.stderr)
        sub = "images/metadata/" if name.startswith("images") else "listings/metadata/"
        save(path, get(BUCKET + sub + name))
    return path


def image_table(d):
    """image_id -> (path of the file, shorter side and pixel count of the original)."""
    with gzip.open(cached(d, "images.csv.gz"), "rt", newline="") as f:
        return {r["image_id"]: (r["path"], min(int(r["width"]), int(r["height"])),
                                  int(r["width"]) * int(r["height"]))
                for r in csv.DictReader(f)}


def shard_listings(d, images, shard):
    """The listings of one shard, downloaded into d once. Of each listing only what the
    selection reads is kept, and only a listing with at least two images whose originals
    are large enough."""
    listings = []
    with gzip.open(cached(d, f"listings_{shard}.json.gz"), "rt") as f:
        for line in f:
            x = json.loads(line)
            ids = ([x["main_image_id"]] if x.get("main_image_id") else []) + \
                x.get("other_image_id", [])
            ids = [i for i in ids if i in images and images[i][1] >= MIN_SIDE and images[i][2] <= MAX_PIXELS]
            if len(ids) < 2:
                continue
            key = tuple(first(x, k).casefold() for k in ("brand", "model_number", "model_name"))
            listings.append({
                "item": x["item_id"], "domain": x["domain_name"],
                "main": x.get("main_image_id"), "type": first(x, "product_type"),
                "key": (x["domain_name"], first(x, "product_type")) + key
                if key[0] and key[1] else None,
                "color": first(x, "color").casefold(),
                "title": re.sub(r"\s+", " ", first(x, "item_name"))[:100],
                "images": [(i, images[i][0]) for i in ids[:IMAGES]],
            })
    return sorted(listings, key=lambda x: (x["domain"], x["item"]))


def build_manifest(out, shards, products, groups, cache=None):
    d = cache_dir(cache)
    images = image_table(os.path.join(d, "metadata"))

    rows, seen_ids, seen_sha, per_type = [], set(), set(), collections.Counter()

    def attempt(url):
        try:
            return get(url)
        except OSError as e:
            print(f"skip {url}: {e}", file=sys.stderr)

    def take(x, group):
        """Appends the product's images to rows if it ends with at least two."""
        if per_type[x["type"]] >= MAX_SHARE * products or any(i in seen_ids for i, _ in x["images"][:IMAGES]):
            return False
        got = []
        # IMAGES for every other product, one fewer for the rest: 2.5 on average
        wanted = x["images"][:IMAGES - len({r["product"] for r in rows}) % 2]
        for (image_id, path), data in zip(wanted, pool.map(
                lambda im: attempt(BUCKET + RENDITION + im[1]), wanted)):
            url = BUCKET + RENDITION + path
            if data is None:
                continue
            digest = hashlib.sha256(data).hexdigest()
            if digest in seen_sha or digest in {r["sha256"] for r in got}:
                continue
            row = {"sha256": digest, "url": url}
            save(os.path.join(d, local_name(row)), data)
            got.append({
                "file": image_id + extension(row), "product": f"{x['item']}@{x['domain']}",
                "group": group, "role": "main" if image_id == x["main"] else "other", "title": x["title"], "color": x["color"], "license": LICENSE,
                "author": AUTHOR, "sha256": digest, "url": url,
                "page": f"https://www.{x['domain']}/dp/{x['item']}"})
        if len(got) < 2:
            return False
        rows.extend(got)
        seen_ids.update(i for i, _ in wanted)
        seen_sha.update(r["sha256"] for r in got)
        per_type[x["type"]] += 1
        print(f"{len(set(r['product'] for r in rows)):5} {x['item']} {x['title'][:60]}",
              file=sys.stderr)
        return True

    pool = concurrent.futures.ThreadPoolExecutor(CONNECTIONS)
    n, ngroups, used = 0, 0, set()
    for shard in shards:
        if n >= products and ngroups >= groups:
            break  # the quota is filled: the remaining shards are not downloaded
        listings = shard_listings(os.path.join(d, "metadata"), images, shard)
        by_key = collections.defaultdict(dict)  # key -> color -> first listing by id
        for x in listings:
            if x["key"] and x["color"] and x["key"] not in used:
                by_key[x["key"]].setdefault(x["color"], x)
        rng = random.Random(f"{SEED}/{shard}")
        multi = sorted(k for k, v in by_key.items() if len(v) >= 2)
        rng.shuffle(multi)
        singles = [x for x in listings if not x["key"] or not x["color"]] + \
            [next(iter(v.values())) for k, v in sorted(by_key.items()) if len(v) < 2]
        rng.shuffle(singles)
        for key in multi:
            if ngroups == groups:
                break
            colors = sorted(by_key[key].values(), key=lambda x: x["item"])
            rng.shuffle(colors)
            gid = f"g{ngroups + 1:03d}"
            taken = sum([take(x, gid) for x in colors[:GROUP_MAX]])
            if taken >= 2:
                ngroups += 1
            else:  # a group left with one product is that product alone
                for r in rows:
                    if r["group"] == gid:
                        r["group"] = ""
            n += taken
            used.add(key)
        for x in singles:
            if n >= products - GROUP_MAX * (groups - ngroups):
                break  # room is kept for the groups the next shards supply
            if x["key"] in used:
                continue
            if take(x, ""):
                n += 1
                used.add(x["key"])
    pool.shutdown()
    with open(out, "w", newline="") as f:
        f.write("# The site's product-photo corpus; written by tools/site/fetch_products.py manifest.\n")
        w = csv.DictWriter(f, FIELDS, delimiter="\t", lineterminator="\n")
        w.writeheader()
        w.writerows(rows)
    print(f"manifest: {n} products, {ngroups} groups, {len(rows)} images in {out}", file=sys.stderr)


PAGE_HEAD = """---
title: 'Product-photo corpus: {n} Amazon catalog products for measuring image hashes'
description: 'The {images} catalog images of {n} Amazon products, from Amazon Berkeley Objects under CC BY 4.0, that the product-photo measurements use, with their credit.'
---

# Product-photo corpus

The product-photo measurements are drawn over {n} products of the
[Amazon Berkeley Objects](https://amazon-berkeley-objects.s3.amazonaws.com/index.html)
dataset (ABO): {images} catalog images, each of a product listing on Amazon.

Credit for the data, including all images, is due to **Amazon.com**. Credit for building
the dataset is due to Matthieu Guillaumin (Amazon.com), Thomas Dideriksen (Amazon.com),
Kenan Deng (Amazon.com), Himanshu Arora (Amazon.com), Jasmine Collins (UC Berkeley) and
Jitendra Malik (UC Berkeley). The dataset is licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The images are used as ABO
publishes them, unchanged; only the selection is ours. This does not suggest that Amazon.com or the authors endorse this
project.

Two kinds of pairs are drawn from the corpus. The images of one product (its main image
and its other images) are "the same product, another photograph". The products of one
group ({groups} groups) are listings of the same model in other colors, "similar but
different". The images of one product are different views of it, not copies, and the
hash's job on them is the limit of what it can do, so they are measured as a case of their
own. The corpus is one shop's catalog photographs, not the same photograph posted again
by other shops.

`tools/site/corpus_products.tsv` lists the images with the SHA-256 of each, and the
site's build downloads them once into a cache, from
[a copy kept for the site](https://github.com/{mirror}), and checks every file against its
digest.

| # | Product | Group | Images | Color |
|---|---|---|---|---|
"""


def write_page(out, manifest):
    rows = read_manifest(manifest)
    products = collections.OrderedDict()
    for r in rows:
        products.setdefault(r["product"], []).append(r)
    with open(out, "w") as f:
        f.write(PAGE_HEAD.format(
            n=len(products), images=len(rows), mirror=fetch_corpus.MIRROR_REPO,
            groups=len({r["group"] for r in rows} - {""})))
        for i, (product, imgs) in enumerate(products.items(), 1):
            r = imgs[0]
            title = r["title"].replace("|", "\\|").replace("[", "\\[").replace("]", "\\]")
            page = r["page"].replace("(", "%28").replace(")", "%29")
            color = r["color"].replace("|", "\\|")
            f.write(f"| {i} | [{title}]({page}) ({product.split('@')[0]}) | {r['group']} | "
                    f"{len(imgs)} | {color} |\n")


def write_mirror(out, manifest, cache=None):
    """The copy of the corpus, from the cache: every image, verified, under its digest."""
    paths, problems, _ = fetch(cache, manifest)
    if problems:
        for msg in problems:
            print(f"error: {msg}", file=sys.stderr)
        return 1
    os.makedirs(os.path.join(out, "products"), exist_ok=True)
    for row in read_manifest(manifest):
        with open(os.path.join(cache_dir(cache), local_name(row)), "rb") as f:
            data = f.read()
        with open(os.path.join(out, "products", row["sha256"] + extension(row)), "wb") as f:
            f.write(data)
    with open(manifest, "rb") as src, open(os.path.join(out, "corpus_products.tsv"), "wb") as dst:
        dst.write(src.read())
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("command", choices=["fetch", "manifest", "page", "mirror"])
    p.add_argument("--cache")
    p.add_argument("--out", help="manifest: the file to write (the tracked manifest); "
                   "page: the Markdown file; mirror: the directory")
    p.add_argument("--manifest", default=MANIFEST, help="fetch, page, mirror: the manifest to read")
    p.add_argument("--shards", default="0123456789abcdef",
                   help="manifest: the listing shards to read (hexadecimal digits)")
    p.add_argument("--products", type=int, default=PRODUCTS)
    p.add_argument("--groups", type=int, default=GROUPS)
    p.add_argument("--strict", action="store_true",
                   help="fetch: fail unless every image is available")
    args = p.parse_args()
    if args.command == "manifest":
        build_manifest(args.out or MANIFEST, args.shards, args.products, args.groups, args.cache)
        return 0
    if args.command == "page":
        write_page(args.out, args.manifest)
        return 0
    if args.command == "mirror":
        return write_mirror(args.out, args.manifest, args.cache)
    paths, problems, downloaded = fetch(args.cache, args.manifest)
    for msg in problems:
        print(f"warning: {msg}", file=sys.stderr)
    print(f"{len(paths)} images, {downloaded} downloaded", file=sys.stderr)
    print("\n".join(paths))
    return 1 if args.strict and problems else 0


if __name__ == "__main__":
    sys.exit(main())
