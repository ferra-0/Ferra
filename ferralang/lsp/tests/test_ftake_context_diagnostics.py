import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp_context", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class FTakeContextDiagnosticTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        (self.root / "types.fe").write_text(
            "stct Token { value: str }\n"
            "stct Result { value: i32 }\n",
            encoding="utf-8",
        )
        (self.root / "root.fe").write_text(
            'ftake "./types.fe"\n'
            "stct Parser { tokens: Vec<Token>, pos: usize }\n"
            "impl Parser make(): Result { ret Result{value: 1} }\n"
            'ftake "./child.fe"\n',
            encoding="utf-8",
        )
        self.child = self.root / "child.fe"
        self.source = (
            "impl Parser parse() {\n"
            "  var parsed = this.make()\n"
            "  log(parsed.value)\n"
            "  var split = this.tokens[this.pos]\n"
            "  log(split.value)\n"
            "}\n"
        )
        self.child.write_text(self.source, encoding="utf-8")
        self.uri = self.child.as_uri()
        ferra_lsp.invalidate_analysis_caches(clear_files=True)

    def tearDown(self):
        ferra_lsp.invalidate_analysis_caches(clear_files=True)
        self.directory.cleanup()

    def test_reverse_ftake_context_supplies_parent_types(self):
        index = ferra_lsp.build_index(self.uri, self.source)
        self.assertIn("Parser", index.structs)
        self.assertIn("Result", index.structs)
        self.assertIn("Token", index.structs)

    def test_declared_values_do_not_get_false_undefined_diagnostics(self):
        messages = [
            diagnostic["message"]
            for diagnostic in ferra_lsp.diagnostics_for(self.uri, self.source)
        ]
        self.assertFalse(any("Unknown type" in message for message in messages))
        self.assertFalse(any(
            "Undefined value -> 'parsed'" in message
            or "Undefined value -> 'split'" in message
            for message in messages
        ))

        symbols = {
            symbol.name: symbol.type_name
            for symbol in ferra_lsp.local_symbols(self.uri, self.source)
        }
        self.assertEqual("Result", symbols["parsed"])
        self.assertIn("split", symbols)


if __name__ == "__main__":
    unittest.main()
