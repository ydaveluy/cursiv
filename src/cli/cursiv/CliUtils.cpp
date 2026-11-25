#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
#include <ios>
#include <iostream>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "cursiv/cli.hpp"
#include "cursiv/grammar/Grammar.hpp"
#include "cursiv/parser/Parser.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/ServiceRegistry.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/syntax-tree/AstNode.hpp"
#include "cursiv/syntax-tree/Cst.hpp"
#include "cursiv/syntax-tree/CstNode.hpp"
#include "cursiv/syntax-tree/ParseResult.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/utils/HostDirectory.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/utils/Utf8.hpp"
#include "cursiv/validation/Diagnostic.hpp"
#include "cursiv/workspace/DefaultWorkspaceManager.hpp"
#include "cursiv/workspace/Document.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "cursiv/workspace/FileSystemProvider.hpp"
#include "cursiv/workspace/LocalFileSystemProvider.hpp"
#include "cursiv/workspace/WorkspaceFolder.hpp"
#include "cursiv/workspace/WorkspaceManager.hpp"

namespace cursiv {
namespace {

constexpr std::string_view severity_name(DiagnosticSeverity s) noexcept {
  switch (s) {
    case DiagnosticSeverity::Error:
      return "error";
    case DiagnosticSeverity::Warning:
      return "warning";
    case DiagnosticSeverity::Information:
      return "note";
    case DiagnosticSeverity::Hint:
      return "hint";
  }
  return "error";
}

// Files in language registration order.
std::string served_files(const ServiceRegistry& registry) {
  std::string out;
  for (const CoreServices* language : registry.all()) {
    const auto& meta = language->languageMetaData;
    for (const auto* names : {&meta.fileExtensions, &meta.fileNames})
      for (const auto& name : *names) {
        if (!out.empty()) out += ", ";
        out += name;
      }
  }
  return out;
}

constexpr std::string_view kHex = "0123456789abcdef";

void flush_output(std::ostream& out) {
  out.flush();
  if (!out) throw std::ios_base::failure("cannot write CLI output");
}

struct Invocation {
  std::string_view path, root;
  std::optional<std::string_view> destination;
  bool dump = false, json = false, help = false;
};

std::optional<Invocation> parse_args(std::span<char* const> args,
                                     const CliCommand* command) {
  Invocation invocation;
  const auto end = std::ranges::find(args, std::string_view("--"));
  const auto options = args.first(end - args.begin());
  if (std::ranges::any_of(options, [](std::string_view arg) {
        return arg == "--help" || arg == "-h";
      })) {
    invocation.help = true;
    return invocation;
  }
  for (auto it = args.begin(); it != args.end(); ++it) {
    const std::string_view arg(*it);
    if (it == end) continue;
    if (it < end && arg.starts_with('-')) {
      const bool root = arg == "--root" || arg == "-r";
      if (!command && arg == "--dump")
        invocation.dump = true;
      else if (!command && arg == "--json")
        invocation.json = true;
      else if (root || (command && command->acceptsDestination &&
                        (arg == "-d" || arg == "--destination"))) {
        if (++it == end || std::string_view(*it).empty()) return std::nullopt;
        if (root)
          invocation.root = *it;
        else
          invocation.destination = *it;
      } else
        return std::nullopt;
    } else {
      if (arg.empty() || !invocation.path.empty()) return std::nullopt;
      invocation.path = arg;
    }
  }
  if (invocation.path.empty()) return std::nullopt;
  return invocation;
}

void print_usage(std::string_view program, const ServiceRegistry& registry,
                 std::span<const CliCommand> commands,
                 const CliCommand* selected, std::ostream& out) {
  if (!selected)
    out << "usage: " << program
        << " [--dump] [--json] [--root <dir>] [--help] [--] <file>\n"
        << "  check <file>, print diagnostics on stderr and return 1 for "
           "errors.\n"
        << "  --dump        print the concrete syntax tree on stdout.\n"
        << "  --json        print that tree as JSON, untruncated.\n";
  for (const auto& command : commands)
    if (!selected || selected == &command)
      out << "usage: " << program << ' ' << command.name
          << (command.acceptsDestination ? " [-d|--destination <dir>]" : "")
          << " [--root <dir>] [--help] [--] <file>\n";
  out << "  -r, --root <dir>  load the workspace for cross-file references.\n"
      << "  --help, -h    print this usage.\n"
      << "  this binary serves " << served_files(registry) << '\n';
}

}  // namespace

void initializeRoot(std::string_view root, SharedCoreServices& shared) {
  std::error_code error;
  const std::filesystem::path absolute =
      std::filesystem::absolute(std::filesystem::path(root), error);
  if (error || !std::filesystem::is_directory(absolute, error))
    throw UsageError(std::string(root) + " is not a directory");
  const WorkspaceFolder folder{pathToFileUri(absolute.string()), {}};
  try {
    shared.workspaceManager->initializeWorkspace(std::span(&folder, 1));
  } catch (const FileSystemError& failure) {
    throw UsageError(failure.what());
  }
}

namespace {

Document& build_document(std::string_view path, SharedCoreServices& shared,
                         std::string_view root, bool loadSiblings) {
  if (path.empty()) throw UsageError("no file given");

  std::error_code error;
  const std::filesystem::path absolute =
      std::filesystem::absolute(std::filesystem::path(path), error);
  if (error) throw UsageError(std::string(path) + ": " + error.message());
  if (!std::filesystem::is_regular_file(absolute, error))
    throw UsageError(absolute.string() + " is not a regular file");

  // Must match the encoder the scan and the store use, or the same file
  // could be spelled as two different URIs.
  const std::string uri = cursiv::pathToFileUri(absolute.string());

  // `TextOffset` is 32 bits; a document past this bound would parse into a
  // tree whose offsets have wrapped.
  if (const std::uintmax_t bytes = std::filesystem::file_size(absolute, error);
      !error && bytes > cursiv::kMaxDocument)
    throw UsageError(absolute.string() + " is larger than the " +
                     std::to_string(cursiv::kMaxDocument) +
                     " byte limit a document offset can address");

  if (!shared.serviceRegistry->hasServices(uri))
    throw UsageError(absolute.string() +
                     ": no language claims this file. This binary serves " +
                     served_files(*shared.serviceRegistry));
  if (!root.empty())
    initializeRoot(root, shared);
  else if (loadSiblings)
    initializeRoot(absolute.parent_path().string(), shared);

  std::shared_ptr<Document> document;
  try {
    document = shared.documents->getOrCreateDocument(uri);
  } catch (const FileSystemError& failure) {
    throw UsageError(failure.what());
  } catch (const ServiceRegistryError& failure) {
    throw UsageError(failure.what());
  }
  if (!document) throw UsageError(absolute.string() + " cannot be read");
  Document* one = document.get();
  shared.documentBuilder->build(std::span<Document* const>(&one, 1));
  return *one;
}

}  // namespace

Document& buildDocumentFromPath(std::string_view path,
                                   SharedCoreServices& shared) {
  return build_document(path, shared, {}, false);
}

bool hasErrorDiagnostics(const Document& document) noexcept {
  return std::ranges::any_of(document.diagnostics, [](const Diagnostic& d) {
    return d.severity == DiagnosticSeverity::Error;
  });
}

void printDiagnostics(const Document& document, std::string_view displayPath,
                       std::ostream& out) {
  if (document.diagnostics.empty()) return;
  // Sorted by position, not discovery order, so a human reads top to bottom.
  // A pointer copy is sorted so the shared diagnostics are left alone;
  // `stable_sort` keeps same-range diagnostics in the pipeline's order.
  std::vector<const Diagnostic*> ordered;
  ordered.reserve(document.diagnostics.size());
  for (const Diagnostic& d : document.diagnostics) ordered.push_back(&d);
  const auto before = [](const Diagnostic* a, const Diagnostic* b) {
    return a->range.begin != b->range.begin ? a->range.begin < b->range.begin
                                            : a->range.end < b->range.end;
  };
  if (!std::ranges::is_sorted(ordered, before))
    std::ranges::stable_sort(ordered, before);

  // Diagnostics are already in source order: advance once up to the last
  // reported offset, without allocating a line index or searching it per row.
  const std::string_view text = document.textDocument().getText();
  std::size_t cursor = 0, lineStart = 0, line = 1;
  std::string row;
  for (const Diagnostic* d : ordered) {
    const std::size_t offset =
        std::min<std::size_t>(d->range.begin, text.size());
    while (cursor < offset) {
      const char ch = text[cursor++];
      // CRLF counts once, after LF; a lone CR also starts a new line.
      if (ch == '\n' ||
          (ch == '\r' && (cursor == text.size() || text[cursor] != '\n'))) {
        ++line;
        lineStart = cursor;
      }
    }
    const std::size_t column = offset - lineStart + 1;
    row.clear();
    row += displayPath;
    row += ':';
    row += std::to_string(line);
    row += ':';
    row += std::to_string(column);
    row += ": ";
    row += severity_name(d->severity);
    row += ": ";
    row += d->message;
    if (!d->source.empty() || d->code) {
      row += " [";
      row += d->source;
      if (d->code) {
        if (!d->source.empty()) row += ':';
        if (d->code->isString())
          row += d->code->string();
        else
          row += std::to_string(d->code->integer());
      }
      row += ']';
    }
    row += '\n';
    out.write(row.data(), static_cast<std::streamsize>(row.size()));
  }
}

namespace {

// The AST class built at each record, by record id: the grammar names only
// the records of terminals and data rules. A wrapper can share its content's
// record; the class kept there is the wrapper's, whose parent sits elsewhere.
std::vector<std::string_view> ast_classes(const ParseResult& parsed) {
  std::vector<std::string_view> classes(parsed.cst().size());
  for (const AstNode* const node : parsed.nodes()) {
    if (node == nullptr || node->type == nullptr ||
        node->cstNodeId >= classes.size())
      continue;
    const AstNode* const parent = node->parent;
    if (classes[node->cstNodeId].empty() || parent == nullptr ||
        parent->cstNodeId != node->cstNodeId)
      classes[node->cstNodeId] = node->type->name;
  }
  return classes;
}

// The concrete syntax tree, indented, in source order. A node is named by the
// AST class it built, or else by its terminal or data rule: the tree does not
// know what its element ids mean, so `grammar` says it.
void dump_cst(const Document& document, const Grammar& grammar,
              std::ostream& out) {
  // Not gated on the AST: the tree is whatever the parser built, and a
  // badly damaged document is exactly the one whose tree a reader needs.
  const cursiv::Cst& cst = document.parseResult.cst();
  if (!cst.hasEntry()) {
    out << "(no tree: the entry rule did not complete)\n";
    return;
  }

  // An explicit stack, not recursion: a deeply nested document would
  // otherwise decide how much native stack a CLI needs.
  struct Frame {
    cursiv::NodeId id;
    std::size_t depth;
  };
  const std::vector<std::string_view> classes =
      ast_classes(document.parseResult);
  std::vector<Frame> stack{{cst.entryRoot(), 0}};
  std::string indentation;
  while (!stack.empty()) {
    const Frame frame = stack.back();
    stack.pop_back();

    const auto width = frame.depth * 2;
    if (indentation.size() < width) indentation.resize(width, ' ');
    out.write(indentation.data(), static_cast<std::streamsize>(width));
    const cursiv::CstNode& node = cst[frame.id];
    // The AST class first: a token that builds a node, such as a number
    // literal, is read as that node.
    const std::string_view name = classes[frame.id].empty()
                                      ? grammar.recordRule(node)
                                      : classes[frame.id];
    out << name << " [" << node.begin << ".." << node.end << ')';
    if (node.isHidden()) out << " hidden";
    if (node.isRecovered()) out << " recovered";
    if (cst.childless(frame.id)) {
      // A long leaf is cut at 60 bytes, never inside a UTF-8 sequence.
      const std::string_view text = cst.text(frame.id);
      std::size_t shown = std::min<std::size_t>(text.size(), 60);
      while (shown < text.size() && shown > 57 &&
             (static_cast<unsigned char>(text[shown]) & 0xC0u) == 0x80u)
        --shown;
      out << ' ' << cursiv::quote(text.substr(0, shown))
          << (shown < text.size() ? "..." : "");
    }
    out << '\n';

    // `reverseChildren` yields the last child first, so pushing in that
    // order pops them back in source order.
    for (const cursiv::NodeId child : cst.reverseChildren(frame.id))
      stack.push_back({child, frame.depth + 1});
  }
}

// Escape controls and replace ill-formed UTF-8 by U+FFFD. Valid spans go to
// the stream together, without copying or an output operation per byte.
void write_json_string(std::ostream& out, std::string_view text) {
  out << '"';
  std::size_t plain = 0;
  const auto writePlain = [&](std::size_t end) {
    if (end > plain)
      out.write(text.data() + plain, static_cast<std::streamsize>(end - plain));
  };
  for (std::size_t index = 0; index < text.size();) {
    const char ch = text[index];
    const auto byte = static_cast<unsigned char>(ch);
    if (byte >= 0x80u) {
      const detail::Utf8Step step =
          detail::utf8_step(text.data() + index, text.size() - index);
      if (!step.wellFormed) {
        writePlain(index);
        out << "\\ufffd";
      }
      index += step.bytes;
      if (step.wellFormed) continue;
      plain = index;
      continue;
    }
    if (byte >= 0x20u && ch != '"' && ch != '\\') {
      ++index;
      continue;
    }
    writePlain(index);
    switch (ch) {
      case '"':
        out << R"(\")";
        break;
      case '\\':
        out << R"(\\)";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        out << "\\u00" << kHex[byte >> 4] << kHex[byte & 0x0Fu];
    }
    plain = ++index;
  }
  writePlain(text.size());
  out << '"';
}

// Offsets are uint32_t: ten decimal digits suffice. Stream flags and locale
// must not change JSON numbers into hexadecimal or grouped decimal text.
void write_json_offset(std::ostream& out, TextOffset offset) {
  char digits[10];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), offset);
  (void)error;
  out.write(digits, end - digits);
}

