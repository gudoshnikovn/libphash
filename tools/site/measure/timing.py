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
    """{"machine": {...}, "small": {...}, "large": {...}}, each image's entry as
    `site_stages time` prints it (width, height, build_info, compiler, and per case the
    minimum and median in milliseconds), from the cache when nothing that decides it has
    changed, the machine included."""
    host = machine()
    paths = [os.path.join(ROOT, p) for p in IMAGES.values()]
    key = fingerprint(KEY_FILES, [tool] + paths, json.dumps(host, sort_keys=True))

    def measure():
        data = {"machine": host}
        for name, path in zip(IMAGES, paths):
            out = subprocess.run([tool, "time", path], check=True, capture_output=True,
                                 text=True).stdout
            data[name] = json.loads(out)
        return data

    return cached("timing", key, measure, "timing")
# --8<-- [end:timing]
