"""The Markdown a page includes beside its figures (`--8<-- "docs/assets/generated/…"`):
tables of the numbers behind a chart, and sentences whose numbers move with the code."""
import os


def write_text(out_dir, name, text):
    """Writes <out_dir>/<name>, ending in exactly one newline."""
    os.makedirs(out_dir, exist_ok=True)
    with open(os.path.join(out_dir, name), "w") as f:
        f.write(text.rstrip("\n") + "\n")


def table(head, rows):
    """A Markdown table: `head` and every row a list of cells."""
    lines = ["| " + " | ".join(head) + " |", "|" + "---|" * len(head)]
    return "\n".join(lines + ["| " + " | ".join(str(c) for c in row) + " |" for row in rows])


def load_grayscale_hash(a, b, apart):
    """The sentence of a 64-bit hash's "Settings that affect it": the example photograph
    hashed with the library's grayscale (`a`) and with the decoder's (`b`)."""
    if apart == 0:
        return f"The example photograph hashes to `{a}` both ways."
    return (f"The example photograph hashes to `{a}` with the library's grayscale and to "
            f"`{b}` with the decoder's, {apart} bit{'' if apart == 1 else 's'} apart.")
