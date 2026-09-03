import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp_unused", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class UnusedVariableDiagnosticsTests(unittest.TestCase):
    SOURCE = (
        "fn probe(unused_parameter: i64, used_parameter: i64): i64 {\n"
        "  let unused_local: i64 = 1\n"
        "  let used_from_nested_scope: i64 = 2\n"
        "  if true {\n"
        "    log(used_from_nested_scope)\n"
        "  }\n"
        "  ret used_parameter\n"
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

    def test_does_not_report_unused_bindings(self):
        diagnostics = ferra_lsp.diagnostics_for(self.uri, self.SOURCE)
        unused = [
            diagnostic for diagnostic in diagnostics
            if diagnostic.get("code") == "unused-variable"
        ]
        self.assertEqual([], unused)

    def test_nested_scope_unused_binding_is_not_reported(self):
        source = (
            "fn main(): i64 {\n"
            "  let outer: i64 = 1\n"
            "  {\n"
            "    let inner: i64 = 2\n"
            "    log(outer)\n"
            "  }\n"
            "  ret 0\n"
            "}\n"
        )
        diagnostics = ferra_lsp.diagnostics_for(self.uri, source)
        unused = [
            diagnostic for diagnostic in diagnostics
            if diagnostic.get("code") == "unused-variable"
        ]
        self.assertEqual([], unused)


if __name__ == "__main__":
    unittest.main()
