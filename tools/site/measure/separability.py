"""Separability: how far apart copies of an image and different images land. A copy is an
original after one moderate edit; different images are every pair of distinct
originals of a corpus (measure/corpus.py measures both)."""
import numpy as np

# --8<-- [start:copies]
# A "copy" is an original after one moderate edit: one strength of each transform, the
# kind of change a picture goes through between two places it is stored.
COPY_STRENGTHS = {
    "JPEG quality": 50,
    "Downscale": 0.5,
    "Rotation": 2,
    "Brightness": 1.15,
    "Contrast": 1.15,
    "Gamma": 1.2,
    "Gaussian blur": 1,
    "Noise": 5,
    "Crop": 5,
}
# --8<-- [end:copies]

RECALL = 0.95


# --8<-- [start:separability]
def separability(copies, different, lower_is_closer):
    """d', the threshold that keeps RECALL of the copies, and the share of different
    pairs that threshold also accepts.

    d' is the gap between the two means in units of their pooled spread, as
    tests/src/test_hash_properties.c defines it (separability()), signed so that a
    positive d' means copies are closer than different images.
    """
    c, d = np.asarray(copies, float), np.asarray(different, float)
    sign = 1.0 if lower_is_closer else -1.0
    pooled = np.sqrt((c.std() ** 2 + d.std() ** 2) / 2.0)
    dprime = sign * (d.mean() - c.mean()) / pooled if pooled > 0 else float("inf")
    # The threshold: the closest value that still accepts RECALL of the copies.
    t = sign * np.quantile(sign * c, RECALL, method="inverted_cdf")
    accepted = (d <= t) if lower_is_closer else (d >= t)
    return dprime, t, accepted.mean()
# --8<-- [end:separability]


def copies_and_different(data, algo, convert):
    """One algorithm's copies and different pairs of a measured corpus, each value through
    `convert`; the comparisons the library refused (None) left out."""
    copies = []
    for name, points in data["robust"][algo].items():
        for strength, values in points:
            if strength == COPY_STRENGTHS[name]:
                copies += [convert(v) for v in values if v is not None]
    different = [convert(v) for v in data["different"][algo] if v is not None]
    return copies, different
