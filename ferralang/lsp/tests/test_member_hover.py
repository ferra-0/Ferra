import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp_members", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class MemberHoverTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        (self.root / "vec.fe").write_text(
            "stct Vec { size: i64 }\n",
            encoding="utf-8",
        )
        (self.root / "env.fe").write_text(
            "stct Env {}\nimpl Env get(p: str): str { ret null }\n",
            encoding="utf-8",
        )
        (self.root / "file.fe").write_text(
            "stct File {}\nimpl File size(): i64 { ret 0 }\n",
            encoding="utf-8",
        )
        (self.root / "json.fe").write_text(
            "stct Json {}\n"
            "impl Json size(): i64 { ret 0 }\n"
            "impl Json get(key: str, out: Json): bol { ret true }\n",
            encoding="utf-8",
        )
        self.source = (
            'ftake "vec.fe"\n'
            'ftake "env.fe"\n'
            'ftake "file.fe"\n'
            'ftake "json.fe"\n'
            'fn main(): i64 {\n'
            '  let j: File()\n'
            '  j.size()\n'
            '  let cfg: Json()\n'
            '  let entryr: Json()\n'
            '  cfg.get("entry", entryr)\n'
            '  let objs: Json()\n'
            '  objs.size()\n'
            '  ret 0\n'
            '}\n'
        )
        self.path = self.root / "main.fe"
        self.path.write_text(self.source, encoding="utf-8")
        self.uri = self.path.as_uri()

    def tearDown(self):
        self.directory.cleanup()

    def hover_at(self, needle: str, occurrence: int = 0):
        start = -1
        for _ in range(occurrence + 1):
            start = self.source.index(needle, start + 1)
        member_start = start + needle.index(".") + 1
        position = ferra_lsp.offset_to_position(self.source, member_start + 1)
        return ferra_lsp.hover_for(
            self.uri,
            self.source,
            position["line"],
            position["character"],
        )

    def test_file_method_uses_receiver_type(self):
        hover = self.hover_at("j.size")
        self.assertIsNotNone(hover)
        value = hover["contents"]["value"]
        self.assertIn("impl File size(): i64", value)
        self.assertIn("file.fe", value)
        self.assertNotIn("vec.fe", value)

    def test_json_method_does_not_resolve_to_env_method(self):
        hover = self.hover_at("cfg.get")
        self.assertIsNotNone(hover)
        value = hover["contents"]["value"]
        self.assertIn("impl Json get(key: str, out: Json): bol", value)
        self.assertIn("json.fe", value)
        self.assertNotIn("impl Env get", value)

    def test_same_member_name_resolves_for_another_receiver(self):
        hover = self.hover_at("objs.size")
        self.assertIsNotNone(hover)
        value = hover["contents"]["value"]
        self.assertIn("impl Json size(): i64", value)
        self.assertIn("json.fe", value)
        self.assertNotIn("size: i64", value)

    def test_nearest_local_wins_over_earlier_parameter_with_same_name(self):
        self.source = (
            'ftake "json.fe"\n'
            'fn write(raw: ptr, out: ptr): i32 { ret 0 }\n'
            'fn main(): i64 {\n'
            '  let out: Json()\n'
            '  out.size()\n'
            '  ret 0\n'
            '}\n'
        )
        self.path.write_text(self.source, encoding="utf-8")

        hover = self.hover_at("out.size")
        self.assertIsNotNone(hover)
        value = hover["contents"]["value"]
        self.assertIn("impl Json size(): i64", value)
        self.assertIn("json.fe", value)

    def test_drop_body_has_this_receiver(self):
        source = (
            "stct Value { text: str }\n"
            "drop Value() {\n"
            "  if this.text not null {\n"
            "    free(this.text)\n"
            "  }\n"
            "}\n"
        )
        path = self.root / "value.fe"
        path.write_text(source, encoding="utf-8")
        uri = path.as_uri()

        diagnostics = ferra_lsp.diagnostics_for(uri, source)
        self.assertFalse(any(
            "before field access" in diagnostic["message"]
            for diagnostic in diagnostics
        ))

        this_start = source.index("this.text")
        this_position = ferra_lsp.offset_to_position(
            source, this_start + 1
        )
        this_hover = ferra_lsp.hover_for(
            uri, source,
            this_position["line"], this_position["character"],
        )
        self.assertIsNotNone(this_hover)
        self.assertIn(
            "this: Value", this_hover["contents"]["value"]
        )

        field_start = this_start + len("this.")
        field_position = ferra_lsp.offset_to_position(
            source, field_start + 1
        )
        field_hover = ferra_lsp.hover_for(
            uri, source,
            field_position["line"], field_position["character"],
        )
        self.assertIsNotNone(field_hover)
        self.assertIn(
            "text: str", field_hover["contents"]["value"]
        )

    def test_tuple_returning_method_has_this_receiver(self):
        source = (
            "stct Http { handle: str }\n"
            "impl Http get(url: str): (str, bol) {\n"
            "  this.handle = url\n"
            "  ret (url, true)\n"
            "}\n"
        )
        path = self.root / "http.fe"
        path.write_text(source, encoding="utf-8")
        uri = path.as_uri()

        diagnostics = ferra_lsp.diagnostics_for(uri, source)
        self.assertFalse(any(
            "'this' before field access" in diagnostic["message"]
            for diagnostic in diagnostics
        ))

        this_start = source.index("this.handle")
        position = ferra_lsp.offset_to_position(source, this_start + 1)
        hover = ferra_lsp.hover_for(
            uri, source, position["line"], position["character"]
        )
        self.assertIsNotNone(hover)
        self.assertIn("this: Http", hover["contents"]["value"])

    def test_this_receiver_does_not_escape_drop_body(self):
        source = (
            "stct Value { text: str }\n"
            "drop Value() { free(this.text) }\n"
            "fn main(): i64 {\n"
            "  this.text\n"
            "  ret 0\n"
            "}\n"
        )
        path = self.root / "receiver_scope.fe"
        path.write_text(source, encoding="utf-8")
        uri = path.as_uri()

        diagnostics = ferra_lsp.diagnostics_for(uri, source)
        self.assertTrue(any(
            "'this' before field access" in diagnostic["message"]
            for diagnostic in diagnostics
        ))

    def test_multiline_generic_fields_do_not_merge(self):
        source = (
            "stct Token {}\n"
            "stct AttrUse {}\n"
            "stct AttrDef {}\n"
            "stct PtrPolicy {}\n"
            "stct Parser {\n"
            "  tokens: Vec<Token>,\n"
            "  pos: usize,\n"
            "  attrDefs: HashMap<str, AttrDef*, PtrPolicy>,\n"
            "  attrsUseWait: Vec<AttrUse>\n"
            "}\n"
            "impl Parser Parser() {\n"
            "  this.pos = 0\n"
            "}\n"
        )
        path = self.root / "parser.fe"
        path.write_text(source, encoding="utf-8")
        uri = path.as_uri()

        index = ferra_lsp.build_index(uri, source)
        fields = {
            symbol.name: symbol.type_name
            for symbol in index.fields["Parser"]
        }
        self.assertEqual(fields["tokens"], "Vec<Token>")
        self.assertEqual(fields["pos"], "usize")
        self.assertEqual(
            fields["attrDefs"], "HashMap<str, AttrDef*, PtrPolicy>"
        )

        diagnostics = ferra_lsp.diagnostics_for(uri, source)
        self.assertFalse(any(
            "has no field -> 'pos'" in diagnostic["message"]
            for diagnostic in diagnostics
        ))

        member_start = source.index("this.pos") + len("this.")
        position = ferra_lsp.offset_to_position(source, member_start + 1)
        hover = ferra_lsp.hover_for(
            uri, source, position["line"], position["character"]
        )
        self.assertIsNotNone(hover)
        self.assertIn("pos: usize", hover["contents"]["value"])


if __name__ == "__main__":
    unittest.main()
