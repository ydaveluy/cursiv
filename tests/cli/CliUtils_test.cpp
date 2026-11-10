#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/Check.hpp"
#include "cursiv/cli.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/workspace/DefaultDocumentFactory.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/workspace/LocalFileSystemProvider.hpp"
#include "support/World.hpp"
#include "workspace/generated/Workspace/Module.hpp"

namespace {

struct CountingParser final : cursiv::Parser {
  std::unique_ptr<cursiv::Parser> parser =
      ws::generated::Workspace::create_parser();
  mutable int calls = 0;
  mutable int grammarQueries = 0;
  // Off, a parse keeps its tree and drops its AST, as a failed AST
  // construction does.
  bool keepsAst = true;
  const cursiv::Grammar& getGrammar() const noexcept override {
    ++grammarQueries;
    return parser->getGrammar();
  }
  cursiv::ParseResult parse(
      std::string_view text,
      const cursiv::CancellationToken& token) const override {
    ++calls;
    cursiv::ParseResult result = parser->parse(text, token);
    if (!keepsAst) result.value = nullptr;
    return result;
  }
};

CountingParser& register_workspace(cursiv::SharedCoreServices& shared,
                                   std::string id = "workspace") {
  auto parser = std::make_unique<CountingParser>();
  auto& counter = *parser;
  (void)support::add_language<cursiv::CoreServices>(
      shared, {std::move(id), {".ws"}, {}}, std::move(parser),
      ws::generated::Workspace::create_ast_reflection());
  return counter;
}

// `argv` points into `values`: the arguments are never copied.
struct Arguments {
  std::vector<std::string> values;
  std::vector<char*> argv;
  Arguments(std::initializer_list<std::string> args) : values(args) {
    for (auto& value : values) argv.push_back(value.data());
  }
  Arguments(const Arguments&) = delete;
  Arguments& operator=(const Arguments&) = delete;
  int argc() const { return static_cast<int>(argv.size()); }
};

struct Capture {
  std::ostringstream out, err;
  std::streambuf* oldOut = std::cout.rdbuf(out.rdbuf());
  std::streambuf* oldErr = std::cerr.rdbuf(err.rdbuf());
  ~Capture() {
    std::cout.rdbuf(oldOut);
    std::cerr.rdbuf(oldErr);
  }
};

struct Scratch {
  const std::filesystem::path root =
      std::filesystem::path(CURSIV_TEST_SCRATCH_DIR) / "cursiv cli # % lane";
  Scratch() {
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "sub");
    std::ofstream(root / "types.ws") << "datatype String\n";
    std::ofstream(root / "sub" / "main.ws") << "entity A { x : String }\n";
  }
  ~Scratch() {
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
  }
  // A file of its own, holding exactly `text`.
  [[nodiscard]] std::string file(std::string_view text) const {
    const std::filesystem::path path = root / "text.ws";
    std::ofstream(path, std::ios::binary)
        .write(text.data(), static_cast<std::streamsize>(text.size()));
    return path.string();
  }
};

// What one run of the command line answered and printed.
struct Run {
  int status = 0;
  std::string out;
  std::string err;
};

Run run_on(cursiv::SharedCoreServices& shared, Arguments& args,
           bool keepsAst = true) {
  Capture capture;
  const int status = cursiv::detail::run_cli_on(
      args.argc(), args.argv.data(),
      [keepsAst](auto& world) {
        register_workspace(world).keepsAst = keepsAst;
        return true;
      },
      shared);
  return {status, capture.out.str(), capture.err.str()};
}

Run run(Arguments&& args, bool keepsAst = true) {
  cursiv::SharedCoreServices shared;
  return run_on(shared, args, keepsAst);
}

void help_wins_even_over_invalid_arguments() {
  Arguments helps[] = {Arguments{"cli", "--unknown", "--help"},
                       Arguments{"cli", "--help", "--root"},
                       Arguments{"cli", "a.ws", "b.ws", "-h"},
                       Arguments{"cli", "--root", "--help"}};
  for (Arguments& args : helps) {
    const Run answer = run(std::move(args));
    CHECK(answer.status == cursiv::kClean);
    CHECK(answer.out.starts_with("usage: cli "));
  }
  Arguments misuses[] = {Arguments{"cli"}, Arguments{"cli", "--root"},
                         Arguments{"cli", "a.ws", "b.ws"},
                         Arguments{"cli", "--root", "", "a.ws"},
                         Arguments{"cli", "", "a.ws"}};
  for (Arguments& args : misuses) {
    const Run answer = run(std::move(args));
    CHECK(answer.status == cursiv::kUsage);
    CHECK(answer.out.empty() && answer.err.starts_with("usage: cli "));
  }
  // Every flag at once: the folder is scanned first, so the reference
  // resolves, and the JSON wins over the text dump.
  const Scratch files;
  const Run answer =
      run({"cli", "--dump", "--json", "--root", files.root.string(),
           (files.root / "sub" / "main.ws").string()});
  CHECK(answer.status == cursiv::kClean);
  CHECK(answer.out.starts_with("{\"element\":"));
}

