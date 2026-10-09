#!/usr/bin/env python3
"""Fetch source-only, commit-pinned official dependencies and verify Git blob IDs."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen


def blob_sha1(data):
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--check", action="store_true", help="verify existing sources without network access")
    args = parser.parse_args()
    lock = json.loads(Path(__file__).with_name("dependencies.json").read_text())
    jobs = [(dependency, entry) for dependency in lock["dependencies"] for entry in dependency["files"]]

    def fetch(job):
        dependency, entry = job
        path = args.output / dependency["name"] / entry["path"]
        expected = entry["git_blob_sha1"]
        if path.is_file() and blob_sha1(path.read_bytes()) == expected:
            return
        if args.check:
            raise RuntimeError(f"Missing or modified pinned source: {path}")
        url = f"https://raw.githubusercontent.com/{dependency['repository']}/{dependency['revision']}/{entry['path']}"
        with urlopen(url, timeout=60) as response:
            data = response.read()
        if blob_sha1(data) != expected:
            raise RuntimeError(f"Source hash mismatch: {url}")
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    with ThreadPoolExecutor(max_workers=4) as executor:
        list(executor.map(fetch, jobs))
    print(f"Verified {len(jobs)} commit-pinned source files")


if __name__ == "__main__":
    main()
