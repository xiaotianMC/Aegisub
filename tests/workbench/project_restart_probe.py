"""Exercise a real workbench restart and optionally interrupt a local registration on a private X11 desktop."""

import argparse
from contextlib import closing
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import time

import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GLib


def wait_for(check, description, app, timeout=20):
    until = time.monotonic() + timeout
    while app.poll() is None and time.monotonic() < until:
        try:
            value = check()
            if value:
                return value
        except GLib.Error:
            pass  # GTK can replace accessible objects while a window maps.
        time.sleep(0.1)
    visible = xdo("search", "--onlyvisible", "--pid", str(app.pid), required=False).splitlines()
    titles = [xdo("getwindowname", win, required=False) for win in visible]
    raise RuntimeError(f"{description}; exit={app.poll()}, visible windows={titles}")


def widget(pid, match):
    desktop = Atspi.get_desktop(0)
    stack = [desktop.get_child_at_index(i) for i in range(desktop.get_child_count())]
    stack = [app for app in stack if app and app.get_process_id() == pid]
    while stack:
        obj = stack.pop()
        if not obj:
            continue
        role, name = obj.get_role_name(), obj.get_name()
        if match(role, name):
            return obj
        if role not in ("menu bar", "menu"):
            stack.extend(obj.get_child_at_index(i) for i in range(obj.get_child_count()))


def xdo(*args, required=True):
    result = subprocess.run(["xdotool", *args], capture_output=True, text=True, timeout=5)
    if required:
        result.check_returncode()
    return result.stdout.strip()


def window(title):
    matches = xdo("search", "--onlyvisible", "--name", title, required=False).splitlines()
    return matches[-1] if matches else None


