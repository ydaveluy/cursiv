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

#include "cursiv/cli/cli.hpp"
#include "cursiv/core/grammar/Grammar.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/core/utils/UriUtils.hpp"
#include "cursiv/core/utils/Utf8.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/FileSystemProvider.hpp"
#include "cursiv/core/workspace/LocalFileSystemProvider.hpp"
#include "cursiv/core/workspace/WorkspaceFolder.hpp"
#include "cursiv/core/workspace/WorkspaceManager.hpp"

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

// Extensions in registration order, so the usage line reads the way the
// example's `main` names them.
std::string served_extensions(const ServiceRegistry& registry) {
  std::string out;
  for (const CoreServices* language : registry.all()) {
    for (const std::string& extension :
         language->languageMetaData.fileExtensions) {
      if (!out.empty()) out += ", ";
      out += extension;
    }
  }
  return out;
}

constexpr std::string_view kHex = "0123456789abcdef";

void flush_output(std::ostream& out) {
  out.flush();
  if (!out) throw std::ios_base::failure("cannot write CLI output");
}

// A leaf's text on one line: control characters are escaped, and a long
// run is cut rather than wrapped.
void quote_text(std::ostream& out, std::string_view text) {
  constexpr std::size_t kMaxQuoted = 60;
  out << '\'';
  std::size_t written = 0;
  for (const char ch : text) {
    if (written >= kMaxQuoted) {
      out << "...";
      break;
    }
    switch (ch) {
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      case '\'':
        out << "\\'";
        break;
      case '\\':
        out << "\\\\";
        break;
      default: {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte < 0x20u || byte == 0x7fu)
          out << "\\x" << kHex[byte >> 4] << kHex[byte & 0x0fu];
        else
          out << ch;
      }
    }
    ++written;
  }
  out << '\'';
}

}  // namespace

std::optional<Invocation> parse_args(int argc, char** argv) {
  Invocation invocation;
  // Help is independent of whether the rest forms a valid invocation.
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--help" || arg == "-h") {
      invocation.help = true;
      return invocation;
    }
  }
  for (int index = 1; index < argc; ++index) {
    const std::string_view arg(argv[index]);
    if (arg == "--dump") {
      invocation.dump = true;
      continue;
    }
    if (arg == "--json") {
      invocation.json = true;
      continue;
    }
    if (arg == "--root") {
      // The flag consumes the next argument; a `--root` with nothing
      // behind it is not a run.
      if (index + 1 >= argc) return std::nullopt;
      invocation.root = argv[++index];
      if (invocation.root.empty()) return std::nullopt;
      continue;
    }
    // A second positional, or an option nobody knows, is not a run: guessing
    // what was meant is how a CLI silently parses the wrong file.
    if (arg.empty() || !invocation.path.empty() || arg.starts_with("-"))
      return std::nullopt;
    invocation.path = arg;
  }
  if (invocation.path.empty()) return std::nullopt;
  return invocation;
}

void print_usage(std::string_view program, const ServiceRegistry& registry,
                 std::ostream& out) {
  out << "usage: " << program
      << " [--dump] [--json] [--root <dir>] [--help] <file>\n"
      << "  parses <file>, prints its diagnostics on stderr and answers 0 "
         "when it is clean, 1 when it is not.\n"
      << "  --dump        print the concrete syntax tree on stdout.\n"
      << "  --json        print that tree as JSON, untruncated.\n"
      << "  --root <dir>  scan <dir> into the workspace first, so\n"
      << "                cross-file references resolve as the servers'.\n"
      << "  --help        print this usage and answer 0.\n"
      << "  this binary serves " << served_extensions(registry) << "\n";
}

