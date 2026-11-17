#!/usr/bin/env python3
"""Checks run on Linux for errors CI otherwise finds only on macOS or Windows.

  libcxx      parse Aegisub's own sources with clang and libc++, as macOS builds
              them, rather than GCC and libstdc++ (-fsyntax-only, nothing linked)
  winmacros   find names in code built everywhere that windows.h makes macros
              of, such as ChooseFont, which it makes ChooseFontW
  cmakecmds   find, in the subprojects Meson builds with CMake, custom commands
              written for a shell, which Meson does not run them in: >>, <,
              pipes, and quotes within arguments
  headless    run the test suites as CI's runners have them: a virtual display
              of 1024x768 and no audio device; though the ALSA player opens
              even then, as it opens the device only once it plays, so that
              Windows CI's having no player at all, which closes the audio,
              is not reproduced here
  all         the four, in that order (the default)

Usage: tools/ci-precheck.py [check] [-B BUILD_DIR] [-j N] [--since REF] [--files REGEX]

The build directory (default build-ci) must be configured and built once, as
libcxx reads its compile_commands.json and the dependency files beside its
objects, and needs the headers it generates.
"""

import argparse
import concurrent.futures
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import time
from pathlib import Path

OWN_DIRS = ("src/", "libaegisub/", "tests/", "automation/")
CXX_SUFFIXES = (".cpp", ".cc", ".cxx")
MINGW_INCLUDE = Path("/usr/x86_64-w64-mingw32/include")

# Macros windows.h defines that are not A/W pairs and are words code might use.
# min and max are left out, as meson.build defines NOMINMAX on Windows.
WINDOWS_WORD_MACROS = {
    "near": "minwindef.h", "far": "minwindef.h",
    "IN": "minwindef.h", "OUT": "minwindef.h", "OPTIONAL": "minwindef.h",
    "CALLBACK": "minwindef.h", "small": "rpcndr.h", "interface": "combaseapi.h",
    "ERROR": "wingdi.h", "TRANSPARENT": "wingdi.h", "OPAQUE": "wingdi.h",
    "ABSOLUTE": "wingdi.h", "RELATIVE": "wingdi.h", "DELETE": "winnt.h",
    "IGNORE": "winbase.h", "INFINITE": "winbase.h", "NO_ERROR": "winerror.h",
    "GetCurrentTime": "winbase.h", "Yield": "winbase.h",
}

WINDOWS_CONDITION = re.compile(r"\b(_WIN32|_WIN64|WIN32|__WXMSW__|_MSC_VER|__MINGW32__|__CYGWIN__)\b")

COLOUR = sys.stdout.isatty()


def say(text, colour=None):
    if COLOUR and colour:
        codes = {"red": "31", "green": "32", "yellow": "33", "bold": "1"}
        text = f"\033[{codes[colour]}m{text}\033[0m"
    print(text, flush=True)


def heading(text):
    say(f"\n== {text}", "bold")


# --- the build's own files ---------------------------------------------------

class Build:
    def __init__(self, root, build_dir):
        self.root = root
        self.dir = build_dir
        database = build_dir / "compile_commands.json"
        if not database.exists():
            sys.exit(f"{database} is missing: configure the build directory first (meson setup {build_dir.name})")
        self.units = {}
        for entry in json.loads(database.read_text()):
            directory = Path(entry["directory"])
            source = Path(os.path.normpath(directory / entry["file"]))
            rel = self.relative(source)
            if rel is None or not rel.startswith(OWN_DIRS) or not rel.endswith(CXX_SUFFIXES):
                continue
            # The same source built into two targets is parsed once.
            self.units.setdefault(rel, (directory, shlex.split(entry["command"])))

    def relative(self, path):
        try:
            return path.relative_to(self.root).as_posix()
        except ValueError:
            return None

    def dependencies(self, rel):
        """The repository's files a unit included when last built, from ninja's log of them,
        as ninja deletes the dependency files the compiler writes once it has read them."""
        if not hasattr(self, "_deps"):
            self._deps = {}
            out = subprocess.run(["ninja", "-C", str(self.dir), "-t", "deps"], capture_output=True, text=True).stdout
            current = None
            known = {}
            root = str(self.root) + os.sep
            for line in out.splitlines():
                if line and not line[0].isspace():
                    current = self._deps.setdefault(line.split(":", 1)[0], set())
                elif current is not None and (token := line.strip()):
                    if token not in known:
                        path = os.path.normpath(os.path.join(self.dir, token))
                        known[token] = path[len(root):] if path.startswith(root) else None
                    if known[token] is not None:
                        current.add(known[token])
        directory, args = self.units[rel]
        objects = [args[i + 1] for i, arg in enumerate(args[:-1]) if arg == "-o"]
        return self._deps.get(objects[0], set()) if objects else set()


