#!/usr/bin/env python3

"""Small dependency-free language server for the dynamic eFerra language."""

from bisect import bisect_right
from dataclasses import dataclass, field
from functools import lru_cache
import json
import os
from pathlib import Path
import re
import sys
from typing import Dict, List, Optional, Set, Tuple
from urllib.parse import unquote, urlparse


KIND_METHOD = 2
KIND_FUNCTION = 3
KIND_FIELD = 5
KIND_VARIABLE = 6
KIND_KEYWORD = 14
KIND_CONSTANT = 21

KEYWORDS = {
    "let", "fn", "ret", "if", "elif", "else", "for", "in", "of",
    "stop", "pass", "is", "not", "and", "or", "true", "false", "null",
}

BUILTINS = {
    "log": ("log(value) -> nul", 1, "Print a dynamic value."),
    "len": ("len(value) -> num", 1, "Return a string, array, or object length.")
}

PRIMITIVE_MEMBERS = {
    "str": {
        "len": ("String.len() -> num", 0, "Return the string length."),
        "contains": ("String.contains(needle) -> bol", 1, "Test whether the string contains text."),
        "starts_with": ("String.starts_with(prefix) -> bol", 1, "Test the string prefix."),
        "ends_with": ("String.ends_with(suffix) -> bol", 1, "Test the string suffix."),
        "split": ("String.split(separator) -> arr", 1, "Split into an array of strings."),
    },
    "arr": {
        "len": ("Array.len() -> num", 0, "Return the number of elements."),
        "push": ("Array.push(value) -> nul", 1, "Append a value and return the new length."),
        "pop": ("Array.pop() -> any", 0, "Remove and return the last value, or null."),
        "first": ("Array.first() -> any", 0, "Return the first value, or null."),
        "last": ("Array.last() -> any", 0, "Return the last value, or null."),
        "contains": ("Array.contains(value) -> bol", 1, "Test for an equal value."),
        "join": ("Array.join(separator) -> str", 1, "Join values into a string."),
    },
    "obj": {
        "len": ("Object.len() -> num", 0, "Return the number of fields."),
        "has": ("Object.has(key) -> bol", 1, "Test whether a field exists."),
        "get": ("Object.get(key) -> any", 1, "Return a field value, or null."),
        "set": ("Object.set(key, value) -> nul", 2, "Create or replace a field."),
        "remove": ("Object.remove(key) -> nul", 1, "Remove and return a field value, or null."),
        "keys": ("Object.keys() -> arr", 0, "Return field names in insertion order."),
        "values": ("Object.values() -> arr", 0, "Return field values in insertion order."),
    },
    "thread": {
        "join": ("Thread.join() -> nul", 0, "Wait for the worker and return null."),
        "done": ("Thread.done() -> bol", 0, "Return whether the worker has finished."),
    },
    "buffer": {
        "len": ("Buffer.len() -> num", 0, "Return the number of stored bytes."),
        "capacity": ("Buffer.capacity() -> num", 0, "Return the allocated byte capacity."),
        "clear": ("Buffer.clear() -> nul", 0, "Remove all bytes without freeing capacity."),
        "reserve": ("Buffer.reserve(capacity) -> nul", 1, "Grow the buffer capacity."),
        "append": ("Buffer.append(value) -> nul", 1, "Append a string, byte, or Buffer."),
        "str": ("Buffer.str() -> str", 0, "Copy buffered text into a string."),
        "bytes": ("Buffer.bytes() -> arr", 0, "Copy all bytes into an array."),
        "get": ("Buffer.get(index) -> num", 1, "Read one byte."),
        "set": ("Buffer.set(index, byte) -> nul", 2, "Replace one byte."),
    },
    "stream": {
        "read": ("Stream.read(buffer) -> nul", 1, "Replace a Buffer with the next chunk and return its byte count."),
        "done": ("Stream.done() -> bol", 0, "Return whether all input has been consumed."),
        "close": ("Stream.close() -> nul", 0, "Close the stream immediately."),
        "error": ("Stream.error() -> str", 0, "Return the transfer error or null."),
        "status": ("Stream.status() -> num", 0, "Return the HTTP status, or 0 for a file."),
    },
}

NATIVE_MEMBERS = {
    "timer": {
        "start": ("timer.start() -> nul", 0, "Reset the native timer."),
        "s": ("timer.s() -> num", 0, "Elapsed seconds."),
        "ms": ("timer.ms() -> num", 0, "Elapsed milliseconds."),
        "ns": ("timer.ns() -> num", 0, "Elapsed nanoseconds."),
    },
    "json": {
        "parse": (
            "json.parse(text) -> obj",
            1,
            "Parse JSON into recursive eFerra values.",
        ),
        "stringify": (
            "json.stringify(value) -> str",
            1,
            "Serialize an eFerra value as JSON.",
        ),
    },
    "file": {
        "read": ("file.read(path) -> str", 1, "Read a whole file as a string."),
        "write": (
            "file.write(path, text) -> bol",
            2,
            "Overwrite a file with a string.",
        ),
        "dir": ("file.dir() -> str", 0, "Return the current working directory."),
        "fdir": ("file.fdir() -> str", 0, "Return the Ferra installation directory."),
        "stream": ("file.stream(path) -> stream", 1, "Open a file for chunked binary reading."),
        "mkdir": ("file.mkdir(path) -> bol", 1, "Create new directory."),
    },
    "math": {
        "sin": ("math.sin(value) -> num", 1, "Sine."),
        "cos": ("math.cos(value) -> num", 1, "Cosine."),
        "tan": ("math.tan(value) -> num", 1, "Tangent."),
    },
    "http": {
        "get": ("http.get(url) -> str", 1, "Perform an HTTP GET request."),
        "post": (
            "http.post(url, body) -> str",
            2,
            "Perform an HTTP POST request.",
        ),
        "stream": ("http.stream(url) -> stream", 1, "Open a streaming HTTP GET request."),
        "stream_post": (
            "http.stream_post(url, body) -> stream",
            2,
            "Open a streaming HTTP POST request.",
        ),
    },
    "thread": {
        "create": (
            "thread.create(f, args) -> thread",
            2,
            "Start an eFerra function in a new VM and return a Thread handle.",
        ),
    },
    "buffer": {
        "create": (
            "buffer.create(capacity) -> buffer",
            1,
            "Create an empty owned byte buffer.",
        ),
        "from": (
            "buffer.from(text) -> num",
            1,
            "Create an owned buffer containing a string.",
        ),
    },
    "sys": {
        "args": (
            "sys.args() -> arr",
            0,
            "Get arguments from command line."
        ),
        "cmd": (
            "sys.cmd(text) -> num",
            1,
            "Run a command."
        ),
        "cmdout": (
            "sys.cmdout(bin, args) -> str",
            2,
            "Run a command and get the output."
        ),
        "exit": (
            "sys.exit(code) -> nul",
            1,
            "Exit with a code."
        ),
        "platform": (
            "sys.platform() -> str",
            0,
            "Get platform."
        )
    }
}


