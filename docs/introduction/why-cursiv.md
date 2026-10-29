# Why cursiv

Writing the grammar in C++ has benefits and costs. This page helps you decide
whether it fits your project.

## Benefits

**Checked field bindings.** `assign<&ast::Person::name>` names a C++ member.
If you rename the member and forget the grammar, the build fails.

**One build system.** CMake builds and runs the generator with your C++
toolchain, and your IDE navigates the grammar and the AST together.

**Control of your data model.** You write the abstract syntax tree (AST)
structs and choose their members.

**Grammar errors are caught early.** The compiler or the generator rejects
unsupported constructions and names the rule or member at fault. See
[Grammar errors](../reference/refusals.md).

## Tradeoffs

**You write C++.** A grammar is a header, with the punctuation C++ requires:
`kw<"person">` is more typing than `'person'`.

**Build time.** A grammar change rebuilds generated C++ code. Large grammars
take longer to compile.

**C++ diagnostics.** A mistake in a template expression can produce a long
compiler message. Grammar-specific checks name the rule or member at fault.
