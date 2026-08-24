import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "ferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("ferra_lsp_func_var", MODULE_PATH)
ferra_lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(ferra_lsp)


class FuncVarKeywordsTests(unittest.TestCase):
    SOURCE = (
        "func echo(value: str): str { ret value }\n"
        "func main(): i64 {\n"
        "  var answer = echo(\"ok\")\n"
        "  log(answer)\n"
        "  ret 0\n"
        "}\n"
    )

    def test_indexes_primary_function_and_variable_keywords(self):
        uri = "file:///tmp/func_var_keywords.fe"
        index = ferra_lsp.build_index(uri, self.SOURCE)
        echo = next(symbol for symbol in index.symbols if symbol.name == "echo")
        self.assertEqual("func echo(value: str): str", echo.signature)

        locals_by_name = {
            symbol.name: symbol.type_name
            for symbol in ferra_lsp.local_symbols(uri, self.SOURCE)
        }
        self.assertEqual("str", locals_by_name["answer"])
        self.assertFalse(ferra_lsp.diagnostics_for(uri, self.SOURCE))


if __name__ == "__main__":
    unittest.main()