def changed_files(root, since):
    out = subprocess.run(["git", "diff", "--name-only", since], cwd=root, capture_output=True, text=True, check=True).stdout
    untracked = subprocess.run(["git", "ls-files", "--others", "--exclude-standard"], cwd=root,
                               capture_output=True, text=True, check=True).stdout
    return set(out.split()) | set(untracked.split())


def select_units(build, args):
    units = sorted(build.units)
    if args.files:
        pattern = re.compile(args.files)
        units = [u for u in units if pattern.search(u)]
    if args.since:
        changed = changed_files(build.root, args.since)
        units = [u for u in units if u in changed or build.dependencies(u) & changed]
    return units


# --- libcxx --------------------------------------------------------------------

def clang_command(args, source_rel, root):
    """The build's GCC command made a clang/libc++ parse of the same unit."""
    i = 0
    # Wrappers like sccache, then the compiler itself.
    while i < len(args) and Path(args[i]).name in ("sccache", "ccache"):
        i += 1
    i += 1
    out = ["clang++", "-stdlib=libc++", "-fsyntax-only", "-ferror-limit=30", "-Wno-unknown-warning-option",
           "-fcolor-diagnostics" if COLOUR else "-fno-color-diagnostics"]
    skip_next = False
    for arg in args[i:]:
        if skip_next:
            skip_next = False
            continue
        if arg in ("-o", "-MQ", "-MF", "-MT"):
            skip_next = True
            continue
        if arg in ("-c", "-MD", "-MMD", "-Winvalid-pch") or arg.startswith("-fdiagnostics-color"):
            continue
        # libc++'s C headers have to come before the system's, which -I/usr/include would put first.
        if arg in ("-I/usr/include", "-I/usr/include/x86_64-linux-gnu"):
            continue
        if arg.endswith(CXX_SUFFIXES) and not arg.startswith("-"):
            continue
        out.append(arg)
    out.append(str(root / source_rel))
    return out


def check_libcxx(build, args):
    heading("libcxx: Aegisub's sources parsed with clang and libc++, as macOS builds them")
    units = select_units(build, args)
    if not units:
        say("no units to check")
        return True
    say(f"{len(units)} units, {args.jobs} at a time")

    def parse(rel):
        directory, command = build.units[rel]
        started = time.monotonic()
        result = subprocess.run(clang_command(command, rel, build.root), cwd=directory, capture_output=True, text=True)
        return rel, result.returncode, result.stderr, time.monotonic() - started

    failed = []
    warned = 0
    started = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for n, (rel, code, stderr, took) in enumerate(pool.map(parse, units), 1):
            warnings = stderr.count("warning: ")
            warned += warnings
            if code != 0:
                failed.append(rel)
                say(f"[{n}/{len(units)}] FAILED {rel}", "red")
                print(stderr.rstrip())
            else:
                note = f", {warnings} warnings" if warnings else ""
                print(f"[{n}/{len(units)}] ok {rel} ({took:.0f} s{note})", flush=True)
                if args.show_warnings and warnings:
                    print(stderr.rstrip())
    minutes = (time.monotonic() - started) / 60
    if failed:
        say(f"libcxx: {len(failed)} of {len(units)} units do not parse ({minutes:.1f} min)", "red")
        for rel in failed:
            print(f"  {rel}")
        return False
    say(f"libcxx: all {len(units)} units parse ({warned} warnings, which --show-warnings shows; {minutes:.1f} min)", "green")
    return True


# --- winmacros -------------------------------------------------------------------

