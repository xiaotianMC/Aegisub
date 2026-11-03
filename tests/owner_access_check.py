#!/usr/bin/env python3
"""C1-C1c-10 live-model owner guard.

1. API scan of src/: the live AssFile may be named only by its owner accessor,
   the test attorney only by its friend declaration, and no subtitle model type
   may be const_cast.
2. Compile-failure cases: each forbidden snippet must be rejected by the real
   compiler with the exact flags of a production translation unit, while a
   control snippet using the permitted routes must compile with the same command
   (so a failure cannot come from bad flags).
"""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--source-root", type=Path, required=True)
parser.add_argument("--build-root", type=Path, required=True)
parser.add_argument("--reference-tu", default="src/subs_controller.cpp")
args = parser.parse_args()
failures = []


def strip_code(text):
    """Remove comments and string/char literals, keeping line numbers."""
    out, i, n = [], 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            while i < n and text[i] != "\n": i += 1
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2); j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j)); i = j
        elif text.startswith('R"', i) and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == "_")):
            m = re.match(r'R"([^(\s]*)\(', text[i:])
            if not m: out.append(c); i += 1; continue
            end = text.find(")" + m.group(1) + '"', i)
            end = n if end < 0 else end + len(m.group(1)) + 2
            out.append('""' + "\n" * text.count("\n", i, end)); i = end
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(c + c); i = j + 1
        else:
            out.append(c); i += 1
    return "".join(out)


# ---- 1. API scan ------------------------------------------------------------
src = args.source_root / "src"
member = re.compile(r"[\w)\]]\s*(->|\.)\s*ass\b")
casts = re.compile(r"const_cast\s*<\s*(const\s+)?(AssFile|AssDialogue|AssDialogueBase|AssStyle|AssInfo|AssAttachment|"
                   r"ExtradataEntry|ProjectProperties|ProjectMetadata|EntryList)\b")
attorney = "LiveSubtitleModelTestAccess"
allowed_member = {"src/subs_controller.cpp": 1}  # SubsController::Live(agi::Context const*)
member_hits = {}
for path in sorted(src.rglob("*")):
    if path.suffix not in (".cpp", ".h", ".hpp", ".cxx", ".mm"): continue
    rel = path.relative_to(args.source_root).as_posix()
    raw = path.read_text(encoding="utf-8", errors="replace")
    code = strip_code(raw)
    for lineno, line in enumerate(code.splitlines(), 1):
        if member.search(line):
            member_hits.setdefault(rel, []).append(lineno)
        if casts.search(line):
            failures.append(f"{rel}:{lineno}: const_cast of a subtitle model type")
    if attorney in raw and rel != "src/include/aegisub/context.h":
        failures.append(f"{rel}: names the test-only attorney {attorney}")
for rel, lines in member_hits.items():
    if len(lines) > allowed_member.get(rel, 0):
        failures.append(f"{rel}:{','.join(map(str, lines))}: accesses Context::ass outside the owner accessor")
owner = strip_code((args.source_root / "src/subs_controller.cpp").read_text(encoding="utf-8"))
if not re.search(r"AssFile&\s*SubsController::Live\(agi::Context const\*\s*c\)\s*noexcept\s*\{\s*return \*c->ass;\s*\}", owner):
    failures.append("src/subs_controller.cpp: the single permitted Context::ass access is not the owner accessor")
context_h = strip_code((src / "include/aegisub/context.h").read_text(encoding="utf-8"))
friends = sorted(re.findall(r"friend\s+(?:class|struct)\s+([:\w]+)\s*;", context_h))
if friends != ["::LiveSubtitleModelTestAccess", "::SubsController"]:
    failures.append(f"context.h: unexpected Context friends {friends}")
if not re.search(r"private:\s*(?:friend[^;]*;\s*)*std::unique_ptr<AssFile>\s+ass\s*;", context_h):
    failures.append("context.h: Context::ass is not declared in a private section")
attorney_def = sorted(p.relative_to(args.source_root).as_posix() for p in (args.source_root / "tests").rglob("*")
                      if p.is_file() and p.suffix in (".h", ".cpp") and re.search(r"struct\s+" + attorney + r"\s*\{", p.read_text(encoding="utf-8", errors="replace")))
if attorney_def != ["tests/support/live_model_test_access.h"]:
    failures.append(f"test attorney must be defined exactly once in tests/support: {attorney_def}")

