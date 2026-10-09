"""Time, as the machine that builds the site measures it.

`site_stages time` times decoding and every hash on two images, the example photograph
(400×400) and a 20-megapixel one; the pages quote those times through the files
draw/timing_tables.py writes, never as numbers in their text, and every time comes with the machine it
was measured on. The site that is published is built on Linux, where these hashes mostly
run; a local build shows the local machine's times, labeled as such.

Timing is the one measurement that differs between two runs of the same code, so it is
cached (measure/cache.py) under a key of the library's sources, the tool, the images and
the machine: a rebuild with none of them changed writes the same bytes. The tables the
pages include are written by draw/timing_tables.py.
"""
import json
import os
import platform
import subprocess

from measure.cache import LIBRARY, ROOT, cached, fingerprint

IMAGES = {"small": "tests/data/photo.jpeg", "large": "tests/data/photo_large.jpeg"}
KEY_FILES = LIBRARY + ["tools/site/measure/timing.py"]


def machine():
    """The processor, its core count and the operating system, as the caption names them."""
    cpu = platform.processor() or platform.machine()
    try:
        if platform.system() == "Darwin":
            cpu = subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"], check=True,
                                 capture_output=True, text=True).stdout.strip()
        elif os.path.exists("/proc/cpuinfo"):
            with open("/proc/cpuinfo") as f:
                names = [ln.split(":", 1)[1].strip() for ln in f if ln.startswith("model name")]
            cpu = names[0] if names else cpu
    except (OSError, subprocess.CalledProcessError):
        pass
    system = {"Darwin": "macOS"}.get(platform.system(), platform.system())
    return {"cpu": cpu, "cores": os.cpu_count(), "os": f"{system} {platform.machine()}"}


# --8<-- [start:timing]
def measure_timing(tool):
    """{"machine": {...}, "small": {...}, "large": {...}, "scan": {...}}, each image's
    entry as `site_stages time` prints it (width, height, build_info, compiler, and per
    case the minimum and median in milliseconds) and the comparisons of a linear search as
    `site_stages scan` prints them (per case, nanoseconds per comparison), from the cache
    when nothing that decides them has changed, the machine included."""
    host = machine()
    paths = [os.path.join(ROOT, p) for p in IMAGES.values()]
    key = fingerprint(KEY_FILES, [tool] + paths, json.dumps(host, sort_keys=True))

    def measure():
        data = {"machine": host}
        for name, path in zip(IMAGES, paths):
            out = subprocess.run([tool, "time", path], check=True, capture_output=True,
                                 text=True).stdout
            data[name] = json.loads(out)
        data["scan"] = json.loads(subprocess.run([tool, "scan"], check=True,
                                                 capture_output=True, text=True).stdout)
        return data

    return cached("timing", key, measure, "timing")
# --8<-- [end:timing]


# --8<-- [start:sizes]
# One photograph at several sizes, in megapixels, and in three formats: the large example
# photograph scaled down, keeping its aspect ratio, and encoded the same way at every size,
# so that a time changes with the number of pixels and nothing else. The JPEG is timed
# decoding and hashing; the PNG and the WebP decoding only, as a hash reads the decoded
# pixels whatever the file was.
SIZES_MPX = (0.25, 1, 2, 5, 10, 20)
FORMATS = {"jpeg": ("JPEG", {"quality": 90}), "png": ("PNG", {}),
           "webp": ("WEBP", {"quality": 90})}
LOADS = ("decode", "gray_decode")
HASHES = ("ahash", "dhash", "phash", "whash", "bmh", "mhash", "radial", "color_hash",
          "color_moments", "multi")


def sized_images():
    """{(mpx, format): path}: the large photograph at each size in each format, written
    under build/site-cache/sizes/ once and kept."""
    from PIL import Image

    source = os.path.join(ROOT, IMAGES["large"])
    out_dir = os.path.join(ROOT, "build", "site-cache", "sizes")
    os.makedirs(out_dir, exist_ok=True)
    paths, image = {}, None
    for mpx in SIZES_MPX:
        for fmt, (pil, options) in FORMATS.items():
            path = os.path.join(out_dir, f"{mpx:g}mpx.{fmt}")
            paths[mpx, fmt] = path
            if os.path.exists(path):
                continue
            if image is None:
                image = Image.open(source).convert("RGB")
            w, h = image.size
            scale = (mpx * 1e6 / (w * h)) ** 0.5
            size = (round(w * scale), round(h * scale))
            image.resize(size, Image.Resampling.LANCZOS).save(path, pil, **options)
    return paths