@dataclass(frozen=True)
class Symbol:
    name: str
    kind: int
    signature: str
    start: int
    end: int
    scope_start: int
    scope_end: int
    value_kind: str = "dynamic"


@dataclass(frozen=True)
class FunctionInfo:
    symbol: Symbol
    parameters: Tuple[str, ...]
    body_start: int
    body_end: int


@dataclass
class Analysis:
    text: str
    masked: str
    symbols: List[Symbol] = field(default_factory=list)
    functions: Dict[str, FunctionInfo] = field(default_factory=dict)
    by_name: Dict[str, List[Symbol]] = field(default_factory=dict)
    object_fields: Dict[str, List[Symbol]] = field(default_factory=dict)
    diagnostics: List[dict] = field(default_factory=list)

    def add(self, symbol: Symbol) -> None:
        self.symbols.append(symbol)
        self.by_name.setdefault(symbol.name, []).append(symbol)


documents: Dict[str, str] = {}
analysis_cache: Dict[str, Analysis] = {}


@lru_cache(maxsize=1024)
def path_from_uri(uri: str) -> Path:
    parsed = urlparse(uri)
    if parsed.scheme == "file":
        return Path(unquote(parsed.path)).resolve()
    return Path(uri).resolve()


@lru_cache(maxsize=64)
def line_starts(text: str) -> Tuple[int, ...]:
    starts = [0]
    starts.extend(i + 1 for i, char in enumerate(text) if char == "\n")
    return tuple(starts)


def utf16_length(value: str) -> int:
    if value.isascii():
        return len(value)
    return len(value.encode("utf-16-le")) // 2


def utf16_to_index(value: str, units: int) -> int:
    if value.isascii():
        return max(0, min(units, len(value)))
    current = 0
    for index, char in enumerate(value):
        width = utf16_length(char)
        if current + width > units:
            return index
        current += width
    return len(value)


def offset_to_position(text: str, offset: int) -> dict:
    offset = max(0, min(offset, len(text)))
    starts = line_starts(text)
    line = bisect_right(starts, offset) - 1
    return {
        "line": line,
        "character": utf16_length(text[starts[line]:offset]),
    }


def position_to_offset(text: str, position: dict) -> int:
    starts = line_starts(text)
    line = position.get("line", 0)
    if line < 0:
        return 0
    if line >= len(starts):
        return len(text)
    start = starts[line]
    end = starts[line + 1] if line + 1 < len(starts) else len(text)
    raw_line = text[start:end].rstrip("\r\n")
    return start + utf16_to_index(raw_line, position.get("character", 0))


def make_range(text: str, start: int, end: int) -> dict:
    return {
        "start": offset_to_position(text, start),
        "end": offset_to_position(text, end),
    }


def diagnostic(text: str, start: int, end: int, message: str) -> dict:
    return {
        "range": make_range(text, start, max(start + 1, end)),
        "severity": 1,
        "source": "eferra",
        "message": message,
    }


def mask_source(text: str) -> Tuple[str, List[dict]]:
    result = list(text)
    diagnostics: List[dict] = []
    index = 0
    while index < len(text):
        if text.startswith("//", index):
            end = text.find("\n", index)
            if end == -1:
                end = len(text)
            for pos in range(index, end):
                result[pos] = " "
            index = end
            continue
        if text.startswith("/*", index):
            end = text.find("*/", index + 2)
            if end == -1:
                diagnostics.append(diagnostic(
                    text, index, len(text), "Unclosed block comment"
                ))
                end = len(text) - 2
            stop = min(len(text), end + 2)
            for pos in range(index, stop):
                if result[pos] not in "\r\n":
                    result[pos] = " "
            index = stop
            continue
        if text[index] in ('"', "'"):
            quote = text[index]
            start = index
            # Keep one non-identifier sentinel so an argument containing only
            # a string literal is still counted after masking its contents.
            result[index] = "0"
            index += 1
            while index < len(text) and text[index] != quote:
                if text[index] == "\\" and index + 1 < len(text):
                    result[index] = " "
                    index += 1
                    if text[index] not in "\r\n":
                        result[index] = " "
                    index += 1
                    continue
                if result[index] not in "\r\n":
                    result[index] = " "
                index += 1
            if index >= len(text):
                diagnostics.append(diagnostic(
                    text, start, len(text), "Unclosed string"
                ))
                break
            result[index] = " "
        index += 1
    return "".join(result), diagnostics


