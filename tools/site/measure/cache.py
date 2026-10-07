"""The cache every measurement goes through: build/site-cache/<name>.json, kept with the
key it was computed under and taken from there while the key is the same, so a rebuild
with nothing changed measures nothing and writes the same bytes."""
import glob
import hashlib
import json
import os

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))))
CACHE = os.path.join(ROOT, "build", "site-cache")

# The files every measurement depends on: the library and the tool that runs it.
LIBRARY = ["src/**/*.c", "src/**/*.h", "include/libphash.h", "tools/site/stages/*"]


def fingerprint(patterns, files=(), extra=""):
    """A key of the content of every file the glob `patterns` (relative to the
    repository) match, of `files` (any paths), and of `extra`."""
    h = hashlib.sha256(extra.encode())
    for pattern in patterns:
        for path in sorted(glob.glob(os.path.join(ROOT, pattern), recursive=True)):
            h.update(os.path.relpath(path, ROOT).encode())
            with open(path, "rb") as f:
                h.update(hashlib.sha256(f.read()).digest())
    for path in files:
        with open(path, "rb") as f:
            h.update(hashlib.sha256(f.read()).digest())
    return h.hexdigest()


def cached(name, key, compute, what=None):
    """compute(), or the value it returned last time under the same key. `what` names the
    measurement in the build's log."""
    path = os.path.join(CACHE, f"{name}.json")
    if os.path.exists(path):
        with open(path) as f:
            saved = json.load(f)
        if saved.get("key") == key:
            if what:
                print(f"render: {what}: unchanged, from the cache")
            return saved["value"]
    if what:
        print(f"render: {what}: measuring")
    value = compute()
    os.makedirs(CACHE, exist_ok=True)
    with open(path, "w") as f:
        json.dump({"key": key, "value": value}, f)
    return value
