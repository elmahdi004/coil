# Coil — design and internals

Coil is a small, dynamically-typed scripting language with a stack-based bytecode
virtual machine and a tracing garbage collector, implemented in portable C11. It
is meant to be embeddable and easy to read end to end: the whole implementation is
a handful of focused modules under `src/`.

This document describes how the pieces fit together. For a quick tour of the
language surface, see the programs under `tests/` — they double as executable
documentation.

---

## 1. Pipeline overview

Source text becomes a running program through a short pipeline:

```
source ──lex──▶ tokens ──compile──▶ Chunk (bytecode) ──▶ VM
                                          │
                                          ├──serialize──▶ .cbc module ──load──▶ verify ──▶ VM
```

There are two ways to reach the VM:

- **Directly from source.** `vm_interpret` lexes and compiles the text to a
  top-level function and runs it. The compiler only ever emits well-formed
  bytecode, so this path needs no separate validation.
- **From a serialized module.** A compiled program can be written out as a `.cbc`
  bytecode module and loaded again later (or on another machine). Loaded bytecode
  is *untrusted*, so it is bounds-checked while decoding and then **verified**
  before the VM will run it (§6).

## 2. Values and objects

A `Value` is a tagged union — simple to reason about and cheap to copy:

```
Value = { ValueType type; union { bool boolean; int64_t i64; double f64; Obj* obj; } }
types:  NIL, BOOL, INT, FLOAT, OBJ
```

Everything heap-allocated shares an `Obj` header so the collector can walk it:

```
Obj         = { ObjType type; bool marked; Obj* next; }   // intrusive heap list
ObjString   : Obj + { uint32_t length, hash; char* chars; }        // interned
ObjArray    : Obj + { ValueArray items; }                          // dense vector
ObjTable    : Obj + { VEntry* entries; size_t count, live, cap; }  // value-keyed map
ObjFunction : Obj + { int arity, upvalue_count; uint16_t num_slots; Chunk chunk; ObjString* name; }
ObjClosure  : Obj + { ObjFunction* fn; ObjUpvalue** upvalues; int upvalue_count; }
ObjUpvalue  : Obj + { Value* location; Value closed; ObjUpvalue* next; }
ObjNative   : Obj + { NativeFn fn; int arity; }
```

Strings are interned in a dedicated table, so string equality is a pointer
comparison. There are two hash maps: an internal string-keyed `Table` (globals and
the intern pool) and the script-visible value-keyed `ObjTable`.

## 3. Compiler

`compiler.c` is a single-pass Pratt parser that emits bytecode directly into a
`Chunk` — there is no separate AST. It resolves locals to stack slots, captures
free variables as upvalues so closures can outlive their defining frame, and
tracks scope depth to close upvalues and pop locals at block exits. Globals are
looked up by name in a hash table.

Because the compiler is the only producer on the direct path, it is also the
reference for what "well-formed bytecode" means: balanced stack effects, in-range
slot and constant indices, and jumps that land on instruction boundaries.

## 4. The VM

`vm.c` is a straightforward stack machine:

- an **operand stack** (`Value* stack`, `Value* sp`) shared across call frames;
- a **call-frame stack** (`CallFrame frames[FRAMES_MAX]`), each frame holding a
  closure, an instruction pointer, and a `slots` base pointer into the operand
  stack;
- one `switch` over the opcode set, with helper routines for calls, returns and
  upvalue capture/closing.

The opcode set: `CONST NIL TRUE FALSE POP`, arithmetic and comparison
(`ADD SUB MUL DIV MOD NEG NOT`, the bitwise group `BAND BOR BXOR BNOT SHL SHR`,
`EQ NE LT LE GT GE`), control flow (`JMP JMP_IF_FALSE LOOP`), variables
(`GET/SET_LOCAL`, `GET/SET/DEF_GLOBAL`, `GET/SET_UPVALUE`), functions
(`CALL RETURN CLOSURE CLOSE_UPVALUE`), aggregates (`NEW_ARRAY NEW_TABLE
INDEX_GET INDEX_SET LEN`), and `PRINT HALT`.

Integer arithmetic uses defined wrapping semantics (unsigned intermediate math),
so overflow is well-defined rather than undefined behaviour.

## 5. Garbage collector

`gc.c` is a precise mark-sweep collector. Allocation goes through `reallocate`,
which triggers a collection once live bytes cross a threshold (and, under the
`COIL_GC_STRESS` build, on *every* allocation — a stress mode the tests run
under). Roots are the operand stack, the call frames, the globals table, the open
upvalue list, and any functions under construction in the compiler. Interned
strings are weak: an unreferenced interned string is dropped from the pool during
the sweep.

## 6. Bytecode modules and verification

`module.c` serializes a function graph to a compact little-endian format and reads
it back. The reader treats its input as hostile: every length and index is
bounds-checked against the remaining buffer, so a truncated or malformed module is
rejected cleanly rather than read out of bounds.

Decoding only proves the bytes are *structurally* well-formed. Before the VM runs
a loaded module it must also be proven *semantically* safe, because the
interpreter executes verified bytecode without per-instruction bounds checks — it
relies on the verifier's guarantees for speed. `verify.c` establishes them in two
passes over every reachable function:

1. **Linear decode.** Walk the instruction stream, recording instruction
   boundaries and checking static operands: constant, local and upvalue indices
   are in range, jump offsets stay inside the code, and `CLOSURE` capture lists
   have the right shape. The code must tile exactly with no trailing bytes.
2. **Abstract interpretation of the operand stack.** Starting from the entry
   height, propagate the stack height through the control-flow graph, checking
   each opcode's operands against the current height and that the height stays
   within `MAX_FRAME_STACK`. Control-flow edges must agree on the height where
   they meet, and execution must not run off the end of the code.

A module is accepted only if every reachable function passes both passes.

## 7. Toolchain

`main.c` is a small driver over the whole pipeline:

```
coil <script.coil>          run a source file
coil repl                   interactive REPL
coil disasm <script.coil>   print the bytecode listing
coil opt <script.coil>      print the peephole-optimized listing
coil build <src> <out.cbc>  compile a source file to a bytecode module
coil exec <module.cbc>      verify and run a bytecode module
coil check <module.cbc>     verify a module and report the result
```

Supporting modules: `debug.c` disassembles a chunk; `asm.c` is a textual
assembler/disassembler used for opcode-level tests and inspection; `optimize.c`
is a peephole optimizer (constant folding, dead-code elimination, jump threading)
that rewrites a chunk while preserving its behaviour and verifiability.

## 8. Testing

`tests/` holds unit and integration tests built with `-fsanitize=address,undefined`.
They cover the lexer, compiler, VM opcodes, closures/upvalues, the collector
(including the stress mode), the standard library, JSON, module round-tripping,
and the verifier (both accepted and rejected modules, plus a mutation sweep of the
loader/verifier). `fuzz/` contains libFuzzer harnesses for the lexer, the source
pipeline, the bytecode loader/verifier/VM, and the JSON bridge, wired for
continuous fuzzing through ClusterFuzzLite.