void initialize_root(std::string_view root, SharedCoreServices& shared) {
  std::error_code error;
  const std::filesystem::path absolute =
      std::filesystem::absolute(std::filesystem::path(root), error);
  if (error || !std::filesystem::is_directory(absolute, error))
    throw UsageError(std::string(root) + " is not a directory");
  const std::string uri = cursiv::path_to_file_uri(absolute.generic_string());
  const std::vector<WorkspaceFolder> folders{{std::string(uri), {}}};
  try {
    shared.workspaceManager->initializeWorkspace(folders);
  } catch (const FileSystemError& failure) {
    throw UsageError(failure.what());
  }
}

Document& build_document_from_path(std::string_view path,
                                   SharedCoreServices& shared) {
  if (path.empty()) throw UsageError("no file given");

  std::error_code error;
  const std::filesystem::path absolute =
      std::filesystem::absolute(std::filesystem::path(path), error);
  if (error) throw UsageError(std::string(path) + ": " + error.message());
  if (!std::filesystem::is_regular_file(absolute, error))
    throw UsageError(absolute.string() + " is not a regular file");

  // Must match the encoder the scan and the store use, or the same file
  // could be spelled as two different URIs.
  const std::string uri = cursiv::path_to_file_uri(absolute.generic_string());

  const ServiceRegistry& registry = *shared.serviceRegistry;
  if (registry.findServices(uri) == nullptr)
    throw UsageError(absolute.string() +
                     ": no language here claims this extension -- this binary "
                     "serves " +
                     served_extensions(registry));

  // `TextOffset` is 32 bits; a document past this bound would parse into a
  // tree whose offsets have wrapped.
  const std::uintmax_t bytes = std::filesystem::file_size(absolute, error);
  if (!error && bytes > cursiv::kMaxDocument)
    throw UsageError(absolute.string() + " is larger than the " +
                     std::to_string(cursiv::kMaxDocument) +
                     " byte limit a document offset can address");

  // The store's copy wins if `--root` already brought this file in;
  // re-reading it would make a second document for one file.
  Documents& store = *shared.documents;
  std::shared_ptr<Document> document = store.getDocument(uri);
  if (document == nullptr) {
    try {
      document = shared.documentFactory->fromUri(uri);
    } catch (const FileSystemError& failure) {
      throw UsageError(failure.what());
    }
    if (document == nullptr)
      throw UsageError(absolute.string() + " cannot be read");
    store.addDocument(document);
  }
  Document* one = document.get();
  shared.documentBuilder->build(std::span<Document* const>(&one, 1));
  return *one;
}

bool has_error_diagnostics(const Document& document) noexcept {
  return std::ranges::any_of(document.diagnostics, [](const Diagnostic& d) {
    return d.severity == DiagnosticSeverity::Error;
  });
}

void print_diagnostics(const Document& document, std::string_view displayPath,
                       std::ostream& out) {
  if (document.diagnostics.empty()) return;
  // Sorted by position, not discovery order, so a human reads top to bottom.
  // A pointer copy is sorted so the shared diagnostics are left alone;
  // `stable_sort` keeps same-range diagnostics in the pipeline's order.
  std::vector<const Diagnostic*> ordered;
  ordered.reserve(document.diagnostics.size());
  for (const Diagnostic& d : document.diagnostics) ordered.push_back(&d);
  std::ranges::stable_sort(ordered, [](const Diagnostic* a,
                                       const Diagnostic* b) {
    return a->range.begin != b->range.begin ? a->range.begin < b->range.begin
                                            : a->range.end < b->range.end;
  });

  // Diagnostics are already in source order: advance once up to the last
  // reported offset, without allocating a line index or searching it per row.
  const std::string_view text = document.textDocument().getText();
  std::size_t cursor = 0, lineStart = 0, line = 1;
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
    out << displayPath << ':' << line << ':' << column << ": "
        << severity_name(d->severity) << ": " << d->message;
    // `[source:code]` lets a script filter by rule and shows which
    // language spoke.
    if (!d->source.empty() || d->code.has_value()) {
      out << " [" << d->source;
      if (!d->source.empty() && d->code.has_value()) out << ':';
      if (d->code.has_value()) {
        if (d->code->isString())
          out << d->code->string();
        else
          out << d->code->integer();
      }
      out << ']';
    }
    out << '\n';
  }
}

