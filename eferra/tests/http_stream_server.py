#!/usr/bin/env python3

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import sys


GET_BODY = b"streaming-http-body"


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        pass

    def write_response(self, status, body):
        self.send_response(status)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        for start in range(0, len(body), 2):
            self.wfile.write(body[start:start + 2])
            self.wfile.flush()

    def do_GET(self):
        self.write_response(200, GET_BODY)

    def do_POST(self):
        size = int(self.headers.get("Content-Length", "0"))
        self.write_response(201, b"POST:" + self.rfile.read(size))


server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
Path(sys.argv[1]).write_text(str(server.server_port), encoding="ascii")
server.serve_forever()
