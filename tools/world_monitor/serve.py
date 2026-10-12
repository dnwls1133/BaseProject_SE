"""Read-only local world monitor. Uses Python 3 standard library only."""
import argparse
import json
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--open", action="store_true", help="Open the browser after binding")
    parser.add_argument("--snapshot-dir", type=Path,
                        help="Folder containing the game's snapshot.json")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be between 1 and 65535")
    repository = Path(__file__).resolve().parents[2]
    candidates = [repository / "x64" / config / "Logs" / "WorldMonitor"
                  for config in ("Debug", "Release")]
    snapshot_directory = args.snapshot_dir
    if snapshot_directory is None:
        existing = [p for p in candidates if (p / "snapshot.json").exists()]
        snapshot_directory = max(existing, key=lambda p: (p / "snapshot.json").stat().st_mtime,
                                 default=candidates[0])
    snapshot = snapshot_directory.resolve() / "snapshot.json"
    html = Path(__file__).with_name("monitor.html").read_bytes()

    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            route = urlsplit(self.path).path
            if route in ("/", "/monitor.html"):
                payload, mime, status = html, "text/html; charset=utf-8", 200
            elif route == "/api/state":
                try:
                    # Close immediately so the game can atomically replace the file.
                    payload = snapshot.read_bytes()
                    json.loads(payload)
                    mime, status = "application/json; charset=utf-8", 200
                except (OSError, ValueError):
                    payload = json.dumps({"error": "snapshot_unavailable",
                                          "message": "게임 상태를 아직 읽을 수 없습니다."},
                                         ensure_ascii=False).encode("utf-8")
                    mime, status = "application/json; charset=utf-8", 503
            else:
                payload, mime, status = b"Not found", "text/plain", 404
            self.send_response(status)
            self.send_header("Content-Type", mime)
            self.send_header("Content-Length", str(len(payload)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.end_headers()
            self.wfile.write(payload)

        def log_message(self, format, *arguments):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    print(f"World monitor: http://127.0.0.1:{args.port}", flush=True)
    print(f"Read-only snapshot: {snapshot}")
    print("Stop: Ctrl+C. Start the game separately. No game commands are accepted.")
    if args.open:
        webbrowser.open(f"http://127.0.0.1:{args.port}/")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