def measure_sizes(tool):
    """{"machine": {...}, "sizes": [{"mpx", "width", "height", "jpeg": {case: ms, "bytes":
    the file's size}, "png": {...}, "webp": {...}}]}: the minimum time of each case of `site_stages time` on
    the photograph at each size, from the cache when nothing that decides them has changed."""
    host = machine()
    paths = sized_images()
    key = fingerprint(KEY_FILES, [tool] + sorted(paths.values()),
                      json.dumps(host, sort_keys=True))

    def measure():
        sizes = []
        for mpx in SIZES_MPX:
            row = {"mpx": mpx}
            for fmt in FORMATS:
                cases = LOADS + (HASHES if fmt == "jpeg" else ())
                out = json.loads(subprocess.run([tool, "time", paths[mpx, fmt], *cases],
                                                check=True, capture_output=True,
                                                text=True).stdout)
                row.update(width=out["width"], height=out["height"])
                row[fmt] = {c: v["min_ms"] for c, v in out["cases"].items()}
                row[fmt]["bytes"] = os.path.getsize(paths[mpx, fmt])
            sizes.append(row)
        return {"machine": host, "sizes": sizes}

    return cached("timing-sizes", key, measure, "timing by size")
# --8<-- [end:sizes]


# --8<-- [start:batch]
# A batch of one photograph's path, repeated: ph_hash_files() with the four 64-bit hashes,
# at every worker count from one to the CPUs the library counts for `threads = 0`, and at
# twice that; on the photograph at 20 megapixels and at a quarter of one, the shortest of
# five runs after a warm-up. Each run is a process of its own, as its peak memory is the
# process's high-water mark. The memory of a worker is measured once more on the
# 20-megapixel JPEG, PNG and WebP, at one worker and at one per CPU.
BATCH_SIZES = {20: 44, 0.25: 2000}  # megapixels: items in the batch
BATCH_RUNS = 5


def _batch(tool, path, threads, items, runs):
    out = subprocess.run([tool, "batch", path, str(threads), str(items), str(runs)],
                         check=True, capture_output=True, text=True).stdout
    data = json.loads(out)
    if "threads=on" not in data["build_info"]:
        raise SystemExit("site_stages batch: the library was built without threads")
    data["per_worker"] = (data["peak_after"] - data["peak_before"]) / data["workers"]
    return data


def measure_batch(tool):
    """{"machine": {...}, "cpus": n, "scaling": {mpx: [run per worker count]},
    "memory": {format: {"bytes": the file's size, "runs": [run at one worker, run at one
    per CPU]}}}, each run as `site_stages batch` prints it plus "per_worker", the
    growth of the peak divided by the workers; from the cache when nothing that decides
    them has changed."""
    host = machine()
    paths = sized_images()
    key = fingerprint(KEY_FILES, [tool] + sorted(paths.values()),
                      json.dumps(host, sort_keys=True))

    def measure():
        cpus = _batch(tool, paths[0.25, "jpeg"], 1, 1, 1)["cpus"]
        counts = list(range(1, cpus + 1)) + [2 * cpus]
        scaling = {}
        for mpx, items in BATCH_SIZES.items():
            scaling[str(mpx)] = [_batch(tool, paths[mpx, "jpeg"], t, max(items, 2 * t),
                                        BATCH_RUNS) for t in counts]
        memory = {}
        for fmt in FORMATS:
            path = paths[20, fmt]
            runs = [_batch(tool, path, t, 2 * t, 1) for t in (1, cpus)]
            memory[fmt] = {"bytes": os.path.getsize(path), "runs": runs}
        return {"machine": host, "cpus": cpus, "scaling": scaling, "memory": memory}

    return cached("timing-batch", key, measure, "batches")
# --8<-- [end:batch]
