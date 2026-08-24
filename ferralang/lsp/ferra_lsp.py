#!/usr/bin/env python3

"""A small, dependency-free language server for Ferra.

The parser is deliberately lightweight: it indexes declarations well enough for
editor features while the Ferra compiler remains the source of truth.
"""

from dataclasses import dataclass, field
from bisect import bisect_right
import difflib
from functools import lru_cache
import json
import os
from pathlib import Path
import re
import sys
from typing import Dict, Iterable, List, Optional, Set, Tuple
from urllib.parse import unquote, urlparse

documents: Dict[str, str] = {}
workspace_root: Optional[Path] = None

PRIMITIVE_TYPES = {
    "nul", "bol", "str", "ptr", "func", "fn", "tup", "i1",
    "i8", "i16", "i32", "i64",
    "u8", "u16", "u32", "u64",
    "isize", "usize", "hex", "f32", "f64",
}

KEYWORDS = (
    "func", "fn", "stct", "impl", "var", "let", "const", "ret", "if", "is", "not", "elif", "else",
    "for", "match", "take", "ftake", "as", "stop", "extern",
    "pass", "drop", "nodrop", "dropnow", "true", "false", "null", "this", "__llh", "__ll",
    "in", "or", "and"
)

BUILTINS = {
    "atomic_load": "func atomic_load<T>(address: T*): T",
    "atomic_store": "func atomic_store<T>(address: T*, value: T): nul",
    "atomic_add": "func atomic_add<T>(address: T*, value: T): T",
    "atomic_exchange": "func atomic_exchange<T>(address: T*, value: T): T",
    "atomic_compare_exchange": (
        "func atomic_compare_exchange<T>(address: T*, expected: T, desired: T): bol"
    ),
    "volatile_load": "func volatile_load<T>(address: T*): T",
    "volatile_store": "func volatile_store<T>(address: T*, value: T): nul",
    "platform": "func platform(): str",
    "sizeof": "func sizeof(value): usize",
    "typeof": "func typeof(value): str",
    "log": "func log(value): nul",
    "logl": "func logl(value): nul",
    "malloc": "func malloc(size: usize): ptr",
    "free": "func free(value): nul"
}

RUNTIME_GLOBALS = {
    "_argc": "const _argc: i64",
    "_args": "const _args: str[]",
}

# LSP CompletionItemKind values.
KIND_METHOD = 2
KIND_FUNCTION = 3
KIND_CONSTRUCTOR = 4
KIND_FIELD = 5
KIND_VARIABLE = 6
KIND_KEYWORD = 14
KIND_FILE = 17
KIND_FOLDER = 19
KIND_CONSTANT = 21
KIND_STRUCT = 22

@dataclass(frozen=True)
class Symbol:
    name: str
    kind: int
    signature: str
    uri: str
    line: int
    character: int
    owner: str = ""
    type_name: str = ""
    imported: bool = False
    offset: int = -1

@dataclass
class ImportProblem:
    path: str
    source_path: Path
    start: int
    end: int

@dataclass
class LocalBinding:
    """One lexical local/parameter binding used by unused-variable hints."""
    name: str
    start: int
    end: int
    scope_start: int
    scope_end: int

@dataclass
class Index:
    symbols: List[Symbol] = field(default_factory=list)
    structs: Dict[str, Symbol] = field(default_factory=dict)
    fields: Dict[str, List[Symbol]] = field(default_factory=dict)
    methods: Dict[str, List[Symbol]] = field(default_factory=dict)
    visited: Set[Path] = field(default_factory=set)
    import_problems: List[ImportProblem] = field(default_factory=list)
    _keys: Set[Tuple[int, str, str, str]] = field(default_factory=set)
    by_name: Dict[str, List[Symbol]] = field(default_factory=dict)

    def add(self, symbol: Symbol) -> None:
        key = (symbol.kind, symbol.name, symbol.owner, symbol.uri)
        if key in self._keys:
            return
        self._keys.add(key)
        self.symbols.append(symbol)
        self.by_name.setdefault(symbol.name, []).append(symbol)
        if symbol.kind == KIND_STRUCT:
            self.structs.setdefault(symbol.name, symbol)
        elif symbol.kind == KIND_FIELD:
            self.fields.setdefault(symbol.owner, []).append(symbol)
        elif symbol.kind in (KIND_METHOD, KIND_CONSTRUCTOR):
            self.methods.setdefault(symbol.owner, []).append(symbol)

@dataclass
class DocumentAnalysis:
    text: str
    index: Optional[Index] = None
    local_symbols: Optional[List[Symbol]] = None
    masked: Optional[str] = None
    diagnostics: Optional[List[dict]] = None
    dependency_stamps: Dict[Path, Tuple[object, ...]] = field(
        default_factory=dict
    )

@dataclass(frozen=True)
class FileTextEntry:
    stamp: Tuple[object, ...]
    text: str

analysis_cache: Dict[str, DocumentAnalysis] = {}
file_text_cache: Dict[Path, FileTextEntry] = {}
project_roots_cache: Dict[Tuple[str, str, str], Tuple[Path, ...]] = {}
directory_cache: Dict[Path, Tuple[int, Tuple[Tuple[str, bool, bool], ...]]] = {}

def source_stamp(path: Path) -> Tuple[object, ...]:
    uri = uri_from_path(path)
    open_document = documents.get(uri)
    if open_document is not None:
        return ("document", len(open_document), hash(open_document))
    try:
        stat = path.stat()
    except OSError:
        return ("missing",)
    return ("file", stat.st_mtime_ns, stat.st_size)

def dependencies_are_current(analysis: DocumentAnalysis) -> bool:
    return all(
        source_stamp(path) == stamp
        for path, stamp in analysis.dependency_stamps.items()
    )

def cached_analysis(
    uri: str,
    text: str,
    validate_dependencies: bool = False,
) -> DocumentAnalysis:
    analysis = analysis_cache.get(uri)
    if analysis is None or analysis.text != text or (
        validate_dependencies
        and analysis.index is not None
        and not dependencies_are_current(analysis)
    ):
        analysis = DocumentAnalysis(text)
        analysis_cache[uri] = analysis
    return analysis

def invalidate_analysis_caches(clear_files: bool = False) -> None:
    analysis_cache.clear()
    project_roots_cache.clear()
    if clear_files:
        file_text_cache.clear()
        directory_cache.clear()

def send(message: dict) -> None:
    encoded = json.dumps(message, ensure_ascii=False).encode("utf-8")
    sys.stdout.buffer.write(
        f"Content-Length: {len(encoded)}\r\n\r\n".encode("ascii")
    )
    sys.stdout.buffer.write(encoded)
    sys.stdout.buffer.flush()

def respond(request_id, result) -> None:
    send({"jsonrpc": "2.0", "id": request_id, "result": result})

def respond_error(request_id, message: str) -> None:
    send({
        "jsonrpc": "2.0",
        "id": request_id,
        "error": {"code": -32603, "message": message},
    })

def log(message: str) -> None:
    print(f"ferra-lsp: {message}", file=sys.stderr, flush=True)

@lru_cache(maxsize=1024)
def path_from_uri(uri: str) -> Path:
    parsed = urlparse(uri)
    if parsed.scheme == "file":
        return Path(unquote(parsed.path)).resolve()
    return Path(uri).resolve()

@lru_cache(maxsize=2048)
def uri_from_path(path: Path) -> str:
    return path.resolve().as_uri()

def open_text(path: Path) -> Optional[str]:
    try:
        canonical = path.resolve()
    except OSError:
        canonical = path
    uri = uri_from_path(canonical)
    if uri in documents:
        return documents[uri]
    try:
        stat = canonical.stat()
    except (OSError, UnicodeError):
        return None
    stamp = ("file", stat.st_mtime_ns, stat.st_size)
    cached = file_text_cache.get(canonical)
    if cached is not None and cached.stamp == stamp:
        return cached.text
    try:
        text = canonical.read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return None
    file_text_cache[canonical] = FileTextEntry(stamp, text)
    return text

def utf16_length(value: str) -> int:
    if value.isascii():
        return len(value)
    return len(value.encode("utf-16-le")) // 2

def utf16_to_index(value: str, units: int) -> int:
    if value.isascii():
        return max(0, min(units, len(value)))
    current = 0
    for index, character in enumerate(value):
        width = utf16_length(character)
        if current + width > units:
            return index
        current += width
    return len(value)

@lru_cache(maxsize=32)
def line_starts(text: str) -> Tuple[int, ...]:
    starts = [0]
    starts.extend(index + 1 for index, value in enumerate(text) if value == "\n")
    return tuple(starts)

def offset_to_position(text: str, offset: int) -> dict:
    offset = max(0, min(offset, len(text)))
    starts = line_starts(text)
    line = bisect_right(starts, offset) - 1
    line_start = starts[line]
    return {"line": line, "character": utf16_length(text[line_start:offset])}

def make_range(text: str, start: int, end: int) -> dict:
    return {
        "start": offset_to_position(text, start),
        "end": offset_to_position(text, end),
    }

def make_diagnostic(text: str, start: int, end: int, message: str) -> dict:
    return {
        "range": make_range(text, start, end),
        "severity": 1,
        "source": "ferra",
        "message": message,
    }