def matching_brace(masked: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(masked)):
        if masked[index] == "{":
            depth += 1
        elif masked[index] == "}":
            depth -= 1
            if depth == 0:
                return index
    return len(masked)


def discover_native_members(text: str) -> Dict[str, Dict[str, Tuple[str, int, str]]]:
    discovered: Dict[str, Dict[str, Tuple[str, int, str]]] = {}
    masked, _ = mask_source(text)
    register_pattern = re.compile(
        r"\bfn\s+register_([A-Za-z_]\w*)\s*\([^)]*\)\s*\{"
    )
    method_pattern = re.compile(
        r"add_native_method\s*\(\s*[^,]+,\s*"
        r"\"([A-Za-z_]\w*)\"\s*,\s*(\d+)",
        re.DOTALL,
    )
    for register in register_pattern.finditer(masked):
        opening = masked.find("{", register.start(), register.end())
        closing = matching_brace(masked, opening)
        body = text[opening + 1:closing]
        globals_found = re.findall(
            r"find_native_global\s*\(\s*\"([A-Za-z_]\w*)\"\s*\)",
            body,
        )
        found_methods = method_pattern.findall(body)
        if not found_methods:
            continue
        # A native type without a global is either a primitive implementation
        # detail or a host-only type. It must not appear as a global namespace.
        if not globals_found:
            continue
        owner = globals_found[-1]
        members = discovered.setdefault(owner, {})
        for method, raw_arity in found_methods:
            arity = int(raw_arity)
            parameters = ", ".join(
                f"arg{index + 1}" for index in range(arity)
            )
            members[method] = (
                f"{owner}.{method}({parameters})",
                arity,
                f"Native eFerra method",
            )
    return discovered


def refresh_native_members(workspace: Optional[Path] = None) -> None:
    candidates: List[Path] = []
    ferra_path = os.environ.get("FERRA_PATH")
    if ferra_path:
        candidates.append(Path(ferra_path) / "eferra" / "natives.fe")
    if workspace is not None:
        base = workspace if workspace.is_dir() else workspace.parent
        for parent in (base, *base.parents):
            candidates.append(parent / "eferra" / "natives.fe")
    candidates.append(Path(__file__).resolve().parents[1] / "natives.fe")

    seen: Set[Path] = set()
    for candidate in candidates:
        try:
            canonical = candidate.resolve()
        except OSError:
            continue
        if canonical in seen:
            continue
        seen.add(canonical)
        try:
            source = canonical.read_text(encoding="utf-8")
        except (OSError, UnicodeError):
            continue
        for owner, members in discover_native_members(source).items():
            target = NATIVE_MEMBERS.setdefault(owner, {})
            for name, discovered in members.items():
                target.setdefault(name, discovered)
        return


def containing_function(
    functions: Dict[str, FunctionInfo],
    offset: int,
) -> Optional[FunctionInfo]:
    candidates = [
        function for function in functions.values()
        if function.body_start <= offset <= function.body_end
    ]
    return max(candidates, key=lambda fn: fn.body_start, default=None)


def value_kind(initializer: str) -> str:
    value = initializer.lstrip()
    if not value:
        return "any"
    if value[0] in ('"', "'"):
        return "str"
    if value[0] == "[":
        return "arr"
    if value[0] == "{":
        return "obj"
    if re.match(r"-?(?:\d+(?:\.\d*)?|\.\d+)", value):
        return "num"
    if re.match(r"(?:true|false)\b", value):
        return "bol"
    if re.match(r"null\b", value):
        return "nul"
    if re.match(r"json\s*\.\s*parse\s*\(", value):
        return "any JSON"
    if re.match(r"json\s*\.\s*stringify\s*\(", value):
        return "str"
    if re.match(r"(?:math\s*\.|timer\s*\.\s*(?:s|ms|ns)\b)", value):
        return "num"
    if re.match(r"file\s*\.\s*read\s*\(", value):
        return "str"
    if re.match(r"file\s*\.\s*(?:dir|fdir)\s*\(", value):
        return "str"
    if re.match(r"http\s*\.\s*(?:get|post)\s*\(", value):
        return "str"
    if re.match(r"thread\s*\.\s*create\s*\(", value):
        return "thread"
    if re.match(r"buffer\s*\.\s*(?:create|from)\s*\(", value):
        return "buffer"
    if re.match(
        r"(?:file\s*\.\s*stream|http\s*\.\s*(?:stream|stream_post))\s*\(",
        value,
    ):
        return "stream"
    native = re.match(r"([A-Za-z_]\w*)\b", value)
    if native and native.group(1) in NATIVE_MEMBERS:
        return f"{native.group(1)} (native)"
    return "any"


def literal_kind_before_dot(text: str, dot: int) -> str:
    index = dot - 1
    while index >= 0 and text[index].isspace():
        index -= 1
    if index < 0:
        return ""
    if text[index] in ('"', "'"):
        return "str"
    if text[index] == "]":
        return "arr"
    if text[index] == "}":
        return "obj"
    return ""


