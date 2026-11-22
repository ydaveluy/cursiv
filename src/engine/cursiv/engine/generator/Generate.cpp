#include "cursiv/engine/generator/Generate.hpp"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__wasi__)
#include <atomic>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

#include "cursiv/engine/ast/EmitAst.hpp"
#include "cursiv/engine/generator/GrammarPrinter.hpp"
#include "cursiv/engine/generator/EmitArray.hpp"
#include "cursiv/engine/generator/TextMate.hpp"
#include "cursiv/engine/grammar/Cfg.hpp"
#include "cursiv/engine/grammar/Lower.hpp"
#include "cursiv/engine/grammar/Refused.hpp"
#include "cursiv/engine/grammar/WritesOnce.hpp"
#include "cursiv/engine/lexer/LexAutomaton.hpp"
#include "cursiv/engine/parser/EmitDrive.hpp"
#include "cursiv/engine/parser/Lr1.hpp"
#include "cursiv/engine/parser/Pack.hpp"
#include "cursiv/syntax-tree/AstMetaData.hpp"

namespace cursiv::engine::gen {

namespace {

// `text` as a `string_view` of its own length: a NUL inside it does not end it.
std::string quoted_view(std::string_view text) {
  if (text.find('\0') == text.npos) return cpp_literal(text);
  return "::std::string_view(" + cpp_literal(text) + ", " + std::to_string(text.size()) + ")";
}

// The entries of a comma-separated list, each quoted.
std::string quoted_list(std::string_view list) {
  std::string out;
  while (!list.empty()) {
    const std::size_t comma = list.find(',');
    const std::string_view entry = list.substr(0, comma);
    if (!entry.empty()) {
      if (!out.empty()) out += ", ";
      out += cpp_literal(entry);
    }
    list = comma == std::string_view::npos ? std::string_view{} : list.substr(comma + 1);
  }
  return out;
}

// The rule, writes, folding and documentation inherited by each stamped element.
std::string grammar_facts(const Cfg& cfg, const Packed& packed, std::string_view ns) {
  struct Told {
    std::uint32_t converted = kNone;
    std::uint32_t firstWrite = kNone;
    std::uint32_t step = kNone;
    bool held = false;
    bool owns = false;
    bool dataRule = false;
    std::uint16_t fold = 0;
    std::string_view doc;
  };
  std::vector<Told> told(cfg.entries.size());
  struct Site { std::uint32_t before; bool opens, ownFlag; };
  std::vector<Site> sites(cfg.entries.size());
  for (std::uint32_t e = 0; e < cfg.entries.size(); ++e) {
    const Cfg::Entry& entry = cfg.entries[e];
    Told t = entry.before == kNone ? Told{} : told[entry.before];
    const RowDesc& row = *cfg.rows[entry.action];
    const bool opens = row.kind == WriteKind::create || row.kind == WriteKind::climb ||
                       (row.kind == WriteKind::close && !t.held) || row.kind == WriteKind::nest;
    const bool write = row.member != nullptr && row.kind != WriteKind::nest;
    if (opens || write) {
      sites[e] = {t.step, opens, row.kind == WriteKind::flag && t.owns};
      t.step = e;
    }
    if (opens || row.kind == WriteKind::close) t.owns = t.held = true;
    else if (row.node) { t.owns = false; t.held = true; }
    switch (row.kind) {
      case WriteKind::convert:
        if (t.converted == kNone && !row.name.empty()) t.converted = e;
        t.dataRule = t.dataRule || row.dataRule;
        break;
      case WriteKind::create:
      case WriteKind::climb:
      case WriteKind::close:
      case WriteKind::nestBody:
        break;
      case WriteKind::assign:
      case WriteKind::append:
      case WriteKind::flag:
      case WriteKind::nest:
        if (t.firstWrite == kNone) t.firstWrite = e;
        break;
      case WriteKind::doc:
        t.doc = row.name;
        break;
      case WriteKind::fold:
        t.fold = row.fold;
        break;
    }
    told[e] = t;
  }
  std::vector<std::string> names{""};
  std::unordered_map<std::string_view, std::uint16_t> nameOf{{"", 0}};
  const auto name = [&](std::string_view text) {
    const auto [at, fresh] = nameOf.try_emplace(text, static_cast<std::uint16_t>(names.size()));
    if (fresh) {
      if (names.size() >= 0xFFFF) throw Refused("the grammar names too many members and rules");
      names.emplace_back(text);
    }
    return at->second;
  };
  const auto row_of = [&](std::uint32_t entry) -> const RowDesc& {
    return *cfg.rows[cfg.entries[entry].action];
  };
  std::unordered_map<std::string_view, std::size_t> docOf;
  std::string docs;
  const auto doc = [&](std::string_view text) -> std::string {
    if (text.empty()) return text.data() == nullptr ? "{}" : "\"\"";
    const auto [at, fresh] = docOf.try_emplace(text, docOf.size());
    const auto ref = "kDoc" + std::to_string(at->second);
    if (fresh) docs += "constexpr ::std::string_view " + ref + " = " + quoted_view(text) + ";\n";
    return ref;
  };
  std::string elements;
  std::map<std::vector<std::uint16_t>, std::size_t> offsets;
  std::vector<std::uint16_t> members, row;
  std::vector<std::uint16_t> ownFlags;
  for (const std::uint32_t entry : packed.elements) {
    const Told& t = told[entry];
    row.clear();
    std::size_t fills = 0;
    bool inside = false;
    ownFlags.clear();
    for (std::uint32_t w = t.step; w != kNone; w = sites[w].before) {
      if (sites[w].opens) {
        if (inside) row.push_back(0);
        else fills = row.size();
        inside = true;
        if (row_of(w).kind == WriteKind::nest) {
          row.push_back(detail::kNoIndex);
          row.push_back(cfg.entries[w].action);
        }
        row.insert(row.end(), ownFlags.begin(), ownFlags.end());
        ownFlags.clear();
      } else {
        const auto member = name(row_of(w).member->name);
        (sites[w].ownFlag ? ownFlags : row).push_back(member);
      }
    }
    if (!inside) fills = row.size();
    const auto [at, fresh] = offsets.try_emplace(row, members.size());
    if (fresh) members.insert(members.end(), row.begin(), row.end());
    const std::size_t writes = at->second;
    const RowDesc* const converted = t.converted == kNone ? nullptr : &row_of(t.converted);
    elements += "\n    {" +
                std::to_string(t.firstWrite == kNone ? 0 : name(row_of(t.firstWrite).member->name)) +
                ", " + std::to_string(converted == nullptr ? 0 : name(converted->name)) + ", " +
                std::to_string(writes) + ", " + std::to_string(writes + fills) + ", " +
                std::to_string(writes + row.size()) + ", " + (t.dataRule ? "true" : "false") + ", " +
                (converted != nullptr && converted->terminal ? "true" : "false") + ", " +
                std::to_string(t.fold) + ", " + doc(t.doc) + "},";
  }
  std::string out = "// GENERATED by the parser generator -- do not edit.\n#include \"Table.hpp\"\n\n"
                    "#include <cstdint>\n#include <string_view>\n\nnamespace ";
  out += ns;
  out += " {\nnamespace {\n\nusing ::cursiv::engine::ElementFacts;\n"
         "using ::cursiv::engine::TerminalFacts;\n\n";
  std::string spellings;
  std::string terminals;
  std::vector<GrammarWord> words;
  std::unordered_map<std::string_view, std::size_t> byText[2];
  for (std::size_t t = 0; t < cfg.terminals.size(); ++t) {
    const Terminal& terminal = cfg.terminals[t];
    // A literal is spelled by its text, a terminal of words by each of them.
    std::string spelled = "{}";
    if (terminal.literal() || !terminal.words.empty()) {
      spelled = "kSpelling" + std::to_string(t);
      spellings += "constexpr ::std::string_view " + spelled + "[] = {";
      std::unordered_set<std::string_view> written;
      terminal.each_word([&](const GrammarWord& word) {
        if (terminal.words.size() < 2 || written.insert(word.text).second)
          spellings += quoted_view(word.text) + ", ";
        const auto [at, fresh] = byText[word.folded].try_emplace(word.text, words.size());
        if (fresh) words.push_back(word);
        else words[at->second].merge(word);
      });
      spellings += "};\n";
    }
    // A terminal is named where no one spelling says it: one with a name of
    // its own and no literal, and the end of input.
    terminals += "\n    {" + cpp_literal((terminal.named && !terminal.literal()) || t == kEndOfInput
                                        ? std::string_view(terminal.name)
                                        : std::string_view{}) +
                 ", " + doc(terminal.doc) + ", " + spelled + "},";
  }
  if (!cfg.foldedSets.empty()) {
    std::string sets;
    for (std::size_t i = 0; i < cfg.foldedSets.size(); ++i) {
      const auto label = "kFolded" + std::to_string(i);
      std::string rows;
      for (const auto& [word, guard] : cfg.foldedSets[i])
        rows += "{" + quoted_view(word) + ", " + std::to_string(guard) + "u}, ";
      spellings += "constexpr ::cursiv::engine::FoldedWord " + label + "[] = {" + rows + "};\n";
      sets += label + ", ";
    }
    spellings += "constexpr ::std::span<const ::cursiv::engine::FoldedWord> kFoldedSets[] = {" + sets + "};\n";
  }
  if (!words.empty()) {
    std::ranges::stable_sort(words, {}, [](const GrammarWord& w) { return GrammarWord::key(w.text); });
    spellings += "constexpr ::cursiv::engine::GrammarWord kWords[] = {\n";
    for (const auto& word : words)
      spellings += "    {" + quoted_view(word.text) + ", " + (word.folded ? "true" : "false") +
                   ", " + doc(word.doc) + "},\n";
    spellings += "};\n";
  }
  // The names are whole only now: the elements asked for them.
  out += docs + "constexpr ::std::string_view kNames[] = {";
  for (const std::string& n : names) out += "\n    " + cpp_literal(std::string_view(n)) + ",";
  out += "\n};\nconstexpr ::std::uint16_t kMembers[] = {";
  for (std::size_t at = 0; at < members.size(); ++at)
    out += (at % 16 == 0 ? "\n    " : " ") + std::to_string(members[at]) + ",";
  out += "\n    0,\n};\nconstexpr ElementFacts kElements[] = {" + elements + "\n    {},\n};\n" +
         spellings + "constexpr TerminalFacts kTerminals[] = {" + terminals +
         "\n};\n\n}  // namespace\n\n"
         "constinit const ::cursiv::engine::GrammarFacts kGrammar = {\n    kElements, " +
         std::to_string(packed.elements.size()) + ", kMembers, kNames, kTerminals, " +
         std::to_string(cfg.terminals.size()) + ", " + (words.empty() ? "{}" : "kWords") +
         (cfg.foldedSets.empty() ? "" : ", kFoldedSets, &kTables") + "};\n\n}  // namespace ";
  out += ns;
  out += "\n";
  return out;
}

// The grammar's text and its rules with what each uses: what a language
// answers when asked for its grammar. `wrapped` is the text folded for review,
// as `GRAMMAR.txt` holds it.
std::string grammar_source(const LanguageNames& names, const GrammarDump& dump,
                           std::string_view wrapped) {
  std::string out = "// GENERATED by the parser generator -- do not edit.\n#pragma once\n\n"
                    "#include <cstddef>\n#include <string_view>\n\n"
                    "#include \"cursiv/grammar/GrammarSourceData.hpp\"\n\nnamespace ";
  out += names.ns;
  out += " {\n\n";
  const auto text = [&](std::string_view name, std::string_view value) {
    out += "inline constexpr ::std::string_view ";
    out += name;
    out += "{\n";
    for (std::size_t at = 0; at < value.size();) {
      const std::size_t end = std::min(value.find('\n', at), value.size() - 1) + 1;
      out += "    " + cpp_literal(value.substr(at, end - at)) + "\n";
      at = end;
    }
    out += "    , " + std::to_string(value.size()) + "u};\n\n";
  };
  text("kGrammarText", dump.text);
  text("kGrammarWrappedText", wrapped);
  out += "inline constexpr ::std::size_t kGrammarDependencies[] = {\n";
  for (const GrammarDump::Rule& rule : dump.rules)
    for (const std::size_t target : rule.dependencies) out += "    " + std::to_string(target) + "u,\n";
  out += "    0u,\n};\n"
         "inline constexpr ::cursiv::detail::GrammarSourceRule kGrammarRules[] = {\n";
  std::size_t at = 0;
  for (const GrammarDump::Rule& rule : dump.rules) {
    out += "    {" + cpp_literal(std::string_view(rule.name)) + ", kGrammarText.substr(" +
           std::to_string(rule.begin) +
           "u, " + std::to_string(rule.size) + "u), {kGrammarDependencies + " + std::to_string(at) +
           "u, " + std::to_string(rule.dependencies.size()) + "u}},\n";
    at += rule.dependencies.size();
  }
  out += "};\ninline constexpr ::cursiv::detail::GrammarSourceData kGrammarSource{\n"
         "    kGrammarWrappedText, kGrammarRules};\n\n}  // namespace ";
  out += names.ns;
  out += "\n";
  return out;
}

std::string module_header(const LanguageNames& names) {
  std::string out = "// GENERATED by the parser generator -- do not edit.\n#pragma once\n\n"
                    "#include <memory>\n#include <string_view>\n\n"
                    "#include \"cursiv/grammar/GrammarSource.hpp\"\n"
                    "#include \"cursiv/parser/Parser.hpp\"\n"
                    "#include \"cursiv/services/CoreServices.hpp\"\n"
                    "#include \"cursiv/syntax-tree/AstReflection.hpp\"\n\nnamespace ";
  out += names.ns;
  out += " {\n\ninline constexpr ::std::string_view kLanguageId = " + cpp_literal(names.id) +
         ";\n\n::std::unique_ptr<::cursiv::Parser> createParser();\n"
         "::std::unique_ptr<::cursiv::AstReflection> createAstReflection();\n"
         "::std::unique_ptr<const ::cursiv::GrammarSource> createGrammarSource();\n"
         "void installGeneratedModule(::cursiv::CoreServices& services);\n\n}  // namespace ";
  out += names.ns;
  out += "\n";
  return out;
}

std::string module_source(const LanguageNames& names) {
  std::string out = "// GENERATED by the parser generator -- do not edit.\n#include \"Module.hpp\"\n\n"
                    "#include \"GrammarSource.hpp\"\n#include \"Table.hpp\"\n"
                    "#include \"cursiv/engine/parser/LanguageParser.hpp\"\n"
                    "#include \"cursiv/grammar/DefaultGrammarSource.hpp\"\n"
                    "#include \"cursiv/syntax-tree/DefaultAstReflection.hpp\"\n\nnamespace ";
  out += names.ns;
  out += " {\nnamespace {\n\n"
         "constexpr ::cursiv::engine::Language kLanguage{kTables, kAst, kGrammar};\n\n"
         "}  // namespace\n\n"
         "::std::unique_ptr<::cursiv::Parser> createParser() {\n"
         "  return ::std::make_unique<::cursiv::engine::LanguageParser>(kLanguage);\n}\n\n"
         "::std::unique_ptr<::cursiv::AstReflection> createAstReflection() {\n"
         "  return ::std::make_unique<::cursiv::DefaultAstReflection>(*kAst.meta);\n}\n\n"
         "::std::unique_ptr<const ::cursiv::GrammarSource> createGrammarSource() {\n"
         "  return ::std::make_unique<::cursiv::DefaultGrammarSource>(kGrammarSource);\n}\n\n"
         "void installGeneratedModule(::cursiv::CoreServices& services) {\n"
         "  if (services.languageMetaData.languageId.empty())\n"
         "    services.languageMetaData.languageId = kLanguageId;\n"
         "  services.languageMetaData.fileExtensions = {" +
         quoted_list(names.extensions) + "};\n";
  if (const std::string files = quoted_list(names.fileNames); !files.empty())
    out += "  services.languageMetaData.fileNames = {" + files + "};\n";
  out += "  services.parser = createParser();\n"
         "  services.astReflection = createAstReflection();\n"
         "  services.grammarSource = createGrammarSource();\n}\n\n}  // namespace ";
  out += names.ns;
  out += "\n";
  return out;
}

namespace fs = std::filesystem;

bool holds(const fs::path& path, std::string_view text) {
  std::ifstream in(path, std::ios::binary);
  char buffer[8192];
  while (!text.empty()) {
    const auto n = std::min(text.size(), sizeof buffer);
    if (!in.read(buffer, n) || !text.starts_with(std::string_view(buffer, n))) return false;
    text.remove_prefix(n);
  }
  return in && in.peek() == std::char_traits<char>::eof() && !in.bad();
}

// Stage changed files beside their destinations until every output is ready.
struct Outputs {
  struct Directory {
    fs::path stage;
#if defined(_WIN32)
    HANDLE lock;
    explicit Directory(const fs::path& path) {
      const auto folder = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
      BY_HANDLE_FILE_INFORMATION id{};
      DWORD error = ERROR_SUCCESS;
      if (folder == INVALID_HANDLE_VALUE) error = GetLastError();
      else {
        if (!GetFileInformationByHandle(folder, &id)) error = GetLastError();
        CloseHandle(folder);
      }
      if (!error) {
        const auto name = L"Global\\CursivGen-" + std::to_wstring(id.dwVolumeSerialNumber) + L"-" +
                          std::to_wstring(id.nFileIndexHigh) + L"-" + std::to_wstring(id.nFileIndexLow);
        lock = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
        error = GetLastError();
        if (lock && error != ERROR_ALREADY_EXISTS) return;
        if (lock) CloseHandle(lock);
      }
      throw fs::filesystem_error("cannot lock output directory", path,
                                 {static_cast<int>(error), std::system_category()});
    }
    ~Directory() { CloseHandle(lock); }
#elif !defined(__wasi__)
    int lock;
    explicit Directory(const fs::path& path) : lock(::open(path.c_str(), O_RDONLY | O_CLOEXEC)) {
      if (lock >= 0 && ::flock(lock, LOCK_EX | LOCK_NB) == 0) return;
      const std::error_code error(errno, std::generic_category());
      if (lock >= 0) ::close(lock);
      throw fs::filesystem_error("cannot lock output directory", path, error);
    }
    ~Directory() { ::close(lock); }
#else
    explicit Directory(const fs::path&) {}
#endif
    Directory(const Directory&) = delete;
  };
  struct File { fs::path target, staged, previous; std::optional<std::string> original; };
  std::map<fs::path, Directory> directories;
  std::vector<File> files;
#ifdef __wasi__
  // The build uses native generators. A WASI guest can coordinate only its own writers.
  static inline std::atomic_flag writing;
  Outputs() {
    if (writing.test_and_set()) throw std::runtime_error("another generator is writing files");
  }
#endif

