"""Measure the Linux GUI executable until its first mapped main window on a private X11 display."""

import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import time


def measure(executable, root, count, open_sample):
    config = root / "config" / "aegisub"
    config.mkdir(parents=True)
    (config / "config.json").write_text(json.dumps({
        "App": {"First Start": False, "Language": "en_US", "Auto": {"Check For Updates": False}},
    }))
    env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / "config"),
               XDG_DATA_HOME=str(root / "data"), XDG_STATE_HOME=str(root / "state"),
               XDG_CACHE_HOME=str(root / "cache"), GDK_BACKEND="x11")
    # The caller supplies a dedicated Xvfb and session bus (libportal needs one).
    env.pop("WAYLAND_DISPLAY", None)
    command = [executable]
    title = r'[^"\n]+'
    if open_sample:
        subtitle = root / "startup-check.srt"
        subtitle.write_text("1\n00:00:00,000 --> 00:00:01,000\nexample\n", encoding="utf-8")
        command.append(subtitle)
        title = re.escape(subtitle.name)
    samples = []
    for _ in range(count):
        start = time.perf_counter()
        log = root / "startup.log"
        tree = ""
        with log.open("w") as output, subprocess.Popen(command, env=env, cwd=root,
                                                      stdout=output, stderr=output) as app:
            try:
                while app.poll() is None and time.perf_counter() - start < 15:
                    tree = subprocess.check_output(["xwininfo", "-root", "-tree"], env=env, text=True)
                    match = re.search(r'(0x[\da-f]+) "' + title + r' - Aegisub [^"\n]*"', tree)
                    if match:
                        details = subprocess.check_output(["xwininfo", "-id", match[1]], env=env, text=True)
                        if "Map State: IsViewable" in details:
                            elapsed = (time.perf_counter() - start) * 1000
                            status = Path(f"/proc/{app.pid}/status").read_text()
                            hwm = int(re.search(r"^VmHWM:\s+(\d+)", status, re.M)[1])
                            samples.append(dict(mapped_ms=round(elapsed), peak_kib=hwm))
                            break
                    time.sleep(.01)
                else:
                    raise RuntimeError(f"window unavailable, exit={app.poll()}: {log.read_text()[-1500:]}\n{tree[-1300:]}")
            finally:
                if app.poll() is None:
                    app.terminate()
                    try:
                        app.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        app.kill()
    return samples


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--count", type=int, default=5)
    parser.add_argument("--open-sample", action="store_true", help="wait for the generated SRT to appear in the window title")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="aeg-startup-probe-") as temporary:
        print(json.dumps(measure(args.executable.resolve(), Path(temporary), args.count, args.open_sample)))
