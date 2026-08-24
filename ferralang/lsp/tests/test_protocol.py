import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SERVER = Path(__file__).resolve().parents[1] / "ferra_lsp.py"


def frame(message):
    body = json.dumps(message).encode("utf-8")
    return f"Content-Length: {len(body)}\r\n\r\n".encode("ascii") + body


def decode_frames(payload):
    messages = []
    while payload:
        header, payload = payload.split(b"\r\n\r\n", 1)
        length = int(header.split(b":", 1)[1].strip())
        body, payload = payload[:length], payload[length:]
        messages.append(json.loads(body))
    return messages


class ProtocolTests(unittest.TestCase):
    def test_uri_alias_keeps_hover_inlays_and_diagnostic_attachment(self):
        source = (
            "func answer() { ret 42 }\n"
            "func main() { var value = answer() }\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "protocol.fe"
            path.write_text(source, encoding="utf-8")
            canonical_uri = path.as_uri()
            opened_uri = canonical_uri.replace("file:///", "file://localhost/", 1)
            value_character = source.splitlines()[1].index("value") + 1
            incoming = b"".join([
                frame({
                    "jsonrpc": "2.0",
                    "id": 1,
                    "method": "initialize",
                    "params": {},
                }),
                frame({
                    "jsonrpc": "2.0",
                    "method": "textDocument/didOpen",
                    "params": {
                        "textDocument": {
                            "uri": opened_uri,
                            "languageId": "ferra",
                            "version": 1,
                            "text": source,
                        }
                    },
                }),
                frame({
                    "jsonrpc": "2.0",
                    "id": 2,
                    "method": "textDocument/hover",
                    "params": {
                        "textDocument": {"uri": canonical_uri},
                        "position": {"line": 1, "character": value_character},
                    },
                }),
                frame({
                    "jsonrpc": "2.0",
                    "id": 3,
                    "method": "textDocument/inlayHint",
                    "params": {
                        "textDocument": {"uri": canonical_uri},
                        "range": {
                            "start": {"line": 0, "character": 0},
                            "end": {"line": 2, "character": 0},
                        },
                    },
                }),
                frame({
                    "jsonrpc": "2.0",
                    "id": 4,
                    "method": "shutdown",
                    "params": {},
                }),
                frame({"jsonrpc": "2.0", "method": "exit", "params": {}}),
            ])
            process = subprocess.run(
                [sys.executable, str(SERVER)],
                input=incoming,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=5,
                check=True,
            )

        messages = decode_frames(process.stdout)
        diagnostics = next(
            message for message in messages
            if message.get("method") == "textDocument/publishDiagnostics"
        )
        self.assertEqual(opened_uri, diagnostics["params"]["uri"])
        hover = next(message for message in messages if message.get("id") == 2)
        self.assertIn("value: int", hover["result"]["contents"]["value"])
        inlays = next(message for message in messages if message.get("id") == 3)
        self.assertIn(": int", [hint["label"] for hint in inlays["result"]])


if __name__ == "__main__":
    unittest.main()
