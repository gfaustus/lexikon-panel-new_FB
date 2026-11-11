#!/usr/bin/env python3
"""Compile the complete driver against an instrumented kernel API fixture."""

import os
from pathlib import Path
import re
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent


def main():
    source = (ROOT / "mddi_lexikon_wvga.c").read_text()
    with tempfile.TemporaryDirectory(prefix="lexikon-backlight-") as directory:
        directory = Path(directory)
        includes = directory / "include"
        for name in re.findall(r'^#include [<"]([^>"]+)[>"]', source, re.MULTILINE):
            header = includes / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text("/* API fixture supplied by kernel_stubs.h. */\n")
        binary = directory / "backlight_test"
        subprocess.run(
            [
                os.environ.get("CC", "cc"),
                "-std=gnu89",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-Wno-unused-parameter",
                "-Wno-unused-function",
                "-g",
                "-O1",
                "-fsanitize=address,undefined",
                "-fno-omit-frame-pointer",
                "-I" + str(includes),
                str(ROOT / "tests/backlight_test.c"),
                "-o",
                str(binary),
            ],
            check=True,
        )
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