def windows_macros(include_dir):
    """Names windows.h's headers make A/W macros of, with the header defining each."""
    if not include_dir.is_dir():
        sys.exit(f"{include_dir} is missing: install mingw-w64-x86-64-dev, or pass --mingw-include")
    aw = re.compile(r"^\s*#\s*define\s+(\w+)\s+(?:__MINGW_NAME_(?:U?AW|AW_EXT)\(\s*(\w+)|(\w+)W\s*$)", re.M)
    names = {}
    for header in include_dir.rglob("*.h"):
        text = header.read_text(errors="replace")
        for m in aw.finditer(text):
            name = m.group(1)
            if (m.group(2) or m.group(3)) == name:
                names.setdefault(name, header.relative_to(include_dir).as_posix())
    for name, header in WINDOWS_WORD_MACROS.items():
        names.setdefault(name, header)
    return names


def undefined_names(paths):
    """Names something #undefs, which code after it may use whatever windows.h made of them."""
    undef = re.compile(r"^\s*#\s*undef\s+(\w+)", re.M)
    names = set()
    for path in paths:
        if path.exists():
            names.update(undef.findall(path.read_text(errors="replace")))
    return names


def strip_code(text):
    """Comments, strings and characters blanked, lines kept, so that only code's names remain."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif c == "R" and text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if not m:
                out.append(c)
                i += 1
                continue
            end = text.find(")" + m.group(1) + '"', i)
            j = n if end < 0 else end + len(m.group(1)) + 2
            out.append('""' + "\n" * text.count("\n", i, j))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c and text[j] != "\n":
                j += 2 if text[j] == "\\" else 1
            out.append(c + c)
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def scan_for_macros(rel, text, macros):
    """(line, column, name) for each macro name used outside code only Windows builds."""
    hits = []
    stack = []  # per #if: [this branch is Windows-only, the #else branch would be]
    word = re.compile(r"\b[A-Za-z_]\w*\b")
    for number, line in enumerate(strip_code(text).split("\n"), 1):
        directive = re.match(r"\s*#\s*(\w+)\s*(.*)", line)
        if directive:
            kind, condition = directive.groups()
            windows = bool(WINDOWS_CONDITION.search(condition))
            negated = windows and bool(re.search(r"!\s*(?:defined\s*\(?\s*)?" + WINDOWS_CONDITION.pattern, condition))
            if kind in ("if", "ifdef"):
                stack.append([windows and not negated, windows and negated])
            elif kind == "ifndef":
                stack.append([False, windows])
            elif kind == "elif" and stack:
                stack[-1] = [windows and not negated, False]
            elif kind == "else" and stack:
                stack[-1] = [stack[-1][1], False]
            elif kind == "endif" and stack:
                stack.pop()
            continue
        if any(frame[0] for frame in stack):
            continue
        for m in word.finditer(line):
            if m.group(0) in macros:
                hits.append((number, m.start() + 1, m.group(0)))
    return hits


def check_winmacros(build, args):
    heading("winmacros: names windows.h makes macros of, in code every platform builds")
    macros = windows_macros(args.mingw_include)
    units = select_units(build, args)
    files = set(units)
    for rel in units:
        files |= {d for d in build.dependencies(rel) if d.startswith(OWN_DIRS)}
    files = sorted(f for f in files if (build.root / f).exists())
    wx_undef = build.root / "subprojects/wxWidgets/include/wx/msw/winundef.h"
    handled = undefined_names([build.root / f for f in files] + [wx_undef])
    say(f"{len(macros)} macros from {args.mingw_include}, {len(files)} files, which include on Linux what they need")

    risky = []
    covered = 0
    for rel in files:
        for line, column, name in scan_for_macros(rel, (build.root / rel).read_text(errors="replace"), macros):
            if name in handled:
                covered += 1
            else:
                risky.append((rel, line, column, name))
    note = f"{covered} more uses of names that wx's winundef.h or Aegisub's own code #undefs are left out"
    if risky:
        say(f"winmacros: {len(risky)} uses of names windows.h redefines, which on Windows become other names "
            f"in units that include it and stay themselves in those that do not:", "red")
        for rel, line, column, name in risky:
            print(f"  {rel}:{line}:{column}: {name} (a macro in {macros[name]})")
        print(f"  ({note})")
        return False
    say(f"winmacros: none ({note})", "green")
    return True


# --- cmakecmds -----------------------------------------------------------------------

CUSTOM_COMMAND_KEYWORDS = {
    "COMMAND", "OUTPUT", "DEPENDS", "BYPRODUCTS", "WORKING_DIRECTORY", "COMMENT", "VERBATIM", "MAIN_DEPENDENCY",
    "IMPLICIT_DEPENDS", "DEPFILE", "JOB_POOL", "USES_TERMINAL", "COMMAND_EXPAND_LISTS", "APPEND", "SOURCES", "ALL",
    "DEPENDS_EXPLICIT_ONLY", "CODEGEN", "JOB_SERVER_AWARE", "TARGET", "PRE_BUILD", "PRE_LINK", "POST_BUILD",
}

# Custom commands of parts of the CMake subprojects that Aegisub does not build.
CMAKE_COMMANDS_NOT_BUILT = {
    "subprojects/wxWidgets/src/expat/expat/CMakeLists.txt": "expat's tests and coverage, which wx does not build",
    "subprojects/wxWidgets/3rdparty/catch/CMake/FindLcov.cmake": "Catch's coverage report",
}


def cmake_arguments(text, open_paren):
    """(argument, quoted, line) of a CMake command's arguments, from its '(' to its ')'."""
    i, depth, out, n = open_paren + 1, 1, [], len(text)
    line = text.count("\n", 0, open_paren) + 1
    escapes = {"n": "\n", "t": "\t", "r": "\r"}
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif c.isspace():
            i += 1
        elif c == "#":
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif c == '"':
            j, value = i + 1, []
            while j < n and text[j] != '"':
                if text[j] == "\\" and j + 1 < n:
                    value.append(escapes.get(text[j + 1], text[j + 1]))
                    j += 2
                else:
                    value.append(text[j])
                    j += 1
            out.append(("".join(value), True, line))
            line += text.count("\n", i, j)
            i = j + 1
        elif c in "()":
            depth += 1 if c == "(" else -1
            i += 1
            if depth == 0:
                break
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] not in '()"#':
                j += 1
            out.append((text[i:j], False, line))
            i = j
    return out


