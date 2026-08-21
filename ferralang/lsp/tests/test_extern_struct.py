import importlib.util
from pathlib import Path
import tempfile
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class ExternStructTests(unittest.TestCase):
    def test_extern_struct_is_indexed_and_hovered_as_opaque(self):
        source = (
            "extern stct pthread_mutex_t\n"
            "let mutex: pthread_mutex_t*\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "main.fe"
            path.write_text(source, encoding="utf-8")
            uri = path.as_uri()

            index = ferra_lsp.build_index(uri, source)
            self.assertIn("pthread_mutex_t", index.structs)
            self.assertFalse(any(
                "WTF is this type" in diagnostic["message"]
                for diagnostic in ferra_lsp.diagnostics_for(uri, source)
            ))

            hover = ferra_lsp.hover_for(uri, source, 0, 15)
            self.assertIsNotNone(hover)
            value = hover["contents"]["value"]
            self.assertIn("extern stct pthread_mutex_t", value)
            self.assertIn("Opaque external type", value)

    def test_opaque_extern_value_has_actionable_diagnostic(self):
        source = (
            "extern stct pthread_mutex_t\n"
            "let mutex: pthread_mutex_t\n"
        )
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "main.fe"
            path.write_text(source, encoding="utf-8")
            diagnostics = ferra_lsp.diagnostics_for(path.as_uri(), source)
            self.assertTrue(any(
                "opaque extern struct" in diagnostic["message"]
                for diagnostic in diagnostics
            ))


if __name__ == "__main__":
    unittest.main()
