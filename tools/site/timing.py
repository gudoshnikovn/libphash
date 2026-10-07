"""Time, as the machine that builds the site measures it.

`site_stages time` times decoding and every hash on two images, the example photograph
(400×400) and a 20-megapixel one; the pages quote those times through the files this
module writes, never as numbers in their text, and every time comes with the machine it
was measured on. The site that is published is built on Linux, where these hashes mostly
run; a local build shows the local machine's times, labeled as such.

Timing is the one measurement that differs between two runs of the same code, so it is
cached in build/site-cache/timing.json under a key of the library's sources, the tool,
the images and the machine: a rebuild with none of them changed writes the same bytes.
"""
import glob
import hashlib
import json
import os
import platform
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
CACHE = os.path.join(ROOT, "build", "site-cache", "timing.json")
IMAGES = {"small": "tests/data/photo.jpeg", "large": "tests/data/photo_large.jpeg"}
KEY_FILES = ["src/**/*.c", "src/**/*.h", "include/libphash.h", "tools/site/stages.c",
             "tools/site/timing.py"]


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


def _key(tool, host):
    h = hashlib.sha256(json.dumps(host, sort_keys=True).encode())
    for pattern in KEY_FILES:
        for path in sorted(glob.glob(os.path.join(ROOT, pattern), recursive=True)):
            h.update(os.path.relpath(path, ROOT).encode())
            with open(path, "rb") as f:
                h.update(hashlib.sha256(f.read()).digest())
    for path in [tool] + [os.path.join(ROOT, p) for p in IMAGES.values()]:
        with open(path, "rb") as f:
            h.update(hashlib.sha256(f.read()).digest())
    return h.hexdigest()


# --8<-- [start:timing]
def measure_timing(tool):
    """{"machine": {...}, "small": {...}, "large": {...}}, each image's entry as
    `site_stages time` prints it (width, height, build_info, compiler, and per case the
    minimum and median in milliseconds), from the cache when nothing that decides it has
    changed."""
    host = machine()
    key = _key(tool, host)
    if os.path.exists(CACHE):
        with open(CACHE) as f:
            data = json.load(f)
        if data.get("key") == key:
            print("render: timing: unchanged, times from the cache")
            return data
    print("render: timing: measuring")
    data = {"key": key, "machine": host}
    for name, path in IMAGES.items():
        out = subprocess.run([tool, "time", os.path.join(ROOT, path)], check=True,
                             capture_output=True, text=True).stdout
        data[name] = json.loads(out)
    os.makedirs(os.path.dirname(CACHE), exist_ok=True)
    with open(CACHE, "w") as f:
        json.dump(data, f)
    return data
# --8<-- [end:timing]


def ms(value):
    """A time as a page quotes it: two significant figures, at most two decimals."""
    if value < 1:
        return f"{value:.2f}"
    return f"{value:.1f}" if value < 10 else f"{value:.0f}"


def megapixels(entry):
    return round(entry["width"] * entry["height"] / 1e6)


def describe(data):
    """The machine and the build, in one sentence."""
    m, build = data["machine"], data["small"]
    decoders = dict(kv.split("=", 1) for kv in build["build_info"].split())
    return (f"{m['cpu']}, {m['cores']} cores, {m['os']}; compiled by "
            f"{build.get('compiler', 'an unnamed compiler')}, optimized, with "
            f"{decoders['jpeg']} decoding the JPEGs")


# The nine hashes at their default settings, by ph_algorithm_name(): the rows every page's
# table shows, beside decoding, so a page compares its hash with the others.
DEFAULTS = ("ahash", "dhash", "phash", "whash", "bmh", "mhash", "radial", "color_hash",
            "color_moments")


def own_cases(algo, cases):
    """A page's own rows: its hash and the variants of it that time_cases[] names after
    it (whash_full, mhash_size_*, radial_*)."""
    return [c for c in cases if c == algo or c.startswith(algo + "_")]


def write_timing(data, algorithms, out_dir):
    """The files the pages include: per algorithm the passport's Cost row and the table of
    its Cost section (decoding, every hash at its defaults, and the page's own variants,
    in bold), and the footnote; and the full table of every case."""
    small, large = data["small"], data["large"]
    size = f"{small['width']}×{small['height']}"
    mpx = f"{megapixels(large)} Mpx"
    for algo, mod in algorithms.items():
        s, l = small["cases"][algo]["min_ms"], large["cases"][algo]["min_ms"]
        text = f"{ms(s)} ms on a {size} image, {ms(l)} ms on {mpx}, after decoding"
        for case, label in getattr(mod, "COST_VARIANTS", ()):
            text += (f"; {ms(small['cases'][case]['min_ms'])} and "
                     f"{ms(large['cases'][case]['min_ms'])} ms {label}")
        path = os.path.join(out_dir, algo, "cost-row.md")
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(f"| **Cost** | {text}[^cost] |\n")

    timing_dir = os.path.join(out_dir, "timing")
    os.makedirs(timing_dir, exist_ok=True)
    with open(os.path.join(timing_dir, "footnote.md"), "w") as f:
        f.write(f"[^cost]: Measured when this site was built, on {describe(data)}: the "
                "minimum of at least five runs after a warm-up, each the first hash on a "
                "freshly loaded image. The section Cost has the code.\n")
    def table(cases, bold=()):
        rows = ["| Case | " + f"{size} | {mpx} |", "|---|---|---|"]
        for case in cases:
            b = "**" if case in bold else ""
            rows.append(f"| {b}`{case}`{b} | {b}{ms(small['cases'][case]['min_ms'])} ms{b} | "
                        f"{b}{ms(large['cases'][case]['min_ms'])} ms{b} |")
        return "\n".join(rows) + "\n"

    head = f"Measured on {describe(data)}; the minimum of the runs."
    with open(os.path.join(timing_dir, "table.md"), "w") as f:
        f.write(f"{head}\n\n" + table(small["cases"]))
    for algo in algorithms:
        own = own_cases(algo, small["cases"])
        shown = ["decode"] + [c for c in DEFAULTS if c in small["cases"]]
        shown += [c for c in own if c not in shown]
        with open(os.path.join(out_dir, algo, "timing-table.md"), "w") as f:
            f.write(f"{head} Decoding, every hash at its default settings, and in bold "
                    "this page's.\n\n" + table(shown, own))
