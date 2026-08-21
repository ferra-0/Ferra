import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp_parameter_modes", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class ParameterModeTests(unittest.TestCase):
    SOURCE = (
        "stct Some { x: i64 }\n"
        "fn mutate(value: Some!) {\n"
        "  value.x = 5\n"
        "}\n"
        "fn main(): i64 { ret 0 }\n"
    )

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "main.fe"
        self.path.write_text(self.SOURCE, encoding="utf-8")
        self.uri = self.path.as_uri()

    def tearDown(self):
        self.directory.cleanup()

    def test_by_value_parameter_keeps_struct_type_for_member_hover(self):
        member_offset = self.SOURCE.index("value.x") + len("value.")
        position = ferra_lsp.offset_to_position(self.SOURCE, member_offset)
        hover = ferra_lsp.hover_for(
            self.uri,
            self.SOURCE,
            position["line"],
            position["character"],
        )
        self.assertIsNotNone(hover)
        self.assertIn("x: i64", hover["contents"]["value"])

    def test_signature_and_parameter_hover_preserve_marker(self):
        index = ferra_lsp.build_index(self.uri, self.SOURCE)
        mutate = next(symbol for symbol in index.symbols if symbol.name == "mutate")
        self.assertIn("value: Some!", mutate.signature)

        value_offset = self.SOURCE.index("value.x")
        position = ferra_lsp.offset_to_position(self.SOURCE, value_offset + 1)
        hover = ferra_lsp.hover_for(
            self.uri,
            self.SOURCE,
            position["line"],
            position["character"],
        )
        self.assertIsNotNone(hover)
        self.assertIn("value: Some!", hover["contents"]["value"])


if __name__ == "__main__":
    unittest.main()