void diagnostics_use_byte_columns_and_all_line_terminators() {
  cursiv::Document document(cursiv::TextDocument::create(
      "file:///a.ws", "workspace", 0, "éx\rfoo\r\nbar\n"));
  document.diagnostics = {
      {.range = {9, 9}, .message = "third", .source = {}},
      {.range = {2, 3}, .message = "first", .source = {}},
      {.range = {4, 4}, .message = "second", .source = {}},
      {.range = {4, 4}, .message = "same range", .source = {}},
      {.range = {13, 13}, .message = "eof", .source = {}}};
  std::ostringstream out;
  cursiv::print_diagnostics(document, "a.ws", out);
  CHECK(out.str() ==
        "a.ws:1:3: error: first\n"
        "a.ws:2:1: error: second\n"
        "a.ws:2:1: error: same range\n"
        "a.ws:3:1: error: third\n"
        "a.ws:4:1: error: eof\n");
  CHECK(document.diagnostics.front().message == "third");
}

// A failed AST construction still leaves the parser's tree for inspection.
void json_keeps_a_cst_without_an_ast() {
  const Scratch files;
  const std::string path = files.file("datatype String");
  const Run expected = run({"cli", "--json", path});
  const Run json = run({"cli", "--json", path}, false);
  const Run text = run({"cli", "--dump", path}, false);
  CHECK(json.out.starts_with("{\"element\":"));
  CHECK(json.out == expected.out);
  CHECK(text.out.find("datatype") != std::string::npos);
}

struct GroupedNumbers final : std::numpunct<char> {
  char do_thousands_sep() const override { return ','; }
  std::string do_grouping() const override { return "\3"; }
};

void json_ignores_stream_number_formatting() {
  const Scratch files;
  const std::string path = files.file("datatype " + std::string(1200, 'a'));
  const Run expected = run({"cli", "--json", path});
  const std::locale locale = std::cout.getloc();
  const auto before = std::cout.flags();
  std::cout.imbue(std::locale(std::locale::classic(), new GroupedNumbers));
  std::cout << std::hex << std::showbase;
  const auto flags = std::cout.flags();
  const Run actual = run({"cli", "--json", path});
  CHECK(std::cout.flags() == flags);
  std::cout.imbue(locale);
  std::cout.flags(before);
  CHECK(actual.out.find("1209") != std::string::npos);
  CHECK(actual.out == expected.out);
}

// A node is named by the AST class it built: the grammar has no name for the
// record of a rule that builds a node. Where a node spans its whole parent,
// the two share a record, and the parent's class is shown.
void the_dump_names_the_ast_classes() {
  const Scratch files;
  for (const auto& [source, expected] :
       {std::pair{"datatype A datatype B",
                  "Workspace [0..21)\n  DataType [0..10)\n"},
        std::pair{"datatype A", "Workspace [0..10)\n   [0..8)"}}) {
    const std::string path = files.file(source);
    CHECK(run({"cli", "--dump", path}).out.starts_with(expected));
    CHECK(run({"cli", "--json", path})
              .out.starts_with("{\"element\":\"\",\"type\":\"Workspace\","));
  }
}

void text_dump_escapes_control_bytes() {
  const Scratch files;
  const Run dump = run(
      {"cli", "--dump",
       files.file(std::string("datatype /* ") + '\0' + '\x1b' + " */ String")});
  CHECK(dump.out.find("\\000\\033") != std::string::npos);
  CHECK(dump.out.find('\0') == std::string::npos);
  CHECK(dump.out.find('\x1b') == std::string::npos);
}

