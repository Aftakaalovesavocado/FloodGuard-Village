#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
BUILD_SHARE.example.py — template generik rebuild folder share.

Versi asli generator ini disimpan di luar repo publik karena memuat
pemetaan anonimisasi project. File ini sengaja tidak memuat nilai
kredensial / data personal apa pun; hanya kerangka whitelist + pola
placeholder generik.

Pakai:
    FGV_SRC="/path/ke/proyek" FGV_OUT="/path/ke/github_share" python BUILD_SHARE.example.py
"""
from __future__ import annotations
import hashlib
import json
import os
import re
import shutil
import time
from pathlib import Path

SRC = Path(os.environ.get("FGV_SRC", ".")).resolve()
OUT = Path(os.environ.get("FGV_OUT", "./github_share")).resolve()

PRESERVE = [
    "README.md",
    "MANIFEST.md",
    ".gitignore",
    "LICENSE",
    "config/secrets.example.h",
]

COPY_JOBS = [
    ("firmware", "firmware"),
    ("sim", "sim"),
    ("hardware_poc", "hardware_poc"),
    ("dashboard/fgv-dashboard", "dashboard/fgv-dashboard"),
    ("dashboard/flood-dashboard-server", "dashboard/flood-dashboard-server"),
]

DOC_FILES = [
    "docs/ARSITEKTUR_DAN_PROTOKOL.md",
    "docs/KALIBRASI_BMKG.md",
    "docs/KALIBRASI_MANNING.md",
    "docs/BOM_PENGADAAN.md",
    "docs/DESAIN_TESTBED.md",
    "docs/PLAN_ROADMAP.md",
    "docs/LISTING_MEDIAN.md",
]

TEXT_EXT = {".md", ".ino", ".h", ".cpp", ".js", ".json", ".html", ".css", ".py", ".txt", ".csv", ".yaml", ".yml"}

GENERIC_REPLACEMENTS = [
    (re.compile(r'(WIFI_SSID\s*=\s*")[^"]*(")'), r"\1GANTI_DENGAN_SSID_KAMU\2"),
    (re.compile(r'(WIFI_PASS\s*=\s*")[^"]*(")'), r"\1GANTI_DENGAN_PASSWORD_KAMU\2"),
    (re.compile(r'https://[A-Za-z0-9-]+\.firebaseio\.com'), "https://GANTI_DENGAN_PROJECT_ID.firebaseio.com"),
    (re.compile(r'\b[0-9]{2}\.[0-9]{2}\.[0-9]{2}\.[0-9]{4}\b'), "00.00.00.0000"),
]


def sanitize(text: str) -> str:
    for pat, rep in GENERIC_REPLACEMENTS:
        text = pat.sub(rep, text)
    return text


def main() -> None:
    print(f"SRC={SRC}")
    print(f"OUT={OUT}")
    OUT.mkdir(parents=True, exist_ok=True)

    for src_rel, dst_rel in COPY_JOBS:
        src, dst = SRC / src_rel, OUT / dst_rel
        if not src.exists():
            print(f"skip: {src_rel}")
            continue
        if src.is_dir():
            shutil.copytree(src, dst, dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns("__pycache__", "*.pyc", ".firebase", ".firebaserc"))
        else:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)

    for rel in DOC_FILES:
        src = SRC / rel
        if src.exists():
            (OUT / "docs").mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, OUT / "docs" / Path(rel).name)

    for p in OUT.rglob("*"):
        if p.is_file() and p.suffix.lower() in TEXT_EXT:
            try:
                raw = p.read_text(encoding="utf-8")
            except UnicodeDecodeError:
                continue
            new = sanitize(raw)
            if new != raw:
                p.write_text(new, encoding="utf-8")

    entries = []
    for p in sorted(x for x in OUT.rglob("*") if x.is_file() and x.name != "MANIFEST.json"):
        entries.append({
            "path": p.relative_to(OUT).as_posix(),
            "size_bytes": p.stat().st_size,
            "sha256": hashlib.sha256(p.read_bytes()).hexdigest(),
        })
    manifest = {
        "package": "FloodGuard Village (FGV)",
        "generated_at": time.strftime("%Y-%m-%d %H:%M:%S %z"),
        "files": entries,
    }
    (OUT / "MANIFEST.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"files={len(entries)}")
    print("DONE")


if __name__ == "__main__":
    main()