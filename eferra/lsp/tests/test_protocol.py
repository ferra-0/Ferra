import json
from pathlib import Path
import subprocess
import sys
import unittest


SERVER = Path(__file__).resolve().parents[1] / "eferra_lsp.py"


def frame(message):
    body = json.dumps(message).encode()
    return f"Content-Length: {len(body)}\r\n\r\n".encode() + body


def decode_frames(payload):
    messages = []
    while payload:
        header, payload = payload.split(b"\r\n\r\n", 1)
        length = int(header.split(b":", 1)[1].strip())
        body, payload = payload[:length], payload[length:]
        messages.append(json.loads(body))
    return messages


class ProtocolTests(unittest.TestCase):
    def test_initialize_open_hover_shutdown(self):
        text = "let value = json.parse('{}')\nlog(value)\n"
        uri = "file:///tmp/protocol.efe"
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
                        "uri": uri,
                        "languageId": "eferra",
                        "version": 1,
                        "text": text,
                    }
                },
            }),
            frame({
                "jsonrpc": "2.0",
                "id": 2,
                "method": "textDocument/hover",
                "params": {
                    "textDocument": {"uri": uri},
                    "position": {"line": 0, "character": 19},
                },
            }),
            frame({
                "jsonrpc": "2.0",
                "id": 3,
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

        self.assertEqual(
            "eferra-lsp", messages[0]["result"]["serverInfo"]["name"]
        )
        self.assertEqual(
            "textDocument/publishDiagnostics", messages[1]["method"]
        )
        self.assertEqual([], messages[1]["params"]["diagnostics"])
        self.assertIn(
            "json.parse(text)",
            messages[2]["result"]["contents"]["value"],
        )
        self.assertIsNone(messages[3]["result"])


if __name__ == "__main__":
    unittest.main()