def cmake_subprojects(root):
    """The directories of the subprojects meson.build builds with cmake.subproject()."""
    names = set(re.findall(r"cmake\.subproject\(\s*'([^']+)'", (root / "meson.build").read_text()))
    dirs = []
    for name in sorted(names):
        directory = name
        wrap = root / "subprojects" / f"{name}.wrap"
        if wrap.exists():
            m = re.search(r"^directory\s*=\s*(\S+)", wrap.read_text(), re.M)
            if m:
                directory = m.group(1)
        dirs.append(root / "subprojects" / directory)
    return dirs


def check_cmakecmds(build, args):
    heading("cmakecmds: CMake subprojects' custom commands, which Meson runs without a shell")
    found, left_out = [], []
    dirs = cmake_subprojects(build.root)
    for directory in dirs:
        if not directory.is_dir():
            say(f"{directory} is missing: configure once, so that Meson unpacks it", "yellow")
            continue
        for path in sorted(list(directory.rglob("CMakeLists.txt")) + list(directory.rglob("*.cmake"))):
            rel = path.relative_to(build.root).as_posix()
            text = path.read_text(errors="replace")
            for m in re.finditer(r"\badd_custom_(?:command|target)\s*\(", text, re.I):
                in_command = False
                for arg, quoted, line in cmake_arguments(text, m.end() - 1):
                    if not quoted and arg in CUSTOM_COMMAND_KEYWORDS:
                        in_command = arg == "COMMAND"
                        continue
                    if not in_command:
                        continue
                    why = None
                    if not quoted and arg.startswith(">>"):
                        why = "Meson writes the output of >> as of >, over what was there"
                    elif not quoted and arg in ("<", "|", "&&", "||", "2>", "2>&1", ";"):
                        why = f"no shell: the command is given {arg} as an argument"
                    elif '"' in arg:
                        why = "Meson drops the quotes within an argument"
                    if why:
                        (left_out if rel in CMAKE_COMMANDS_NOT_BUILT else found).append((rel, line, arg, why))
    names = ", ".join(d.name for d in dirs)
    note = f"{len(left_out)} more in parts not built are left out" if left_out else ""
    if found:
        say(f"cmakecmds: {len(found)} arguments in {names} that Meson does not run as a shell would:", "red")
        for rel, line, arg, why in found:
            shown = arg if len(arg) <= 40 else arg[:37] + "..."
            print(f"  {rel}:{line}: {shown!r}: {why}")
        if note:
            print(f"  ({note})")
        return False
    say(f"cmakecmds: none in {names}" + (f" ({note})" if note else ""), "green")
    return True


