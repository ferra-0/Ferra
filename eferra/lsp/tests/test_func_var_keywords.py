import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "eferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("eferra_lsp_func_var", MODULE_PATH)
lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(lsp)


class FuncVarKeywordsTests(unittest.TestCase):
    def test_indexes_primary_function_and_variable_keywords(self):
        uri = "file:///tmp/func_var_keywords.efe"
        text = (
            "func add(a, b) { ret a + b }\n"
            "var answer = add(20, 22)\n"
            "log(answer)\n"
        )
        analysis = lsp.analyze(uri, text)
        self.assertFalse(analysis.diagnostics)
        self.assertEqual("func add(a, b)", analysis.functions["add"].symbol.signature)

        answer = lsp.hover_for(
            uri, text, lsp.offset_to_position(text, text.index("answer") + 1)
        )
        self.assertIn("var answer: any", answer["contents"]["value"])


if __name__ == "__main__":
    unittest.main()
