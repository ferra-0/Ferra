# Ferra language server

`ferra_lsp.py` is a dependency-free LSP server for `.fe` files. It currently
provides:

- hover for variables, functions, structs, methods, and compiler built-ins;
  struct hover includes constructor parameters when a constructor is declared;
- go to definition (`Ctrl+Click` / `F12`) for variables, parameters, types,
  functions, fields, methods, imported symbols, and `take` paths;
- completion for declarations, keywords, built-ins, fields, and methods;
- recursive `take`/`ftake` indexing, including completion and hover for
  imported names;
- member diagnostics, completion, and navigation for imported global values;
- file and directory completion while typing paths such as `take "fe/` or
  `ftake "../shared/`;
- simple diagnostics for missing imports, unknown declared types, and unmatched
  delimiters;
- diagnostics for misspelled objects, unknown struct fields, and obvious unknown
  standalone statements.

Diagnostics are available through both the traditional push notification and
the LSP 3.17 document-diagnostic request, so editors refresh them reliably after
full or incremental document changes.

The compiler remains the source of truth. The language server intentionally uses
a small declaration index instead of duplicating the complete compiler parser.

The declaration index, local symbols, diagnostics, source masks, imported file
contents, line offsets, and directory listings are cached. Editing a document
invalidates analysis without re-diagnosing every other open file; filesystem
notifications invalidate recursive imports and path-completion entries.

## VS Code

From the Ferra repository root run:

```sh
./lang.sh
```

Then restart VS Code completely. `lang.sh` installs both the TextMate grammar and
the language client. By default it starts the server with `python3`; this can be
changed with the `ferra.lsp.pythonPath` setting.

The project root opened in VS Code and `FERRA_PATH` are used to resolve `take`
paths. `ftake` paths are resolved strictly relative to the importing file (or
as absolute paths), which matches the compiler and allows arbitrary source-file
names outside the standard library.

## Tests

```sh
python3 -m unittest discover -s ferralang/lsp/tests -v
```