void text_dump_cuts_between_characters() {
  const Scratch files;
  // The comment's sixtieth byte is the first of `é`.
  const std::string comment = "/*" + std::string(57, 'a') + "\xc3\xa9*/";
  const Run dump =
      run({"cli", "--dump", files.file("datatype " + comment + " String")});
  CHECK(dump.out.find("'/*" + std::string(57, 'a') + "'...") !=
        std::string::npos);
  CHECK(dump.out.find('\xc3') == std::string::npos);
}

void json_replaces_invalid_utf8_without_changing_the_document() {
  const Scratch files;
  const std::pair<std::string, std::string> cases[] = {
      {"é€😀", "é€😀"},
      {std::string("\xff"), "\\ufffd"},
      {std::string("\xc0\xaf"), "\\ufffd\\ufffd"},
      {std::string("\xed\xa0\x80"), "\\ufffd\\ufffd\\ufffd"},
      {std::string("\xf4\x90\x80\x80"), "\\ufffd\\ufffd\\ufffd\\ufffd"},
      {std::string("\xe1\x80") + "x", "\\ufffdx"},
      {std::string("\xf0\x9f\x98"), "\\ufffd"},
      {std::string("\x80\xe0\x9f\x80"), "\\ufffd\\ufffd\\ufffd\\ufffd"}};
  for (const auto& [bytes, escaped] : cases) {
    const std::string source = "datatype /*" + bytes + "*/ String";
    cursiv::SharedCoreServices shared;
    Arguments args{"cli", "--json", files.file(source)};
    const Run json = run_on(shared, args);
    CHECK(json.out.find("\"text\":\"/*" + escaped + "*/\"") !=
          std::string::npos);
    const auto documents = shared.documents->all();
    CHECK(documents.size() == 1);
    if (documents.size() != 1) continue;
    CHECK(documents[0]->textDocument().getText() == source);
    CHECK(documents[0]->parseResult.cst().text() == source);
  }
}

void root_documents_are_reused_and_references_resolve() {
  Scratch files;
  cursiv::SharedCoreServices shared;
  cursiv::install_default_shared_core_services(shared);
  shared.fileSystemProvider =
      std::make_unique<cursiv::LocalFileSystemProvider>();
  auto& parser = register_workspace(shared);
  cursiv::initialize_root(files.root.string(), shared);
  CHECK(parser.calls == 2);
  auto& document = cursiv::build_document_from_path(
      (files.root / "sub" / "main.ws").string(), shared);
  CHECK(parser.calls == 2);
  CHECK(shared.documents->all().size() == 2);
  CHECK(document.diagnostics.empty());
  CHECK(document.state.load() == cursiv::DocumentState::Validated);
  auto* ast = document.parseResult.value;
  auto& again = cursiv::build_document_from_path(
      (files.root / "sub" / ".." / "sub" / "main.ws").string(), shared);
  CHECK(&again == &document && again.parseResult.value == ast);
  CHECK(parser.calls == 2);
}

void read_failures_are_usage_errors() {
  Scratch files;
  cursiv::SharedCoreServices shared;
  // Simulates a read refused after a successful stat, without permissions or
  // a race with a real file's removal.
  shared.fileSystemProvider =
      std::make_unique<cursiv::EmptyFileSystemProvider>();
  Arguments args{"cli", (files.root / "types.ws").string()};
  const Run answer = run_on(shared, args);
  CHECK(answer.status == cursiv::kUsage);
  CHECK(answer.out.empty());
  CHECK(!answer.err.empty());
}

void the_factory_selects_the_grammar_used_for_reporting() {
  struct Factory final : cursiv::DefaultDocumentFactory {
    using DefaultDocumentFactory::DefaultDocumentFactory;
    const cursiv::CoreServices& servicesFor(std::string_view uri,
                                            std::string_view) const override {
      return shared.serviceRegistry->getServices(uri, "workspace");
    }
  };
  Scratch files;
  cursiv::SharedCoreServices shared;
  shared.documentFactory = std::make_unique<Factory>(shared);
  CountingParser *selected = nullptr, *byExtension = nullptr;
  Arguments args{"cli", "--json", (files.root / "types.ws").string()};
  Capture capture;
  const int status = cursiv::detail::run_cli_on(
      args.argc(), args.argv.data(),
      [&](auto& world) {
        selected = &register_workspace(world);
        byExtension = &register_workspace(world, "alternate");
        selected->grammarQueries = byExtension->grammarQueries = 0;
        return true;
      },
      shared);
  CHECK(status == cursiv::kClean);
  CHECK(selected != nullptr && selected->grammarQueries > 0);
  CHECK(byExtension != nullptr && byExtension->grammarQueries == 0);
  CHECK(capture.out.str().starts_with("{\"element\":"));
}

