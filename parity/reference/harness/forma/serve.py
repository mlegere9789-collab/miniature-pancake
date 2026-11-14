#!/usr/bin/env python3
"""Serve this folder on 127.0.0.1 only, so Forma can load the embedded view during the run.
Usage: python3 serve.py [port]    (default 8080). Stops with Ctrl+C. Makes no outbound connections."""
import http.server, socketserver, sys, os
port = int(sys.argv[1]) if len(sys.argv) > 1 else 8080
os.chdir(os.path.dirname(os.path.abspath(__file__)))
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store"); super().end_headers()
with socketserver.TCPServer(("127.0.0.1", port), H) as srv:
    print("Serving http://127.0.0.1:%d/index.html (Ctrl+C to stop)" % port)
    try: srv.serve_forever()
    except KeyboardInterrupt: pass
