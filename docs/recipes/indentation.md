# Indentation-sensitive languages

In languages such as Python, indentation delimits blocks. Cursiv tracks the
indentation and gives your grammar two tokens: `indent` where a block opens
and `dedent` where it closes.

This recipe builds a small task language:

```text
# Build steps.

task build:
    run compile(main, util)
    task check:
        run lint   # a comment

        run test(unit,
                 integration)

run deploy
```

## The AST

```cpp title="src/tasks/core/ast.hpp"
namespace tasks::ast {

struct Statement : cursiv::Extends<cursiv::AstNode> {};

struct Run : cursiv::Extends<Statement> {
  string_view command;
  vector<string_view> args;
};

struct Task : cursiv::Extends<Statement> {
  string_view name;
  vector<Statement*> body;
};

struct Model : cursiv::Extends<cursiv::AstNode> {
  vector<Statement*> statements;
};

}  // namespace tasks::ast
```

## The tokens

```cpp title="src/tasks/core/TasksGrammar.hpp"
namespace tasks {

struct Tasks : cursiv::GrammarSpec {
  struct WS : terminal<some(cr<R"( \t)">)> {};
  struct COMMENT : terminal<lit<"#"> + many(cr<R"(^\r\n)">)> {};
  struct NEWLINE
      : terminal<eol + many(many(cr<R"( \t)">) + option(ref<COMMENT>) + eol)
                 + option(many(cr<R"( \t)">) + option(ref<COMMENT>) + eof)> {};
  struct INDENT : terminal<indent> {};
  struct DEDENT : terminal<dedent> {};
  struct ID : terminal<cr<"a-zA-Z_"> + many(w)> {};

  using ignored = WS;
  using hidden = COMMENT;
  using indent_newline = NEWLINE;
  static constexpr auto indent_open = lit<"(">;
  static constexpr auto indent_close = lit<")">;

  // The rules follow.
};

}  // namespace tasks
```

- `NEWLINE` ends a statement. It also reads the blank and comment-only lines
  that follow, so they never open or close a block. `WS` and `COMMENT` stop
  before the line ending.
- `indent_newline` names that terminal. After it, the indentation of the
  next line decides whether `INDENT`, `DEDENT` or nothing comes next.
- Between `indent_open` and `indent_close`, line endings and indentation are
  ignored, so an argument list can span lines.

## The rules

```cpp
  struct Statement;
  struct Run : rule<kw<"run"> + assign<&ast::Run::command>(ref<ID>)
                    + option(lit<"(">
                             + many(append<&ast::Run::args>(ref<ID>), lit<",">)
                             + lit<")">)
                    + (ref<NEWLINE> | eof)> {};
  struct Task : rule<kw<"task"> + assign<&ast::Task::name>(ref<ID>) + lit<":">
                     + ref<NEWLINE> + ref<INDENT>
                     + some(append<&ast::Task::body>(ref<Statement>))
                     + ref<DEDENT>> {};
  struct Statement : rule<ref<Run> | ref<Task>> {};
  struct Model : rule<create<ast::Model> + option(ref<NEWLINE>)
                      + many(append<&ast::Model::statements>(ref<Statement>))> {};

  using entry = Model;
```

- A block is `NEWLINE`, `INDENT`, its statements, then `DEDENT`.
- A simple statement ends with `NEWLINE`, or with the end of input when the
  last line has no line ending. The end of input closes every open block.
- `option(ref<NEWLINE>)` accepts blank and comment lines before the first
  statement.

## Consistent indentation

Cursiv compares the leading spaces and tabs of each line as written, without
expanding tabs. A line that adds spaces or tabs after the current indentation
opens a block. A line back at the indentation of an enclosing block closes
every block inside that one. Any other indentation is reported as
`Unexpected indentation.`:

```text
task build:
    run compile
  run test
```

Here `run test` matches no enclosing level. See
[Indentation](../reference/grammar.md#indentation) for the complete rules.