  ~Outputs() {
    std::error_code error;
    for (const auto& [_, dir] : directories)
      if (!dir.stage.empty()) fs::remove_all(dir.stage, error);
#ifdef __wasi__
    writing.clear();
#endif
  }

  void write(const fs::path& path, const std::string& text, std::optional<std::string> original = {}) {
    auto target = fs::weakly_canonical(path);
    while (fs::is_symlink(fs::symlink_status(target)))
      target = fs::weakly_canonical(target.parent_path() / fs::read_symlink(target));
    const auto parent = target.parent_path();
    auto& dir = directories.try_emplace(parent, parent).first->second.stage;
    const auto status = fs::status(target);
    if (fs::exists(status)) {
      if (!fs::is_regular_file(status)) throw std::runtime_error("expected a file at " + path.string());
      if (holds(target, text)) return;
    }
    if (dir.empty()) {
      for (unsigned i = 0;; ++i) {
        const auto name = parent / (".cursiv-gen-" + std::to_string(i));
        std::error_code error;
        if (fs::create_directory(name, error)) {
          dir = name;
          break;
        }
        if (error && error != std::errc::file_exists)
          throw fs::filesystem_error("cannot stage generated files", name, error);
      }
    }
    const auto staged = dir / std::to_string(files.size());
    std::ofstream out(staged, std::ios::binary);
    out << text;
    out.close();
    if (!out) throw std::runtime_error("cannot write " + path.string());
    fs::path previous;
    if (fs::exists(status)) {
      fs::permissions(staged, status.permissions());
      previous = staged.string() + ".old";
      std::error_code error;
      fs::create_hard_link(target, previous, error);
      if (error) fs::copy_file(target, previous);
    }
    files.push_back({target, staged, previous, std::move(original)});
  }

