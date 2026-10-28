"""Run with a completed MSVC/Ninja build from a Visual Studio developer shell.

Usage: python tests/run_native_preview_regression.py build
The test owns its windows and config; it does not inject operating-system input.
"""
import json
from pathlib import Path
import subprocess
import sys

build = Path(sys.argv[1]).resolve()
source = Path(__file__).with_name("native_preview_regression.cpp").resolve()
commands = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
compile = next(command for command in commands if command["file"].endswith("/dialog_rectangle.cpp"))
target = build / "native-preview-regression"
target.mkdir(exist_ok=True)
obj = "native-preview-regression/check.obj"
command = compile["command"].replace(compile["output"], obj).replace(compile["file"], f'"{source}"')
subprocess.run(command, cwd=build, check=True)
links = json.loads(subprocess.check_output(["ninja", "-C", str(build), "-t", "compdb", "-x"], text=True))
link = next(command["command"] for command in links if command["output"] == "aegisub.exe")
link = link.replace("/OUT:aegisub.exe", "/OUT:native-preview-regression/check.exe")
link = link.replace("/IMPLIB:aegisub.lib", "/IMPLIB:native-preview-regression/check.lib")
link = link.replace("/SUBSYSTEM:WINDOWS", "/SUBSYSTEM:CONSOLE")
(target / "check.rsp").write_text(link[link.index('"link"') + len('"link"'):] + f" {obj}", encoding="utf-8")
subprocess.run(["link", "@native-preview-regression/check.rsp"], cwd=build, check=True)
(target / "config.json").write_text(json.dumps({"App": {"First Start": False, "Language": "en_US", "Auto": {"Check For Updates": False, "Backup": False, "Save": False}}}), encoding="utf-8")
subprocess.run([str(target / "check.exe")], cwd=build, check=True, timeout=60)