def click(app, description, match):
    def target():
        node = widget(app.pid, match)
        if node and node.get_state_set().contains(Atspi.StateType.SENSITIVE):
            rect = node.get_extents(Atspi.CoordType.SCREEN)
            if rect.x >= 0 and rect.y >= 0 and rect.width > 0 and rect.height > 0:
                return rect

    rect = wait_for(target, description, app)
    xdo("mousemove", str(rect.x + rect.width // 2), str(rect.y + rect.height // 2), "click", "1")


def button(app, label):
    click(app, label, lambda role, text: "button" in role and text == label)


def edit_metadata(app, status_steps=2):
    wait_for(lambda: not window("^Choose a cover image$"), "cover picker closed", app)
    main = window("^Workbench - Aegisub")
    xdo("windowraise", main, "windowfocus", main)
    button(app, "Project Details")
    dialog = wait_for(lambda: window("^Project Details"), "metadata dialog", app)
    bounds = dict(line.split("=", 1) for line in xdo("getwindowgeometry", "--shell", dialog).splitlines())
    field_x = int(bounds["X"]) + int(bounds["WIDTH"]) * 3 // 4
    for label, value in (("Source language", "Japanese"), ("Target language", "Chinese"), ("Production status", None)):
        title = wait_for(lambda: widget(app.pid, lambda role, text: role == "label" and text == label), label, app)
        rect = title.get_extents(Atspi.CoordType.SCREEN)
        # Each native form row has its label on the left and its input on the right.
        xdo("mousemove", str(field_x), str(rect.y + rect.height // 2), "click", "1")
        if value:
            xdo("key", "ctrl+a")
            xdo("type", value)
        else:
            xdo("key", "Home", *(["Down"] * status_steps), "Return")
    button(app, "OK")


def rows(database, sql):
    with closing(sqlite3.connect(database.as_uri() + "?mode=ro", uri=True)) as db:
        return db.execute(sql).fetchall()


def fault(database, install):
    # Only the isolated fixture's acknowledgement is interrupted. All application
    # operations still go through the real GUI, database, and manifest writer.
    with closing(sqlite3.connect(database)) as db:
        db.execute("""CREATE TRIGGER probe_ack BEFORE UPDATE OF stage ON outputs
                      WHEN NEW.stage='registered' BEGIN SELECT RAISE(ABORT,'probe acknowledgement'); END"""
                   if install else "DROP TRIGGER probe_ack")


def release_desktop_mounts(runtime):
    owned = {str(runtime / "doc"), str(runtime / "gvfs")}

    def remaining():
        return owned.intersection(line.split()[4] for line in Path("/proc/self/mountinfo").read_text().splitlines())

    deadline = time.monotonic() + 5
    while remaining() and time.monotonic() < deadline:
        time.sleep(0.1)
    for mount in remaining():
        detached = subprocess.run(["fusermount3", "-uz", mount], capture_output=True, text=True)
        if detached.returncode and mount in remaining():
            raise RuntimeError(f"could not release the test desktop mount {mount}: {detached.stderr}")


def fill_dialog(app, title, value):
    dialog = wait_for(lambda: window("^" + title + "$"), title, app)
    xdo("windowfocus", dialog, "key", "ctrl+a")
    xdo("type", value)
    xdo("key", "Return")


def create_item(app, command, parent, name):
    button(app, command)
    dialog = wait_for(lambda: window("^Choose the parent directory$"), "parent folder picker", app)
    xdo("windowfocus", dialog, "key", "ctrl+l")
    xdo("type", str(parent))
    xdo("key", "Return")
    fill_dialog(app, command, name)


def series_restart(executable, root, env, projects):
    series = projects / "Season"
    manifest = series / ".aeg/series.json"
    ten, two = (series / "episodes" / name for name in ("Ten", "Two"))
    snapshot = None

    def progress(app, label):
        wait_for(lambda: widget(app.pid, lambda role, text: text == label), label, app)

    def open_row(app, name, label):
        click(app, "episode " + label, lambda role, text: text.startswith(label + "  —  " + name + "  —"))
        button(app, "Open Episode")
        wait_for(lambda: not window("^Series Episodes"), "returned to project", app)
        wait_for(lambda: widget(app.pid, lambda role, text: role == "label" and text.startswith(name + "  —")),
                 "selected project " + name, app)

    for launch in (1, 2):
        with (root / "application.log").open("a") as log, subprocess.Popen(
                [executable], cwd=root, env=env, stdout=log, stderr=log) as app:
            try:
                main = wait_for(lambda: window("^Workbench - Aegisub"), "workbench", app)
                if launch == 1:
                    create_item(app, "New Series", projects, series.name)
                    wait_for(manifest.is_file, "series manifest", app)
                    button(app, "Series Episodes")
                    for name, label in (("Ten", "10"), ("Two", "02")):
                        button(app, "New Episode")
                        fill_dialog(app, "New Episode", name)
                        fill_dialog(app, "Series Episodes", label)
                        wait_for(lambda: any(row["label"] == label for row in json.loads(manifest.read_bytes())["episodes"]),
                                 "persisted episode " + label, app)
                    open_row(app, "Ten", "10")
                    edit_metadata(app, status_steps=3)
                    wait_for(lambda: json.loads((ten / ".aeg/project.json").read_bytes())["status"] == "done", "completed episode", app)
                    button(app, "Back to Series")
                    progress(app, "1 of 2 episodes completed, 0 unknown")
                    snapshot = manifest.read_bytes()
                else:
                    click(app, "series location", lambda role, text: text == "Season  —  " + str(series))
                    button(app, "Series Episodes")
                    progress(app, "0 of 2 episodes completed, 1 unknown")
                    open_row(app, "Two", "02")
                    (root / "offline").rename(ten)
                    button(app, "Back to Series")
                    progress(app, "1 of 2 episodes completed, 0 unknown")
                    open_row(app, "Ten", "10")
                xdo("windowfocus", main, "key", "ctrl+q")
                if app.wait(timeout=10) != 0:
                    raise RuntimeError("workbench did not exit cleanly")
            finally:
                if app.poll() is None:
                    app.kill()
                    app.wait(timeout=10)
        if launch == 1:
            ten.rename(root / "offline")
    child_ids = {json.loads((p / ".aeg/project.json").read_bytes())["project_id"] for p in (ten, two)}
    data = json.loads(snapshot)
    if (len(child_ids) != 2 or {row["project_id"] for row in data["episodes"]} != child_ids
            or manifest.read_bytes() != snapshot or data["revision"] != 3):
        raise RuntimeError("series membership changed across restart")
    return dict(launches=2, episodes=2, series_revision=3, offline_unknown=1, restored_completed=1)


def exercise(executable, root, registration, series):
    profile = root / "config/aegisub"
    profile.mkdir(parents=True)
    (profile / "config.json").write_text(json.dumps({
        "App": {"First Start": False, "Language": "en_US", "Auto": {"Check For Updates": False}},
    }))
    projects = root / "projects"
    projects.mkdir()
    env = dict(os.environ, HOME=str(root), XDG_CONFIG_HOME=str(root / "config"),
               XDG_STATE_HOME=str(root / "state"), XDG_DATA_HOME=str(root / "data"),
               XDG_CACHE_HOME=str(root / "cache"), GDK_BACKEND="x11", NO_AT_BRIDGE="0",
               GTK_USE_PORTAL="0", GSETTINGS_BACKEND="memory")
    env.pop("WAYLAND_DISPLAY", None)
    if series:
        return series_restart(executable, root, env, projects)
    name = "restart-check"
    manifest = projects / name / ".aeg/project.json"
    cover = root / "chosen.png"
    if not registration:
        cover_contents = (Path(__file__).resolve().parents[1] / "charset/binary.png").read_bytes()
        cover.write_bytes(cover_contents)
    identity = output_ids = published = None
    relative = "subtitles/restart.srt"
    launches = 3 if registration else 2
    for launch in range(1, launches + 1):
        with (root / "application.log").open("a") as log, subprocess.Popen(
                [executable], cwd=root, env=env, stdout=log, stderr=log) as app:
            try:
                main = wait_for(lambda: window("^Workbench - Aegisub"), "home window", app)
                if launch == 1:
                    create_item(app, "New Project", projects, name)
                    wait_for(manifest.is_file, "project manifest on disk", app)
                    identity = json.loads(manifest.read_text())["project_id"]
                wait_for(lambda: widget(app.pid, lambda role, text: name in text and str(projects) in text),
                         "project visible on the home page", app)
                persistent = [root / part for part in ("config", "state", "data", "cache", "projects")]
                databases = [db for directory in persistent for db in directory.rglob("workbench.sqlite")]
                if len(databases) != 1:
                    raise RuntimeError(f"unexpected number of databases: {databases}")
                db = databases[0]
                stored = rows(db, "SELECT id,name,root FROM locations")
                if stored != [(identity, name, str(projects / name))]:
                    raise RuntimeError(f"unexpected stored location: {stored!r}")
                if not registration:
                    if launch == 1:
                        button(app, "Choose Cover…")
                        dialog = wait_for(lambda: window("^Choose a cover image$"), "cover picker", app)
                        xdo("windowfocus", dialog, "key", "ctrl+l")
                        xdo("type", str(cover))
                        xdo("key", "Return")
                        wait_for(lambda: json.loads(manifest.read_bytes())["cover"], "saved cover", app)
                        cover.unlink()  # On the next launch only the project copy is available.
                        edit_metadata(app)
                        wait_for(lambda: json.loads(manifest.read_bytes())["source_language"] == "Japanese", "saved metadata", app)
                    details = json.loads(manifest.read_bytes())
                    if (details["source_language"], details["target_language"], details["status"]) != ("Japanese", "Chinese", "review"):
                        raise RuntimeError("the two languages or manual status did not survive the form save and restart")
                    relative_cover = Path(details["cover"])
                    if (relative_cover.parent != Path(".aeg/covers")
                            or (projects / name / relative_cover).read_bytes() != cover_contents):
                        raise RuntimeError("the project cover did not survive relaunch")
                if registration:
                    path = projects / name / relative
                    if launch == 1:
                        path.write_text("test input\n", encoding="utf-8")
                        button(app, "Project Files")
                        button(app, "Rebuild File Index")
                        click(app, "indexed subtitle", lambda role, text: text == relative)
                        button(app, "Queue Selected Files for Registration")
                        wait_for(lambda: rows(db, "SELECT state FROM tasks") == [("queued",)], "queued task", app)
                        output_ids = rows(db, "SELECT task_id,output_id,asset_id FROM outputs")[0]
                    else:
                        expected = "queued" if launch == 2 else "needs_attention"
                        if rows(db, "SELECT state FROM tasks") != [(expected,)]:
                            raise RuntimeError(f"registration did not survive restart in {expected}")
                        fault(db, launch == 2)
                        button(app, "Registration Tasks (1 unfinished)")
                        click(app, "saved task in the list", lambda role, text: "restart.srt" in text and name in text)
                        button(app, "Run or Check Registration")
                        expected = "needs_attention" if launch == 2 else "succeeded"
                        wait_for(lambda: rows(db, "SELECT state FROM tasks") == [(expected,)], expected, app)
                        snapshot = manifest.read_bytes()
                        document = json.loads(snapshot)
                        asset, = document["assets"]
                        if ((asset["origin_task_id"], asset["output_id"], asset["asset_id"]) != output_ids
                                or asset["path"] != relative or len(document["history"]) != 1):
                            raise RuntimeError("registration identity or history changed")
                        if launch == 2:
                            published = snapshot
                            if rows(db, "SELECT count(*) FROM pending_steps") != [(1,)]:
                                raise RuntimeError("missing pending acknowledgement")
                        elif (snapshot != published or rows(db, "SELECT count(*) FROM pending_steps") != [(0,)]
                              or rows(db, "SELECT stage FROM outputs") != [("registered",)]
                              or rows(db, "SELECT count(*) FROM attempts") != [(2,)]
                              or path.read_bytes() != b"test input\n"):
                            raise RuntimeError("reconciliation changed the file, manifest or saved checkpoints")
                    if launch < launches:
                        app.kill()
                        app.wait(timeout=10)
                        continue
                xdo("windowfocus", main, "key", "ctrl+q")
                if app.wait(timeout=10) != 0:
                    raise RuntimeError("application failed to exit cleanly")
            finally:
                if app.poll() is None:
                    app.terminate()
                    try:
                        app.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        app.kill()
    result = {"launches": launches, "project_id": identity, "registered_projects": len(stored), "databases": len(databases)}
    if registration:
        result.update(abrupt_exits=2, task_state="succeeded", attempts=2, assets=1, history_entries=1, pending_steps=0)
    else:
        result.update(cover_preserved=True, source_language="Japanese", target_language="Chinese", status="review")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--registration", action="store_true", help="terminate the app after queuing and after a failed acknowledgement")
    mode.add_argument("--series", action="store_true", help="create two episodes, restart with one offline, then restore it")
    parser.add_argument("--desktop", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.desktop:
        print(json.dumps(exercise(args.executable.resolve(), args.desktop, args.registration, args.series)))
    else:
        with tempfile.TemporaryDirectory(prefix="aeg-project-restart-") as temporary:
            root = Path(temporary)
            runtime = root / "runtime"
            runtime.mkdir(mode=0o700)
            env = dict(os.environ, HOME=temporary, XDG_RUNTIME_DIR=str(runtime), GIO_USE_VFS="local", GVFS_DISABLE_FUSE="1",
                       XDG_CONFIG_HOME=str(root / "config"), XDG_DATA_HOME=str(root / "data"),
                       XDG_CACHE_HOME=str(root / "cache"), XDG_STATE_HOME=str(root / "state"))
            for key in ("AT_SPI_BUS_ADDRESS", "DBUS_SESSION_BUS_ADDRESS", "GNOME_KEYRING_CONTROL", "WAYLAND_DISPLAY"):
                env.pop(key, None)
            try:
                subprocess.run(["dbus-run-session", "--", "xvfb-run", "-a", sys.executable,
                                str(Path(__file__).resolve()), str(args.executable.resolve()), "--desktop", temporary,
                                *(["--registration"] if args.registration else ["--series"] if args.series else [])],
                               env=env, check=True)
            finally:
                release_desktop_mounts(runtime)
