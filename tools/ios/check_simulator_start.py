"""Validate a simulator first start from the actual launcher log and screenshot.

Requires Pillow. This catches startup/renderer failures and blank or letterboxed
screens; screenshots remain artifacts for visual review of the game list.
"""
import re
import sys
from pathlib import Path

from PIL import Image


def check_start(log, screenshot):
    if re.search(r"\[launcher\] (?:no window:|window creation failed:|renderer initialisation failed:|drawing failed:)", log):
        raise ValueError("launcher reported a window, renderer or drawing failure")
    associations = re.findall(r"iOS window=(\d+) renderer-window=(\d+) association=(\w+)", log)
    if not associations or associations[-1][0] != associations[-1][1] or associations[-1][2] != "correct":
        raise ValueError("missing or incorrect launcher renderer/window association")
    window = associations[-1][0]
    frames = re.findall(r"frame presented window=(\d+) renderer=(\S+) canvas=(\d+)x(\d+) drawable=(\d+)x(\d+)", log)
    if not frames or frames[-1][0] != window or any(int(n) <= 0 for n in frames[-1][2:]):
        raise ValueError("no valid launcher frame was presented for its window")
    drawable = re.findall(r"drawable window=(\d+) points=(\d+)x(\d+) pixels=(\d+)x(\d+)", log)
    if not drawable or drawable[-1][0] != window or any(int(n) <= 0 for n in drawable[-1][1:]):
        raise ValueError("missing positive window and drawable dimensions")
    pixels = tuple(map(int, drawable[-1][-2:]))
    if pixels != tuple(map(int, frames[-1][-2:])):
        raise ValueError("latest drawable resize has not reached a presented frame")
    with Image.open(screenshot) as image:
        width, height = image.size
        # The CI first start uses a full-screen scene. Compare drawable pixels,
        # not the deliberately smaller software-composition canvas.
        if any(abs(actual - expected) > max(2, expected * 0.05)
               for actual, expected in zip(pixels, (width, height))):
            raise ValueError(f"drawable {pixels} does not fill screenshot {width}x{height}")
        # Exclude system chrome, the home indicator and rounded screen corners.
        centre = image.convert("RGB").crop((width // 10, height // 10, width * 9 // 10, height * 9 // 10))
        centre.thumbnail((256, 256))
        extrema = centre.getextrema()
        bright = sum(centre.convert("HSV").getchannel("V").histogram()[41:])
        if bright < max(1, centre.width * centre.height // 1000) or max(hi - lo for lo, hi in extrema) < 16:
            raise ValueError("simulator screenshot is blank or almost black")
    return f"PASS: window {window}, renderer {frames[-1][1]}, drawable {pixels[0]}x{pixels[1]}, nonblank full-screen screenshot"


if __name__ == "__main__":
    try:
        print(check_start(Path(sys.argv[1]).read_text(encoding="utf-8", errors="replace"), sys.argv[2]))
    except ValueError as error:
        raise SystemExit(str(error))