# ---- 2. Compile-failure cases ----------------------------------------------
PRELUDE = r'''
#include "include/aegisub/context.h"
#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_style.h"
#include "selection_controller.h"
#include "subs_controller.h"
#include "subtitle_project_access.h"
#include <wx/string.h>
'''
CONTROL = r'''
void permitted(agi::Context& c) {
    AssFile const& live = c.subtitleAccess->Read();
    (void)live.Events.size();
    AssFile copy(live);                        // independent model stays mutable
    copy.Events.front().Text = "x";
    copy.AddExtradata("k", "v");
    copy.Commit(wxString("x"), AssFile::COMMIT_DIAG_TEXT);
    auto snapshot = c.subtitleAccess->Snapshot();
    snapshot->Events.clear();
    if (auto row = c.selectionController->ReadActiveLine()) (void)row->Id;
    if (auto scope = c.subtitleAccess->TryWrite()) {
        auto edit = c.subtitleAccess->Edit(scope->Lease());
        edit.SetDialogueField(1, &AssDialogueBase::Layer, 2);
    }
}
'''
NEGATIVE = {
    "context_ass_member": "void bad(agi::Context& c) { (void)c.ass; }",
    "context_ass_pointer": "void bad(agi::Context& c) { AssFile* p = c.ass.get(); (void)p; }",
    "read_bind_mutable": "void bad(agi::Context& c) { AssFile& f = c.subtitleAccess->Read(); (void)f; }",
    "read_write_row_field": 'void bad(agi::Context& c) { c.subtitleAccess->Read().Events.front().Text = "x"; }',
    "read_write_container": "void bad(agi::Context& c) { c.subtitleAccess->Read().Events.clear(); }",
    "read_commit": 'void bad(agi::Context& c) { c.subtitleAccess->Read().Commit(wxString("x"), 0); }',
    "read_add_extradata": 'void bad(agi::Context& c) { c.subtitleAccess->Read().AddExtradata("k", "v"); }',
    "read_properties": 'void bad(agi::Context& c) { c.subtitleAccess->Read().Properties.style_storage = "x"; }',
    "read_style_write": 'void bad(agi::Context& c) { c.subtitleAccess->Read().GetStyle("Default")->fontsize = 1; }',
    "selection_active_write": 'void bad(agi::Context& c) { c.selectionController->ReadActiveLine()->Text = "x"; }',
    "selection_set_write": "void bad(agi::Context& c) { for (auto row : c.selectionController->ReadSelectedSet()) row->Start = 0; }",
    "selection_legacy_getter": "void bad(agi::Context& c) { (void)c.selectionController->GetActiveLine(); }",
    "forge_write_access": "void bad() { SubtitleProjectAccess::WriteAccess w; (void)w; }",
    "forge_write_scope": "void bad() { SubtitleProjectAccess::WriteScope w; (void)w; }",
    "forge_interaction": "void bad() { SubtitleProjectAccess::Interaction i; (void)i; }",
    "owner_static_accessor": "void bad(agi::Context& c) { (void)SubsController::Live(&c); }",
    "owner_member_accessor": "void bad(agi::Context& c) { (void)c.subsController->Live(); }",
    "legacy_rows_for_write": "void bad(agi::Context& c) { (void)c.subtitleAccess->RowsForWrite(); }",
    "live_history_writer": "void bad(agi::Context& c) { (void)c.subtitleAccess->Read().AddUndoManager([](AssFileCommit const&) {}); }",
}

compile_commands = args.build_root / "compile_commands.json"
if not compile_commands.exists():
    failures.append(f"{compile_commands} missing: cannot run compile-failure cases")
else:
    entries = [e for e in json.loads(compile_commands.read_text(encoding="utf-8"))
               if Path(e["file"]).as_posix().endswith(args.reference_tu)]
    if not entries:
        failures.append(f"no compile command for {args.reference_tu}")
    else:
        entry = entries[0]
        command = entry.get("arguments")
        if not command:
            command = shlex.split(entry["command"], posix=os.name != "nt")
            if os.name == "nt":  # non-POSIX split keeps quotes; subprocess re-quotes as needed
                command = [a[1:-1].replace('\\"', '"') if len(a) > 1 and a[0] == a[-1] == '"' else a for a in command]
        msvc = Path(command[0]).stem.lower() in ("cl", "clang-cl")
        base, skip = [], False
        for a in command:
            if skip: skip = False; continue
            if a in ("-o", "-MF", "-MQ", "-MT"): skip = True; continue
            low = a.lower()
            if a in ("-c", "/c", "-MD", "-MMD", "/showIncludes") or low.startswith(("/fo", "-fo", "/fd", "/yu", "/yc", "/fp")):
                continue
            if Path(a).as_posix().endswith(args.reference_tu): continue
            base.append(a)
        base.append("/Zs" if msvc else "-fsyntax-only")

        with tempfile.TemporaryDirectory(prefix="owner-access-") as tmp:
            def compiles(name, body):
                path = Path(tmp) / f"{name}.cpp"
                path.write_text(PRELUDE + CONTROL + body + "\n", encoding="utf-8")
                result = subprocess.run(base + [str(path)], cwd=entry["directory"], capture_output=True, text=True)
                return result.returncode == 0, (result.stdout + result.stderr)
            ok, log = compiles("control", "")
            if not ok:
                failures.append("control snippet failed to compile; cannot trust negative cases:\n" + log[-4000:])
            else:
                print("PASS control (permitted routes and independent AssFile compile)")
                for name, body in NEGATIVE.items():
                    ok, _ = compiles(name, body)
                    if ok: failures.append(f"compile-failure case '{name}' unexpectedly compiled: {body}")
                    else: print(f"PASS rejected {name}")

if failures:
    print("\n".join("FAIL " + f for f in failures))
    sys.exit(1)
print(f"owner access check passed ({len(NEGATIVE)} compile-failure cases, API scan clean)")