// The same tree as one line of JSON: per node `element`, `begin`, `end`, plus
// `type`, `hidden` and `recovered` when set, and `text` on a leaf. Ill-formed
// UTF-8 is written as U+FFFD.
void dump_cst_json(const Document& document, const Grammar& grammar,
                   std::ostream& out) {
  const cursiv::Cst& cst = document.parseResult.cst();
  if (!cst.hasEntry()) {
    out << "null\n";
    return;
  }

  // Not `JsonValue`, whose serializer recurses: an explicit stack keeps deep
  // documents off the native stack.
  struct Frame {
    cursiv::NodeId id;
    bool close;
    bool comma;
  };
  const std::vector<std::string_view> classes =
      ast_classes(document.parseResult);
  std::vector<Frame> stack{{cst.entryRoot(), false, false}};
  while (!stack.empty()) {
    const Frame frame = stack.back();
    stack.pop_back();
    if (frame.close) {
      out << "]}";
      continue;
    }
    if (frame.comma) out << ',';
    const cursiv::CstNode& node = cst[frame.id];
    out << "{\"element\":";
    write_json_string(out, grammar.recordRule(node));
    if (!classes[frame.id].empty()) {
      out << ",\"type\":";
      write_json_string(out, classes[frame.id]);
    }
    out << ",\"begin\":";
    write_json_offset(out, node.begin);
    out << ",\"end\":";
    write_json_offset(out, node.end);
    if (node.isHidden()) out << ",\"hidden\":true";
    if (node.isRecovered()) out << ",\"recovered\":true";
    if (cst.childless(frame.id)) {
      // Untruncated: the text dump's cut serves a human's terminal, this
      // spelling serves a parser.
      out << ",\"text\":";
      write_json_string(out, cst.text(frame.id));
      out << '}';
      continue;
    }
    out << ",\"children\":[";
    stack.push_back({frame.id, true, false});
    const std::size_t before = stack.size();
    // `reverseChildren` yields the last child first, so pushing in that
    // order pops them back in source order; the last one pushed is first
    // in source order, whose comma is then cleared.
    for (const cursiv::NodeId child : cst.reverseChildren(frame.id))
      stack.push_back({child, false, true});
    if (stack.size() > before) stack.back().comma = false;
  }
  out << '\n';
}

