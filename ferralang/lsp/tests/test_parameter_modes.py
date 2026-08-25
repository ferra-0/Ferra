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
        "func primitive(value: i64!) {\n"
        "  value = 5\n"
        "}\n"
        "func tuple_mode(pair: (bol, i64)!) {\n"
        "  log(pair[0])\n"
        "}\n"
        "func array_mode(values: i64[]!) {\n"
        "  log(values[0])\n"
        "}\n"
        "func generic_mode<T>(value: T!) {\n"
        "  log(value)\n"
        "}\n"
        "func const_number(): i64! { ret 7 }\n"
        "func partial_const(): (str!, bol) { ret (\"Bob\", true) }\n"
        "func whole_const(): (str, bol)! { ret (\"Bob\", true) }\n"
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

    def test_primitive_by_value_signature_preserves_marker(self):
        index = ferra_lsp.build_index(self.uri, self.SOURCE)
        primitive = next(
            symbol for symbol in index.symbols if symbol.name == "primitive"
        )
        self.assertIn("value: i64!", primitive.signature)

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

    def test_tuple_array_and_generic_parameters_preserve_by_value_marker(self):
        index = ferra_lsp.build_index(self.uri, self.SOURCE)
        signatures = {
            symbol.name: symbol.signature
            for symbol in index.symbols
            if symbol.kind == ferra_lsp.KIND_FUNCTION
        }
        self.assertIn("pair: (bol, i64)!", signatures["tuple_mode"])
        self.assertIn("values: i64[]!", signatures["array_mode"])
        self.assertIn("value: T!", signatures["generic_mode"])

        pair_offset = self.SOURCE.index("pair[0]") + 1
        position = ferra_lsp.offset_to_position(self.SOURCE, pair_offset)
        hover = ferra_lsp.hover_for(
            self.uri, self.SOURCE, position["line"], position["character"]
        )
        self.assertIsNotNone(hover)
        self.assertIn("pair: (bol, i64)!", hover["contents"]["value"])

    def test_const_return_markers_are_indexed_without_losing_tuple_suffix(self):
        index = ferra_lsp.build_index(self.uri, self.SOURCE)
        return_types = {
            symbol.name: symbol.type_name
            for symbol in index.symbols
            if symbol.kind == ferra_lsp.KIND_FUNCTION
        }
        self.assertEqual("i64!", return_types["const_number"])
        self.assertEqual("(str!, bol)", return_types["partial_const"])
        self.assertEqual("(str, bol)!", return_types["whole_const"])


if __name__ == "__main__":
    unittest.main()