void failed_output_is_not_a_successful_command() {
  struct FailedFlush final : std::stringbuf {
    int sync() override { return -1; }
  };
  Scratch files;
  Arguments lines[] = {
      Arguments{"cli", "--help"},
      Arguments{"cli", "--json", (files.root / "types.ws").string()}};
  for (Arguments& args : lines) {
    cursiv::SharedCoreServices shared;
    Capture capture;
    FailedFlush output;
    std::cout.rdbuf(&output);
    const int status = cursiv::detail::run_cli_on(
        args.argc(), args.argv.data(),
        [](auto& world) {
          register_workspace(world);
          return true;
        },
        shared);
    std::cout.rdbuf(capture.out.rdbuf());
    CHECK(status == cursiv::kFatal);
    CHECK(capture.err.str().find("fatal:") != std::string::npos);
  }
}

// A language's own command answers with the verdict's exit codes: its own
// answer, misuse, or a fatal error -- a failed write among them.
void a_command_answers_like_the_verdict() {
  {
    Capture capture;
    CHECK(cursiv::run_command([] {
            std::cout << "generated";
            return cursiv::kDiagnostics;
          }) == cursiv::kDiagnostics);
    CHECK(capture.out.str() == "generated");
    CHECK(capture.err.str().empty());
  }
  {
    Capture capture;
    CHECK(cursiv::run_command([]() -> int {
            throw cursiv::UsageError("x.req is not a regular file");
          }) == cursiv::kUsage);
    CHECK(capture.err.str() == "x.req is not a regular file\n");
  }
  {
    Capture capture;
    CHECK(cursiv::run_command([]() -> int {
            throw std::runtime_error("cannot create the directory");
          }) == cursiv::kFatal);
    CHECK(capture.err.str() == "fatal: cannot create the directory\n");
  }
  {
    // A command may throw what no standard type names.
    Capture capture;
    CHECK(cursiv::run_command([]() -> int { throw 42; }) == cursiv::kFatal);
    CHECK(capture.err.str() == "fatal: an unknown exception\n");
  }
  {
    struct FailedFlush final : std::stringbuf {
      int sync() override { return -1; }
    };
    Capture capture;
    FailedFlush output;
    std::cout.rdbuf(&output);
    const int status = cursiv::run_command([] {
      std::cout << "generated";
      return cursiv::kClean;
    });
    std::cout.rdbuf(capture.out.rdbuf());
    CHECK(status == cursiv::kFatal);
    CHECK(capture.err.str().starts_with("fatal: cannot write CLI output"));
  }
  {
    // A stderr that throws when it cannot write still leaves the code.
    struct Refusing final : std::streambuf {};
    Capture capture;
    Refusing refusing;
    std::cerr.rdbuf(&refusing);
    std::cerr.exceptions(std::ios::badbit);
    const std::function<int()> commands[] = {
        []() -> int { throw cursiv::UsageError("misuse"); },
        []() -> int { throw std::runtime_error("broken"); },
        []() -> int { throw 42; }};
    std::vector<int> statuses;
    for (const auto& command : commands) {
      std::cerr.clear();
      try {
        statuses.push_back(cursiv::run_command(command));
      } catch (...) {
        statuses.push_back(-1);
      }
    }
    std::cerr.exceptions(std::ios::goodbit);
    std::cerr.clear();
    CHECK(statuses ==
          std::vector<int>({cursiv::kUsage, cursiv::kFatal, cursiv::kFatal}));
  }
}

}  // namespace

int main() {
  cursiv::test::count_expectations();
  help_wins_even_over_invalid_arguments();
  diagnostics_use_byte_columns_and_all_line_terminators();
  json_keeps_a_cst_without_an_ast();
  json_ignores_stream_number_formatting();
  the_dump_names_the_ast_classes();
  text_dump_escapes_control_bytes();
  text_dump_cuts_between_characters();
  json_replaces_invalid_utf8_without_changing_the_document();
  root_documents_are_reused_and_references_resolve();
  read_failures_are_usage_errors();
  the_factory_selects_the_grammar_used_for_reporting();
  failed_output_is_not_a_successful_command();
  a_command_answers_like_the_verdict();
  return cursiv::test::report("cli_utils");
}