// The tree (`--json` wins over `--dump`) on `out`, and the diagnostics and
// the verdict line on `err`, so a script piping the tree does not see them
// mixed in. Throws `std::ios_base::failure` when a write fails.
int report(const Document& document, std::string_view displayPath, bool dump,
           bool json, std::ostream& out, std::ostream& err) {
  if (json || dump) {
    const auto& grammar = document.language->parser->getGrammar();
    (json ? dump_cst_json : dump_cst)(document, grammar, out);
  }
  printDiagnostics(document, displayPath, err);
  const bool errors = hasErrorDiagnostics(document);
  err << displayPath << (errors ? ": not clean\n" : ": ok\n");
  return errors ? kDiagnostics : kClean;
}

}  // namespace

int runCommand(const std::function<int()>& command) {
  // Says why on stderr and answers `status`, even when stderr throws.
  const auto failed = [](int status, const char* prefix,
                         const char* what) noexcept {
    try {
      std::cerr << prefix << what << '\n';
    } catch (...) {
    }
    return status;
  };
  try {
    detail::enter_host_directory();
    const int status = command();
    flush_output(std::cout);
    flush_output(std::cerr);
    return status;
  } catch (const UsageError& error) {
    return failed(kUsage, "", error.what());
  } catch (const std::exception& error) {
    return failed(kFatal, "fatal: ", error.what());
  } catch (...) {
    return failed(kFatal, "fatal: ", "an unknown exception");
  }
}

