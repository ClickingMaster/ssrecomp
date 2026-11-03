"""Capture simulator displays, including Duo devices requiring --display selection."""
from pathlib import Path
import re
import subprocess
import sys


def capture(udid, prefix, run=subprocess.run):
    prefix = Path(prefix)
    prefix.parent.mkdir(parents=True, exist_ok=True)
    def shot(display):
        path = prefix.with_name(prefix.name + f"-display-{display or 'default'}.png")
        command = ["xcrun", "simctl", "io", udid, "screenshot"]
        if display is not None:
            command.append(f"--display={display}")
        result = run(command + [str(path)], capture_output=True, text=True)
        return path, result
    path, result = shot(None)
    diagnostic = result.stdout + result.stderr
    prefix.with_name(prefix.name + "-screenshot.log").write_text(diagnostic, encoding="utf-8")
    if result.returncode == 0:
        return [path]
    displays = list(dict.fromkeys(re.findall(r"--display=(\d+)", diagnostic)))
    if "Cannot choose between" not in diagnostic or not displays:
        raise RuntimeError("Simulator screenshot failed: " + diagnostic.strip())
    paths = []
    for display in displays:
        path, result = shot(display)
        with prefix.with_name(prefix.name + "-screenshot.log").open("a", encoding="utf-8") as log:
            log.write(f"\nDisplay {display}:\n" + result.stdout + result.stderr)
        if result.returncode == 0:
            paths.append(path)
        else:
            print(f"Display {display} capture failed; see screenshot log", file=sys.stderr)
    if not paths:
        raise RuntimeError("No simulator display could be captured")
    return paths


if __name__ == "__main__":
    try:
        for path in capture(sys.argv[1], sys.argv[2]):
            print(path)
    except RuntimeError as error:
        raise SystemExit(str(error))
