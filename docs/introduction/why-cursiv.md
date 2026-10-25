# Why cursiv

Writing the grammar in C++ has practical benefits and costs. This page helps
you decide whether that approach fits your project.

## Benefits

**Checked field bindings.** `assign<&ast::Person::name>` names a C++ member.
If you rename that member without updating the grammar, the build reports it.

**One build system.** CMake builds and runs the generator with your C++
toolchain. Your IDE can navigate the grammar and AST together.

**Control of your data model.** You write the abstract syntax tree (AST)
structs and choose their members.

**Grammar errors are caught early.** The compiler or generator rejects
unsupported constructions and names the affected rule or member. See
[Grammar errors](../reference/refusals.md) for causes and corrections.

## Tradeoffs

**You write C++.** A grammar is a header, with the punctuation C++ requires.
`kw<"person">` is more typing than `'person'`.

**Build time.** Changing the grammar requires rebuilding generated C++ code.
Large grammars take longer to compile.

**C++ diagnostics.** Mistakes in template expressions can produce long
compiler messages. Grammar-specific checks also report affected rules and
members.
