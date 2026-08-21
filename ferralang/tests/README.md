# Ferra regression suite

This directory exercises the compiler by topic. Every positive test goes
through the complete `Ferra -> LLVM IR -> native executable` pipeline unless a
neighboring `.compile_only` marker exists. Tests with an `.out` file must print
that output exactly. Tests with an `.args` file receive one program argument per
line. An `.ir_contains` file lists LLVM IR fragments that must be present.

Negative tests must fail during Ferra compilation. Their `.error` files contain
diagnostic fragments that must be reported.

Run everything from any directory:

```bash
ferralang/tests/run.sh
```

Set `FERRA_COMPILER=/path/to/ferra` to test another compiler binary, or
`KEEP_TEST_ARTIFACTS=1` to preserve generated IR and executables. Native tests
use `-O2` by default, matching normal Ferra builds; override it with, for
example, `FERRA_NATIVE_OPT_LEVEL=-O0` when debugging generated IR.