def object_literal_fields(
    text: str,
    opening: int,
    closing: int,
    scope_start: int,
    scope_end: int,
) -> List[Symbol]:
    fields: List[Symbol] = []
    index = opening + 1
    depth = 1
    expecting_key = True
    while index < closing:
        char = text[index]
        if char in ('"', "'"):
            quote = char
            key_start = index + 1
            index += 1
            while index < closing and text[index] != quote:
                if text[index] == "\\":
                    index += 1
                index += 1
            key_end = index
            probe = index + 1
            while probe < closing and text[probe].isspace():
                probe += 1
            if expecting_key and probe < closing and text[probe] == ":":
                name = text[key_start:key_end]
                fields.append(Symbol(
                    name, KIND_FIELD, f"{name}: dynamic",
                    key_start, key_end, scope_start, scope_end,
                ))
                expecting_key = False
            index += 1
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
        elif char == "[" or char == "(":
            nested = {"[": "]", "(": ")"}[char]
            nested_depth = 1
            index += 1
            while index < closing and nested_depth:
                if text[index] == char:
                    nested_depth += 1
                elif text[index] == nested:
                    nested_depth -= 1
                elif text[index] in ('"', "'"):
                    quote = text[index]
                    index += 1
                    while index < closing and text[index] != quote:
                        index += 2 if text[index] == "\\" else 1
                index += 1
            continue
        elif depth == 1 and char == ",":
            expecting_key = True
        elif depth == 1 and expecting_key and (
            char.isalpha() or char == "_"
        ):
            match = re.match(r"[A-Za-z_]\w*", text[index:])
            if match:
                name = match.group(0)
                end = index + len(name)
                probe = end
                while probe < closing and text[probe].isspace():
                    probe += 1
                if probe < closing and text[probe] == ":":
                    fields.append(Symbol(
                        name, KIND_FIELD, f"{name}: dynamic",
                        index, end, scope_start, scope_end,
                    ))
                    expecting_key = False
                index = end
                continue
        index += 1
    return fields


def delimiter_diagnostics(text: str, masked: str) -> List[dict]:
    result: List[dict] = []
    stack: List[Tuple[str, int]] = []
    pairs = {")": "(", "]": "[", "}": "{"}
    closers = {"(": ")", "[": "]", "{": "}"}
    for index, char in enumerate(masked):
        if char in closers:
            stack.append((char, index))
        elif char in pairs:
            if not stack or stack[-1][0] != pairs[char]:
                result.append(diagnostic(
                    text, index, index + 1, f"Unexpected -> '{char}'"
                ))
            else:
                stack.pop()
    for char, index in stack:
        result.append(diagnostic(
            text, index, index + 1,
            f"Missing closing -> '{closers[char]}'",
        ))
    return result


def argument_count(masked: str, opening: int, closing: int) -> int:
    if not masked[opening + 1:closing].strip():
        return 0
    count = 1
    depth = 0
    for char in masked[opening + 1:closing]:
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            count += 1
    return count


def closing_paren(masked: str, opening: int) -> int:
    depth = 0
    for index in range(opening, len(masked)):
        if masked[index] == "(":
            depth += 1
        elif masked[index] == ")":
            depth -= 1
            if depth == 0:
                return index
    return -1


