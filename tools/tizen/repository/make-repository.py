#!/usr/bin/env python3
"""Turns the add-on zips of a Tizen build into a Kodi add-on repository.

The repository is a directory to serve over HTTP(S):
  addons.xml, addons.xml.md5        what the repository has
  <id>/<id>-<version>.zip           each add-on, its libraries stripped
  <id>/icon.png, <id>/fanart.jpg    shown before an add-on is installed
  repository.kodi-tizen/...         the add-on that points Kodi at the repository
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import stat
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

PLATFORM = "tizen-armv7"


def addon_xml(data: bytes) -> ET.Element:
    return ET.fromstring(data)


def platforms(root: ET.Element) -> list[str]:
    tags = []
    for element in root.iter("platform"):
        tags += (element.text or "").split()
    return tags


def strip_zip(source: Path, target: Path, strip: str) -> None:
    """Copies an add-on zip with its shared libraries stripped of debugging symbols.

    Symbolic links to a library are left out: Kodi does not make links when it extracts
    a zip, and loads the library by the name its addon.xml gives.
    """
    with tempfile.TemporaryDirectory() as tmp, zipfile.ZipFile(source) as zin, \
            zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as zout:
        for info in zin.infolist():
            if stat.S_ISLNK(info.external_attr >> 16):
                continue
            data = zin.read(info)
            if ".so" in Path(info.filename).name and not info.is_dir():
                lib = Path(tmp) / "lib"
                lib.write_bytes(data)
                subprocess.run([strip, "--strip-unneeded", str(lib)], check=True)
                data = lib.read_bytes()
            out = zipfile.ZipInfo(info.filename, info.date_time)
            out.external_attr = info.external_attr
            out.compress_type = zipfile.ZIP_DEFLATED
            zout.writestr(out, data)


def copy_art(zip_path: Path, addon_id: str, root: ET.Element, target: Path) -> None:
    """Puts the icon and fanart next to the zip, where Kodi looks for them."""
    with zipfile.ZipFile(zip_path) as z:
        names = set(z.namelist())
        for asset in root.iter("assets"):
            for element in asset:
                if element.tag not in ("icon", "fanart") or not element.text:
                    continue
                name = f"{addon_id}/{element.text}"
                if name in names:
                    dest = target / element.text
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    dest.write_bytes(z.read(name))


def repository_zip(template: Path, url: str, out: Path) -> ET.Element:
    """Builds the repository add-on from its template."""
    text = (template / "addon.xml.in").read_text().replace("@URL@", url)
    root = addon_xml(text.encode())
    addon_id = root.get("id")
    version = root.get("version")
    target = out / addon_id
    target.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(target / f"{addon_id}-{version}.zip", "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr(f"{addon_id}/addon.xml", text)
        for path in sorted(template.rglob("*")):
            if path.is_file() and path.name != "addon.xml.in":
                z.write(path, f"{addon_id}/{path.relative_to(template)}")
                if path.name in ("icon.png", "fanart.jpg"):
                    shutil.copy(path, target / path.name)
    return root


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", required=True, help="where the repository is served")
    parser.add_argument("--zips", required=True, type=Path, help="PACKAGE_DIR of the build")
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--strip", default="strip")
    parser.add_argument("--repository", required=True, type=Path,
                        help="template of the repository add-on")
    args = parser.parse_args()

    entries = []
    for zip_path in sorted(args.zips.glob("*/*.zip")):
        with zipfile.ZipFile(zip_path) as z:
            name = next((n for n in z.namelist() if n.count("/") == 1 and n.endswith("/addon.xml")),
                        None)
            if not name:
                print(f"skipped {zip_path.name}: no addon.xml", file=sys.stderr)
                continue
            root = addon_xml(z.read(name))
        addon_id, version = root.get("id"), root.get("version")
        if PLATFORM not in platforms(root):
            print(f"skipped {addon_id}: built for {platforms(root)}, not {PLATFORM}",
                  file=sys.stderr)
            continue
        target = args.out / addon_id
        shutil.rmtree(target, ignore_errors=True)
        target.mkdir(parents=True)
        strip_zip(zip_path, target / f"{addon_id}-{version}.zip", args.strip)
        copy_art(zip_path, addon_id, root, target)
        entries.append(root)
        print(f"{addon_id} {version}")

    entries.append(repository_zip(args.repository, args.url, args.out))

    index = ET.Element("addons")
    index.extend(entries)
    if hasattr(ET, "indent"):
        ET.indent(index)
    data = b'<?xml version="1.0" encoding="UTF-8" standalone="yes"?>\n' + \
        ET.tostring(index, encoding="utf-8")
    (args.out / "addons.xml").write_bytes(data)
    (args.out / "addons.xml.md5").write_text(hashlib.md5(data).hexdigest())
    print(f"{len(entries)} add-ons in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
