"""Run the workbench feasibility probes against generated data in a private directory."""

import argparse
import contextlib
import functools
import hashlib
import http.server
import json
import math
import os
from pathlib import Path
import signal
import socketserver
import sqlite3
import statistics
import subprocess
import sys
import tempfile
import threading
import time


def process_probe(root):
    if os.name != "posix":
        raise SystemExit("The process-group probe requires POSIX")
    # The parent and its helper deliberately keep the same pipe open.
    # communicate() reaches EOF only when both ends have closed.
    child = "import time; print('ready', flush=True); time.sleep(30)"
    parent = ("import subprocess, sys, time; "
              "subprocess.Popen([sys.executable, '-c', sys.argv[1]]); time.sleep(30)")
    with subprocess.Popen([sys.executable, "-c", parent, child], cwd=root,
                          start_new_session=True, stdout=subprocess.PIPE, text=True) as group:
        try:
            assert group.stdout.readline().strip() == "ready"
            os.killpg(group.pid, signal.SIGTERM)
            group.communicate(timeout=3)
            assert group.returncode == -signal.SIGTERM
        finally:
            with contextlib.suppress(ProcessLookupError):
                os.killpg(group.pid, signal.SIGKILL)
    return dict(parent_exit=group.returncode, helper_pipe_closed=True)


def index_probe(root):
    catalog = root / "projects"
    for project in range(40):
        directory = catalog / f"project-{project:02}" / "subtitles"
        directory.mkdir(parents=True)
        for number in range(250):
            prefix = "translation" if number % 10 == 0 else "episode"
            (directory / f"{prefix}-{number:03}.ass").touch()

    def refresh(connection):
        connection.execute("DELETE FROM file_entries")
        connection.executemany(
            "INSERT INTO file_entries VALUES (?, ?)",
            ((project.name, entry.name) for project in catalog.iterdir()
             for entry in (project / "subtitles").iterdir() if entry.is_file()),
        )

    database = root / "workbench.sqlite"
    with contextlib.closing(sqlite3.connect(database)) as db:
        db.executescript("""
            CREATE TABLE tasks(id INTEGER PRIMARY KEY, state TEXT NOT NULL);
            INSERT INTO tasks VALUES (1, 'paused');
            CREATE TABLE file_entries(project TEXT, filename TEXT, PRIMARY KEY(project, filename));
            CREATE INDEX filename_order ON file_entries(filename, project);
        """)
        start = time.perf_counter()
        with db:
            refresh(db)
        scan = (time.perf_counter() - start) * 1000
        assert db.execute("SELECT count(*) FROM file_entries").fetchone() == (10000,)
        query = ("SELECT project, filename FROM file_entries WHERE instr(filename, ?) > 0 "
                 "ORDER BY filename, project LIMIT 50")
        timings = []
        for _ in range(101):
            start = time.perf_counter()
            assert len(db.execute(query, ("translation",)).fetchall()) == 50
            timings.append((time.perf_counter() - start) * 1000)
        with db:
            refresh(db)
        db.execute("DELETE FROM file_entries")
        db.rollback()
    with contextlib.closing(sqlite3.connect(database)) as db:
        assert db.execute("SELECT id, state FROM tasks").fetchall() == [(1, "paused")]
        assert db.execute("SELECT count(*) FROM file_entries").fetchone() == (10000,)
    warm = sorted(timings[1:])
    return dict(sqlite=sqlite3.sqlite_version, projects=40, files=10000, matches=1000,
                scan_ms=round(scan, 2), first_query_ms=round(timings[0], 2),
                warm_median_ms=round(statistics.median(warm), 2),
                warm_p95_ms=round(warm[math.ceil(.95 * len(warm)) - 1], 2))


