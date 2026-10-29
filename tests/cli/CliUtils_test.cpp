#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/cli/cli.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/workspace/DefaultDocumentFactory.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"
#include "cursiv/test/Check.hpp"
#include "support/World.hpp"
#include "workspace/generated/Workspace/Module.hpp"

namespace {

struct CountingParser final : cursiv::Parser {
  std::unique_ptr<cursiv::Parser> parser =
      ws::generated::Workspace::create_parser();
  mutable int calls = 0;
  mutable int grammarQueries = 0;
  const cursiv::Grammar& getGrammar() const noexcept override {
    ++grammarQueries;
    return parser->getGrammar();
  }
  cursiv::ParseResult parse(
      std::string_view text,
      const cursiv::CancellationToken& token) const override {
    ++calls;
    return parser->parse(text, token);
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

struct Arguments {
  std::vector<std::string> values;
  std::vector<char*> argv;
  Arguments(std::initializer_list<std::string> args) : values(args) {
    for (auto& value : values) argv.push_back(value.data());
  }
  int argc() const { return static_cast<int>(argv.size()); }
  std::optional<cursiv::Invocation> parse() {
    return cursiv::parse_args(argc(), argv.data());
  }
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
};

void help_wins_even_over_invalid_arguments() {
  for (Arguments args : {Arguments{"cli", "--unknown", "--help"},
                         Arguments{"cli", "--help", "--root"},
                         Arguments{"cli", "a.ws", "b.ws", "-h"},
                         Arguments{"cli", "--root", "--help"}}) {
    const auto parsed = args.parse();
    CHECK(parsed.has_value() && parsed->help);
  }
  for (Arguments args :
       {Arguments{"cli"}, Arguments{"cli", "--root"},
        Arguments{"cli", "a.ws", "b.ws"},
        Arguments{"cli", "--root", "", "a.ws"}, Arguments{"cli", "", "a.ws"}})
    CHECK(!args.parse().has_value());
  Arguments normal{"cli", "--dump", "--json", "--root", "dir", "a.ws"};
  const auto parsed = normal.parse();
  CHECK(parsed.has_value() && parsed->path == "a.ws" && parsed->root == "dir" &&
        parsed->dump && parsed->json && !parsed->help);
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

void json_keeps_a_cst_without_an_ast() {
  const auto parser = ws::generated::Workspace::create_parser();
  cursiv::Document document(cursiv::TextDocument::create(
      "file:///a.ws", "workspace", 0, "datatype String"));
  document.parseResult = parser->parse(document.textDocument().getText());
  CHECK(document.parseResult.cst().has_entry());
  std::ostringstream expected;
  cursiv::dump_cst_json(document, parser->getGrammar(), expected);
  // A failed AST construction still leaves the parser's CST for inspection.
  document.parseResult.value = nullptr;
  std::ostringstream actual, text;
  cursiv::dump_cst_json(document, parser->getGrammar(), actual);
  cursiv::dump_cst(document, parser->getGrammar(), text);
  CHECK(actual.str() == expected.str());
  CHECK(text.str().find("datatype") != std::string::npos);
}

struct GroupedNumbers final : std::numpunct<char> {
  char do_thousands_sep() const override { return ','; }
  std::string do_grouping() const override { return "\3"; }
};

void json_ignores_stream_number_formatting() {
  const auto parser = ws::generated::Workspace::create_parser();
  cursiv::Document document(cursiv::TextDocument::create(
      "file:///a.ws", "workspace", 0, "datatype " + std::string(1200, 'a')));
  document.parseResult = parser->parse(document.textDocument().getText());
  std::ostringstream expected, actual;
  cursiv::dump_cst_json(document, parser->getGrammar(), expected);
  actual.imbue(std::locale(std::locale::classic(), new GroupedNumbers));
  actual << std::hex << std::showbase;
  const auto flags = actual.flags();
  cursiv::dump_cst_json(document, parser->getGrammar(), actual);
  CHECK(actual.str() == expected.str());
  CHECK(actual.flags() == flags);
}

void text_dump_escapes_control_bytes() {
  const auto parser = ws::generated::Workspace::create_parser();
  cursiv::Document document(cursiv::TextDocument::create(
      "file:///a.ws", "workspace", 0,
      std::string("datatype /* ") + '\0' + '\x1b' + " */ String"));
  document.parseResult = parser->parse(document.textDocument().getText());
  std::ostringstream out;
  cursiv::dump_cst(document, parser->getGrammar(), out);
  CHECK(out.str().find("\\x00\\x1b") != std::string::npos);
  CHECK(out.str().find('\0') == std::string::npos);
  CHECK(out.str().find('\x1b') == std::string::npos);
}

void json_replaces_invalid_utf8_without_changing_the_document() {
  const auto parser = ws::generated::Workspace::create_parser();
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
    cursiv::Document document(
        cursiv::TextDocument::create("file:///a.ws", "workspace", 0, source));
    document.parseResult = parser->parse(document.textDocument().getText());
    CHECK(document.parseResult.cst().has_entry());
    std::ostringstream out;
    cursiv::dump_cst_json(document, parser->getGrammar(), out);
    CHECK(out.str().find("\"text\":\"/*" + escaped + "*/\"") !=
          std::string::npos);
    CHECK(document.textDocument().getText() == source);
    CHECK(document.parseResult.cst().text() == source);
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
  Capture capture;
  const int status = cursiv::detail::run_cli_on(
      args.argc(), args.argv.data(),
      [](auto& world) {
        register_workspace(world);
        return true;
      },
      shared);
  CHECK(status == cursiv::kUsage);
  CHECK(capture.out.str().empty());
  CHECK(!capture.err.str().empty());
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
  for (Arguments args :
       {Arguments{"cli", "--help"},
        Arguments{"cli", "--json", (files.root / "types.ws").string()}}) {
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

}  // namespace

int main() {
  cursiv::test::count_expectations();
  help_wins_even_over_invalid_arguments();
  diagnostics_use_byte_columns_and_all_line_terminators();
  json_keeps_a_cst_without_an_ast();
  json_ignores_stream_number_formatting();
  text_dump_escapes_control_bytes();
  json_replaces_invalid_utf8_without_changing_the_document();
  root_documents_are_reused_and_references_resolve();
  read_failures_are_usage_errors();
  the_factory_selects_the_grammar_used_for_reporting();
  failed_output_is_not_a_successful_command();
  return cursiv::test::report("cli_utils");
}