def analyze(uri: str, text: str) -> Analysis:
    cached = analysis_cache.get(uri)
    if cached is not None and cached.text == text:
        return cached

    masked, diagnostics = mask_source(text)
    result = Analysis(text, masked, diagnostics=diagnostics)

    function_pattern = re.compile(
        r"\bfn\s+([A-Za-z_]\w*)\s*\(([^()]*)\)\s*\{"
    )
    for match in function_pattern.finditer(masked):
        name = match.group(1)
        opening = masked.find("{", match.start(), match.end())
        closing = matching_brace(masked, opening)
        parameters = tuple(
            value.strip()
            for value in match.group(2).split(",")
            if value.strip()
        )
        signature = f"fn {name}({', '.join(parameters)})"
        symbol = Symbol(
            name, KIND_FUNCTION, signature,
            match.start(1), match.end(1), 0, len(text), "function",
        )
        if name in result.functions:
            result.diagnostics.append(diagnostic(
                text, match.start(1), match.end(1),
                f"Duplicate function -> '{name}'",
            ))
        else:
            result.functions[name] = FunctionInfo(
                symbol, parameters, opening + 1, closing
            )
        result.add(symbol)

        seen: Set[str] = set()
        parameter_base = match.start(2)
        for parameter_match in re.finditer(r"[A-Za-z_]\w*", match.group(2)):
            parameter = parameter_match.group(0)
            start = parameter_base + parameter_match.start()
            if parameter in seen:
                result.diagnostics.append(diagnostic(
                    text, start, start + len(parameter),
                    f"Duplicate parameter -> '{parameter}'",
                ))
            seen.add(parameter)
            result.add(Symbol(
                parameter, KIND_VARIABLE, f"{parameter}: dynamic parameter",
                start, start + len(parameter), opening + 1, closing,
            ))

    let_pattern = re.compile(r"\blet\s+([A-Za-z_]\w*)\s*(=)?")
    for match in let_pattern.finditer(masked):
        name = match.group(1)
        function = containing_function(result.functions, match.start())
        scope_start = function.body_start if function else 0
        scope_end = function.body_end if function else len(text)
        if match.group(2) != "=":
            result.diagnostics.append(diagnostic(
                text, match.start(1), match.end(1),
                f"Expected '=' after variable -> '{name}'",
            ))
            initializer = ""
        else:
            initializer = text[match.end():]
        kind = value_kind(initializer)
        symbol = Symbol(
            name, KIND_VARIABLE, f"let {name}: {kind}",
            match.start(1), match.end(1), scope_start, scope_end, kind,
        )
        result.add(symbol)
        stripped = initializer.lstrip()
        if stripped.startswith("{"):
            opening = match.end() + len(initializer) - len(stripped)
            closing = matching_brace(masked, opening)
            result.object_fields[name] = object_literal_fields(
                text, opening, closing, scope_start, scope_end
            )

    # Detect arrow-function parameters assigned to a `let` binding, e.g.
    # `let id = x -> { ret x }` or `let add = (a, b) -> { ... }` and register
    # the parameter names as variables scoped to the arrow function body so
    # hover/diagnostics treat them as known names.
    arrow_pattern = re.compile(
        r"\blet\s+([A-Za-z_]\w*)\s*=\s*(?:\(([^()]*)\)|([A-Za-z_]\w*))\s*->\s*\{"
    )
    for match in arrow_pattern.finditer(masked):
        name = match.group(1)
        params = match.group(2) if match.group(2) is not None else match.group(3)
        param_base = match.start(2) if match.group(2) is not None else match.start(3)
        # find opening brace of the function body and its matching close
        opening = masked.find("{", match.end() - 1)
        if opening == -1:
            continue
        closing = matching_brace(masked, opening)
        if closing == -1:
            continue
        seen: Set[str] = set()
        for parameter_match in re.finditer(r"[A-Za-z_]\w*", params or ""):
            parameter = parameter_match.group(0)
            start = param_base + parameter_match.start()
            if parameter in seen:
                result.diagnostics.append(diagnostic(
                    text, start, start + len(parameter),
                    f"Duplicate parameter -> '{parameter}'",
                ))
                continue
            seen.add(parameter)
            existing_same_name = any(
                other.name == parameter and other.scope_start >= opening + 1 and
                other.scope_end <= closing and other.start != start
                for other in result.symbols
            )
            if existing_same_name:
                continue
            result.add(Symbol(
                parameter, KIND_VARIABLE, f"{parameter}: dynamic parameter",
                start, start + len(parameter), opening + 1, closing,
            ))

    # Also detect arrow-function parameters for concise expression bodies,
    # e.g. `let id = x -> x * x` or `let add = (a, b) -> a + b` and register
    # the parameter names as variables scoped to the expression so hover/
    # diagnostics treat them as known names.
    expr_arrow_pattern = re.compile(
        r"\blet\s+([A-Za-z_]\w*)\s*=\s*(?:\(([^()]*)\)|([A-Za-z_]\w*))\s*->\s*(?!\{)"
    )
    for match in expr_arrow_pattern.finditer(masked):
        name = match.group(1)
        params = match.group(2) if match.group(2) is not None else match.group(3)
        param_base = match.start(2) if match.group(2) is not None else match.start(3)
        # expression body runs until the end of the line (or end of file)
        opening = match.end()
        closing = text.find("\n", opening)
        if closing == -1:
            closing = len(text)
        seen: Set[str] = set()
        for parameter_match in re.finditer(r"[A-Za-z_]\w*", params or ""):
            parameter = parameter_match.group(0)
            start = param_base + parameter_match.start()
            if parameter in seen:
                result.diagnostics.append(diagnostic(
                    text, start, start + len(parameter),
                    f"Duplicate parameter -> '{parameter}'",
                ))
                continue
            seen.add(parameter)
            existing_same_name = any(
                other.name == parameter and other.scope_start >= opening and
                other.scope_end <= closing and other.start != start
                for other in result.symbols
            )
            if existing_same_name:
                continue
            result.add(Symbol(
                parameter, KIND_VARIABLE, f"{parameter}: dynamic parameter",
                start, start + len(parameter), opening, closing,
            ))

    for match in re.finditer(
        r"\bfor\s+([A-Za-z_]\w*)\s+(?:in|of)\b", masked
    ):
        name = match.group(1)
        opening = masked.find("{", match.end())
        closing = matching_brace(masked, opening) if opening != -1 else len(text)
        result.add(Symbol(
            name, KIND_VARIABLE, f"{name} -> loop variable",
            match.start(1), match.end(1),
            opening + 1 if opening != -1 else match.end(), closing,
        ))

    result.diagnostics.extend(delimiter_diagnostics(text, masked))

    for match in re.finditer(
        r"\b([A-Za-z_]\w*)\s*\.\s*([A-Za-z_]\w*)",
        masked,
    ):
        owner, member = match.groups()
        if owner in NATIVE_MEMBERS and member not in NATIVE_MEMBERS[owner]:
            result.diagnostics.append(diagnostic(
                text, match.start(2), match.end(2),
                f"Unknown native method -> '{owner}.{member}'",
            ))
        elif owner not in NATIVE_MEMBERS:
            primitive = primitive_members_for(result, owner, match.start())
            fields = {item.name for item in result.object_fields.get(owner, [])}
            if primitive and member not in primitive and member not in fields:
                result.diagnostics.append(diagnostic(
                    text, match.start(2), match.end(2),
                    f"Unknown {visible_symbol(result, owner, match.start()).value_kind} "
                    f"method -> '{member}'",
                ))

    known_names = KEYWORDS | set(BUILTINS) | set(NATIVE_MEMBERS)
    declaration_offsets = {symbol.start for symbol in result.symbols}
    for match in re.finditer(r"\b[A-Za-z_]\w*\b", masked):
        word = match.group(0)
        # skip explicit declarations and known global names; for local symbols
        # we must check visibility at this occurrence using `visible_symbol`.
        if match.start() in declaration_offsets or word in known_names:
            continue
        if visible_symbol(result, word, match.start()) is not None:
            continue
        before = masked[:match.start()].rstrip()
        after = masked[match.end():].lstrip()
        if before.endswith(".") or after.startswith(":"):
            continue
        result.diagnostics.append(diagnostic(
            text, match.start(), match.end(),
            f"Unknown name -> '{word}'",
        ))

    call_pattern = re.compile(
        r"(?:(?P<object>[A-Za-z_]\w*)\s*\.\s*)?"
        r"(?P<name>[A-Za-z_]\w*)\s*\("
    )
    for match in call_pattern.finditer(masked):
        prefix = masked[max(0, match.start() - 4):match.start()]
        if re.search(r"\bfn\s*$", prefix):
            continue
        owner = match.group("object")
        name = match.group("name")
        expected: Optional[int] = None
        signature = ""
        if owner in NATIVE_MEMBERS and name in NATIVE_MEMBERS[owner]:
            signature, expected, _ = NATIVE_MEMBERS[owner][name]
        elif owner is not None:
            primitive = primitive_members_for(result, owner, match.start())
            if name in primitive:
                signature, expected, _ = primitive[name]
        elif masked[:match.start("name")].rstrip().endswith("."):
            dot = masked.rfind(".", 0, match.start("name"))
            primitive = PRIMITIVE_MEMBERS.get(
                literal_kind_before_dot(text, dot), {}
            )
            if name in primitive:
                signature, expected, _ = primitive[name]
        elif owner is None and name in BUILTINS:
            signature, expected, _ = BUILTINS[name]
        elif owner is None and name in result.functions:
            function = result.functions[name]
            signature = function.symbol.signature
            expected = len(function.parameters)
        if expected is None:
            continue
        opening = masked.find("(", match.start(), match.end())
        closing = closing_paren(masked, opening)
        if closing == -1:
            continue
        actual = argument_count(masked, opening, closing)
        if actual != expected:
            result.diagnostics.append(diagnostic(
                text, match.start("name"), match.end("name"),
                f"{signature} expects -> {expected} argument(s), got -> {actual}",
            ))

    analysis_cache[uri] = result
    return result


