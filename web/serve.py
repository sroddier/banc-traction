#!/usr/bin/env python3
# Serveur statique local pour la séance sans ESP32 / pour PWA localhost.
# Usage : python3 serve.py
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import os

HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(HERE)
PORT = int(os.environ.get("PORT", "8080"))

class H(SimpleHTTPRequestHandler):
    extensions_map = {
        **SimpleHTTPRequestHandler.extensions_map,
        ".js": "application/javascript; charset=utf-8",
        ".mjs": "application/javascript; charset=utf-8",
        ".json": "application/json",
        ".svg": "image/svg+xml",
        ".css": "text/css; charset=utf-8",
        ".html": "text/html; charset=utf-8",
        ".webmanifest": "application/manifest+json",
    }
    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

if __name__ == "__main__":
    httpd = ThreadingHTTPServer(("127.0.0.1", PORT), H)
    print("Banc de traction — http://127.0.0.1:%d/" % PORT)
    print("Simulation sans ESP32. Ctrl+C pour arrêter.")
    httpd.serve_forever()
