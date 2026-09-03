import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp_optional", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class OptionalArgumentTests(unittest.TestCase):
    SOURCE = (
        "stct Some { value: i32 }\n"
        "impl Some Some(value: i32 = 3) { this.value = value }\n"
        "func sum(value: i32 = 2): i32 { ret value }\n"
        "func main(): i64 {\n"
        "  var item = Some(5)\n"
        "  ret sum() + item.value\n"
        "}\n"
    )

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.path = Path(self.directory.name) / "main.fe"
        self.path.write_text(self.SOURCE, encoding="utf-8")
        self.uri = self.path.as_uri()
        ferra_lsp.invalidate_analysis_caches(clear_files=True)

    def tearDown(self):
        ferra_lsp.invalidate_analysis_caches(clear_files=True)
        self.directory.cleanup()

    def test_signatures_keep_default_values(self):
        index = ferra_lsp.build_index(self.uri, self.SOURCE)
        function = next(
            symbol for symbol in index.by_name["sum"]
            if symbol.kind == ferra_lsp.KIND_FUNCTION
        )
        constructor = next(
            symbol for symbol in index.methods["Some"]
            if symbol.kind == ferra_lsp.KIND_CONSTRUCTOR
        )
        self.assertIn("value: i32 = 2", function.signature)
        self.assertIn("value: i32 = 3", constructor.signature)

    def test_constructor_call_infers_struct_type_and_inlay_hint(self):
        item = next(
            symbol for symbol in ferra_lsp.local_symbols(self.uri, self.SOURCE)
            if symbol.name == "item"
        )
        self.assertEqual("Some", item.type_name)
        labels = {
            hint["label"]
            for hint in ferra_lsp.inlay_hints_for(self.uri, self.SOURCE)
        }
        self.assertIn(": Some", labels)


