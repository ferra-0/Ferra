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

    def test_typed_callback_field_infers_call_result(self):
        source = (
            "stct Task { poll: func(Task*): i8, data: ptr }\n"
            "func select(): func(i64!): i64 { ret null }\n"
            "func main() {\n"
            "  var task = Task{poll: null, data: null}\n"
            "  var result = task.poll(^task)\n"
            "}\n"
        )
        index = ferra_lsp.build_index(self.uri, source)
        select = next(
            symbol for symbol in index.by_name["select"]
            if symbol.kind == ferra_lsp.KIND_FUNCTION
        )
        self.assertEqual("func(i64!): i64", select.type_name)
        symbols = {
            symbol.name: symbol.type_name
            for symbol in ferra_lsp.local_symbols(self.uri, source)
        }
        self.assertEqual("i8", symbols["result"])

        hints = ferra_lsp.inlay_hints_for(self.uri, source)
        labels = {
            (hint["position"]["line"], hint["position"]["character"]):
                hint["label"]
            for hint in hints
        }
        self.assertEqual(": i8", labels[(4, 12)])

        field_offset = source.index("task.poll") + len("task.")
        position = ferra_lsp.offset_to_position(source, field_offset + 1)
        hover = ferra_lsp.hover_for(
            self.uri, source, position["line"], position["character"]
        )
        self.assertIsNotNone(hover)
        self.assertIn("poll: func(Task*): i8", hover["contents"]["value"])

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

    def test_imported_receiver_tuple_result_adds_hints_to_each_binding(self):
        fe = self.path.parent / "fe"
        fe.mkdir()
        (fe / "http.fe").write_text(
            "stct Http {}\n"
            "impl Http get(url: str): (str, bol) { ret (url, true) }\n"
            "const http: Http\n",
            encoding="utf-8",
        )
        source = (
            'take "fe/http.fe"\n'
            "func main() {\n"
            '  var r1, ok = http.get("https://example.com")\n'
            "}\n"
        )
        self.path.write_text(source, encoding="utf-8")

        symbols = {
            symbol.name: symbol.type_name
            for symbol in ferra_lsp.local_symbols(self.uri, source)
        }
        self.assertEqual("str", symbols["r1"])
        self.assertEqual("bol", symbols["ok"])

        hints = ferra_lsp.inlay_hints_for(self.uri, source)
        by_position = {
            (hint["position"]["line"], hint["position"]["character"]): hint["label"]
            for hint in hints
        }
        self.assertEqual(": str", by_position[(2, 8)])
        self.assertEqual(": bol", by_position[(2, 12)])


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

    def test_grouped_declarations_add_a_hint_to_every_binding(self):
        source = (
            "func main() {\n"
            "  var i32(a = 1, b = 2, c = 3)\n"
            "  const i8(B_SOME = 0 pass, B_VOID, B_INT)\n"
            "  var\n"
            "    x = 1,\n"
            "    y = 2,\n"
            "    z = 3\n"
            "}\n"
        )
        hints = ferra_lsp.inlay_hints_for(self.uri, source)
        by_position = {
            (hint["position"]["line"], hint["position"]["character"]): hint["label"]
            for hint in hints
        }

        for name in ("a", "b", "c"):
            offset = source.index(f"{name} =")
            position = ferra_lsp.offset_to_position(source, offset + len(name))
            self.assertEqual(
                ": i32",
                by_position[(position["line"], position["character"])],
            )

        for name in ("B_SOME", "B_VOID", "B_INT"):
            offset = source.index(name)
            position = ferra_lsp.offset_to_position(source, offset + len(name))
            self.assertEqual(
                ": i8",
                by_position[(position["line"], position["character"])],
            )

        for name in ("x", "y", "z"):
            offset = source.index(f"{name} =")
            position = ferra_lsp.offset_to_position(source, offset + len(name))
            self.assertEqual(
                ": int",
                by_position[(position["line"], position["character"])],
            )
        symbols = {
            symbol.name: symbol.type_name
            for symbol in ferra_lsp.local_symbols(self.uri, source)
        }
        for name in ("a", "b", "c"):
            self.assertEqual("i32", symbols[name])
        for name in ("x", "y", "z"):
            self.assertEqual("int", symbols[name])
        for name in ("B_SOME", "B_VOID", "B_INT"):
            self.assertEqual("i8", symbols[name])

        for name, expected in (
            ("a", "i32"), ("b", "i32"), ("c", "i32"),
            ("x", "int"), ("y", "int"), ("z", "int"),
        ):
            offset = source.index(f"{name} =")
            position = ferra_lsp.offset_to_position(source, offset)
            hover = ferra_lsp.hover_for(
                self.uri, source, position["line"], position["character"]
            )
            self.assertIsNotNone(hover)
            self.assertIn(
                f"{name}: {expected}", hover["contents"]["value"]
            )

        for name in ("B_SOME", "B_VOID", "B_INT"):
            offset = source.index(name)
            position = ferra_lsp.offset_to_position(source, offset)
            hover = ferra_lsp.hover_for(
                self.uri, source, position["line"], position["character"]
            )
            self.assertIn(f"{name}: i8", hover["contents"]["value"])


    def test_module_index_exports_automatic_enum_bindings(self):
        source = "const i8(B_SOME = 0 pass, B_VOID, B_INT)\nfunc main() {}\n"
        index = ferra_lsp.build_index(self.uri, source)
        constants = {
            symbol.name: (symbol.type_name, symbol.kind)
            for symbol in index.symbols
            if symbol.name.startswith("B_")
        }
        self.assertEqual(
            {name: ("i8", ferra_lsp.KIND_CONSTANT)
             for name in ("B_SOME", "B_VOID", "B_INT")},
            constants,
        )

    def test_const_returns_flow_into_inferred_and_destructured_hints(self):
        source = (
            "func number(): i64! { ret 7 }\n"
            "func partial(): (str!, bol) { ret (\"Bob\", true) }\n"
            "func whole(): (str, bol)! { ret (\"Alice\", false) }\n"
            "func main() {\n"
            "  var n = number()\n"
            "  var name, ok = partial()\n"
            "  var whole_name, whole_ok = whole()\n"
            "}\n"
        )
        symbols = {
            symbol.name: symbol.type_name
            for symbol in ferra_lsp.local_symbols(self.uri, source)
        }
        self.assertEqual("i64!", symbols["n"])
        self.assertEqual("str!", symbols["name"])
        self.assertEqual("bol", symbols["ok"])
        self.assertEqual("str!", symbols["whole_name"])
        self.assertEqual("bol!", symbols["whole_ok"])

        labels = [
            hint["label"]
            for hint in ferra_lsp.inlay_hints_for(self.uri, source)
        ]
        for expected in (": i64!", ": str!", ": bol", ": bol!"):
            self.assertIn(expected, labels)

        name_offset = source.index("name, ok")
        position = ferra_lsp.offset_to_position(
            source, name_offset + 1
        )
        hover = ferra_lsp.hover_for(
            self.uri, source, position["line"], position["character"]
        )
        self.assertIsNotNone(hover)
        self.assertIn("name: str!", hover["contents"]["value"])
if __name__ == "__main__":
    unittest.main()