  void publish() {
    std::size_t done = 0;
    try {
      for (const auto& f : files) {
        if (f.original && !holds(f.target, *f.original))
          throw std::runtime_error(f.target.string() + " changed during generation");
        fs::rename(f.staged, f.target);
        ++done;
      }
    } catch (...) {
      while (done != 0) {
        const auto& f = files[--done];
        std::error_code error;
        if (f.previous.empty()) fs::remove(f.target, error);
        else fs::rename(f.previous, f.target, error);
        if (error) {
          directories.at(f.target.parent_path()).stage.clear();
          std::fprintf(stderr, "cannot restore %s from %s: %s\n", f.target.string().c_str(),
                       f.previous.string().c_str(), error.message().c_str());
        }
      }
      throw;
    }
  }
};

// Update the keywords of an existing editor grammar.
void write_keywords(Outputs& files, const fs::path& path, const std::string& match) {
  if (!fs::exists(path)) return;
  std::ifstream in(path, std::ios::binary);
  std::string json{std::istreambuf_iterator<char>(in), {}};
  if (in.is_open() && !in.bad())
    if (auto edited = with_keyword_match(json, match)) {
      if (*edited != json) files.write(path, *edited, std::move(json));
      return;
    }
  throw std::runtime_error(path.string() + " must be JSON with a string at repository.keywords.match");
}

// Children precede their expressions in the image. Rule calls use LR nullability,
// including recursive calls and the virtual tokens made by indentation.
void warn_nullable_flags(const std::string& who, GrammarImage& image, const Cfg& cfg,
                         const Packed& packed) {
  if (cfg.flags.empty()) return;
  std::vector<bool> rules(image.rules.size()), empty(image.nodes.size());
  for (const auto& site : cfg.rules)
    if (packed.nullable[site.nt]) rules[site.rule] = true;
  for (ImageId id = 0; id < image.nodes.size(); ++id) {
    const ImageNode& n = image.nodes[id];
    const auto parts = std::span(image.children).subspan(n.first, n.count);
    const auto nullable = [&](ImageId child) { return bool(empty[child]); };
    switch (n.kind) {
      case ImageKind::ref: {
        const auto target = image.entry_rule(n.a);
        empty[id] = target < rules.size() && rules[target];
        break;
      }
      case ImageKind::repeat: empty[id] = n.m == 0 || empty[parts.front()]; break;
      case ImageKind::choice: empty[id] = std::ranges::any_of(parts, nullable); break;
      case ImageKind::seq:
      case ImageKind::unordered:
      case ImageKind::commit: empty[id] = std::ranges::all_of(parts, nullable); break;
      case ImageKind::create:
      case ImageKind::nest:
      case ImageKind::assign:
      case ImageKind::append:
      case ImageKind::flag:
      case ImageKind::skip:
      case ImageKind::doc:
      case ImageKind::trigger:
      case ImageKind::embed: empty[id] = empty[parts.front()]; break;
      case ImageKind::epsilon:
      case ImageKind::eof:
      case ImageKind::gapHas:
      case ImageKind::gapIs:
      case ImageKind::andOf:
      case ImageKind::notOf:
      case ImageKind::implied: empty[id] = true; break;
      default: break;
    }
  }
  GrammarPrinter printer(image);
  for (const auto& [flag, rule] : cfg.flags) {
    const ImageNode& n = image.nodes[flag];
    const ImageId child = image.children[n.first];
    if (!empty[child]) continue;
    const MemberDesc& member = *static_cast<const RowDesc*>(n.row)->member;
    const std::string property = std::string(member.owner->name) + "::" + std::string(member.name);
    std::fprintf(stderr, "%s: warning: %.*s sets %s even when its flag operand reads no token (%s)",
                 who.c_str(), int(image.rules[rule]->qualified.size()),
                 image.rules[rule]->qualified.data(), property.c_str(), printer.spell(child).c_str());
    const ImageNode& operand = image.nodes[child];
    if (operand.kind == ImageKind::repeat && operand.m == 0 && operand.n == 1)
      std::fprintf(stderr, "; for a presence flag write option(flag<&%s>(e))", property.c_str());
    std::fputc('\n', stderr);
  }
}

// Each cell of several actions, the text decides, named after the shortest
// run of symbols that leads the parse into its state.
void report_conflicts(const std::string& who, const GrammarImage& image, const GrammarDump& dump,
                      const Cfg& cfg, const Packed& packed) {
  std::vector<std::string_view> names(cfg.nonterminals);
  for (const auto& site : cfg.rules) names[site.nt] = image.rules[site.rule]->qualified;
  std::vector<bool> involved(image.rules.size());
  const auto origin = [&](const Production& p) {
    std::string where;
    if (p.rule != kNone) {
      involved[p.rule] = true;
      where = image.rules[p.rule]->qualified;
      if (p.alternative) where += " alternative " + std::to_string(p.alternative);
    }
    return where;
  };
  const auto properties = [&](std::uint32_t entry) {
    std::string text;
    for (; entry != kNone; entry = cfg.entries[entry].before) {
      const MemberDesc* member = cfg.rows[cfg.entries[entry].action]->member;
      if (member) text += " " + std::string(member->owner->name) + "::" + std::string(member->name);
    }
    return text;
  };
  const auto symbol = [&](Symbol s) {
    if (is_terminal(s)) return cfg.terminals[s].name;
    const auto name = names[index_of(s)];
    return name.empty() ? "N" + std::to_string(index_of(s)) : std::string(name);
  };
  // The state each state is first reached from, and by what.
  const std::size_t n = packed.states.size();
  std::vector<std::uint32_t> back(n, kNone);
  std::vector<Symbol> via(n, 0);
  std::vector<std::vector<std::pair<Symbol, std::uint32_t>>> edges(n);
  for (std::uint32_t s = 0; s < n; ++s)
    for (const PackedAction& action : packed.states[s].actions)
      if (action.kind == PackedAction::Kind::shift) edges[s].emplace_back(action.terminal, action.value);
  for (std::uint32_t nt = 0; nt < packed.gotos.size(); ++nt)
    for (const PackedGoto& site : packed.gotos[nt])
      edges[site.from].emplace_back(nt | kNonterminal, site.target);
  std::vector<std::uint32_t> queue{0};
  back[0] = 0;
  for (std::uint32_t c = 0; c < packed.conditions.size(); ++c) {
    const auto state = packed.conditions[c];
    if (back[state] != kNone) continue;
    back[state] = state;
    via[state] = c;
    queue.push_back(state);
  }
  for (std::size_t i = 0; i < queue.size(); ++i)
    for (const auto& [by, to] : edges[queue[i]])
      if (back[to] == kNone) {
        back[to] = queue[i];
        via[to] = by;
        queue.push_back(to);
      }
  for (std::uint32_t s = 0; s < n; ++s) {
    const std::vector<PackedAction>& actions = packed.states[s].actions;
    for (std::size_t i = 0, j = 0; i < actions.size(); i = j) {
      for (j = i + 1; j < actions.size() && actions[j].terminal == actions[i].terminal;) ++j;
      // A reread, which stands last, is no action of the grammar's.
      const std::size_t real = j - i - (actions[j - 1].kind == PackedAction::Kind::reread ? 1 : 0);
      if (real < 2) continue;
      std::printf("%s: local LR conflict in state %u on %s --\n", who.c_str(), s,
                  cfg.terminals[actions[i].terminal].name.c_str());
      std::vector<Symbol> path;
      std::uint32_t at = s;
      while (back[at] != kNone && at != back[at]) {
        path.push_back(via[at]);
        at = back[at];
      }
      if (at != 0 && back[at] == at) {
        const auto c = via[at];
        const auto where = origin(cfg.productions[cfg.conditions[c]]);
        std::printf("      condition %u%s%s\n", c + 1, where.empty() ? "" : " in ", where.c_str());
      }
      std::string after;
      for (auto it = path.rbegin(); it != path.rend(); ++it) after += (after.empty() ? "" : " ") + symbol(*it);
      if (!after.empty()) std::printf("      after   %s\n", after.c_str());
      for (std::size_t k = i; k < i + real; ++k)
        if (actions[k].kind == PackedAction::Kind::shift) {
          std::string source;
          path.clear();
          for (auto unit = actions[k].hops; unit; unit = packed.hops[unit].outer)
            path.push_back(packed.hops[unit].production);
          for (auto it = path.rbegin(); it != path.rend(); ++it) {
            const auto by = origin(cfg.productions[*it]);
            if (!by.empty()) source += " [" + by + "]";
          }
          std::printf("      shift   -> state %u%s%s\n", actions[k].value,
                      source.c_str(), properties(actions[k].entry).c_str());
        } else {
          const Production& p = cfg.productions[actions[k].value];
          std::string text = symbol(p.lhs | kNonterminal) + " ->";
          for (const Symbol x : p.rhs) text += " " + symbol(x);
          const auto by = origin(p);
          if (!by.empty()) text += " [" + by + "]";
          text += properties(p.entry);
          std::printf("      %s  %s\n", actions[k].kind == PackedAction::Kind::reduce ? "reduce" : "accept",
                      text.c_str());
        }
    }
  }
  for (const auto& rule : dump.rules) {
    const auto caller = [&](std::size_t dep) { return bool(involved[dump.rules[dep].imageRule]); };
    if (involved[rule.imageRule] || std::ranges::any_of(rule.dependencies, caller))
      std::printf("  %.*s\n", int(rule.size), dump.text.data() + rule.begin);
  }
}

}  // namespace

int generate(GrammarImage& image, const LowerDesc& desc, const LanguageNames& names,
             int argc, char** argv) {
  const std::string who(names.name);
  std::string dir;
  std::string textmate;
  std::uint32_t maxStates = 0;
  for (int at = 1; at < argc; ++at) {
    const std::string_view arg = argv[at];
    constexpr std::string_view kTextMate = "--textmate=";
    if (arg.starts_with(kTextMate)) {
      textmate = arg.substr(kTextMate.size());
      continue;
    }
    constexpr std::string_view kMaxStates = "--max-states=";
    if (arg.starts_with(kMaxStates)) {
      const std::string_view value = arg.substr(kMaxStates.size());
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), maxStates);
      if (error != std::errc{} || end != value.data() + value.size()) {
        std::fprintf(stderr, "%s: --max-states takes a uint32 count, zero for no limit\n", who.c_str());
        return 2;
      }
      continue;
    }
    if (arg.starts_with("--") || !dir.empty()) {
      std::fprintf(stderr, "%s: usage: [--max-states=<count>] [--textmate=<file>] <directory>\n", who.c_str());
      return 2;
    }
    dir = arg;
  }
  if (dir.empty()) {
    std::fprintf(stderr, "%s: usage: [--max-states=<count>] [--textmate=<file>] <directory>\n", who.c_str());
    return 2;
  }
  try {
    check_writes_once(image, desc);
    Cfg cfg = lower(image, desc);
    // The lexer's builder and the automaton as first built are gone once the
    // plan is packed.
    const Packed packed = [&] {
      const std::size_t nterms = cfg.terminals.size();
      LexDfa dfa = [&] {
        LexAutomatonBuilder lexer(image);
        for (std::uint16_t t = 1; t < nterms; ++t) lexer.add(t, cfg.terminals[t]);
        // The patterns the gap tests read are read by the lexer's monitors.
        for (Production& p : cfg.productions)
          for (Test& test : p.tests)
            if (test.gapPattern != kNoImage)
              test.gapMonitor = lexer.pattern(test.gapPattern, test.gap == Test::Gap::has);
        for (auto& set : cfg.foldedSets)
          for (auto& word : set)
            if (word.guard < FoldedWord::kWord) word.guard = lexer.fold_guard(word.guard);
        return lexer.build(nterms);
      }();
      Bits always(nterms);
      for (std::uint16_t t = 1; t < nterms; ++t)
        if (cfg.terminals[t].trivia != Trivia::none && cfg.terminals[t].twin == 0) always.set(t);
      split_leads(cfg);
      const LexRivals rivals = lex_rivals(image, cfg, dfa);
      shadow_tests(cfg, rivals.reads);
      bar_tests(cfg);
      const LrAutomaton automaton = build_lr1(cfg, rivals.takes, always, maxStates);
      return pack(image, cfg, std::move(dfa), rivals.takes, automaton);
    }();
    for (const std::string& warning : packed.warnings)
      std::fprintf(stderr, "%s: warning: %s\n", who.c_str(), warning.c_str());
    warn_nullable_flags(who, image, cfg, packed);
    const std::string ns(names.ns);
    const GrammarDump dump = GrammarPrinter(image).render();
    if (std::getenv("CURSIV_CONFLICTS") != nullptr) report_conflicts(who, image, dump, cfg, packed);
    const std::string wrapped = dump_wrapped(dump.text);
    Outputs files;
    const auto write = [&](const char* name, const std::string& text) {
      files.write(fs::path(dir) / name, text);
    };
    emit_drive(cfg, packed, names, write);
    write("Ast.cpp", emit_ast(cfg, packed, ns, names.header, names.grammar));
    write("Grammar.cpp", grammar_facts(cfg, packed, ns));
    write("GrammarSource.hpp", grammar_source(names, dump, wrapped));
    write("GRAMMAR.txt", wrapped);
    write("Module.hpp", module_header(names));
    write("Module.cpp", module_source(names));
    if (!textmate.empty())
      write_keywords(files, textmate, textmate_keyword_match(textmate_keywords(cfg, packed), image.bound));
    files.publish();
    return 0;
  } catch (const Refused& refused) {
    std::fprintf(stderr, "%s: refused: %s\n", who.c_str(), refused.what());
  } catch (const std::exception& failed) {
    std::fprintf(stderr, "%s: failed: %s\n", who.c_str(), failed.what());
  }
  return 1;
}

}  // namespace cursiv::engine::gen
