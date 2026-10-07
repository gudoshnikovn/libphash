"""Running site_stages (tools/site/stages/), the measuring tool linked against the library."""
import json
import os
import subprocess
import tempfile

import numpy as np


def read_pnm(path):
    """A binary PGM or PPM, as site_stages writes them: (h, w) or (h, w, 3) bytes."""
    with open(path, "rb") as f:
        magic = f.readline().strip()
        w, h = map(int, f.readline().split())
        f.readline()
        data = np.frombuffer(f.read(), dtype=np.uint8)
    return data.reshape(h, w, 3) if magic == b"P6" else data.reshape(h, w)


def run_stages(tool, mode, image, images=(), floats=()):
    """Runs `site_stages <mode> <image> <dir>` and returns ({json}, {file name: array})
    for the `<mode>.json` it wrote, the named PNM images, and the named raw float files
    (flat float32 arrays)."""
    with tempfile.TemporaryDirectory() as tmp:
        subprocess.run([tool, mode, image, tmp], check=True)
        with open(os.path.join(tmp, f"{mode}.json")) as f:
            stages = json.load(f)
        arrays = {name: read_pnm(os.path.join(tmp, name)) for name in images}
        arrays.update({name: np.fromfile(os.path.join(tmp, name), dtype=np.float32)
                       for name in floats})
    return stages, arrays


def run_lines(tool, mode, *args):
    """Runs a site_stages mode that prints one JSON object per line; the objects."""
    out = subprocess.run([tool, mode, *args], check=True, capture_output=True,
                         text=True).stdout
    return [json.loads(line) for line in out.splitlines()]


def run_on_images(tool, mode, images):
    """run_lines() on Pillow images, written losslessly as PPM in their order."""
    with tempfile.TemporaryDirectory() as tmp:
        files = []
        for i, im in enumerate(images):
            files.append(os.path.join(tmp, f"{i:04d}.ppm"))
            im.save(files[-1])
        return run_lines(tool, mode, *files)
