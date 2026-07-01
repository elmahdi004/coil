# Coil

A small, embeddable, dynamically-typed scripting language with a stack-based
bytecode VM and a tracing garbage collector, written in portable C11.

```coil
fn fib(n) { if (n < 2) { return n; } return fib(n - 1) + fib(n - 2); }

let total = 0;
for (let i = 0; i < 10; i = i + 1) { total = total + fib(i); }
print total;                       # 88

let words = split("the cat the dog the", " ");
let counts = {};                                        # tables, arrays, closures
for (let i = 0; i < len(words); i = i + 1) {
    let w = words[i];
    if (has(counts, w)) { counts[w] = counts[w] + 1; } else { counts[w] = 1; }
}
```

Coil has integers and floats, strings, arrays and hash tables, first-class
functions and closures, a small standard library, and a JSON bridge. Compiled
programs can be serialized to a bytecode module and loaded back; loaded bytecode
is verified before it runs.

## Layout

```
src/                language + VM implementation
main.c              the `coil` command-line driver
fuzz/               libFuzzer harnesses + seed corpus
.clusterfuzzlite/   build.sh + project.yaml
tests/              unit and integration tests
DESIGN.md           architecture and internals
```

## Build

```
make                # build the `coil` binary
make test           # run the unit tests
```

Or directly:

```
cc -std=c11 -O2 -I src main.c src/*.c -lm -o coil
./coil examples/hello.coil
```

## Command line

```
coil <script.coil>          run a source file
coil repl                   start a REPL
coil disasm <script.coil>   print the bytecode listing
coil build <src> <out.cbc>  compile to a bytecode module
coil exec <module.cbc>      verify and run a bytecode module
```

## Testing

The tests build under AddressSanitizer + UndefinedBehaviorSanitizer and cover the
lexer, compiler, VM, closures, the garbage collector (including a collect-on-every-
allocation stress mode), the standard library, JSON, bytecode round-tripping, and
the verifier. The `fuzz/` harnesses run under ClusterFuzzLite.
