import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).resolve().parents[1] / "eferra_lsp.py"
SPEC = importlib.util.spec_from_file_location("eferra_lsp_test", MODULE_PATH)
lsp = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(lsp)


class EFerraLspTests(unittest.TestCase):
    def setUp(self):
        lsp.analysis_cache.clear()
        self.uri = "file:///tmp/example.efe"

    def position(self, text, needle, inside=1):
        return lsp.offset_to_position(text, text.index(needle) + inside)

    def test_indexes_functions_parameters_and_locals(self):
        text = (
            "fn add(a, b) { ret a + b }\n"
            "let answer = add(20, 22)\n"
            "log(answer)\n"
        )
        analysis = lsp.analyze(self.uri, text)
        self.assertFalse(analysis.diagnostics)
        self.assertEqual(("a", "b"), analysis.functions["add"].parameters)
        self.assertEqual("any", analysis.by_name["answer"][0].value_kind)

        hover = lsp.hover_for(
            self.uri, text, self.position(text, "answer", 2)
        )
        self.assertIn("var answer: any", hover["contents"]["value"])

        call = text.index("add(20")
        definition = lsp.definition_for(
            self.uri, text, lsp.offset_to_position(text, call + 1)
        )
        self.assertEqual(
            lsp.offset_to_position(text, text.index("add")),
            definition["range"]["start"],
        )

    def test_native_hover_completion_and_signature_help(self):
        text = "let result = json.parse('{}')\nmath.si"
        hover = lsp.hover_for(
            self.uri, text, self.position(text, "parse", 2)
        )
        self.assertIn("json.parse(text)", hover["contents"]["value"])

        completion = lsp.completion_for(
            self.uri, text, lsp.offset_to_position(text, len(text))
        )
        labels = {item["label"] for item in completion["items"]}
        self.assertEqual({"sin", "cos", "tan", "rand", "randNum"}, labels)

        source = "json.stringify("
        help_value = lsp.signature_help_for(
            self.uri, source, lsp.offset_to_position(source, len(source))
        )
        self.assertEqual(
            "json.stringify(value)",
            help_value["signatures"][0]["label"],
        )

    def test_thread_create_and_handle_members(self):
        text = (
            "fn worker(value) { ret value }\n"
            "let task = thread.create(worker, [42])\n"
            "task."
        )
        completion = lsp.completion_for(
            self.uri, text, lsp.offset_to_position(text, len(text))
        )
        self.assertEqual(
            {"join", "done"},
            {item["label"] for item in completion["items"]},
        )
        source = "thread.create("
        help_value = lsp.signature_help_for(
            self.uri, source, lsp.offset_to_position(source, len(source))
        )
        self.assertEqual(
            "thread.create(f, args)",
            help_value["signatures"][0]["label"],
        )

    def test_buffer_and_stream_members(self):
        text = (
            "let chunk = buffer.create(4096)\n"
            "let source = file.stream('data.bin')\n"
            "chunk.\n"
            "source.\n"
        )
        chunk_end = text.index("chunk.") + len("chunk.")
        completion = lsp.completion_for(
            self.uri, text, lsp.offset_to_position(text, chunk_end)
        )
        self.assertEqual(
            {
                "len", "capacity", "clear", "reserve", "append",
                "str", "bytes", "get", "set",
            },
            {item["label"] for item in completion["items"]},
        )

        completion = lsp.completion_for(
            self.uri, text, lsp.offset_to_position(text, len(text))
        )
        self.assertEqual(
            {"read", "done", "close", "error", "status"},
            {item["label"] for item in completion["items"]},
        )

        source = "http.stream_post("
        help_value = lsp.signature_help_for(
            self.uri, source, lsp.offset_to_position(source, len(source))
        )
        self.assertEqual(
            "http.stream_post(url, body)",
            help_value["signatures"][0]["label"],
        )

    def test_object_field_hover_completion_and_definition(self):
        text = "let cfg = {entry: 1, mode: 'dev'}\nlog(cfg.entry)\ncfg."
        position = self.position(text, "cfg.entry", len("cfg."))
        hover = lsp.hover_for(self.uri, text, position)
        self.assertIn("entry: dynamic", hover["contents"]["value"])

        definition = lsp.definition_for(self.uri, text, position)
        expected = lsp.offset_to_position(text, text.index("entry"))
        self.assertEqual(expected, definition["range"]["start"])

        completion = lsp.completion_for(
            self.uri, text, lsp.offset_to_position(text, len(text))
        )
        self.assertEqual(
            {
                "entry", "mode", "len", "has", "get", "set",
                "remove", "keys", "values",
            },
            {item["label"] for item in completion["items"]},
        )

    def test_primitive_method_hover_completion_signature_and_diagnostics(self):
        text = (
            'let text = "hello"\n'
            'let values = [1, 2]\n'
            'let object = {name: "Ferra"}\n'
            'log(text.starts_with("he"))\n'
            'values.\n'
        )
        hover = lsp.hover_for(
            self.uri, text, self.position(text, "starts_with", 3)
        )
        self.assertIn("String.starts_with(prefix)", hover["contents"]["value"])

        completion = lsp.completion_for(
            self.uri, text, lsp.offset_to_position(text, text.index("values.") + 7)
        )
        self.assertEqual(
            {"len", "push", "pop", "first", "last", "contains", "join", "map"},
            {item["label"] for item in completion["items"]},
        )

        map_source = "let values = [1, 2]\nvalues.map("
        map_help = lsp.signature_help_for(
            self.uri,
            map_source,
            lsp.offset_to_position(map_source, len(map_source)),
        )
        self.assertEqual(
            "Array.map(callback)",
            map_help["signatures"][0]["label"],
        )

        source = "let object = {name: 'Ferra'}\nobject.set("
        help_value = lsp.signature_help_for(
            self.uri, source, lsp.offset_to_position(source, len(source))
        )
        self.assertEqual(
            "Object.set(key, value)",
            help_value["signatures"][0]["label"],
        )

        bad = 'let text = "x"\ntext.push(1)\n'
        messages = [
            item["message"] for item in lsp.analyze(self.uri, bad).diagnostics
        ]
        self.assertTrue(any("Unknown str method -> 'push'" in value for value in messages))

        literal = 'log("hello".len())\n[1, 2].'
        literal_analysis = lsp.analyze(self.uri, literal)
        self.assertFalse(any(
            "expects" in item["message"] for item in literal_analysis.diagnostics
        ))
        literal_completion = lsp.completion_for(
            self.uri, literal, lsp.offset_to_position(literal, len(literal))
        )
        self.assertIn(
            "push", {item["label"] for item in literal_completion["items"]}
        )

    def test_take_and_ftake_are_keywords(self):
        text = 'take "library.efe"\nftake "relative.efe"\n'
        messages = [
            item["message"] for item in lsp.analyze(self.uri, text).diagnostics
        ]
        self.assertFalse(any("Unknown name" in value for value in messages))

    def test_imported_functions_are_visible(self):
        source = MODULE_PATH.parents[1] / "testdata" / "imports" / "main.efe"
        text = source.read_text(encoding="utf-8")
        uri = source.resolve().as_uri()
        analysis = lsp.analyze(uri, text)
        messages = [item["message"] for item in analysis.diagnostics]
        self.assertFalse(any("Unknown name" in value for value in messages))

        position = self.position(text, "imported_left", 3)
        hover = lsp.hover_for(uri, text, position)
        self.assertIn("func imported_left()", hover["contents"]["value"])
        definition = lsp.definition_for(uri, text, position)
        self.assertTrue(definition["uri"].endswith("/root_take.efe"))

    def test_arrow_function_single_parameter_has_no_false_duplicate_warning(self):
        text = "let sqrt = x -> { ret x * x }\n"
        messages = [
            item["message"] for item in lsp.analyze(self.uri, text).diagnostics
        ]
        self.assertFalse(any("Duplicate parameter" in value for value in messages))
        self.assertFalse(any("Unknown name -> 'x'" in value for value in messages))

    def test_inline_arrow_parameter_is_visible_in_map(self):
        text = "var doubled = [1, 2].map(x -> x * 2)\n"
        analysis = lsp.analyze(self.uri, text)
        messages = [item["message"] for item in analysis.diagnostics]
        self.assertFalse(any("Unknown name -> 'x'" in value for value in messages))

        body_x = text.index("x * 2")
        hover = lsp.hover_for(
            self.uri, text, lsp.offset_to_position(text, body_x)
        )
        self.assertIn("x: dynamic parameter", hover["contents"]["value"])

    def test_nested_inline_arrow_parameters_have_separate_scopes(self):
        text = (
            "var nested = [[1]].map(group -> group.map(x -> x + 1))\n"
            "log(x)\n"
        )
        messages = [
            item["message"] for item in lsp.analyze(self.uri, text).diagnostics
        ]
        self.assertFalse(any("Unknown name -> 'group'" in value for value in messages))
        self.assertEqual(1, messages.count("Unknown name -> 'x'"))

    def test_inline_block_arrow_parameter_is_visible(self):
        text = "var doubled = [1, 2].map((x) -> { ret x * 2 })\n"
        messages = [
            item["message"] for item in lsp.analyze(self.uri, text).diagnostics
        ]
        self.assertFalse(any("Unknown name -> 'x'" in value for value in messages))

    def test_reports_simple_syntax_and_semantic_errors(self):
        text = (
            "fn bad(a, a) {\n"
            "  let value 42\n"
            "  timer.minutes()\n"
            "  json.parse()\n"
            "  mystery\n"
        )
        messages = [
            item["message"] for item in lsp.analyze(self.uri, text).diagnostics
        ]
        self.assertTrue(any("Duplicate parameter" in value for value in messages))
        self.assertTrue(any("Expected '='" in value for value in messages))
        self.assertTrue(any("Missing closing -> '}'" in value for value in messages))
        self.assertTrue(any("timer.minutes" in value for value in messages))
        self.assertTrue(any("expects -> 1 argument" in value for value in messages))
        self.assertTrue(any("Unknown name -> 'mystery'" in value for value in messages))

    def test_analysis_is_cached_until_text_changes(self):
        first = lsp.analyze(self.uri, "let value = 1\n")
        self.assertIs(first, lsp.analyze(self.uri, "let value = 1\n"))
        second = lsp.analyze(self.uri, "let value = 2\n")
        self.assertIsNot(first, second)

    def test_existing_eferra_json_program_has_no_false_diagnostics(self):
        path = Path(__file__).resolve().parents[2] / "tests/native_json.efe"
        if not path.exists():
            self.skipTest("Ferra repository fixture is unavailable")
        text = path.read_text(encoding="utf-8")
        self.assertEqual([], lsp.analyze(path.as_uri(), text).diagnostics)

    def test_discovers_registered_native_methods(self):
        source = (
            'fn register_net(module: Module*) {\n'
            '  module.add_native_method(net_type, "fetch", 2, callback)\n'
            '  if module.find_native_global("net") < 0 { pass }\n'
            '}\n'
        )
        discovered = lsp.discover_native_members(source)
        self.assertEqual(2, discovered["net"]["fetch"][1])
        self.assertEqual("net.fetch(arg1, arg2)", discovered["net"]["fetch"][0])


if __name__ == "__main__":
    unittest.main()
