#!/usr/bin/env python3
"""Run the simulator's 1,100-book RetroInk Library and move-recovery fixture."""

from __future__ import annotations

import os
import subprocess
import tempfile
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
PROGRAM = ROOT / ".pio" / "build" / "simulator" / "program"
CAPTURE = ROOT / "artifacts" / "library-preview"


def main() -> int:
    if not PROGRAM.exists():
        print("Build first: pio run -e simulator", flush=True)
        return 2
    CAPTURE.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="retroink-library-test-") as temporary:
        root = Path(temporary)
        # Existing RetroInk installations already have this directory for
        # reading goals, so the first Library scan must accept it.
        (root / "fs_" / ".retroink").mkdir(parents=True)
        source = "/books/Genre 00/Author 00/Book 0000.txt"
        for number in range(1100):
            genre = number // 100
            author = (number // 20) % 5
            folder = root / "fs_" / "books" / f"Genre {genre:02d}" / f"Author {author:02d}"
            folder.mkdir(parents=True, exist_ok=True)
            title = f"Book {number:04d}.txt"
            if number in (1080, 1081):
                title = "A very long System 6 book title that should remain searchable and sortable " + "X" * 90 + f" {number}.txt"
            if number in (1000, 1020):
                title = "Shared Title.txt"
            if number == 1082:
                title = "# Notes 1082.txt"
            if number in (1098, 1099):
                title = f"Book {number:04d}.epub"
            if number == 1099:
                with zipfile.ZipFile(folder / title, "w") as epub:
                    epub.writestr("META-INF/container.xml", '<container><rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
                    epub.writestr("OEBPS/content.opf", '<package><metadata><dc:title xmlns:dc="http://purl.org/dc/elements/1.1/">The Retro Macintosh Reader</dc:title><dc:creator xmlns:dc="http://purl.org/dc/elements/1.1/">AltFlow</dc:creator></metadata></package>')
                continue
            (folder / title).write_text(f"Book fixture {number}\n", encoding="utf-8")
        env = os.environ.copy()
        env.update(
            CROSSINK_SIMULATOR_SMOKE_TEST="1",
            CROSSINK_SIMULATOR_LIBRARY_TEST="1",
            CROSSINK_SIMULATOR_LIBRARY_SOURCE=source,
            CROSSINK_SIMULATOR_SMOKE_THEME="7",
            CROSSINK_SIMULATOR_CAPTURE_DIR=str(CAPTURE),
            SDL_VIDEODRIVER="dummy",
        )
        result = subprocess.run(
            [str(PROGRAM)], cwd=root, env=env, text=True,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180,
        )
        print(result.stdout, end="")
        return 0 if result.returncode == 0 and "RetroInk Library 1,100-book test passed" in result.stdout else 2


if __name__ == "__main__":
    raise SystemExit(main())