def make_hint_diagnostic(text: str, start: int, end: int, message: str) -> dict:
    """A non-blocking editor note for code that is valid but unnecessary."""
    return {
        "range": make_range(text, start, end),
        "severity": 4,  # DiagnosticSeverity.Hint
        "tags": [1],  # DiagnosticTag.Unnecessary
        "code": "unused-variable",
        "source": "ferra",
        "message": message,
    }

def base_type(type_name: str) -> str:
    value = type_name.strip()
    if value.endswith("!"):
        value = value[:-1].rstrip()
    if value.startswith("(") and value.endswith(")"):
        return "tup"
    while value.endswith("*"):
        value = value[:-1].rstrip()
    while value.endswith("[]"):
        value = value[:-2].rstrip()
    generic = value.find("<")
    if generic != -1:
        value = value[:generic]
    return value.strip()

@lru_cache(maxsize=64)
def mask_comments_and_strings(text: str, keep_strings: bool = False) -> str:
    """Replace comments/strings with spaces while retaining newlines/offsets."""
    result = list(text)
    index = 0
    state = "code"
    quote = ""
    while index < len(text):
        current = text[index]
        following = text[index + 1] if index + 1 < len(text) else ""
        if state == "code":
            if current == "/" and following == "/":
                result[index] = result[index + 1] = " "
                index += 2
                state = "line-comment"
                continue
            if current == "/" and following == "*":
                result[index] = result[index + 1] = " "
                index += 2
                state = "block-comment"
                continue
            if current in ('"', "'"):
                quote = current
                if not keep_strings:
                    result[index] = " "
                state = "string"
        elif state == "line-comment":
            if current == "\n":
                state = "code"
            else:
                result[index] = " "
        elif state == "block-comment":
            if current == "*" and following == "/":
                result[index] = result[index + 1] = " "
                index += 2
                state = "code"
                continue
            if current != "\n":
                result[index] = " "
        elif state == "string":
            if not keep_strings and current != "\n":
                result[index] = " "
            if current == "\\" and following:
                if not keep_strings and following != "\n":
                    result[index + 1] = " "
                index += 2
                continue
            if current == quote:
                state = "code"
        index += 1
    return "".join(result)

@lru_cache(maxsize=64)
def brace_depths(masked: str) -> Tuple[int, ...]:
    depths = [0] * (len(masked) + 1)
    depth = 0
    for index, character in enumerate(masked):
        depths[index] = depth
        if character == "{":
            depth += 1
        elif character == "}":
            depth = max(0, depth - 1)
    depths[len(masked)] = depth
    return tuple(depths)

