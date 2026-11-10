#!/usr/bin/env python3
"""Run a test executable with disposable fixtures, without the legacy setup scripts."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("executable", type=Path)
parser.add_argument("--output-root", type=Path, required=True)
args, test_args = parser.parse_known_args()
args.output_root.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix="isolated-", dir=args.output_root.resolve()))
fixtures = root / "fixtures"
shutil.copytree(Path(__file__).resolve().parent, fixtures,
                ignore=shutil.ignore_patterns("tests", "support", "__pycache__"))
shutil.copytree(Path(__file__).resolve().parent.parent / "automation" / "include",
                root / "automation" / "include")
data = root / "data"
for directory in ["dir", "dir_iterator", "options", "vfr/out", "keyframe"]:
    (data / directory).mkdir(parents=True, exist_ok=True)
for name, value in {"file": "", "ten_bytes": "1234567890", "touch_mod_time": "",
                    "dir_iterator/1.a": "", "dir_iterator/2.a": "", "dir_iterator/1.b": "", "dir_iterator/2.b": "",
                    "mru_ok.json": json.dumps({"Video": ["Entry One", "Entry Two"]}),
                    "mru_invalid.json": json.dumps({"Video": [1, 3]})}.items():
    (data / name).write_text(value, encoding="utf-8")
temporary = root / "temp"
temporary.mkdir()
env = dict(os.environ, AEGISUB_TEST_DATA_DIR=str(fixtures), TEMP=str(temporary), TMP=str(temporary), TMPDIR=str(temporary))
print("Isolated test directory:", root, flush=True)
raise SystemExit(subprocess.call([str(args.executable.resolve()), *test_args], cwd=root, env=env))
