# eFerra LSP

A small dependency-free Python language server for dynamic `.efe` programs.
It does not invoke the compiler and keeps a cached single-document index.

Features:

- completion for variables, functions, keywords and native objects;
- member completion for native globals, object literals, and inferred
  string/array/object values;
- hover, signature help, document symbols and go-to-definition;
- diagnostics for delimiters, strings, declarations, unknown names, duplicate
  parameters/functions, native/primitive methods and argument counts.

Native globals and methods are discovered from `eferra/natives.fe` through
`FERRA_PATH` or the opened workspace, so newly registered bindings appear in
completion without changing the LSP.

The server can also be launched directly:

```sh
python3 eferra/lsp/eferra_lsp.py
```

Run the tests from this directory with:

```sh
python3 -m unittest discover -s tests -v
```
