"""The robustness measurement: every edit of transforms.py applied to one image, every
variant hashed and compared with the original by site_stages, by each algorithm's own
metric. draw/robustness_charts.py draws it; measure/corpus.py repeats it over a corpus."""
import os
import tempfile

from PIL import Image

from measure.tool import run_lines
from measure.transforms import content_edits, transforms, write_edit


# --8<-- [start:measure]
def measure_variants(tool, base, ref):
    """{algorithm: {transform: [(strength, value)]}}: every edit of `base` (a Pillow
    image), of both groups of transforms.py, compared with `ref`, the same pixels saved
    losslessly.

    Every variant is written to a file, and `site_stages measure` hashes them with the
    library and compares each variant with the reference, by each algorithm's own
    metric; `value` is None where the comparison does not apply.
    """
    with tempfile.TemporaryDirectory() as tmp:
        files, keys = [], []
        for name, _, steps in transforms() + content_edits():
            for strength, op in steps:
                files.append(write_edit(op, base, os.path.join(tmp, str(len(files)))))
                keys.append((name, strength))
        rows = run_lines(tool, "measure", ref, *files)
    result = {}
    for (name, strength), row in zip(keys, rows):
        for algo, value in row.items():
            if algo != "file":
                result.setdefault(algo, {}).setdefault(name, []).append((strength, value))
    return result


def measure_robustness(tool, image):
    """measure_variants() for one image file."""
    base = Image.open(image).convert("RGB")
    with tempfile.TemporaryDirectory() as tmp:
        ref = os.path.join(tmp, "reference.ppm")
        base.save(ref)
        return measure_variants(tool, base, ref)
# --8<-- [end:measure]
