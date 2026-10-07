"""How an algorithm's comparison reads on its page: the value on the chart, which way is
closer, the range, and the format of one value. A page module (pages/<algo>.py) says
which by its constants; tools/site/README.md lists them."""


# --8<-- [start:bits]
def bits_that_differ(similarity, bits_total):
    """The number on a bit hash's chart. ph_similarity_digest() is 1 - distance / bits,
    so this is the Hamming distance itself: how many of the hash's bits the edit flipped."""
    return (1.0 - similarity) * bits_total
# --8<-- [end:bits]


def metric(mod):
    """How an algorithm page module's metric reads: (axis label, lower is closer, raw value
    -> value on the chart, the metric's range, format of one value).

    A bit hash sets BITS, and its charts show the bits that differ, lower closer. A
    module with BITS = None names its own METRIC, LOWER_IS_CLOSER, METRIC_RANGE (whose top
    is None for a metric without one) and FORMAT, and the raw value is the chart's value.
    """
    if mod.BITS:
        return ("bits that differ", True, lambda v: bits_that_differ(v, mod.BITS),
                (0, mod.BITS), lambda v: f"{round(float(v), 1):g}")
    return (mod.METRIC, mod.LOWER_IS_CLOSER, lambda v: v, mod.METRIC_RANGE,
            lambda v: mod.FORMAT.format(v))