def download_probe(root):
    # Only this loopback server and this invented media file are used.
    source = root / "source"
    output = root / "output"
    source.mkdir()
    output.mkdir()
    sample = source / "sample.mp4"
    subprocess.run(["ffmpeg", "-v", "error", "-f", "lavfi", "-i",
                    "color=size=32x32:duration=0.4:rate=5", "-c:v", "mpeg4", str(sample)], check=True)

    requests = []

    class Handler(http.server.SimpleHTTPRequestHandler):
        def log_message(self, fmt, *args):
            requests.append((self.command, self.path))

        def send_head(self):
            if self.path == "/restricted.mp4" and "workbench_fixture=allowed" not in self.headers.get("Cookie", ""):
                self.send_error(403)
                return None
            return super().send_head()

        def end_headers(self):
            if self.path == "/restricted.mp4" and "workbench_fixture=allowed" in self.headers.get("Cookie", ""):
                self.send_header("Set-Cookie", "fixture_checked=yes; Path=/")
            super().end_headers()

    (source / "restricted.mp4").write_bytes(sample.read_bytes())
    env = {k: os.environ[k] for k in ("PATH", "PYTHONPATH", "SYSTEMROOT") if k in os.environ}
    env.update(HOME=str(root), TMP=str(root), TEMP=str(root), TMPDIR=str(root),
               XDG_CONFIG_HOME=str(root), XDG_CACHE_HOME=str(root), PYTHONDONTWRITEBYTECODE="1")
    command = [sys.executable, "-m", "yt_dlp", "--ignore-config", "--no-plugin-dirs",
               "--no-cache-dir", "--proxy", "", "--no-playlist", "--no-simulate",
               "--progress", "--newline", "--no-color", "--socket-timeout", "5",
               "--progress-template", "download:PROGRESS %(progress.status)s",
               "--print", "after_move:OUTPUT %(filepath)j", "-o", str(output / "%(id)s.%(ext)s"),
               "--batch-file", "-"]
    version = subprocess.check_output([sys.executable, "-m", "yt_dlp", "--version"], env=env, text=True).strip()
    with socketserver.ThreadingTCPServer(("127.0.0.1", 0), functools.partial(Handler, directory=source)) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        address = f"http://127.0.0.1:{server.server_address[1]}"

        def invoke(path, extra=()):
            return subprocess.run([*command, *extra], input=f"{address}/{path}\n",
                                  text=True, capture_output=True, env=env, cwd=root, timeout=30)

        try:
            assert not requests
            simulated = invoke("sample.mp4", ["--simulate"])
            assert simulated.returncode == 0 and not list(output.iterdir()), simulated.stderr
            denied = invoke("restricted.mp4", ["--simulate"])
            assert denied.returncode != 0 and not list(output.iterdir())
            jar = root / "fixture-cookies.txt"
            jar.write_text("# Netscape HTTP Cookie File\n127.0.0.1\tFALSE\t/\tFALSE\t0\tworkbench_fixture\tallowed\n")
            jar.chmod(0o600)
            checked = invoke("restricted.mp4", ["--simulate", "--cookies", str(jar)])
            assert checked.returncode == 0 and not list(output.iterdir()), checked.stderr
            assert "fixture_checked\tyes" in jar.read_text()
            downloaded = invoke("sample.mp4")
            assert downloaded.returncode == 0, downloaded.stderr
            lines = downloaded.stdout.splitlines()
            paths = [Path(json.loads(line[7:])) for line in lines if line.startswith("OUTPUT ")]
            assert any("PROGRESS finished" in line for line in lines), lines
            assert len(paths) == 1 and paths[0].parent == output, paths
            assert paths[0].read_bytes() == sample.read_bytes()
            failure = invoke("missing.mp4")
            assert failure.returncode != 0 and list(output.iterdir()) == paths
            assert {path for _, path in requests} == {"/sample.mp4", "/restricted.mp4", "/missing.mp4"}
        finally:
            server.shutdown()
            thread.join()
    return dict(yt_dlp=version, sha256=hashlib.sha256(sample.read_bytes()).hexdigest(),
                simulation=simulated.returncode, download=downloaded.returncode, missing=failure.returncode,
                fixture_denied=denied.returncode, fixture_accepted=checked.returncode,
                simulated_cookie_updated=True, loopback_requests=len(requests))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    probes = {"index": index_probe, "download": download_probe, "process": process_probe}
    parser.add_argument("probe", choices=probes)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="aeg-workbench-probe-") as private:
        print(json.dumps(probes[args.probe](Path(private)), ensure_ascii=False))