def word_at(text: str, position: dict) -> Tuple[str, int, int]:
    offset = position_to_offset(text, position)
    start = offset
    while start > 0 and (
        text[start - 1].isalnum() or text[start - 1] == "_"
    ):
        start -= 1
    end = offset
    while end < len(text) and (
        text[end].isalnum() or text[end] == "_"
    ):
        end += 1
    return text[start:end], start, end


def object_before(text: str, word_start: int) -> str:
    index = word_start - 1
    while index >= 0 and text[index].isspace():
        index -= 1
    if index < 0 or text[index] != ".":
        return ""
    index -= 1
    while index >= 0 and text[index].isspace():
        index -= 1
    end = index + 1
    while index >= 0 and (
        text[index].isalnum() or text[index] == "_"
    ):
        index -= 1
    return text[index + 1:end]


def visible_symbol(analysis: Analysis, name: str, offset: int) -> Optional[Symbol]:
    candidates = [
        symbol for symbol in analysis.by_name.get(name, [])
        if symbol.start <= offset
        and symbol.scope_start <= offset <= symbol.scope_end
    ]
    if candidates:
        return max(candidates, key=lambda symbol: symbol.start)
    functions = [
        symbol for symbol in analysis.by_name.get(name, [])
        if symbol.kind == KIND_FUNCTION
    ]
    return functions[0] if functions else None


def primitive_members_for(
    analysis: Analysis,
    owner: str,
    offset: int,
) -> Dict[str, Tuple[str, int, str]]:
    symbol = visible_symbol(analysis, owner, offset)
    if symbol is None:
        return {}
    return PRIMITIVE_MEMBERS.get(symbol.value_kind, {})


def hover_for(uri: str, text: str, position: dict):
    word, start, end = word_at(text, position)
    if not word:
        return None
    analysis = analyze(uri, text)
    owner = object_before(text, start)
    signature = ""
    documentation = ""
    primitive = primitive_members_for(analysis, owner, start) if owner else {}
    if not owner:
        dot = text.rfind(".", 0, start)
        if dot >= 0:
            primitive = PRIMITIVE_MEMBERS.get(
                literal_kind_before_dot(text, dot), {}
            )
    if owner in NATIVE_MEMBERS and word in NATIVE_MEMBERS[owner]:
        signature, _, documentation = NATIVE_MEMBERS[owner][word]
    elif owner and owner in analysis.object_fields:
        field = next((
            item for item in analysis.object_fields[owner]
            if item.name == word
        ), None)
        if field:
            signature = field.signature
            documentation = f"Dynamic field of -> '{owner}'"
    if not signature and word in primitive:
        signature, _, documentation = primitive[word]
    elif word in NATIVE_MEMBERS:
        signature = f"{word}: native {word.title()}"
        documentation = "eFerra native object"
    elif word in BUILTINS:
        signature, _, documentation = BUILTINS[word]
    else:
        symbol = visible_symbol(analysis, word, start)
        if symbol:
            signature = symbol.signature
            documentation = "eFerra declaration"
    if not signature:
        return None
    # Trim any trailing return annotation like '-> type' from signatures
    display_sig = signature.split("->")[0].strip()
    value = f"\x60\x60\x60eferra\n{display_sig}\n\x60\x60\x60"
    if documentation:
        value += f"\n\n{documentation}"
    return {
        "contents": {"kind": "markdown", "value": value},
        "range": make_range(text, start, end),
    }


def completion_item(label: str, kind: int, detail: str, documentation="") -> dict:
    item = {"label": label, "kind": kind, "detail": detail}
    if documentation:
        item["documentation"] = documentation
    return item


