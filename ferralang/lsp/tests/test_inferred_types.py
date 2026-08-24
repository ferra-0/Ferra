import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp_inferred", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class InferredTypesTests(unittest.TestCase):
    SOURCE = (
        "fn echo(value) { ret value }\n"
        "fn answer() { ret 42 }\n"
        "fn main(): i64 {\n"
        "  let name = \"Bob\"\n"
        "  let copy = echo(name)\n"
        "  let number = answer()\n"
        "  let ratio = 1.5\n"
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

    def test_unannotated_variables_expose_inferred_types(self):
        symbols = {
            symbol.name: symbol.type_name
            for symbol in ferra_lsp.local_symbols(self.uri, self.SOURCE)
        }
        self.assertEqual("str", symbols["name"])
        self.assertEqual("str", symbols["copy"])
        self.assertEqual("int", symbols["number"])
        self.assertEqual("f64", symbols["ratio"])

        copy_offset = self.SOURCE.index("copy =") + 1
        position = ferra_lsp.offset_to_position(self.SOURCE, copy_offset)
        hover = ferra_lsp.hover_for(
            self.uri, self.SOURCE, position["line"], position["character"]
        )
        self.assertIsNotNone(hover)
        self.assertIn("copy: str", hover["contents"]["value"])

    def test_unannotated_function_return_is_indexed(self):
        index = ferra_lsp.build_index(self.uri, self.SOURCE)
        answer = next(symbol for symbol in index.symbols if symbol.name == "answer")
        self.assertEqual("int", answer.type_name)
        self.assertIn("func answer(): int", answer.signature)

        echo = next(symbol for symbol in index.symbols if symbol.name == "echo")
        self.assertEqual("str", echo.type_name)
        self.assertIn("func echo(value: str): str", echo.signature)

        value_offset = self.SOURCE.index("ret value") + len("ret ")
        position = ferra_lsp.offset_to_position(self.SOURCE, value_offset)
        hover = ferra_lsp.hover_for(
            self.uri, self.SOURCE, position["line"], position["character"]
        )
        self.assertIsNotNone(hover)
        self.assertIn("value: str", hover["contents"]["value"])

    def test_inlay_hints_show_inferred_variable_and_function_types(self):
        labels = [
            hint["label"]
            for hint in ferra_lsp.inlay_hints_for(self.uri, self.SOURCE)
        ]
        self.assertIn(": str", labels)
        self.assertIn(": int", labels)
        self.assertIn(": f64", labels)

    def test_tuple_type_is_indexed_and_inferred(self):
        source = (
            "fn pair(): (str, i64) { ret (\"Bob\", 5) }\n"
            "fn main() { let value = (\"Bob\", 5) }\n"
        )
        index = ferra_lsp.build_index(self.uri, source)
        pair = next(symbol for symbol in index.symbols if symbol.name == "pair")
        self.assertEqual("(str, i64)", pair.type_name)
        self.assertEqual("(str, int)", ferra_lsp.infer_initializer_type("(\"Bob\", 5)"))

    def test_tuple_destructuring_infers_each_name_and_receiver_type(self):
        source = (
            "stct Reply { code: i32 }\n"
            "stct Client {}\n"
            "impl Client fetch(): (Reply, bol) { ret (Reply{code: 200}, true) }\n"
            "fn main() {\n"
            "  let client: Client()\n"
            "  let r, ok = client.fetch()\n"
            "  r.code\n"
            "  log(ok)\n"
            "}\n"
        )
        path = self.path.parent / "tuple_destructure.fe"
        path.write_text(source, encoding="utf-8")
        uri = path.as_uri()

        symbols = {
            symbol.name: symbol.type_name
            for symbol in ferra_lsp.local_symbols(uri, source)
        }
        self.assertEqual("Reply", symbols["r"])
        self.assertEqual("bol", symbols["ok"])

        diagnostics = ferra_lsp.diagnostics_for(uri, source)
        self.assertFalse(any(
            "'r' before field access" in diagnostic["message"]
            for diagnostic in diagnostics
        ))

        r_offset = source.index("r.code")
        position = ferra_lsp.offset_to_position(source, r_offset + 1)
        hover = ferra_lsp.hover_for(
            uri, source, position["line"], position["character"]
        )
        self.assertIsNotNone(hover)
        self.assertIn("r: Reply", hover["contents"]["value"])

        labels = [hint["label"] for hint in ferra_lsp.inlay_hints_for(uri, source)]
        self.assertIn(": Reply", labels)
        self.assertIn(": bol", labels)


    def test_tuple_annotation_does_not_consume_cast_expression(self):
        source = (
            "func main() {\n"
            "  var value: (str, i32) = (\"Bob\", 5 as i32)\n"
            "  log(value[1])\n"
            "}\n"
        )
        diagnostics = ferra_lsp.diagnostics_for(self.uri, source)
        self.assertFalse(any(
            "Unknown type -> 'as'" in diagnostic["message"]
            for diagnostic in diagnostics
        ))
    def test_var_tuple_destructuring_shows_a_type_for_every_binding(self):
        source = (
            "func user(): (str, i64) { ret (\"Bob\", 42) }\n"
            "func main() {\n"
            "  var name, age = user()\n"
            "  log(name)\n"
            "  log(age)\n"
            "}\n"
        )
        symbols = {
            symbol.name: symbol.type_name
            for symbol in ferra_lsp.local_symbols(self.uri, source)
        }
        self.assertEqual("str", symbols["name"])
        self.assertEqual("i64", symbols["age"])

        labels = [hint["label"] for hint in ferra_lsp.inlay_hints_for(self.uri, source)]
        self.assertIn(": str", labels)
        self.assertIn(": i64", labels)


    def test_inferred_tuple_return_adds_hints_to_destructured_bindings(self):
        source = (
            "func user() { ret (\"Bob\", 42) }\n"
            "func main() {\n"
            "  var name, age = user()\n"
            "}\n"
        )
        hints = ferra_lsp.inlay_hints_for(self.uri, source)
        by_position = {
            (hint["position"]["line"], hint["position"]["character"]): hint["label"]
            for hint in hints
        }
        self.assertEqual(": str", by_position[(2, 10)])
        self.assertEqual(": int", by_position[(2, 15)])
if __name__ == "__main__":
    unittest.main()
