"""Best-effort simulator shutdown with a hard command deadline for CI cleanup."""
import subprocess
import sys


def shutdown(udid, run=subprocess.run):
    try:
        result = run(["xcrun", "simctl", "shutdown", udid], capture_output=True, text=True, timeout=30)
    except subprocess.TimeoutExpired:
        print(f"::warning::Simulator {udid} shutdown exceeded 30 seconds; continuing after completed test")
        return
    if result.returncode:
        print(f"::warning::Simulator shutdown returned {result.returncode}: {result.stderr.strip()}")
    else:
        print(f"Simulator {udid} shutdown completed")


if __name__ == "__main__":
    shutdown(sys.argv[1])
