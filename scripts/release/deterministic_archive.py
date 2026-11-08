#!/usr/bin/env python3
"""Packs a directory into a .tar.gz or .zip whose bytes depend only on the files' contents,
names, modes and symlink targets -- not on when, where or by whom it was packed.

    deterministic_archive.py <parent-dir> <name> <archive.tar.gz|archive.zip>

<name> is the directory under <parent-dir> that becomes the archive's top level. Entries
are sorted; every timestamp is SOURCE_DATE_EPOCH (when unset: the time of the current git
commit, or 0 outside a checkout); owner and group are 0 with empty names; gzip writes no
timestamp and no file name. Standard library only, so the result is the same from GNU
tar, bsdtar and Windows alike -- none of them is used.
"""

import gzip
import io
import os
import subprocess
import sys
import tarfile
import time
import zipfile


def source_date_epoch(root):
    if os.environ.get("SOURCE_DATE_EPOCH"):
        return int(os.environ["SOURCE_DATE_EPOCH"])
    try:
        out = subprocess.run(["git", "-C", root, "log", "-1", "--format=%ct"],
                             capture_output=True, text=True, check=True).stdout
        return int(out.strip())
    except (OSError, subprocess.CalledProcessError, ValueError):
        return 0


def entries(parent, name):
    """Every path under parent/name, directories first within each level, sorted."""
    paths = [name]
    for dirpath, dirnames, filenames in os.walk(os.path.join(parent, name)):
        dirnames.sort()
        rel = os.path.relpath(dirpath, parent)
        for d in dirnames:
            paths.append(os.path.join(rel, d))
        for f in sorted(filenames):
            paths.append(os.path.join(rel, f))
    return sorted(paths, key=lambda p: p.split(os.sep))


def write_tar_gz(parent, name, out, epoch):
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.PAX_FORMAT) as tar:
        for rel in entries(parent, name):
            info = tar.gettarinfo(os.path.join(parent, rel), arcname=rel.replace(os.sep, "/"))
            info.mtime = epoch
            info.uid = info.gid = 0
            info.uname = info.gname = ""
            info.pax_headers = {}
            if info.isfile():
                with open(os.path.join(parent, rel), "rb") as f:
                    tar.addfile(info, f)
            else:
                tar.addfile(info)
    with open(out, "wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0, compresslevel=9) as gz:
            gz.write(buf.getvalue())


def write_zip(parent, name, out, epoch):
    # ZIP stores local time with two-second resolution from 1980 on.
    stamp = max(epoch, 315532800)
    date_time = time.gmtime(stamp)[:6]
    with zipfile.ZipFile(out, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for rel in entries(parent, name):
            path = os.path.join(parent, rel)
            arc = rel.replace(os.sep, "/")
            if os.path.isdir(path):
                info = zipfile.ZipInfo(arc + "/", date_time)
                info.external_attr = (0o40755 << 16) | 0x10
                zf.writestr(info, b"")
            else:
                info = zipfile.ZipInfo(arc, date_time)
                info.external_attr = (os.stat(path).st_mode & 0xFFFF) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(path, "rb") as f:
                    zf.writestr(info, f.read())


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    parent, name, out = sys.argv[1:]
    epoch = source_date_epoch(os.path.dirname(os.path.abspath(__file__)))
    if out.endswith(".tar.gz"):
        write_tar_gz(parent, name, out, epoch)
    elif out.endswith(".zip"):
        write_zip(parent, name, out, epoch)
    else:
        sys.exit("deterministic_archive.py: the archive must end in .tar.gz or .zip")


if __name__ == "__main__":
    main()
