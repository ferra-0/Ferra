# EFerra VM

EFerra can call low-level Ferra code through native bindings. The standard
bindings are installed by `compile_program`; they expose native objects such
as `timer`, `json`, `file`, `http`, `thread`, and `buffer`.

```efe
timer.start()
log(timer.ns())

let now = timer.ns
log(now())
```

## JSON

`json.parse(text)` recursively converts JSON objects, arrays, strings,
numbers, booleans and null into EFerra values. `json.stringify(value)` performs
the reverse conversion:

```efe
let value = json.parse('{"name":"Ferra","items":[1,2,null]}')
log(value.name)

let text = json.stringify({
  answer: 42,
  items: ["x", null]
})
log(text)
```

Invalid JSON and values containing native functions or native objects produce
a runtime error. EFerra currently represents booleans as numeric `1` and `0`,
so parsed JSON booleans follow the same convention.

## Registering a native function

Native callbacks use one stable ABI and return `0` on success:

```ferra
fn native_add(
  receiver: ptr,
  raw_args: ptr,
  argument_count: i32,
  raw_out: ptr
): i32 {
  let args: Value[] = raw_args
  let out: Value* = raw_out
  value_write_num(out, args[0].num + args[1].num)
  ret 0
}

module.add_native_global_function("native_add", 2, native_add)
```

Register custom object methods with `add_native_type` and
`add_native_method`, then expose an instance through `add_native_global`.
`value_write_native_object` creates a borrowed handle. For heap-owned native
objects, `value_write_owned_native_object` accepts a drop callback and shares
ownership with bound method values, so the host object is destroyed exactly
once.

Custom globals and types must be registered on `Module` before calling
`compile_program`.

## Primitive methods

Strings, arrays, and objects expose bound methods. A bound method owns a safe
reference to its receiver, so it may be stored and called later:

```efe
let words = "one,two".split(",")
let push = words.push
push("three")
log(words.join(" | "))

let config = {mode: "debug"}
config.set("threads", 4)
log(config.get("mode"))
```

Available methods:

- `String`: `len`, `contains`, `starts_with`, `ends_with`, `split`;
- `Array`: `len`, `push`, `pop`, `first`, `last`, `contains`, `join`;
- `Object`: `len`, `has`, `get`, `set`, `remove`, `keys`, `values`.

Array aliases share their elements, length, and capacity, therefore mutations
through an alias or a stored method are visible through every reference. For
objects, a real field takes precedence over a built-in method with the same
name.

## Threads

An eFerra function is a first-class value and can run in a separate VM:

```efe
fn worker(left, right) {
  log(left + right)
}

let task = thread.create(worker, [20, 22])
log(task.done())
task.join()
log(task.done()) // 1
```

`thread.create(f, args)` requires an eFerra function and an array whose length
matches the function parameter count. Every worker owns a separate VM stack.
`Thread.join()` waits and returns `null`; `Thread.done()` returns `0` or `1`.
Dropping the final handle automatically joins a running worker.

The runtime's ownership counters are atomic, but mutable arrays and objects are
not implicitly synchronized. Sharing and mutating them from several workers
still requires an application-level mutex or atomic abstraction.

## Expressions and anonymous functions

Logical `and` and `or` operators short-circuit. Conditional expressions use
the familiar `condition ? when_true : when_false` form, and `elif` can be used
between `if` and `else` blocks:

```efe
let x = 5
log(x > 2 and x < 10)
log(x > 2 ? "more" : "less")

if x < 0 {
  log("negative")
} elif x is 0 {
  log("zero")
} else {
  log("positive")
}
```

Functions are first-class values and anonymous functions support block and
expression bodies:

```efe
let a = fn() { ret 42 }
let add = fn(a, b) -> a + b
let square = x -> { ret x * x }
let multiply = (a, b) -> { ret a * b }
```

A standalone zero-argument arrow block is a local block and executes in the
surrounding scope, so it can directly use surrounding locals:

```efe
let values = [1, 2, 3]
() -> {
  for value of values {
    log(value)
  }
}
```

Anonymous function values currently do not capture surrounding local
variables; pass such values as parameters when they are needed by the
function. Local arrow blocks do use the surrounding scope because they are
compiled inline.

## Buffers and streams

`Buffer` is an owned, growable byte sequence. It can safely contain zero bytes
and other binary data:

```efe
let data = buffer.create(4096)
data.append("hello")
data.append(0)
data.append(255)
log(data.len())
log(data.bytes())
```

Available constructors are `buffer.create(capacity)` and `buffer.from(text)`.
A buffer exposes `len`, `capacity`, `clear`, `reserve`, `append`, `str`,
`bytes`, `get`, and `set`. `str()` treats the contents as NUL-terminated text,
so use `bytes()` when embedded zero bytes must be preserved.

Files and HTTP responses use the same `Stream` interface. `read(buffer)`
replaces the buffer contents with at most `buffer.capacity()` bytes and returns
the count. It returns `0` at the end of input and `-1` for an HTTP transfer
error:

```efe
let input = file.stream("data.bin")
// The same loop works with http.stream("https://example.com/data.bin").
let chunk = buffer.create(4096)

for input.done() is 0 {
  let count = input.read(chunk)
  if count < 0 {
    log(input.error())
    stop
  }
  if count > 0 {
    log(chunk.bytes())
  }
}

log(input.status()) // HTTP status, or 0 for a file
input.close()
```

Use `http.stream_post(url, body)` for a streaming POST response. Streams and
buffers release their native memory automatically when the final eFerra handle
goes out of scope; `close()` is available when the underlying file or socket
should be released earlier.
