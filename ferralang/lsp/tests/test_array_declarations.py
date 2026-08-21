import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp_arrays", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class ArrayDeclarationTests(unittest.TestCase):
    SOURCE = (
        "fn main(): i64 {\n"
        "  let buf[5]: u8 = [0]\n"
        "  log(buf[0])\n"
        "  let values[]: i32\n"
        "  log(values[0])\n"
        "  ret 0\n"
        "}\n"
    )

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "main.fe"
        self.path.write_text(self.SOURCE, encoding="utf-8")
        self.uri = self.path.as_uri()

    def tearDown(self):
        self.directory.cleanup()

    def test_hover_recognizes_sized_and_empty_array_declarators(self):
        buf_hover = ferra_lsp.hover_for(self.uri, self.SOURCE, 2, 7)
        self.assertIsNotNone(buf_hover)
        self.assertIn("buf[5]: u8", buf_hover["contents"]["value"])

        values_hover = ferra_lsp.hover_for(self.uri, self.SOURCE, 4, 8)
        self.assertIsNotNone(values_hover)
        self.assertIn("values[]: i32", values_hover["contents"]["value"])

    def test_definition_and_completion_include_static_array(self):
        definition = ferra_lsp.definition_for(
            self.uri, self.SOURCE, 2, 7
        )
        self.assertIsNotNone(definition)
        self.assertEqual(
            {"line": 1, "character": 6},
            definition["range"]["start"],
        )

        completion = ferra_lsp.completion_for(
            self.uri, self.SOURCE, 2, 5
        )
        buf = next(
            item for item in completion["items"] if item["label"] == "buf"
        )
        self.assertIn("buf[5]: u8", buf["detail"])

    def test_array_type_is_available_to_inference_and_diagnostics(self):
        use_offset = self.SOURCE.index("buf[0]")
        index = ferra_lsp.build_index(self.uri, self.SOURCE)
        self.assertEqual(
            "u8",
            ferra_lsp.infer_variable_type(
                self.uri, self.SOURCE, "buf", use_offset, index
            ),
        )
        self.assertFalse(any(
            "buf" in diagnostic["message"]
            or "values" in diagnostic["message"]
            for diagnostic in ferra_lsp.diagnostics_for(
                self.uri, self.SOURCE
            )
        ))

    def test_module_level_array_is_exported_by_index(self):
        source = "let table[16]: u32\n"
        self.path.write_text(source, encoding="utf-8")
        index = ferra_lsp.build_index(self.uri, source)
        table = next(symbol for symbol in index.symbols if symbol.name == "table")
        self.assertEqual("let table[16]: u32", table.signature)
        self.assertEqual("u32[]", table.type_name)


if __name__ == "__main__":
    unittest.main()
