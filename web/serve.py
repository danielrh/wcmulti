#!/usr/bin/env python3
"""Serves web/dist (the browser build) for local testing.

    scripts/build-web.sh && web/serve.py [port]

Then open http://localhost:8000/ in two browsers (or two tabs) and enter
the same room code.  The lobby server must allow this page's origin
(p2p-lobby-server --allowed-origin http://localhost:8000); the public
server only allows pages served from its own host.
"""
import http.server, mimetypes, os, sys

mimetypes.add_type("application/wasm", ".wasm")
mimetypes.add_type("text/javascript", ".js")
mimetypes.add_type("application/gzip", ".gz")

# (WEB_ROOT: another directory, e.g. a copy of dist without wc.tar.gz)
root = os.environ.get("WEB_ROOT") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "dist")
port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000

class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *a, **kw):
        super().__init__(*a, directory=root, **kw)
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

print(f"serving {root} on http://localhost:{port}/")
http.server.ThreadingHTTPServer(("", port), Handler).serve_forever()