def completion_for(uri: str, text: str, position: dict) -> dict:
    offset = position_to_offset(text, position)
    analysis = analyze(uri, text)
    prefix = text[:offset]
    member = re.search(r"([A-Za-z_]\w*)\s*\.\s*[A-Za-z_]*$", prefix)
    if member:
        owner = member.group(1)
        items: List[dict] = []
        if owner in NATIVE_MEMBERS:
            for name, (signature, _, docs) in NATIVE_MEMBERS[owner].items():
                items.append(completion_item(name, KIND_METHOD, signature, docs))
        for field in analysis.object_fields.get(owner, []):
            items.append(completion_item(
                field.name, KIND_FIELD, field.signature,
                f"Dynamic field of -> '{owner}'",
            ))
        fields = {field.name for field in analysis.object_fields.get(owner, [])}
        for name, (signature, _, docs) in primitive_members_for(
            analysis, owner, offset
        ).items():
            if name not in fields:
                items.append(completion_item(name, KIND_METHOD, signature, docs))
        return {"isIncomplete": False, "items": items}

    literal_member = re.search(r"\.\s*[A-Za-z_]*$", prefix)
    if literal_member:
        kind = literal_kind_before_dot(text, literal_member.start())
        items = [
            completion_item(name, KIND_METHOD, signature, docs)
            for name, (signature, _, docs) in PRIMITIVE_MEMBERS.get(kind, {}).items()
        ]
        return {"isIncomplete": False, "items": items}

    items: List[dict] = []
    seen: Set[Tuple[str, int]] = set()
    for symbol in analysis.symbols:
        if symbol.kind != KIND_FUNCTION and not (
            symbol.start <= offset
            and symbol.scope_start <= offset <= symbol.scope_end
        ):
            continue
        key = (symbol.name, symbol.kind)
        if key not in seen:
            seen.add(key)
            items.append(completion_item(
                symbol.name, symbol.kind, symbol.signature
            ))
    for name, (signature, _, docs) in BUILTINS.items():
        items.append(completion_item(name, KIND_FUNCTION, signature, docs))
    for name in NATIVE_MEMBERS:
        items.append(completion_item(
            name, KIND_CONSTANT, f"{name}: native {name.title()}"
        ))
    for keyword in sorted(KEYWORDS):
        items.append(completion_item(keyword, KIND_KEYWORD, keyword))
    return {"isIncomplete": False, "items": items}


def definition_for(uri: str, text: str, position: dict):
    word, start, _ = word_at(text, position)
    if not word:
        return None
    analysis = analyze(uri, text)
    owner = object_before(text, start)
    symbol: Optional[Symbol] = None
    if owner in analysis.object_fields:
        symbol = next((
            item for item in analysis.object_fields[owner]
            if item.name == word
        ), None)
    if symbol is None:
        symbol = visible_symbol(analysis, word, start)
    if symbol is None:
        return None
    return {
        "uri": uri,
        "range": make_range(text, symbol.start, symbol.end),
    }


def signature_help_for(uri: str, text: str, position: dict):
    offset = position_to_offset(text, position)
    analysis = analyze(uri, text)
    depth = 0
    opening = -1
    for index in range(offset - 1, -1, -1):
        char = analysis.masked[index]
        if char == ")":
            depth += 1
        elif char == "(":
            if depth == 0:
                opening = index
                break
            depth -= 1
    if opening == -1:
        return None
    before = analysis.masked[:opening]
    match = re.search(
        r"(?:(?P<object>[A-Za-z_]\w*)\s*\.\s*)?"
        r"(?P<name>[A-Za-z_]\w*)\s*$",
        before,
    )
    if not match:
        return None
    owner = match.group("object")
    name = match.group("name")
    signature = ""
    params: List[str] = []
    if owner in NATIVE_MEMBERS and name in NATIVE_MEMBERS[owner]:
        signature = NATIVE_MEMBERS[owner][name][0]
        inside = signature[signature.find("(") + 1:signature.rfind(")")]
        params = [value.strip() for value in inside.split(",") if value.strip()]
    elif owner:
        primitive = primitive_members_for(analysis, owner, opening)
        if name in primitive:
            signature = primitive[name][0]
            inside = signature[signature.find("(") + 1:signature.rfind(")")]
            params = [value.strip() for value in inside.split(",") if value.strip()]
    elif before[:match.start("name")].rstrip().endswith("."):
        dot = before.rfind(".", 0, match.start("name"))
        primitive = PRIMITIVE_MEMBERS.get(
            literal_kind_before_dot(text, dot), {}
        )
        if name in primitive:
            signature = primitive[name][0]
            inside = signature[signature.find("(") + 1:signature.rfind(")")]
            params = [value.strip() for value in inside.split(",") if value.strip()]
    elif owner is None and name in BUILTINS:
        signature = BUILTINS[name][0]
        inside = signature[signature.find("(") + 1:signature.rfind(")")]
        params = [value.strip() for value in inside.split(",") if value.strip()]
    elif owner is None and name in analysis.functions:
        function = analysis.functions[name]
        signature = function.symbol.signature
        params = list(function.parameters)
    if not signature:
        return None
    active = argument_count(
        analysis.masked, opening, max(opening + 1, offset)
    ) - 1
    active = max(0, min(active, max(0, len(params) - 1)))
    display_sig = signature.split("->")[0].strip()
    return {
        "signatures": [{
            "label": display_sig,
            "parameters": [{"label": value} for value in params],
        }],
        "activeSignature": 0,
        "activeParameter": active,
    }


def document_symbols_for(uri: str, text: str) -> List[dict]:
    analysis = analyze(uri, text)
    return [
        {
            "name": symbol.name,
            "kind": symbol.kind,
            "range": make_range(text, symbol.start, symbol.end),
            "selectionRange": make_range(text, symbol.start, symbol.end),
            "detail": symbol.signature,
        }
        for symbol in analysis.symbols
        if symbol.kind in (KIND_FUNCTION, KIND_VARIABLE)
        and symbol.scope_start == 0
    ]


