import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp_ftake", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class FTakeTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.assets = self.root / "assets"
        self.nested = self.assets / "nested"
        self.nested.mkdir(parents=True)
        (self.nested / "value.code").write_text(
            "fn ftaken_value(): i64 { ret 42 }\n",
            encoding="utf-8",
        )
        (self.assets / "module.any").write_text(
            'ftake "nested/value.code"\n',
            encoding="utf-8",
        )
        (self.assets / "random.txt").write_text(
            "fn text_value(): i64 { ret 1 }\n",
            encoding="utf-8",
        )
        self.source = (
            'ftake "assets/module.any"\n'
            'fn main(): i64 {\n'
            '  ret ftaken_value()\n'
            '}\n'
        )
        self.path = self.root / "main.fe"
        self.path.write_text(self.source, encoding="utf-8")
        self.uri = self.path.as_uri()

    def tearDown(self):
        self.directory.cleanup()

    def test_indexes_nested_arbitrary_files(self):
        index = ferra_lsp.build_index(self.uri, self.source)
        symbol = next(
            item for item in index.symbols if item.name == "ftaken_value"
        )
        self.assertEqual(
            (self.nested / "value.code").resolve().as_uri(),
            symbol.uri,
        )
        self.assertTrue(symbol.imported)

    def test_hover_and_definition_use_ftaken_symbol(self):
        offset = self.source.index("ftaken_value") + 2
        position = ferra_lsp.offset_to_position(self.source, offset)
        hover = ferra_lsp.hover_for(
            self.uri, self.source, position["line"], position["character"]
        )
        self.assertIsNotNone(hover)
        self.assertIn("fn ftaken_value(): i64", hover["contents"]["value"])
        self.assertIn("value.code", hover["contents"]["value"])

        definition = ferra_lsp.definition_for(
            self.uri, self.source, position["line"], position["character"]
        )
        self.assertIsNotNone(definition)
        self.assertEqual(
            (self.nested / "value.code").resolve().as_uri(),
            definition["uri"],
        )

    def test_ctrl_click_on_ftake_path_opens_exact_file(self):
        offset = self.source.index("module.any") + 2
        position = ferra_lsp.offset_to_position(self.source, offset)
        definition = ferra_lsp.definition_for(
            self.uri, self.source, position["line"], position["character"]
        )
        self.assertIsNotNone(definition)
        self.assertEqual(
            (self.assets / "module.any").resolve().as_uri(),
            definition["uri"],
        )

    def test_completion_accepts_files_with_any_extension(self):
        source = 'ftake "assets/ran'
        result = ferra_lsp.completion_for(
            self.uri, source, 0, len(source)
        )
        self.assertIn(
            "random.txt",
            {item["label"] for item in result["items"]},
        )

        take_source = 'take "assets/ran'
        take_result = ferra_lsp.completion_for(
            self.uri, take_source, 0, len(take_source)
        )
        self.assertNotIn(
            "random.txt",
            {item["label"] for item in take_result["items"]},
        )


if __name__ == "__main__":
    unittest.main()