// The AST class built at each record, by record id: the grammar names only
// the records of terminals and data rules. A wrapper can share its content's
// record; the class kept there is the wrapper's, whose parent sits elsewhere.
static std::vector<std::string_view> ast_classes(const ParseResult& parsed) {
  std::vector<std::string_view> classes(parsed.cst().size());
  for (const AstNode* const node : parsed.nodes) {
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

void dump_cst(const Document& document, const Grammar& grammar,
              std::ostream& out) {
  // Not gated on the AST: the tree is whatever the parser built, and a
  // badly damaged document is exactly the one whose tree a reader needs.
  const cursiv::Cst& cst = document.parseResult.cst();
  if (!cst.has_entry()) {
    out << "(no tree: the entry rule did not complete)\n";
    return;
  }

  // An explicit stack, not recursion: a deeply nested document would
  // otherwise decide how much native stack a CLI needs.
  struct Frame {
    cursiv::NodeId id;
    int depth;
  };
  const std::vector<std::string_view> classes =
      ast_classes(document.parseResult);
  std::vector<Frame> stack{{cst.entry_root(), 0}};
  while (!stack.empty()) {
    const Frame frame = stack.back();
    stack.pop_back();

    for (int i = 0; i < frame.depth; ++i) out << "  ";
    const cursiv::CstNode& node = cst[frame.id];
    // The AST class first: a token that builds a node, such as a number
    // literal, is read as that node.
    const std::string_view name = classes[frame.id].empty()
                                      ? grammar.recordRule(node)
                                      : classes[frame.id];
    out << name << " [" << node.begin << ".." << node.end << ')';
    if (node.hidden != 0) out << " hidden";
    if (node.recovered != 0) out << " recovered";
    if (cst.childless(frame.id)) {
      out << ' ';
      quote_text(out, cst.text(frame.id));
    }
    out << '\n';

    // `reverse_children` yields the last child first, so pushing in that
    // order pops them back in source order.
    for (const cursiv::NodeId child : cst.reverse_children(frame.id))
      stack.push_back({child, frame.depth + 1});
  }
}

// Escape controls and replace ill-formed UTF-8 by U+FFFD. Valid spans go to
// the stream together, without copying or an output operation per byte.
static void write_json_string(std::ostream& out, std::string_view text) {
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
      const std::size_t width = utf8_width(ch);
      std::size_t length = 1;
      if (byte >= 0xc2u && byte <= 0xf4u) {
        while (length < width && index + length < text.size()) {
          const auto next = static_cast<unsigned char>(text[index + length]);
          if ((next & 0xc0u) != 0x80u) break;
          // Exclude overlong encodings, surrogates and values above U+10FFFF.
          if (length == 1 && ((byte == 0xe0u && next < 0xa0u) ||
                              (byte == 0xedu && next >= 0xa0u) ||
                              (byte == 0xf0u && next < 0x90u) ||
                              (byte == 0xf4u && next >= 0x90u)))
            break;
          ++length;
        }
        if (length == width) {
          index += length;
          continue;
        }
      }
      writePlain(index);
      out << "\\ufffd";
      // Consume the valid prefix of the failed sequence, leaving the next
      // character intact (for example E1 80 'x' becomes U+FFFD followed by x).
      index += length;
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
        out << "\\\"";
        break;
      case '\\':
        out << "\\\\";
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
static void write_json_offset(std::ostream& out, TextOffset offset) {
  char digits[10];
  const auto [end, error] =
      std::to_chars(digits, digits + sizeof(digits), offset);
  (void)error;
  out.write(digits, end - digits);
}

void dump_cst_json(const Document& document, const Grammar& grammar,
                   std::ostream& out) {
  const cursiv::Cst& cst = document.parseResult.cst();
  if (!cst.has_entry()) {
    out << "null\n";
    return;
  }

  // Not `JsonValue`: its serializer recurses, which would let the
  // document's nesting decide the native stack depth. A frame either opens
  // a node (queuing its children) or closes the brackets a parent left
  // open.
  struct Frame {
    cursiv::NodeId id;
    bool close;
    bool comma;
  };
  const std::vector<std::string_view> classes =
      ast_classes(document.parseResult);
  std::vector<Frame> stack{{cst.entry_root(), false, false}};
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
    if (node.hidden != 0) out << ",\"hidden\":true";
    if (node.recovered != 0) out << ",\"recovered\":true";
    if (cst.childless(frame.id)) {
      // Untruncated: `quote_text`'s 60-byte cut serves a human's terminal,
      // this spelling serves a parser.
      out << ",\"text\":";
      write_json_string(out, cst.text(frame.id));
      out << '}';
      continue;
    }
    out << ",\"children\":[";
    stack.push_back({frame.id, true, false});
    const std::size_t before = stack.size();
    // `reverse_children` yields the last child first, so pushing in that
    // order pops them back in source order; the last one pushed is first
    // in source order, whose comma is then cleared.
    for (const cursiv::NodeId child : cst.reverse_children(frame.id))
      stack.push_back({child, false, true});
    if (stack.size() > before) stack.back().comma = false;
  }
  out << '\n';
}

int report(const Document& document, const Grammar& grammar,
           std::string_view displayPath, bool dump, bool json,
           std::ostream& out, std::ostream& err) {
  if (json)
    dump_cst_json(document, grammar, out);
  else if (dump)
    dump_cst(document, grammar, out);
  if (json || dump) flush_output(out);
  print_diagnostics(document, displayPath, err);
  const bool errors = has_error_diagnostics(document);
  if (errors)
    err << displayPath << ": not clean\n";
  else
    err << displayPath << ": ok\n";
  flush_output(err);
  return errors ? kDiagnostics : kClean;
}

namespace detail {

int run_cli_on(
    int argc, char** argv,
    const std::function<bool(SharedCoreServices&)>& registerLanguageServices,
    SharedCoreServices& shared) {
  try {
    // A CLI reads the disk, and says so: without this, the shared default
    // would be the empty provider.
    if (shared.fileSystemProvider == nullptr)
      shared.fileSystemProvider = std::make_unique<LocalFileSystemProvider>();
    install_default_shared_core_services(shared);
    // The registry owns the languages; a container it merely pointed at
    // would leave the routing table dangling once that statement ended. A
    // language the registry refuses says why, as a fatal run does.
    if (registerLanguageServices && !registerLanguageServices(shared)) {
      std::cerr << "no language could be registered\n";
      return kFatal;
    }

    const std::string_view program = argc > 0 ? argv[0] : "cli";
    const ServiceRegistry& registry = *shared.serviceRegistry;
    const std::optional<Invocation> invocation = parse_args(argc, argv);
    if (!invocation.has_value()) {
      print_usage(program, registry, std::cerr);
      flush_output(std::cerr);
      return kUsage;
    }
    // Asking for usage is a run that succeeded: a `set -e` script calling
    // `--help` must not die of asking.
    if (invocation->help) {
      print_usage(program, registry, std::cout);
      flush_output(std::cout);
      return kClean;
    }

    // `--root` first, so the document's references see the neighbours an
    // editor's scan would have brought in.
    if (!invocation->root.empty()) initialize_root(invocation->root, shared);
    const Document& document =
        build_document_from_path(invocation->path, shared);
    // The factory may override URI routing; its bound language interprets
    // the records produced by this document's parser.
    return report(document, document.language->parser->getGrammar(),
                  invocation->path, invocation->dump, invocation->json,
                  std::cout, std::cerr);
  } catch (const UsageError& error) {
    std::cerr << error.what() << '\n';
    return kUsage;
  } catch (const std::exception& error) {
    std::cerr << "fatal: " << error.what() << '\n';
    return kFatal;
  }
}

}  // namespace detail

}  // namespace cursiv
