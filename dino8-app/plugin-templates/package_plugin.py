#!/usr/bin/env python3
"""Package a built Dino 8 plug-in for distribution.

    python3 package_plugin.py --lib build/my_plugin.so --manifest manifest.json \
        --url https://example.org/dl/my_plugin-1.0.0.so --out dist [--api-version 2] [--min-app-version 0.1.0] [--tags a,b]

Writes, into --out:
  <slug>-<version>/manifest.json + <slug>-<version>/<library>   the folder layout the Package Manager installs
  <slug>-<version>.index-entry.json                              a marketplace index entry (schema_version 1) with
                                                                 the library's real SHA-256, ready to paste into
                                                                 the "plugins" array of your plug-in index.
The library's file name stem becomes the marketplace id; the manifest supplies name/version/author/description.
Standard library only.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lib", required=True)
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--url", required=True, help="where the library will be hosted (http:// or https://)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--api-version", type=int, default=2, help="DINO8_PLUGIN_API_VERSION the library was built against")
    ap.add_argument("--min-app-version", default="0.1.0")
    ap.add_argument("--tags", default="")
    ap.add_argument("--homepage", default="")
    args = ap.parse_args(argv)

    if not (args.url.startswith("http://") or args.url.startswith("https://")):
        ap.error("--url must be http:// or https:// (the app's marketplace only fetches those)")
    with open(args.manifest, encoding="utf-8") as f:
        man = json.load(f)
    for k in ("name", "version"):
        if not man.get(k):
            ap.error("manifest.json is missing %r" % k)
    lib_name = os.path.basename(args.lib)
    slug = os.path.splitext(lib_name)[0]
    with open(args.lib, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()

    pkg = "%s-%s" % (slug, man["version"])
    pkg_dir = os.path.join(args.out, pkg)
    os.makedirs(pkg_dir, exist_ok=True)
    shutil.copyfile(args.lib, os.path.join(pkg_dir, lib_name))
    shutil.copyfile(args.manifest, os.path.join(pkg_dir, "manifest.json"))

    entry = {
        "id": slug,
        "name": man["name"],
        "version": man["version"],
        "description": man.get("description", ""),
        "author": man.get("author", ""),
        "api_version": args.api_version,
        "min_app_version": args.min_app_version,
        "download_url": args.url,
        "sha256": digest,
    }
    if args.homepage:
        entry["homepage"] = args.homepage
    tags = [t.strip() for t in args.tags.split(",") if t.strip()]
    if tags:
        entry["tags"] = tags
    entry_path = os.path.join(args.out, pkg + ".index-entry.json")
    with open(entry_path, "w", encoding="utf-8") as f:
        json.dump(entry, f, indent=2)
        f.write("\n")
    print("packaged %s  sha256=%s" % (pkg_dir, digest))
    print("index entry: %s" % entry_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
