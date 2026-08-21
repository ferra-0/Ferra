import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp_cache", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class AnalysisCacheTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = Path(self.directory.name)
        self.imported = self.root / "shared.fe"
        self.imported.write_text(
            "stct Shared { value: i64 }\n",
            encoding="utf-8",
        )
        self.source = (
            'ftake "shared.fe"\n'
            "fn main(): i64 {\n"
            "  let shared: Shared()\n"
            "  log(shared.value)\n"
            "  ret 0\n"
            "}\n"
        )
        self.path = self.root / "main.fe"
        self.path.write_text(self.source, encoding="utf-8")
        self.uri = self.path.as_uri()
        ferra_lsp.invalidate_analysis_caches(clear_files=True)

    def tearDown(self):
        ferra_lsp.invalidate_analysis_caches(clear_files=True)
        self.directory.cleanup()

    def test_reuses_index_locals_and_diagnostics_for_unchanged_text(self):
        first_index = ferra_lsp.build_index(self.uri, self.source)
        first_locals = ferra_lsp.local_symbols(self.uri, self.source)
        first_diagnostics = ferra_lsp.diagnostics_for(self.uri, self.source)

        self.assertIs(first_index, ferra_lsp.build_index(self.uri, self.source))
        self.assertIs(
            first_locals,
            ferra_lsp.local_symbols(self.uri, self.source),
        )
        self.assertIs(
            first_diagnostics,
            ferra_lsp.diagnostics_for(self.uri, self.source),
        )

    def test_import_change_invalidates_recursive_index(self):
        first = ferra_lsp.build_index(self.uri, self.source)
        self.assertIn("Shared", first.structs)

        self.imported.write_text(
            "stct RenamedShared { value: i64, extra: i32 }\n",
            encoding="utf-8",
        )
        second = ferra_lsp.build_index(self.uri, self.source)

        self.assertIsNot(first, second)
        self.assertNotIn("Shared", second.structs)
        self.assertIn("RenamedShared", second.structs)

    def test_text_change_replaces_only_the_document_analysis(self):
        first = ferra_lsp.build_index(self.uri, self.source)
        changed = self.source.replace("shared.value", "shared.missing")
        second = ferra_lsp.build_index(self.uri, changed)
        self.assertIsNot(first, second)


if __name__ == "__main__":
    unittest.main()