def apply_changes(text: str, changes: List[dict]) -> str:
    for change in changes:
        if "range" not in change:
            text = change.get("text", "")
            continue
        start = position_to_offset(text, change["range"]["start"])
        end = position_to_offset(text, change["range"]["end"])
        text = text[:start] + change.get("text", "") + text[end:]
    return text


def send(message: dict) -> None:
    payload = json.dumps(message, ensure_ascii=False).encode("utf-8")
    sys.stdout.buffer.write(
        f"Content-Length: {len(payload)}\r\n\r\n".encode("ascii")
    )
    sys.stdout.buffer.write(payload)
    sys.stdout.buffer.flush()


def reply(request_id, result) -> None:
    send({"jsonrpc": "2.0", "id": request_id, "result": result})


def read_message() -> Optional[dict]:
    content_length = None
    while True:
        line = sys.stdin.buffer.readline()
        if not line:
            return None
        if line in (b"\r\n", b"\n"):
            break
        name, _, value = line.decode("ascii", errors="replace").partition(":")
        if name.lower() == "content-length":
            content_length = int(value.strip())
    if content_length is None:
        return None
    body = sys.stdin.buffer.read(content_length)
    return json.loads(body.decode("utf-8"))


def document_text(uri: str) -> str:
    if uri in documents:
        return documents[uri]
    try:
        return path_from_uri(uri).read_text(encoding="utf-8")
    except (OSError, UnicodeError):
        return ""


def publish_diagnostics(uri: str) -> None:
    text = document_text(uri)
    send({
        "jsonrpc": "2.0",
        "method": "textDocument/publishDiagnostics",
        "params": {"uri": uri, "diagnostics": analyze(uri, text).diagnostics},
    })


def main() -> None:
    shutting_down = False
    refresh_native_members()
    while True:
        message = read_message()
        if message is None:
            return
        method = message.get("method")
        params = message.get("params") or {}
        request_id = message.get("id")
        try:
            if method == "initialize":
                root_uri = params.get("rootUri")
                root_path = params.get("rootPath")
                workspace = (
                    path_from_uri(root_uri) if root_uri
                    else Path(root_path).resolve() if root_path
                    else None
                )
                refresh_native_members(workspace)
                reply(request_id, {
                    "capabilities": {
                        "textDocumentSync": 2,
                        "hoverProvider": True,
                        "definitionProvider": True,
                        "completionProvider": {"triggerCharacters": ["."]},
                        "signatureHelpProvider": {
                            "triggerCharacters": ["(", ","],
                        },
                        "documentSymbolProvider": True,
                        "diagnosticProvider": {
                            "interFileDependencies": False,
                            "workspaceDiagnostics": False,
                        },
                    },
                    "serverInfo": {
                        "name": "eferra-lsp",
                        "version": "0.1.0",
                    },
                })
            elif method == "shutdown":
                shutting_down = True
                reply(request_id, None)
            elif method == "exit":
                raise SystemExit(0 if shutting_down else 1)
            elif method == "textDocument/didOpen":
                document = params["textDocument"]
                documents[document["uri"]] = document["text"]
                analysis_cache.pop(document["uri"], None)
                publish_diagnostics(document["uri"])
            elif method == "textDocument/didChange":
                uri = params["textDocument"]["uri"]
                documents[uri] = apply_changes(
                    document_text(uri), params.get("contentChanges") or []
                )
                analysis_cache.pop(uri, None)
                publish_diagnostics(uri)
            elif method == "textDocument/didSave":
                uri = params["textDocument"]["uri"]
                if "text" in params:
                    documents[uri] = params["text"]
                analysis_cache.pop(uri, None)
                publish_diagnostics(uri)
            elif method == "textDocument/didClose":
                uri = params["textDocument"]["uri"]
                documents.pop(uri, None)
                analysis_cache.pop(uri, None)
                send({
                    "jsonrpc": "2.0",
                    "method": "textDocument/publishDiagnostics",
                    "params": {"uri": uri, "diagnostics": []},
                })
            elif method == "textDocument/hover":
                uri = params["textDocument"]["uri"]
                reply(request_id, hover_for(
                    uri, document_text(uri), params["position"]
                ))
            elif method == "textDocument/completion":
                uri = params["textDocument"]["uri"]
                reply(request_id, completion_for(
                    uri, document_text(uri), params["position"]
                ))
            elif method == "textDocument/definition":
                uri = params["textDocument"]["uri"]
                reply(request_id, definition_for(
                    uri, document_text(uri), params["position"]
                ))
            elif method == "textDocument/signatureHelp":
                uri = params["textDocument"]["uri"]
                reply(request_id, signature_help_for(
                    uri, document_text(uri), params["position"]
                ))
            elif method == "textDocument/documentSymbol":
                uri = params["textDocument"]["uri"]
                reply(request_id, document_symbols_for(
                    uri, document_text(uri)
                ))
            elif method == "textDocument/diagnostic":
                uri = params["textDocument"]["uri"]
                reply(request_id, {
                    "kind": "full",
                    "items": analyze(
                        uri, document_text(uri)
                    ).diagnostics,
                })
            elif request_id is not None:
                reply(request_id, None)
        except SystemExit:
            raise
        except Exception as error:
            if request_id is not None:
                send({
                    "jsonrpc": "2.0",
                    "id": request_id,
                    "error": {"code": -32603, "message": str(error)},
                })
            else:
                print(f"eferra-lsp: {error}", file=sys.stderr, flush=True)


if __name__ == "__main__":
    main()