def find_closing_brace(masked: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(masked)):
        if masked[index] == "{":
            depth += 1
        elif masked[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(masked)

@lru_cache(maxsize=64)
def brace_pairs(masked: str) -> Tuple[Tuple[int, int], ...]:
    stack: List[int] = []
    pairs: List[Tuple[int, int]] = []
    for index, character in enumerate(masked):
        if character == "{":
            stack.append(index)
        elif character == "}" and stack:
            pairs.append((stack.pop(), index))
    return tuple(pairs)

def innermost_scope(masked: str, offset: int) -> Optional[Tuple[int, int]]:
    candidates = [
        pair for pair in brace_pairs(masked)
        if pair[0] < offset < pair[1]
    ]
    return max(candidates, key=lambda pair: pair[0]) if candidates else None

@lru_cache(maxsize=32)
def generic_parameters(text: str) -> Set[str]:
    parameters: Set[str] = set()
    pattern = re.compile(
        r"\b(?:func|fn|stct|impl)\s+[A-Za-z_]\w*\s*<([^>{}()]*)>"
    )
    for match in pattern.finditer(text):
        for item in match.group(1).split(","):
            name_match = re.match(r"\s*([A-Za-z_]\w*)", item)
            if name_match:
                parameters.add(name_match.group(1))
    return parameters

def probable_project_roots(source_path: Path) -> Iterable[Path]:
    ferra_path = os.environ.get("FERRA_PATH") or ""
    cache_key = (
        str(source_path.parent),
        str(workspace_root) if workspace_root is not None else "",
        ferra_path,
    )
    cached = project_roots_cache.get(cache_key)
    if cached is not None:
        return cached

    seen: Set[Path] = set()
    candidates: List[Path] = []
    if workspace_root is not None:
        candidates.append(workspace_root)
    for parent in (source_path.parent, *source_path.parents):
        if (parent / "fe").is_dir() or (parent / "ferralang").is_dir():
            candidates.append(parent)
    if ferra_path:
        candidates.append(Path(ferra_path).expanduser())
    # This is useful while the server runs directly from the Ferra repository.
    candidates.append(Path(__file__).resolve().parents[2])
    result: List[Path] = []
    for candidate in candidates:
        try:
            resolved = candidate.resolve()
        except OSError:
            continue
        if resolved not in seen:
            seen.add(resolved)
            result.append(resolved)
    roots = tuple(result)
    project_roots_cache[cache_key] = roots
    return roots

def resolve_take(source_path: Path, requested: str) -> Optional[Path]:
    requested_path = Path(requested).expanduser()
    candidates: List[Path] = []
    if requested_path.is_absolute():
        candidates.append(requested_path)
    else:
        for root in probable_project_roots(source_path):
            candidates.append(root / requested_path)
    for candidate in candidates:
        try:
            resolved = candidate.resolve()
        except OSError:
            continue
        if resolved.is_file():
            return resolved
    return None

def resolve_import(
    source_path: Path,
    requested: str,
    kind: str,
) -> Optional[Path]:
    if kind == "take":
        return resolve_take(source_path, requested)

    # ftake is a literal file include: relative names belong to the importing
    # file, while absolute paths remain absolute. It intentionally does not
    # search FERRA_PATH or unrelated project roots.
    requested_path = Path(requested).expanduser()
    candidate = (
        requested_path
        if requested_path.is_absolute()
        else source_path.parent / requested_path
    )
    try:
        resolved = candidate.resolve()
    except OSError:
        return None
    return resolved if resolved.is_file() else None

IMPORT_PATTERN = re.compile(
    r"\b(?P<kind>take|ftake)\s+"
    r"(?P<quote>[\"'])(?P<path>[^\"']+)(?P=quote)"
)
STRUCT_PATTERN = re.compile(
    r"\bstct\s+([A-Za-z_]\w*)(\s*<[^>{}]*>)?\s*\{"
)
EXTERN_STRUCT_PATTERN = re.compile(
    r"\bextern\s+stct\s+([A-Za-z_]\w*)(\s*<[^>{}]*>)?"
)
FUNCTION_PATTERN = re.compile(
    r"(?<!#)\b(?:func|fn)\s+([A-Za-z_]\w*)(\s*<[^>{}()]*>)?\s*"
    r"\(([^()]*)\)\s*(?::\s*(\([^={}\r\n]*\)|[^\s{]+))?"
)
ATTRIBUTE_PATTERN = re.compile(
    r"#(?:func|fn)\s+([A-Za-z_]\w*)\s*\(([^()]*)\)\s*(?::\s*(\([^={}\r\n]*\)|[^\s{]+))?"
)
IMPL_PATTERN = re.compile(
    r"\bimpl\s+([A-Za-z_]\w*(?:\s*<[^>{}]*>)?)\s+"
    r"([A-Za-z_]\w*)\s*\(([^()]*)\)\s*"
    r"(?::\s*(\([^={}\r\n]*\)|[^\s{]+))?"
)
DROP_PATTERN = re.compile(
    r"\bdrop\s+([A-Za-z_]\w*(?:\s*<[^>{}]*>)?)\s*"
    r"\([^()]*\)\s*\{"
)
DECLARATION_PATTERN = re.compile(
    r"\b(?P<declaration>var|let|const)\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*"
    # Ferra puts a static/dynamic size between the name and its annotation:
    # `let buf[5]: u8`, `let data[count]: T`.  Accept an empty pair as well
    # so hover/completion keep working while `let data[]: T` is being typed.
    r"(?P<array>\[\s*[^\]\r\n]*\s*\])?\s*:\s*"
    r"(?P<type>(?:\([^={}\r\n]*\)|[A-Za-z_]\w*(?:\s*<[^;={}()]+>)?)"
    r"(?:\s*\*)*(?:\s*\[\])*)"
)

# An annotation is optional in Ferra. Keep this separate from
# DECLARATION_PATTERN so diagnostics for explicitly written types remain
# strict, while editor features can show the type inferred from `= value`.
INFERRED_DECLARATION_PATTERN = re.compile(
    r"\b(?P<declaration>var|let|const)\s+"
    r"(?P<name>[A-Za-z_]\w*)\s*"
    r"(?P<array>\[\s*[^\]\r\n]*\s*\])?\s*=\s*"
    r"(?P<value_start>[^\s])"
)

# `let result, ok = request()` binds the positions of a fixed tuple.  Keep
# this separate from ordinary declarations because the type belongs to the
# expression on the right, not to either name on the left.
TUPLE_DESTRUCTURE_PATTERN = re.compile(
    r"\b(?P<declaration>var|let|const)\s+"
    r"(?P<names>[A-Za-z_]\w*(?:\s*,\s*[A-Za-z_]\w*)+)\s*=\s*"
    r"(?P<value_start>[^\s])"
)

IDENTIFIER_PATTERN = re.compile(r"\b[A-Za-z_]\w*\b")
PARAMETER_NAME_PATTERN = re.compile(
    r"(?:^|,)\s*([A-Za-z_]\w*)\s*(?=:\s*|,|$)"
)

def function_parameter_bindings(masked: str) -> List[LocalBinding]:
    """Return parameters with the lexical body in which they are visible."""
    result: List[LocalBinding] = []
    callables = list(FUNCTION_PATTERN.finditer(masked))
    callables.extend(IMPL_PATTERN.finditer(masked))
    for callable_match in callables:
        opening = masked.find("{", callable_match.end())
        if opening == -1:
            continue
        closing = find_closing_brace(masked, opening)
        if closing >= len(masked):
            continue
        parameters_start = callable_match.start(3)
        parameters = masked[parameters_start:callable_match.end(3)]
        for parameter in PARAMETER_NAME_PATTERN.finditer(parameters):
            name = parameter.group(1)
            if name == "_":
                continue
            start = parameters_start + parameter.start(1)
            result.append(LocalBinding(
                name, start, start + len(name), opening, closing
            ))
    return result

def local_declaration_bindings(masked: str) -> List[LocalBinding]:
    """Collect ordinary and tuple-destructured locals from their scopes."""
    result: List[LocalBinding] = []
    seen: Set[Tuple[int, int]] = set()

    def add(name: str, start: int, end: int) -> None:
        if name == "_" or (start, end) in seen:
            return
        scope = innermost_scope(masked, start)
        # Module values have different lifetime/linkage and are deliberately
        # not diagnosed as local unused variables.
        if scope is None:
            return
        seen.add((start, end))
        result.append(LocalBinding(name, start, end, scope[0], scope[1]))

    for match in DECLARATION_PATTERN.finditer(masked):
        add(match.group("name"), match.start("name"), match.end("name"))
    for match in INFERRED_DECLARATION_PATTERN.finditer(masked):
        add(match.group("name"), match.start("name"), match.end("name"))
    for match in TUPLE_DESTRUCTURE_PATTERN.finditer(masked):
        names_offset = match.start("names")
        for name_match in IDENTIFIER_PATTERN.finditer(match.group("names")):
            start = names_offset + name_match.start()
            add(name_match.group(0), start, start + len(name_match.group(0)))
    return result

def unused_variable_diagnostics(text: str, masked: str) -> List[dict]:
    """Find unused locals with lexical scoping and shadowing awareness."""
    bindings = function_parameter_bindings(masked)
    bindings.extend(local_declaration_bindings(masked))
    if not bindings:
        return []

    declaration_ranges = {(binding.start, binding.end) for binding in bindings}
    used: Set[Tuple[int, int]] = set()
    by_name: Dict[str, List[LocalBinding]] = {}
    for binding in bindings:
        by_name.setdefault(binding.name, []).append(binding)

    for identifier in IDENTIFIER_PATTERN.finditer(masked):
        start, end = identifier.span()
        if (start, end) in declaration_ranges:
            continue
        # Do not mistake `.field` or `field:` in a struct literal for a
        # reference to a local with the same name.
        before = masked[:start].rstrip()
        after = masked[end:].lstrip()
        if (before and before[-1] == ".") or (after and after[0] == ":"):
            continue

        candidates = [
            binding for binding in by_name.get(identifier.group(0), [])
            if binding.start < start and
            binding.scope_start < start < binding.scope_end
        ]
        if candidates:
            binding = max(
                candidates,
                key=lambda item: (item.scope_start, item.start),
            )
            used.add((binding.start, binding.end))

    return [
        make_hint_diagnostic(
            text, binding.start, binding.end,
            f"Unused variable '{binding.name}'.",
        )
        for binding in bindings
        if (binding.start, binding.end) not in used
    ]

def declaration_parts(match: re.Match) -> Tuple[str, str, str, str, str]:
    declaration = match.group("declaration")
    name = match.group("name")
    array = (match.group("array") or "").strip()
    written_type = match.group("type").strip()
    # `type_name` is used for type inference; retain the fact that the value is
    # an array even though Ferra writes its size before the colon.
    type_name = written_type
    if array and not type_name.rstrip().endswith("[]"):
        type_name += "[]"
    return declaration, name, array, written_type, type_name

def initializer_text(text: str, start: int) -> str:
    """Return one declaration initializer without crossing its statement."""
    depth = 0
    quote = ""
    index = start
    while index < len(text):
        character = text[index]
        if quote:
            if character == "\\":
                index += 2
                continue
            if character == quote:
                quote = ""
        elif character in ('"', "'"):
            quote = character
        elif character in "([{":
            depth += 1
        elif character in ")]}":
            if depth == 0:
                break
            depth -= 1
        elif depth == 0 and character in ";\r\n":
            break
        index += 1
    return text[start:index].strip()

def split_top_level(value: str, delimiter: str = ",") -> List[str]:
    """Split a comma list while preserving nested types and expressions."""
    result: List[str] = []
    start = 0
    depths = {"(": 0, "[": 0, "{": 0, "<": 0}
    closing = {")": "(", "]": "[", "}": "{", ">": "<"}
    quote = ""
    index = 0
    while index < len(value):
        character = value[index]
        if quote:
            if character == "\\":
                index += 2
                continue
            if character == quote:
                quote = ""
        elif character in ('"', "'"):
            quote = character
        elif character in depths:
            depths[character] += 1
        elif character in closing:
            opening = closing[character]
            if depths[opening] > 0:
                depths[opening] -= 1
        elif character == delimiter and not any(depths.values()):
            result.append(value[start:index].strip())
            start = index + 1
        index += 1
    result.append(value[start:].strip())
    return result

def tuple_element_types(type_name: str) -> List[str]:
    """Return the written elements of `(A, B)`, including nested tuples."""
    value = type_name.strip()
    if not (value.startswith("(") and value.endswith(")")):
        return []
    elements = split_top_level(value[1:-1])
    return elements if len(elements) > 1 and all(elements) else []

def infer_initializer_type(
    initializer: str,
    visible_types: Optional[Dict[str, str]] = None,
    function_types: Optional[Dict[str, str]] = None,
    method_types: Optional[Dict[Tuple[str, str], str]] = None,
) -> str:
    """Best-effort source-level inference for hovers and completions."""
    value = initializer.strip()
    if not value:
        return ""
    if value[0] in ('"', "'"):
        return "str"
    if re.match(r"^(true|false)\b", value):
        return "bol"
    if re.match(r"^null\b", value):
        return "ptr"
    if value.startswith("("):
        depth = 0
        closing = -1
        for index, character in enumerate(value):
            if character == "(":
                depth += 1
            elif character == ")":
                depth -= 1
                if depth == 0:
                    closing = index
                    break
        if closing != -1:
            elements = split_top_level(value[1:closing])
            if len(elements) > 1 and all(elements):
                element_types = [
                    infer_initializer_type(
                        element, visible_types, function_types, method_types
                    )
                    for element in elements
                ]
                if all(element_types):
                    return "(" + ", ".join(element_types) + ")"
                return "tup"
    if value.startswith("["):
        contents = value[1:].lstrip()
        if not contents or contents.startswith("]"):
            return "arr"
        element = infer_initializer_type(
            contents, visible_types, function_types, method_types
        )
        return f"{element}[]" if element else "arr"
    if re.match(r"^[+-]?\d+(?:\.\d+|[eE][+-]?\d+)", value):
        return "f64"
    if re.match(r"^[+-]?\d+\b", value):
        return "int"

    struct = re.match(
        r"^([A-Za-z_]\w*(?:\s*<[^{}()\r\n]*>)?)\s*\{", value
    )
    if struct:
        return re.sub(r"\s+", "", struct.group(1))

    call = re.match(r"^([A-Za-z_]\w*)\s*\(", value)
    if call and function_types:
        return function_types.get(call.group(1), "")

    method = re.match(
        r"^([A-Za-z_]\w*)\s*\.\s*([A-Za-z_]\w*)\s*\(", value
    )
    if method and visible_types and method_types:
        receiver_type = base_type(visible_types.get(method.group(1), ""))
        return method_types.get((receiver_type, method.group(2)), "")

    variable = re.match(r"^([A-Za-z_]\w*)\b", value)
    if variable and visible_types:
        return visible_types.get(variable.group(1), "")
    return ""

def inferred_declaration_parts(
    match: re.Match,
    text: str,
    visible_types: Optional[Dict[str, str]] = None,
    function_types: Optional[Dict[str, str]] = None,
    method_types: Optional[Dict[Tuple[str, str], str]] = None,
) -> Tuple[str, str, str, str]:
    declaration = match.group("declaration")
    name = match.group("name")
    array = (match.group("array") or "").strip()
    value = initializer_text(text, match.start("value_start"))
    type_name = infer_initializer_type(
        value, visible_types, function_types, method_types
    )
    if array and type_name and not type_name.endswith("[]"):
        type_name += "[]"
    return declaration, name, array, type_name

def inferred_function_return_type(
    text: str,
    masked: str,
    function_match: re.Match,
    visible_types: Optional[Dict[str, str]] = None,
    function_types: Optional[Dict[str, str]] = None,
) -> str:
    opening = masked.find("{", function_match.end())
    if opening == -1:
        return ""
    closing = find_closing_brace(masked, opening)
    body = text[opening + 1:closing]
    result = re.search(r"\bret\s+([^;\r\n}]+)", body)
    if result is None:
        return "nul"
    return infer_initializer_type(
        result.group(1), visible_types, function_types
    )

def inferred_parameter_types(
    text: str,
    comment_free: str,
    function_match: re.Match,
    visible_types: Dict[str, str],
    function_types: Optional[Dict[str, str]] = None,
) -> Dict[str, str]:
    """Infer simple unannotated parameters from direct call arguments."""
    name = function_match.group(1)
    parameters = text[function_match.start(3):function_match.end(3)]
    pending = [
        part.strip() for part in parameters.split(",")
        if re.fullmatch(r"[A-Za-z_]\w*", part.strip())
    ]
    if not pending:
        return {}

    result: Dict[str, str] = {}
    calls = re.compile(rf"\b{re.escape(name)}\s*\(([^()]*)\)")
    declaration_name_start = function_match.start(1)
    for call in calls.finditer(comment_free):
        if call.start() == declaration_name_start:
            continue
        arguments = call.group(1).split(",")
        for index, parameter in enumerate(pending):
            if index >= len(arguments) or parameter in result:
                continue
            inferred = infer_initializer_type(
                arguments[index], visible_types, function_types
            )
            if inferred:
                result[parameter] = inferred
    return result

def inferred_parameter_signature(
    parameters: str,
    inferred_types: Dict[str, str],
) -> str:
    parts: List[str] = []
    for part in parameters.split(","):
        stripped = part.strip()
        if stripped in inferred_types:
            parts.append(f"{stripped}: {inferred_types[stripped]}")
        else:
            parts.append(stripped)
    return ", ".join(parts)

def source_value_types(text: str, masked: str, comment_free: str) -> Dict[str, str]:
    """Collect declaration types for use while inferring function parameters."""
    result: Dict[str, str] = {}
    declarations = []
    for match in DECLARATION_PATTERN.finditer(masked):
        declarations.append((match.start(), "annotated", match))
    for match in INFERRED_DECLARATION_PATTERN.finditer(comment_free):
        declarations.append((match.start(), "inferred", match))
    for _, kind, match in sorted(declarations, key=lambda item: item[0]):
        if kind == "annotated":
            _, name, _, _, type_name = declaration_parts(match)
        else:
            _, name, _, type_name = inferred_declaration_parts(
                match, text, result
            )
        if type_name:
            result[name] = type_name
    return result

def location(text: str, offset: int) -> Tuple[int, int]:
    value = offset_to_position(text, offset)
    return value["line"], value["character"]

def signature_return_suffix(return_type: Optional[str]) -> str:
    return f": {return_type}" if return_type else ""

def parse_module(
    path: Path,
    text: str,
    index: Index,
    root_path: Path,
) -> None:
    try:
        canonical = path.resolve()
    except OSError:
        canonical = path
    if canonical in index.visited:
        return
    index.visited.add(canonical)

    imported = canonical != root_path.resolve()
    uri = uri_from_path(canonical)
    comment_free = mask_comments_and_strings(text, keep_strings=True)
    masked = mask_comments_and_strings(text)
    depths = brace_depths(masked)
    value_types = source_value_types(text, masked, comment_free)
    inferred_function_types: Dict[str, str] = {}

    # Import first so all recursively taken declarations become visible.
    for match in IMPORT_PATTERN.finditer(comment_free):
        requested = match.group("path")
        imported_path = resolve_import(
            canonical, requested, match.group("kind")
        )
        if imported_path is None:
            index.import_problems.append(ImportProblem(
                requested,
                canonical,
                match.start("path"),
                match.end("path"),
            ))
            continue
        imported_text = open_text(imported_path)
        if imported_text is None:
            index.import_problems.append(ImportProblem(
                requested,
                canonical,
                match.start("path"),
                match.end("path"),
            ))
            continue
        parse_module(imported_path, imported_text, index, root_path)

    for match in STRUCT_PATTERN.finditer(masked):
        if depths[match.start()] != 0:
            continue
        name = match.group(1)
        line, character = location(text, match.start(1))
        generic = (match.group(2) or "").strip()
        struct_symbol = Symbol(
            name, KIND_STRUCT, f"stct {name}{generic}", uri,
            line, character, imported=imported,
        )
        index.add(struct_symbol)

        opening = masked.find("{", match.start(), match.end())
        closing = find_closing_brace(masked, opening)
        body = masked[opening + 1:closing]
        body_offset = opening + 1
        field_pattern = re.compile(
            r"\b([A-Za-z_]\w*)\s*:\s*"
            r"([A-Za-z_]\w*(?:\s*<[^;={}()]+>)?(?:\s*\*)*(?:\s*\[\])*)"
        )
        body_depths = brace_depths(body)
        for field_match in field_pattern.finditer(body):
            if body_depths[field_match.start()] != 0:
                continue
            field_name = field_match.group(1)
            type_name = field_match.group(2).strip()
            field_start = body_offset + field_match.start(1)
            field_line, field_character = location(text, field_start)
            index.add(Symbol(
                field_name, KIND_FIELD, f"{field_name}: {type_name}", uri,
                field_line, field_character, owner=name, type_name=type_name,
                imported=imported,
            ))

    for match in EXTERN_STRUCT_PATTERN.finditer(masked):
        if depths[match.start()] != 0:
            continue
        name = match.group(1)
        generic = (match.group(2) or "").strip()
        line, character = location(text, match.start(1))
        index.add(Symbol(
            name, KIND_STRUCT, f"extern stct {name}{generic}", uri,
            line, character, imported=imported,
        ))

    for match in FUNCTION_PATTERN.finditer(masked):
        if depths[match.start()] != 0:
            continue
        name = match.group(1)
        generic = (match.group(2) or "").strip()
        params = text[match.start(3):match.end(3)].strip()
        inferred_params = inferred_parameter_types(
            text, comment_free, match, value_types, inferred_function_types
        )
        displayed_params = inferred_parameter_signature(params, inferred_params)
        return_type = match.group(4) or inferred_function_return_type(
            text, masked, match, inferred_params, inferred_function_types
        )
        signature = (
            f"func {name}{generic}({displayed_params})"
            f"{signature_return_suffix(return_type)}"
        )
        line, character = location(text, match.start(1))
        index.add(Symbol(
            name, KIND_FUNCTION, signature, uri, line, character,
            type_name=return_type or "nul", imported=imported,
        ))
        inferred_function_types[name] = return_type or "nul"

    for match in ATTRIBUTE_PATTERN.finditer(masked):
        if depths[match.start()] != 0:
            continue
        name = match.group(1)
        params = text[match.start(2):match.end(2)].strip()
        return_type = match.group(3)
        signature = (
            f"#func {name}({params}){signature_return_suffix(return_type)}"
        )
        line, character = location(text, match.start(1))
        index.add(Symbol(
            name, KIND_FUNCTION, signature, uri, line, character,
            type_name=return_type or "nul", imported=imported,
        ))

    for match in IMPL_PATTERN.finditer(masked):
        if depths[match.start()] != 0:
            continue
        owner_written = re.sub(r"\s+", "", match.group(1))
        owner = base_type(owner_written)
        name = match.group(2)
        params = text[match.start(3):match.end(3)].strip()
        return_type = match.group(4)
        kind = KIND_CONSTRUCTOR if name == owner else KIND_METHOD
        signature = (
            f"impl {owner_written} {name}({params})"
            f"{signature_return_suffix(return_type)}"
        )
        line, character = location(text, match.start(2))
        index.add(Symbol(
            name, kind, signature, uri, line, character, owner=owner,
            type_name=return_type or "nul", imported=imported,
        ))

    # Only module-level values are exported through `take`.
    for match in DECLARATION_PATTERN.finditer(masked):
        if depths[match.start()] != 0:
            continue
        declaration, name, array, written_type, type_name = (
            declaration_parts(match)
        )
        line, character = location(text, match.start("name"))
        kind = KIND_CONSTANT if declaration == "const" else KIND_VARIABLE
        index.add(Symbol(
            name, kind,
            f"{declaration} {name}{array}: {written_type}", uri,
            line, character, type_name=type_name, imported=imported,
        ))

    function_types = {
        symbol.name: symbol.type_name
        for symbol in index.symbols
        if symbol.kind in (KIND_FUNCTION, KIND_METHOD, KIND_CONSTRUCTOR)
        and symbol.type_name
    }
    method_types = {
        (symbol.owner, symbol.name): symbol.type_name
        for symbol in index.symbols
        if symbol.kind in (KIND_METHOD, KIND_CONSTRUCTOR)
        and symbol.owner and symbol.type_name
    }
    visible_types = {
        symbol.name: symbol.type_name
        for symbol in index.symbols
        if symbol.kind in (KIND_VARIABLE, KIND_CONSTANT) and symbol.type_name
    }
    for match in INFERRED_DECLARATION_PATTERN.finditer(comment_free):
        if depths[match.start()] != 0:
            continue
        declaration, name, array, type_name = inferred_declaration_parts(
            match, text, visible_types, function_types
        )
        if not type_name:
            continue
        line, character = location(text, match.start("name"))
        kind = KIND_CONSTANT if declaration == "const" else KIND_VARIABLE
        index.add(Symbol(
            name, kind, f"{declaration} {name}{array}: {type_name}", uri,
            line, character, type_name=type_name, imported=imported,
        ))
        visible_types[name] = type_name

def build_index(uri: str, text: str) -> Index:
    analysis = cached_analysis(uri, text, validate_dependencies=True)
    if analysis.index is not None:
        return analysis.index
    path = path_from_uri(uri)
    index = Index()
    parse_module(path, text, index, path)
    try:
        root_path = path.resolve()
    except OSError:
        root_path = path
    analysis.index = index
    analysis.dependency_stamps = {
        dependency: source_stamp(dependency)
        for dependency in index.visited
        if dependency != root_path
    }
    return index

def local_symbols(uri: str, text: str) -> List[Symbol]:
    analysis = cached_analysis(uri, text)
    if analysis.local_symbols is not None:
        return analysis.local_symbols
    result: List[Symbol] = []
    masked = analysis.masked
    if masked is None:
        masked = mask_comments_and_strings(text)
        analysis.masked = masked
    for match in DECLARATION_PATTERN.finditer(masked):
        declaration, name, array, written_type, type_name = (
            declaration_parts(match)
        )
        line, character = location(text, match.start("name"))
        kind = KIND_CONSTANT if declaration == "const" else KIND_VARIABLE
        result.append(Symbol(
            name, kind, f"{name}{array}: {written_type}", uri,
            line, character, type_name=type_name, offset=match.start("name"),
        ))

    comment_free = mask_comments_and_strings(text, keep_strings=True)
    index = build_index(uri, text)
    function_types = {
        symbol.name: symbol.type_name
        for symbol in index.symbols
        if symbol.kind in (KIND_FUNCTION, KIND_METHOD, KIND_CONSTRUCTOR)
        and symbol.type_name
    }
    method_types = {
        (symbol.owner, symbol.name): symbol.type_name
        for symbol in index.symbols
        if symbol.kind in (KIND_METHOD, KIND_CONSTRUCTOR)
        and symbol.owner and symbol.type_name
    }
    all_value_types = source_value_types(text, masked, comment_free)
    visible_types: Dict[str, str] = {}
    declarations = []
    for match in DECLARATION_PATTERN.finditer(masked):
        declarations.append((match.start(), "annotated", match))
    for match in INFERRED_DECLARATION_PATTERN.finditer(comment_free):
        declarations.append((match.start(), "inferred", match))
    for match in TUPLE_DESTRUCTURE_PATTERN.finditer(comment_free):
        declarations.append((match.start(), "destructure", match))
    for _, kind_name, match in sorted(declarations, key=lambda item: item[0]):
        if kind_name == "annotated":
            _, name, _, _, type_name = declaration_parts(match)
            visible_types[name] = type_name
            continue

        if kind_name == "destructure":
            initializer = initializer_text(text, match.start("value_start"))
            tuple_types = tuple_element_types(infer_initializer_type(
                initializer, visible_types, function_types, method_types
            ))
            names = list(re.finditer(r"[A-Za-z_]\w*", match.group("names")))
            if len(tuple_types) != len(names):
                continue
            declaration = match.group("declaration")
            kind = KIND_CONSTANT if declaration == "const" else KIND_VARIABLE
            names_offset = match.start("names")
            for position, name_match in enumerate(names):
                name = name_match.group(0)
                if name == "_":
                    continue
                type_name = tuple_types[position]
                start = names_offset + name_match.start()
                line, character = location(text, start)
                result.append(Symbol(
                    name, kind, f"{name}: {type_name}", uri,
                    line, character, type_name=type_name, offset=start,
                ))
                visible_types[name] = type_name
            continue

        declaration, name, array, type_name = inferred_declaration_parts(
            match, text, visible_types, function_types, method_types
        )
        if not type_name:
            continue
        line, character = location(text, match.start("name"))
        kind = KIND_CONSTANT if declaration == "const" else KIND_VARIABLE
        result.append(Symbol(
            name, kind, f"{name}{array}: {type_name}", uri,
            line, character, type_name=type_name, offset=match.start("name"),
        ))
        visible_types[name] = type_name

    # Infer the type of an unannotated variable initialized with a struct
    # literal, including nested generic types:
    # `let line = Line{...}` / `let outer = Box<Box<i64>>{...}`.
    inferred_struct_pattern = re.compile(
        r"\b(var|let|const)\s+([A-Za-z_]\w*)\s*=\s*"
        r"([A-Za-z_]\w*(?:\s*<[^;={}()]+>)?)\s*\{"
    )
    for match in inferred_struct_pattern.finditer(masked):
        declaration, name, type_name = match.groups()
        line, character = location(text, match.start(2))
        kind = KIND_CONSTANT if declaration == "const" else KIND_VARIABLE
        result.append(Symbol(
            name, kind, f"{name}: {type_name.strip()}", uri,
            line, character, type_name=type_name.strip(), offset=match.start(2),
        ))

    # Function and method arguments are also useful for hover/completion.
    callable_pattern = re.compile(
        r"\b(?:(?:func|fn)\s+[A-Za-z_]\w*(?:\s*<[^>]*>)?|"
        r"impl\s+[A-Za-z_]\w*(?:\s*<[^>]*>)?\s+[A-Za-z_]\w*)"
        r"\s*\(([^()]*)\)"
    )
    parameter_pattern = re.compile(
        r"\b([A-Za-z_]\w*)\s*:\s*([^,=]+)"
    )
    for callable_match in callable_pattern.finditer(masked):
        parameters = callable_match.group(1)
        offset = callable_match.start(1)
        for parameter in parameter_pattern.finditer(parameters):
            name = parameter.group(1)
            type_name = parameter.group(2).strip()
            start = offset + parameter.start(1)
            line, character = location(text, start)
            result.append(Symbol(
                name, KIND_VARIABLE, f"{name}: {type_name}", uri,
                line, character, type_name=type_name, offset=start,
            ))

    # Parameters with no `: type` use the same direct-call inference as the
    # compiler. They become normal local symbols, so hovering them and using
    # them for member completion remains useful inside the function body.
    for function_match in FUNCTION_PATTERN.finditer(masked):
        inferred = inferred_parameter_types(
            text, comment_free, function_match, all_value_types, function_types
        )
        parameters = text[function_match.start(3):function_match.end(3)]
        parameters_offset = function_match.start(3)
        for name, type_name in inferred.items():
            start_in_parameters = re.search(
                rf"\b{re.escape(name)}\b", parameters
            )
            if start_in_parameters is None:
                continue
            start = parameters_offset + start_in_parameters.start()
            line, character = location(text, start)
            result.append(Symbol(
                name, KIND_VARIABLE, f"{name}: {type_name}", uri,
                line, character, type_name=type_name, offset=start,
            ))

    # Binary operator parameters use square brackets instead of parentheses:
    # `oper Some <-[rhs: Some]>: Some { ... }`.
    operator_pattern = re.compile(
        r"\boper\s+[A-Za-z_]\w*(?:\s*<[^>]*>)?\s+"
        r"[^\n{]*\[([^\]]*)\]"
    )
    for operator_match in operator_pattern.finditer(masked):
        parameters = operator_match.group(1)
        offset = operator_match.start(1)
        for parameter in parameter_pattern.finditer(parameters):
            name = parameter.group(1)
            type_name = parameter.group(2).strip()
            start = offset + parameter.start(1)
            line, character = location(text, start)
            result.append(Symbol(
                name, KIND_VARIABLE, f"{name}: {type_name}", uri,
                line, character, type_name=type_name, offset=start,
            ))
    analysis.local_symbols = result
    return result

def closest_visible_name(uri: str, text: str, name: str, before: int) -> str:
    candidates: List[str] = []
    for symbol in local_symbols(uri, text):
        if symbol.offset < before and symbol.name not in candidates:
            candidates.append(symbol.name)
    matches = difflib.get_close_matches(name, candidates, n=1, cutoff=0.55)
    return matches[0] if matches else ""

def unknown_standalone_identifiers(
    uri: str,
    text: str,
    masked: str,
    index: Index,
) -> List[dict]:
    """Report obvious garbage statements such as a line containing only `foo`."""
    known_names = set(KEYWORDS) | set(BUILTINS) | set(RUNTIME_GLOBALS) | PRIMITIVE_TYPES
    known_names.update(symbol.name for symbol in index.symbols)
    known_names.update(symbol.name for symbol in local_symbols(uri, text))

    result: List[dict] = []
    offset = 0
    parenthesis_depth = 0
    bracket_depth = 0
    for line in masked.splitlines(keepends=True):
        line_without_ending = line.rstrip("\r\n")
        match = re.fullmatch(
            r"\s*([A-Za-z_]\w*)\s*;?\s*",
            line_without_ending,
        )
        if match and parenthesis_depth == 0 and bracket_depth == 0:
            name = match.group(1)
            if name not in known_names:
                result.append(make_diagnostic(
                    text,
                    offset + match.start(1),
                    offset + match.end(1),
                    f"WTF is this: '{name}'? >:o",
                ))
        parenthesis_depth += line_without_ending.count("(")
        parenthesis_depth -= line_without_ending.count(")")
        bracket_depth += line_without_ending.count("[")
        bracket_depth -= line_without_ending.count("]")
        parenthesis_depth = max(0, parenthesis_depth)
        bracket_depth = max(0, bracket_depth)
        offset += len(line)
    return result

def diagnostics_for(uri: str, text: str) -> List[dict]:
    index = build_index(uri, text)
    analysis = cached_analysis(uri, text)
    if analysis.diagnostics is not None:
        return analysis.diagnostics
    result: List[dict] = []
    path = path_from_uri(uri)

    for problem in index.import_problems:
        if problem.source_path.resolve() == path.resolve():
            result.append(make_diagnostic(
                text,
                problem.start,
                problem.end,
                f"WTF? I couldnt open this file: {problem.path} :/",
            ))

    known_types = PRIMITIVE_TYPES | set(index.structs) | generic_parameters(text)
    masked = analysis.masked
    if masked is None:
        masked = mask_comments_and_strings(text)
        analysis.masked = masked
    for match in DECLARATION_PATTERN.finditer(masked):
        type_name = match.group("type").strip()
        type_identifiers = re.findall(r"[A-Za-z_]\w*", type_name)
        unknown = next(
            (name for name in type_identifiers if name not in known_types),
            None,
        )
        if unknown:
            unknown_offset = match.start("type") + type_name.find(unknown)
            result.append(make_diagnostic(
                text,
                unknown_offset,
                unknown_offset + len(unknown),
                f"Unknown type -> '{unknown}'",
            ))
            continue

        declared_base = base_type(type_name)
        declared_struct = index.structs.get(declared_base)
        if (
            declared_struct is not None
            and declared_struct.signature.startswith("extern stct ")
            and "*" not in type_name
        ):
            result.append(make_diagnostic(
                text,
                match.start("type"),
                match.end("type"),
                f"'{declared_base}' is an opaque extern struct; use "
                "a pointer or a native allocation wrapper",
            ))

    # Validate direct `object.field` accesses. Unknown object names are reported
    # when a nearby declared name makes this likely to be a typo; known struct
    # values additionally get their field/method name checked.
    member_pattern = re.compile(
        r"(?<!\.)\b([A-Za-z_]\w*)\s*\.\s*([A-Za-z_]\w*)"
    )
    for match in member_pattern.finditer(masked):
        object_name = match.group(1)
        member_name = match.group(2)
        object_type = infer_variable_type(
            uri, text, object_name, match.start(), index
        )
        if not object_type:
            suggestion = closest_visible_name(
                uri, text, object_name, match.start()
            )
            message = f"Undefined value -> '{object_name}' before field access."
            if suggestion:
                message += f" Did you mean -> '{suggestion}' ?"
            result.append(make_diagnostic(
                text,
                match.start(1),
                match.end(1),
                message,
            ))
            continue
        if object_type not in index.structs:
            continue
        member_names = {
            symbol.name for symbol in (
                index.fields.get(object_type, [])
                + index.methods.get(object_type, [])
            )
        }
        if member_name not in member_names:
            result.append(make_diagnostic(
                text,
                match.start(2),
                match.end(2),
                f"Structure -> '{object_type}' has no field -> '{member_name}' neither method",
            ))

    result.extend(unused_variable_diagnostics(text, masked))
    result.extend(unknown_standalone_identifiers(uri, text, masked, index))

    pairs = {"}": "{", ")": "(", "]": "["}
    opening = set(pairs.values())
    stack: List[Tuple[str, int]] = []
    for offset, character in enumerate(masked):
        if character in opening:
            stack.append((character, offset))
        elif character in pairs:
            if stack and stack[-1][0] == pairs[character]:
                stack.pop()
            else:
                result.append(make_diagnostic(
                    text, offset, offset + 1, f"Unexpected -> '{character}'"
                ))
    closing_for = {"{": "}", "(": ")", "[": "]"}
    for character, offset in stack:
        result.append(make_diagnostic(
            text,
            offset,
            offset + 1,
            f"Missing closing -> '{closing_for[character]}'",
        ))
    analysis.diagnostics = result
    return result

def publish_diagnostics(uri: str) -> None:
    text = documents.get(uri)
    if text is None:
        return
    send({
        "jsonrpc": "2.0",
        "method": "textDocument/publishDiagnostics",
        "params": {"uri": uri, "diagnostics": diagnostics_for(uri, text)},
    })

def publish_all_diagnostics() -> None:
    for uri in list(documents):
        publish_diagnostics(uri)

def line_and_offset(text: str, line_number: int, character: int) -> Tuple[str, int, int]:
    starts = line_starts(text)
    if line_number < 0 or line_number >= len(starts):
        return "", 0, len(text)
    start = starts[line_number]
    end = starts[line_number + 1] if line_number + 1 < len(starts) else len(text)
    line_with_ending = text[start:end]
    line = line_with_ending.rstrip("\r\n")
    index = utf16_to_index(line, character)
    absolute = start + index
    return line, index, absolute

def apply_content_changes(text: str, changes: List[dict]) -> str:
    """Apply both Full and Incremental TextDocumentContentChangeEvent values."""
    for change in changes:
        change_range = change.get("range")
        if change_range is None:
            text = change.get("text", "")
            continue
        start_position = change_range["start"]
        end_position = change_range["end"]
        _, _, start = line_and_offset(
            text,
            start_position["line"],
            start_position["character"],
        )
        _, _, end = line_and_offset(
            text,
            end_position["line"],
            end_position["character"],
        )
        text = text[:start] + change.get("text", "") + text[end:]
    return text

def word_at(text: str, line_number: int, character: int) -> Tuple[str, int, int]:
    line, index, absolute = line_and_offset(text, line_number, character)
    start = index
    end = index
    while start > 0 and (line[start - 1].isalnum() or line[start - 1] == "_"):
        start -= 1
    while end < len(line) and (line[end].isalnum() or line[end] == "_"):
        end += 1
    return line[start:end], absolute - index + start, absolute - index + end

def symbol_markdown(symbol: Symbol) -> str:
    value = f"```ferra\n{symbol.signature}\n```"
    if symbol.signature.startswith("extern stct "):
        value += (
            "\n\nOpaque external type. Its size and fields are unknown to Ferra; "
            "use a pointer or a native allocation wrapper."
        )
    if symbol.imported:
        value += f"\n\nImported from `{path_from_uri(symbol.uri)}`"
    return value

def constructor_hover_markdown(struct_symbol: Symbol, index: Index) -> str:
    constructors = [
        symbol for symbol in index.methods.get(struct_symbol.name, [])
        if symbol.kind == KIND_CONSTRUCTOR
    ]
    if not constructors:
        return symbol_markdown(struct_symbol)

    signatures: List[str] = []
    for constructor in constructors:
        match = re.fullmatch(
            r"impl\s+([^\s]+)\s+[A-Za-z_]\w*\((.*)\)"
            r"(?:\s*:\s*[^\s]+)?",
            constructor.signature,
        )
        if match:
            signatures.append(f"stct {match.group(1)}({match.group(2)})")
        else:
            signatures.append(constructor.signature)

    value = "\n".join(signatures)
    markdown = f"```ferra\n{value}\n```\n\nConstructor"
    if struct_symbol.imported:
        markdown += f"\n\nImported from `{path_from_uri(struct_symbol.uri)}`"
    return markdown

def hover_for(uri: str, text: str, line: int, character: int):
    word, start, end = word_at(text, line, character)
    if not word:
        return None
    if word in RUNTIME_GLOBALS:
        signature = RUNTIME_GLOBALS[word]
        contents = f"```ferra\n{signature}\n```\n\nRuntime global value"
    elif word in BUILTINS:
        signature = BUILTINS[word]
        contents = f"```ferra\n{signature}\n```\n\nCompiler built-in"
    else:
        index = build_index(uri, text)

        if word == "this":
            owner_type = infer_variable_type(
                uri, text, "this", start, index
            )
            owner = index.structs.get(owner_type)
            if owner is None:
                return None
            contents = (
                f"```ferra\nthis: {owner_type}\n```\n\n"
                f"Receiver of `{owner.signature}`"
            )
            if owner.imported:
                contents += (
                    f"\n\nImported from `{path_from_uri(owner.uri)}`"
                )
            return {
                "contents": {"kind": "markdown", "value": contents},
                "range": make_range(text, start, end),
            }

        # A member name is not a globally unique symbol. Resolve the value on
        # the left-hand side first so `file.size()`, `json.size()` and
        # `vec.size` cannot accidentally hover the first imported `size`.
        object_name = member_object_before(text, start)
        if object_name:
            owner_type = infer_variable_type(
                uri, text, object_name, start, index
            )
            members = (
                index.fields.get(owner_type, [])
                + index.methods.get(owner_type, [])
            )
            member = next(
                (symbol for symbol in members if symbol.name == word),
                None,
            )
            if member is None:
                # Falling back to a global name here would show a believable
                # but unrelated declaration from another imported struct.
                return None
            contents = symbol_markdown(member)
            return {
                "contents": {"kind": "markdown", "value": contents},
                "range": make_range(text, start, end),
            }

        struct_symbol = index.structs.get(word)
        if struct_symbol is not None:
            contents = constructor_hover_markdown(struct_symbol, index)
            return {
                "contents": {"kind": "markdown", "value": contents},
                "range": make_range(text, start, end),
            }
        candidates = local_symbols(uri, text) + index.symbols
        symbol = next((item for item in candidates if item.name == word), None)
        if symbol is None:
            return None
        contents = symbol_markdown(symbol)
    return {
        "contents": {"kind": "markdown", "value": contents},
        "range": make_range(text, start, end),
    }

def symbol_location(symbol: Symbol) -> dict:
    start = {"line": symbol.line, "character": symbol.character}
    end = {
        "line": symbol.line,
        "character": symbol.character + utf16_length(symbol.name),
    }
    return {"uri": symbol.uri, "range": {"start": start, "end": end}}

def take_definition(uri: str, text: str, absolute: int):
    source_path = path_from_uri(uri)
    comment_free = mask_comments_and_strings(text, keep_strings=True)
    for match in IMPORT_PATTERN.finditer(comment_free):
        if match.start("path") <= absolute <= match.end("path"):
            imported_path = resolve_import(
                source_path,
                match.group("path"),
                match.group("kind"),
            )
            if imported_path is None:
                return None
            return {
                "uri": uri_from_path(imported_path),
                "range": {
                    "start": {"line": 0, "character": 0},
                    "end": {"line": 0, "character": 0},
                },
            }
    return None

def member_object_before(text: str, word_start: int) -> str:
    index = word_start - 1
    while index >= 0 and text[index].isspace():
        index -= 1
    if index < 0 or text[index] != ".":
        return ""
    index -= 1
    while index >= 0 and text[index].isspace():
        index -= 1
    end = index + 1
    while index >= 0 and (text[index].isalnum() or text[index] == "_"):
        index -= 1
    return text[index + 1:end]

def definition_for(uri: str, text: str, line: int, character: int):
    _, _, absolute = line_and_offset(text, line, character)
    imported_file = take_definition(uri, text, absolute)
    if imported_file is not None:
        return imported_file

    word, word_start, _ = word_at(text, line, character)
    if not word:
        return None
    index = build_index(uri, text)

    object_name = member_object_before(text, word_start)
    if object_name:
        owner_type = infer_variable_type(
            uri, text, object_name, word_start, index
        )
        members = (
            index.fields.get(owner_type, [])
            + index.methods.get(owner_type, [])
        )
        member = next((symbol for symbol in members if symbol.name == word), None)
        if member is not None:
            return symbol_location(member)

    local_candidates = [
        symbol for symbol in local_symbols(uri, text)
        if symbol.name == word
        and (
            symbol.line < line
            or (symbol.line == line and symbol.character <= character)
        )
    ]
    if local_candidates:
        symbol = max(
            local_candidates,
            key=lambda item: (item.line, item.character),
        )
        return symbol_location(symbol)

    if word == "this":
        owner_type = infer_variable_type(
            uri, text, "this", word_start, index
        )
        owner = index.structs.get(owner_type)
        if owner is not None:
            return symbol_location(owner)

    candidates = [symbol for symbol in index.symbols if symbol.name == word]
    if candidates:
        # Prefer a declaration in this document; imported declarations follow.
        candidates.sort(key=lambda symbol: symbol.uri != uri)
        return symbol_location(candidates[0])
    return None

def infer_variable_type(
    uri: str,
    text: str,
    variable: str,
    before: int,
    symbol_index: Optional[Index] = None,
) -> str:
    candidates = [
        symbol for symbol in local_symbols(uri, text)
        if symbol.name == variable and symbol.offset < before
    ]
    if candidates:
        closest = max(candidates, key=lambda symbol: symbol.offset)
        return base_type(closest.type_name)
    if symbol_index is not None:
        global_value = next((
            symbol for symbol in symbol_index.by_name.get(variable, [])
            if symbol.name == variable
            and symbol.kind in (KIND_VARIABLE, KIND_CONSTANT)
            and not symbol.owner
            and symbol.type_name
        ), None)
        if global_value is not None:
            return base_type(global_value.type_name)
    if variable == "this":
        return receiver_owner_at(text, before)
    return ""

@lru_cache(maxsize=32)
def receiver_scopes(text: str) -> Tuple[Tuple[int, int, str], ...]:
    masked = mask_comments_and_strings(text)
    scopes: List[Tuple[int, int, str]] = []

    def remember(pattern: re.Pattern) -> None:
        for match in pattern.finditer(masked):
            opening = masked.find("{", match.start(), match.end())
            if opening == -1:
                continue
            closing = find_closing_brace(masked, opening)
            scopes.append((opening, closing, base_type(match.group(1))))

    impl_body_pattern = re.compile(
        r"\bimpl\s+([A-Za-z_]\w*(?:\s*<[^>{}]*>)?)\s+"
        r"[A-Za-z_]\w*\s*\([^()]*\)\s*"
        r"(?::\s*(?:\([^{}\r\n]*\)|[^\s{]+))?\s*\{"
    )
    operator_body_pattern = re.compile(
        r"\boper\s+([A-Za-z_]\w*(?:\s*<[^>{}]*>)?)\s+"
        r"[^\n{]*\[[^\]]*\][^\n{]*\{"
    )

    remember(impl_body_pattern)
    remember(operator_body_pattern)
    remember(DROP_PATTERN)
    return tuple(scopes)

def receiver_owner_at(text: str, position: int) -> str:
    """Return the struct owning `this` at a source position."""
    candidates = [
        (opening, owner)
        for opening, closing, owner in receiver_scopes(text)
        if opening < position <= closing
    ]
    return max(candidates, default=(-1, ""))[1]

def completion_item(symbol: Symbol) -> dict:
    detail = symbol.signature
    documentation = None
    if symbol.imported:
        documentation = {
            "kind": "markdown",
            "value": f"Imported with `take` from `{path_from_uri(symbol.uri)}`",
        }
    item = {
        "label": symbol.name,
        "kind": symbol.kind,
        "detail": detail,
        "sortText": f"1_{symbol.name}",
    }
    if documentation:
        item["documentation"] = documentation
    return item

def cached_directory_entries(
    directory: Path,
) -> Tuple[Tuple[str, bool, bool], ...]:
    try:
        stamp = directory.stat().st_mtime_ns
    except OSError:
        return ()
    cached = directory_cache.get(directory)
    if cached is not None and cached[0] == stamp:
        return cached[1]
    try:
        entries = tuple(sorted(
            (
                (entry.name, entry.is_dir(), entry.is_file())
                for entry in directory.iterdir()
            ),
            key=lambda entry: (not entry[1], entry[0].lower()),
        ))
    except OSError:
        return ()
    directory_cache[directory] = (stamp, entries)
    return entries

def take_path_completion(
    uri: str,
    text: str,
    line_prefix: str,
    absolute: int,
) -> Optional[dict]:
    match = re.search(
        r"\b(take|ftake)\s+([\"'])([^\"']*)$",
        line_prefix,
    )
    if match is None:
        return None

    import_kind = match.group(1)
    typed_path = match.group(3)
    separator = max(typed_path.rfind("/"), typed_path.rfind("\\"))
    if separator == -1:
        directory_part = ""
        partial_name = typed_path
    else:
        directory_part = typed_path[:separator + 1]
        partial_name = typed_path[separator + 1:]

    source_path = path_from_uri(uri)
    directories: List[Path] = []
    requested_directory = Path(directory_part or ".").expanduser()
    if requested_directory.is_absolute():
        directories.append(requested_directory)
    elif import_kind == "ftake":
        directories.append(source_path.parent / requested_directory)
    else:
        for root in probable_project_roots(source_path):
            directories.append(root / requested_directory)

    replacement_start = absolute - len(partial_name)
    replacement_range = make_range(text, replacement_start, absolute)
    items: List[dict] = []
    seen_directories: Set[Path] = set()
    seen_names: Set[Tuple[str, bool]] = set()
    for directory in directories:
        try:
            directory = directory.resolve()
        except OSError:
            continue
        if directory in seen_directories or not directory.is_dir():
            continue
        seen_directories.add(directory)
        for entry_name, is_directory, is_file in cached_directory_entries(directory):
            entry = directory / entry_name
            if not is_directory:
                if not is_file:
                    continue
                if import_kind == "take" and entry.suffix != ".fe":
                    continue
            if not entry_name.startswith(partial_name):
                continue
            if entry_name.startswith(".") and not partial_name.startswith("."):
                continue
            key = (entry_name, is_directory)
            if key in seen_names:
                continue
            seen_names.add(key)
            inserted = entry_name + ("/" if is_directory else "")
            item = {
                "label": inserted,
                "kind": KIND_FOLDER if is_directory else KIND_FILE,
                "detail": str(entry),
                "filterText": entry_name,
                "sortText": ("0_" if is_directory else "1_") + entry_name,
                "textEdit": {
                    "range": replacement_range,
                    "newText": inserted,
                },
            }
            if is_directory:
                item["command"] = {
                    "title": "Show files in this folder",
                    "command": "editor.action.triggerSuggest",
                }
            items.append(item)
    return {"isIncomplete": False, "items": items}

def completion_for(uri: str, text: str, line: int, character: int) -> dict:
    current_line, index_in_line, absolute = line_and_offset(
        text, line, character
    )
    prefix = current_line[:index_in_line]
    path_items = take_path_completion(uri, text, prefix, absolute)
    if path_items is not None:
        return path_items

    index = build_index(uri, text)
    member_match = re.search(r"([A-Za-z_]\w*)\.([A-Za-z_]\w*)?$", prefix)
    if member_match:
        owner_type = infer_variable_type(
            uri, text, member_match.group(1), absolute, index
        )
        members = index.fields.get(owner_type, []) + index.methods.get(owner_type, [])
        return {
            "isIncomplete": False,
            "items": [completion_item(item) for item in members],
        }

    items: List[dict] = []
    seen: Set[Tuple[str, int]] = set()
    for symbol in local_symbols(uri, text) + index.symbols:
        key = (symbol.name, symbol.kind)
        if key not in seen:
            seen.add(key)
            items.append(completion_item(symbol))
    for name, signature in BUILTINS.items():
        key = (name, KIND_FUNCTION)
        if key not in seen:
            seen.add(key)
            items.append({
                "label": name,
                "kind": KIND_FUNCTION,
                "detail": signature,
                "documentation": "Ferra compiler built-in",
                "sortText": f"0_{name}",
            })
    for name, signature in RUNTIME_GLOBALS.items():
        key = (name, KIND_CONSTANT)
        if key not in seen:
            seen.add(key)
            items.append({
                "label": name,
                "kind": KIND_CONSTANT,
                "detail": signature,
                "documentation": "Runtime command-line value",
                "sortText": f"0_{name}",
            })
    for keyword in KEYWORDS:
        items.append({
            "label": keyword,
            "kind": KIND_KEYWORD,
            "sortText": f"2_{keyword}",
        })
    return {"isIncomplete": False, "items": items}

def inlay_hints_for(uri: str, text: str, requested_range: Optional[dict] = None) -> List[dict]:
    """Show inferred source types next to declarations without rewriting code."""
    range_start = 0
    range_end = len(text)
    if requested_range:
        start = requested_range.get("start", {})
        end = requested_range.get("end", {})
        _, _, range_start = line_and_offset(
            text, start.get("line", 0), start.get("character", 0)
        )
        _, _, range_end = line_and_offset(
            text, end.get("line", 10**9), end.get("character", 10**9)
        )

    def hint(offset: int, type_name: str) -> Optional[dict]:
        if not type_name or not (range_start <= offset <= range_end):
            return None
        return {
            "position": offset_to_position(text, offset),
            "label": f": {type_name}",
            # LSP InlayHintKind.Type.
            "kind": 1,
            "paddingLeft": True,
        }

    hints: List[dict] = []
    comment_free = mask_comments_and_strings(text, keep_strings=True)
    masked = mask_comments_and_strings(text)
    index = build_index(uri, text)
    local = {
        (symbol.name, symbol.offset): symbol.type_name
        for symbol in local_symbols(uri, text)
        if symbol.type_name
    }

    for match in INFERRED_DECLARATION_PATTERN.finditer(comment_free):
        type_name = local.get((match.group("name"), match.start("name")), "")
        item = hint(match.end("name"), type_name)
        if item is not None:
            hints.append(item)

    for match in TUPLE_DESTRUCTURE_PATTERN.finditer(comment_free):
        names_offset = match.start("names")
        for name_match in re.finditer(r"[A-Za-z_]\w*", match.group("names")):
            name = name_match.group(0)
            if name == "_":
                continue
            start = names_offset + name_match.start()
            type_name = local.get((name, start), "")
            item = hint(start + len(name), type_name)
            if item is not None:
                hints.append(item)

    value_types = source_value_types(text, masked, comment_free)
    function_types = {
        symbol.name: symbol.type_name
        for symbol in index.symbols
        if symbol.kind == KIND_FUNCTION and symbol.type_name
    }
    for function_match in FUNCTION_PATTERN.finditer(masked):
        inferred = inferred_parameter_types(
            text, comment_free, function_match, value_types, function_types
        )
        parameters = text[function_match.start(3):function_match.end(3)]
        parameters_offset = function_match.start(3)
        for name, type_name in inferred.items():
            name_match = re.search(rf"\b{re.escape(name)}\b", parameters)
            if name_match is None:
                continue
            item = hint(parameters_offset + name_match.end(), type_name)
            if item is not None:
                hints.append(item)

        if function_match.group(4):
            continue
        function = next((
            symbol for symbol in index.by_name.get(function_match.group(1), [])
            if symbol.kind == KIND_FUNCTION and symbol.uri == uri
        ), None)
        if function is not None:
            item = hint(function_match.end(3) + 1, function.type_name)
            if item is not None:
                hints.append(item)
    return hints

def read_message():
    headers = {}
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        try:
            name, value = line.decode("ascii").split(":", 1)
        except ValueError:
            continue
        headers[name.lower()] = value.strip()
    try:
        length = int(headers.get("content-length", "0"))
    except ValueError:
        return None
    body = sys.stdin.buffer.read(length)
    if len(body) != length:
        return None
    return json.loads(body.decode("utf-8"))

def set_workspace_root(params: dict) -> None:
    global workspace_root
    folders = params.get("workspaceFolders") or []
    uri = folders[0].get("uri") if folders else params.get("rootUri")
    if uri:
        workspace_root = path_from_uri(uri)
    elif params.get("rootPath"):
        workspace_root = Path(params["rootPath"]).resolve()
    invalidate_analysis_caches(clear_files=True)

def main() -> None:
    shutting_down = False
    log("started (version 0.7.0)")
    while True:
        try:
            message = read_message()
            if message is None:
                break
            method = message.get("method")
            params = message.get("params", {})
            request_id = message.get("id")

            if method == "initialize":
                set_workspace_root(params)
                respond(request_id, {
                    "capabilities": {
                        "textDocumentSync": {
                            "openClose": True,
                            "change": 1,
                            "save": {"includeText": True},
                        },
                        "hoverProvider": True,
                        "definitionProvider": True,
                        "diagnosticProvider": {
                            "identifier": "ferra",
                            "interFileDependencies": True,
                            "workspaceDiagnostics": False,
                        },
                        "completionProvider": {
                            "resolveProvider": False,
                            "triggerCharacters": [".", "<", '"', "/", "\\"],
                        },
                        "inlayHintProvider": True,
                    },
                    "serverInfo": {"name": "ferra-lsp", "version": "0.7.0"},
                })
            elif method == "initialized":
                pass
            elif method == "shutdown":
                shutting_down = True
                respond(request_id, None)
            elif method == "exit":
                sys.exit(0 if shutting_down else 1)
            elif method == "textDocument/didOpen":
                document = params["textDocument"]
                documents[document["uri"]] = document["text"]
                invalidate_analysis_caches()
                publish_diagnostics(document["uri"])
            elif method == "textDocument/didChange":
                uri = params["textDocument"]["uri"]
                changes = params.get("contentChanges") or []
                if changes:
                    current = documents.get(uri)
                    if current is None:
                        current = open_text(path_from_uri(uri)) or ""
                    documents[uri] = apply_content_changes(current, changes)
                    invalidate_analysis_caches()
                    publish_diagnostics(uri)
            elif method == "textDocument/didSave":
                document = params["textDocument"]
                if "text" in params:
                    documents[document["uri"]] = params["text"]
                invalidate_analysis_caches(clear_files=True)
                publish_diagnostics(document["uri"])
            elif method == "textDocument/didClose":
                uri = params["textDocument"]["uri"]
                documents.pop(uri, None)
                invalidate_analysis_caches(clear_files=True)
                send({
                    "jsonrpc": "2.0",
                    "method": "textDocument/publishDiagnostics",
                    "params": {"uri": uri, "diagnostics": []},
                })
            elif method == "workspace/didChangeWatchedFiles":
                invalidate_analysis_caches(clear_files=True)
                publish_all_diagnostics()
            elif method == "textDocument/diagnostic":
                uri = params["textDocument"]["uri"]
                text = documents.get(uri, open_text(path_from_uri(uri)) or "")
                respond(request_id, {
                    "kind": "full",
                    "items": diagnostics_for(uri, text),
                })
            elif method == "textDocument/hover":
                uri = params["textDocument"]["uri"]
                cursor = params["position"]
                text = documents.get(uri, open_text(path_from_uri(uri)) or "")
                respond(request_id, hover_for(
                    uri, text, cursor["line"], cursor["character"]
                ))
            elif method == "textDocument/definition":
                uri = params["textDocument"]["uri"]
                cursor = params["position"]
                text = documents.get(uri, open_text(path_from_uri(uri)) or "")
                respond(request_id, definition_for(
                    uri, text, cursor["line"], cursor["character"]
                ))
            elif method == "textDocument/completion":
                uri = params["textDocument"]["uri"]
                cursor = params["position"]
                text = documents.get(uri, open_text(path_from_uri(uri)) or "")
                respond(request_id, completion_for(
                    uri, text, cursor["line"], cursor["character"]
                ))
            elif method == "textDocument/inlayHint":
                uri = params["textDocument"]["uri"]
                text = documents.get(uri, open_text(path_from_uri(uri)) or "")
                respond(request_id, inlay_hints_for(
                    uri, text, params.get("range")
                ))
            elif request_id is not None:
                respond(request_id, None)
        except Exception as error:  # Keep the server alive on malformed source.
            log(f"{type(error).__name__}: {error}")
            if "request_id" in locals() and request_id is not None:
                respond_error(request_id, str(error))

    def test_tuple_annotation_does_not_consume_cast_expression(self):
        source = (
            "func main() {\n"
            "  var value: (str, i32) = (\"Bob\", 5 as i32)\n"
            "  log(value[1])\n"
            "}\n"
        )
        diagnostics = ferra_lsp.diagnostics_for(self.uri, source)
        self.assertFalse(any(
            "Unknown type -> 'as'" in diagnostic["message"]
            for diagnostic in diagnostics
        ))

if __name__ == "__main__":
    main()