namespace detail {

int run_cli_on(
    int argc, char** argv,
    const std::function<void(SharedCoreServices&)>& registerLanguageServices,
    SharedCoreServices& shared, std::span<const CliCommand> commands) {
  if (!shared.fileSystemProvider)
    shared.fileSystemProvider =
        std::make_unique<LocalFileSystemProvider>(kMaxDocument);
  if (!shared.workspaceManager)
    shared.workspaceManager = std::make_unique<DefaultWorkspaceManager>(
        shared, DefaultWorkspaceManager::ReadErrors::Throw);
  installDefaultSharedCoreServices(shared);
  if (registerLanguageServices) registerLanguageServices(shared);

  const std::string_view program = argc > 0 ? argv[0] : "cli";
  std::span<char* const> args =
      argc > 1 ? std::span(argv + 1, argc - 1) : std::span<char* const>{};
  const CliCommand* command = nullptr;
  if (!args.empty()) {
    const auto found = std::ranges::find(
        commands, std::string_view(args.front()), &CliCommand::name);
    if (found != commands.end()) {
      command = &*found;
      args = args.subspan(1);
    }
  }
  const auto invocation = parse_args(args, command);
  if (!invocation || invocation->help) {
    print_usage(program, *shared.serviceRegistry, commands, command,
                invocation ? std::cout : std::cerr);
    return invocation ? kClean : kUsage;
  }
  const auto& document =
      build_document(invocation->path, shared, invocation->root,
                     command && command->loadSiblings);
  if (command)
    return command->run(document, invocation->path, invocation->destination);
  return report(document, invocation->path, invocation->dump, invocation->json,
                std::cout, std::cerr);
}

}  // namespace detail

}  // namespace cursiv