# --- headless ----------------------------------------------------------------------

def check_headless(build, args):
    heading("headless: the test suites on a 1024x768 virtual display with no audio device")
    if not args.no_build:
        say(f"building {build.dir.name}")
        if subprocess.run(["meson", "compile", "-C", str(build.dir)]).returncode != 0:
            say("headless: the build failed", "red")
            return False
    with tempfile.TemporaryDirectory(prefix="precheck-") as scratch:
        empty_alsa = Path(scratch) / "alsa.conf"
        empty_alsa.write_text("")
        env = dict(os.environ)
        for name in ("WAYLAND_DISPLAY", "DBUS_SESSION_BUS_ADDRESS"):
            env.pop(name, None)
        env.update({
            "GDK_BACKEND": "x11",
            # No sound device: PulseAudio, OpenAL and ALSA find none, though ALSA's
            # player opens anyway, as it looks for one only once it plays.
            "PULSE_SERVER": "unix:" + str(Path(scratch) / "no-pulse"),
            "PIPEWIRE_REMOTE": str(Path(scratch) / "no-pipewire"),
            "ALSA_CONFIG_PATH": str(empty_alsa),
            "ALSOFT_DRIVERS": "no-such-driver",
        })
        command = ["xvfb-run", "-a", "-s", f"-screen 0 {args.screen}x24",
                   "meson", "test", "-C", str(build.dir), "--suite", "Aegisub", "--print-errorlogs"]
        if args.tests:
            command += args.tests
        code = subprocess.run(command, env=env).returncode
    log = build.dir / "meson-logs" / "testlog.txt"
    if log.exists():
        skipped = list(dict.fromkeys(re.findall(r"\[  SKIPPED \] (\w+\.\w+)", log.read_text(errors="replace"))))
        if skipped:
            say(f"skipped here ({len(skipped)}):")
            for name in skipped:
                print(f"  {name}")
            print(f"  (the reasons are in {log})")
    if code != 0:
        say("headless: tests failed", "red")
        return False
    say("headless: the tests pass", "green")
    return True


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("check", nargs="?", default="all", choices=["all", "libcxx", "winmacros", "cmakecmds", "headless"])
    parser.add_argument("-B", "--build-dir", default="build-ci", help="build directory, from the repository (default build-ci)")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2, help="libcxx: parses at a time")
    parser.add_argument("--since", help="libcxx, winmacros: only units including a file changed since REF (git diff REF, and untracked files)")
    parser.add_argument("--files", help="libcxx, winmacros: only units whose path matches REGEX")
    parser.add_argument("--show-warnings", action="store_true", help="libcxx: print clang's warnings too")
    parser.add_argument("--mingw-include", type=Path, default=MINGW_INCLUDE, help=f"winmacros: Windows headers (default {MINGW_INCLUDE})")
    parser.add_argument("--screen", default="1024x768", help="headless: virtual screen size (default 1024x768, as Windows CI)")
    parser.add_argument("--no-build", action="store_true", help="headless: do not build first")
    parser.add_argument("tests", nargs="*", help="headless: meson test names (default: every test of suite Aegisub)")
    args = parser.parse_args()

    root = Path(subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True).stdout.strip())
    build = Build(root, (root / args.build_dir).resolve())

    checks = {"libcxx": check_libcxx, "winmacros": check_winmacros, "cmakecmds": check_cmakecmds,
              "headless": check_headless}
    order = ["winmacros", "cmakecmds", "libcxx", "headless"] if args.check == "all" else [args.check]
    results = {name: checks[name](build, args) for name in order}
    if len(results) > 1:
        heading("summary")
        for name, ok in results.items():
            say(f"  {name}: {'ok' if ok else 'FAILED'}", "green" if ok else "red")
    sys.exit(0 if all(results.values()) else 1)


if __name__ == "__main__":
    main()
