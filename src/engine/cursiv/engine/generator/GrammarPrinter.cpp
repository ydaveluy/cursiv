#include "cursiv/engine/generator/GrammarPrinter.hpp"

#include <algorithm>
#include <utility>

#include "cursiv/engine/lexer/LexAutomaton.hpp"
#include "cursiv/grammar/expr.hpp"
#include "cursiv/text/ClassRanges.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/text/unicode/UnicodeProperty.hpp"

namespace cursiv::engine::gen {
namespace {

// Where a byte of a printed line is: plain, in a doc's prose, or in a quote, a
// class or an escape.
enum class Place : unsigned char { plain, doc, quoted };

// Past the escape at `i`: `\u{...}` reads to its brace, any other is two bytes.
std::size_t escape_end(std::string_view line, std::size_t i) {
  if (!line.substr(i).starts_with("\\u{")) return i + 2;
  const std::size_t close = line.find('}', i + 3);
  return close == std::string_view::npos ? line.size() : close + 1;
}

void mark_places(std::string_view line, std::vector<Place>& out) {
  out.assign(line.size(), Place::plain);
  for (std::size_t i = 0; i < line.size();) {
    const char c = line[i];
    std::size_t end = i + 1;
    Place at = Place::plain;
    if (const bool props = line.substr(i).starts_with("\\p{");
        props || c == '"' || c == '\'' || c == '[') {
      // A quote or a class reads to its close: a class of properties holds
      // bare scalars, quotes and spaces included.
      const char close = props ? '}' : c == '[' ? ']' : c;
      end = props ? i + 3 : i + 1;
      while (end < line.size() && line[end] != close)
        end = line[end] == '\\' ? escape_end(line, end) : end + 1;
      ++end;
      at = Place::quoted;
    } else if (c == '\\') {
      end = escape_end(line, i);
      at = Place::quoted;
    } else if (line.substr(i).starts_with("/**")) {
      // An apostrophe in a doc's prose is no quote.
      const std::size_t close = line.find("*/", i + 3);
      end = close == std::string_view::npos ? line.size() : close + 2;
      at = Place::doc;
    } else {
      ++i;
      continue;
    }
    end = std::min(end, line.size());
    std::fill(out.begin() + static_cast<std::ptrdiff_t>(i),
              out.begin() + static_cast<std::ptrdiff_t>(end), at);
    i = end;
  }
}

// Fill each top-level alternative to width, keeping quoted words intact.
void wrap_line(std::string& out, std::string_view line, std::size_t width, std::vector<Place>& marks) {
  const auto colon = line.find(": ");
  const bool split = colon != line.npos;
  auto prefix = split ? line.substr(0, colon + 2) : std::string_view{};
  const auto body = line.substr(prefix.size());
  mark_places(body, marks);
  std::size_t begin = 0;
  const auto fill = [&](std::size_t end) {
    if (split) {
      while (begin < end && body[begin] == ' ') ++begin;
      while (end > begin && body[end - 1] == ' ') --end;
    }
    auto start = out.size();
    out += prefix;
    for (auto word = begin, i = begin; i <= end; ++i) {
      if (i < end && (body[i] != ' ' || marks[i] == Place::quoted)) continue;
      if (word != begin) {
        if (out.size() - start + 1 + (i - word) > width) {
          out += '\n';
          start = out.size();
          out += "      ";
        } else {
          out += ' ';
        }
      }
      out += body.substr(word, i - word);
      word = i + 1;
    }
    out += '\n';
  };
  if (split) {
    int depth = 0;
    for (std::size_t i = 0; i < body.size(); ++i) {
      if (marks[i] != Place::plain) continue;
      const char c = body[i];
      if (c == '(' || c == '{') ++depth;
      else if (c == ')' || c == '}') --depth;
      else if (depth == 0 && c == '|' && i && body[i - 1] == ' ' && body.substr(i).starts_with("| ")) {
        fill(i);
        begin = i + 1;
        prefix = "    | ";
      }
    }
  }
  fill(body.size());
}

// Prose on one line: a raw newline would split the rule and break the source's
// line index.
void one_line(std::string& out, std::string_view text) {
  for (const char ch : text) {
    if (ch == '\n')
      out += "\\n";
    else if (ch == '\r')
      out += "\\r";
    else
      out += ch;
  }
}

// The canonical spelling of `\s`.
const std::string& white_space_spelling() {
  static const std::string spelled = [] {
    std::string s;
    detail::ClassRanges::of(R"(\s)").spell([&s](char c) { s += c; });
    return s;
  }();
  return spelled;
}

// Print a scalar class in its ASCII, shorthand or Unicode-property form.
void emit_class(std::string& out, const ImageClassDesc& cls) {
  const std::uint64_t props = cls.props;
  const bool complement = cls.complement;
  const std::string_view scalars = cls.scalars;
  if (props == 0 && complement && scalars.empty()) {
    out += '.';
    return;
  }
  if (props == 0 && !complement && !scalars.empty()) {
    const detail::ClassRanges set = detail::ClassRanges::of(scalars);
    if (set.r.empty() || set.r.back().hi < 0x80) {
      out += '[';
      append_class_ranges(out, set.r, R"(]\-^)");
      out += ']';
      return;
    }
  }
  if (props == 0 && scalars == white_space_spelling()) {
    out += complement ? "\\S" : "\\s";
    return;
  }
  out += "\\p{";
  if (complement) out += '^';
  bool first = true;
  for (unsigned i = 0; i < static_cast<unsigned>(unicode::Property::Count); ++i)
    if ((props >> i) & 1) {
      if (!first) out += '|';
      first = false;
      out += unicode::detail::kNames[i];
    }
  out += scalars;
  out += '}';
}

}  // namespace

void append_class_ranges(std::string& out, std::span<const unicode::Range> ranges,
                         std::string_view escaped) {
  const auto put = [&](char32_t cp) {
    if (cp >= U'\t' && cp <= U'\r') {
      out += {'\\', "tnvfr"[cp - U'\t']};
    } else if (cp < 32 || cp == 127) {
      constexpr char hex[] = "0123456789abcdef";
      out += {'\\', 'x', hex[cp >> 4], hex[cp & 15]};
    } else {
      if (cp < 128 && escaped.find(static_cast<char>(cp)) != escaped.npos) out += '\\';
      appendScalar(out, cp);
    }
  };
  for (const auto [lo, hi] : ranges) {
    put(lo);
    if (lo == hi) continue;
    if (hi > lo + 1) out += '-';
    put(hi);
  }
}

std::string dump_wrapped(std::string_view text, std::size_t width) {
  std::string out;
  std::vector<Place> marks;
  while (!text.empty()) {
    const auto end = text.find('\n');
    const auto line = text.substr(0, end);
    if (line.size() > width) {
      wrap_line(out, line, width, marks);
    } else if (!line.empty()) {
      out += line;
      out += '\n';
    }
    if (end == text.npos) break;
    text.remove_prefix(end + 1);
  }
  return out;
}

std::size_t GrammarPrinter::index_for(std::uint32_t rule) {
  if (rule >= _visits.size()) _visits.resize(_image.rules.size());
  auto& visit = _visits[rule];
  if (visit.entry != kNoImage) return visit.entry;
  const ImageRuleDesc& r = *_image.rules[rule];
  std::string name;
  if (r.anonymous) {
    name = "$" + std::to_string(_synth++);
  } else {
    name = std::string(r.name);
    if (_names.contains(name)) name = std::string(r.qualified);
    if (_names.contains(name)) name = "::" + std::string(r.qualified);
  }
  // A numbered suffix belongs only to the generated name of a repeated rule.
  if (const auto [at, fresh] = _names.try_emplace(name, 1); !fresh)
    name += "#" + std::to_string(++at->second);
  visit.entry = static_cast<std::uint32_t>(_entries.size());
  _entries.push_back({.imageRule = rule, .name = std::move(name), .dependencies = {}});
  return visit.entry;
}

const std::string& GrammarPrinter::reference_name(std::uint32_t rule) {
  const auto target = index_for(rule);
  auto& caller = _visits[rule].caller;
  if (_current != kNoImage && caller != _current) {
    caller = _current;
    _entries[_current].dependencies.push_back(target);
  }
  return _entries[target].name;
}

void GrammarPrinter::name_list(std::string& out, ImageRules rules) {
  for (std::size_t i = 0; i < rules.size(); ++i) {
    if (i != 0) out += ", ";
    out += reference_name(_image.rule(*rules[i]));
  }
}

void GrammarPrinter::trivia(std::string& out, const ImageTrivia& scope) {
  if (!scope.ignored.empty()) {
    out += " skip ";
    name_list(out, scope.ignored);
  }
  if (!scope.hidden.empty()) {
    out += " hidden ";
    name_list(out, scope.hidden);
  }
}

GrammarPrinter::Prec GrammarPrinter::prec(ImageId id) const {
  for (;;) {
    const ImageNode& n = _image.nodes[id];
    switch (n.kind) {
      case ImageKind::seq:
        return Prec::Sequence;
      case ImageKind::choice:
        return n.count == 2 && printed_by_next(n, 0) ? Prec::Item : Prec::Choice;
      case ImageKind::notOf:
      case ImageKind::andOf:
      case ImageKind::create:
      case ImageKind::nest:
      case ImageKind::assign:
      case ImageKind::append:
      case ImageKind::flag:
        return Prec::Prefix;
      case ImageKind::doc:
      case ImageKind::trigger:
      case ImageKind::embed:
        id = _image.children[n.first];
        break;
      default:
        return Prec::Item;
    }
  }
}

detail::Descent<void> GrammarPrinter::item(std::string& out, ImageId id,
                                           Prec here) {
  const bool need = prec(id) < here;
  if (need) out += '(';
  co_await emit(out, id);
  if (need) out += ')';
}

bool GrammarPrinter::printed_by_next(const ImageNode& n,
                                     std::uint32_t arm) const {
  if (arm + 1 >= n.count) return false;
  const ImageNode& next = _image.nodes[_image.children[n.first + arm + 1]];
  return next.kind == ImageKind::implied &&
         _image.children[next.first] == _image.children[n.first + arm];
}

detail::Descent<void> GrammarPrinter::emit_lit(std::string& out,
                                               const ImageNode& n) {
  const bool expression = n.flags & kLitGuardExpr;
  const bool guarded = expression || word_guarded(_image, n);
  out += cursiv::quote(n.text, guarded ? '"' : '\'');
  if (n.flags & kLitFolded) out += '~';
  if (!guarded) co_return;

  auto bound = _image.bound;
  if (expression) {
    const auto id = _image.children[n.first];
    const auto& guard = _image.nodes[id];
    if (guard.kind != ImageKind::notOf || _image.nodes[_image.children[guard.first]].kind != ImageKind::cpset) {
      out += "\\b(";
      co_await emit(out, id);
      out += ')';
      co_return;
    }
    bound = _image.nodes[_image.children[guard.first]].characterClass();
  }
  if (bound != _image.plain) {
    out += "\\b";
    emit_class(out, bound);
  }
}

detail::Descent<void> GrammarPrinter::emit_choice(std::string& out,
                                                  const ImageNode& n) {
  bool first = true;
  for (std::uint32_t arm = 0; arm < n.count; ++arm) {
    if (printed_by_next(n, arm)) continue;
    if (!first) out += " | ";
    first = false;
    co_await item(out, _image.children[n.first + arm], Prec::Sequence);
  }
}

detail::Descent<void> GrammarPrinter::emit_repeat(std::string& out,
                                                  const ImageNode& n) {
  if (const ImageId child = _image.children[n.first];
      _image.nodes[child].kind == ImageKind::repeat) {
    out += '(';
    co_await emit(out, child);
    out += ')';
  } else {
    co_await item(out, child, Prec::Item);
  }
  const auto unbounded = static_cast<std::uint64_t>(detail::kUnbounded);
  if (n.m == 0 && n.n == unbounded)
    out += '*';
  else if (n.m == 1 && n.n == unbounded)
    out += '+';
  else if (n.m == 0 && n.n == 1)
    out += '?';
  else {
    out += '{';
    out += std::to_string(n.m);
    if (n.m != n.n) {
      out += ',';
      if (n.n != unbounded) out += std::to_string(n.n);
    }
    out += '}';
  }
}

detail::Descent<void> GrammarPrinter::emit_write(std::string& out,
                                                 const ImageNode& n,
                                                 std::string_view op) {
  out += n.text;
  if (!n.text2.empty()) {
    out += '[';
    out += n.text2;
    out += ']';
  }
  out += op;
  const ImageId child = _image.children[n.first];
  // A created payload brackets as a whole -- `m=({T} a b)` -- because the
  // create would otherwise read as absorbing past the assignment.
  co_await item(out, child,
                _image.nodes[child].kind == ImageKind::create ? Prec::Item
                                                              : Prec::Prefix);
}

detail::Descent<void> GrammarPrinter::emit(std::string& out, ImageId id) {
  const ImageNode n = _image.nodes[id];
  const auto child = [&](std::uint32_t i) {
    return _image.children[n.first + i];
  };
  switch (n.kind) {
    case ImageKind::epsilon:
    case ImageKind::commit:
      co_return;
    case ImageKind::lit:
      co_await emit_lit(out, n);
      co_return;
    case ImageKind::doc:
      out += "/** ";
      one_line(out, n.text);
      out += " */ ";
      co_await emit(out, child(0));
      co_return;
    case ImageKind::cpset:
      emit_class(out, n.characterClass());
      co_return;
    case ImageKind::byte:
      out += "\\C";
      co_return;
    case ImageKind::eof:
      out += "!.";
      co_return;
    case ImageKind::indent:
      out += "indent";
      co_return;
    case ImageKind::dedent:
      out += "dedent";
      co_return;
    case ImageKind::gapHas:
      if (n.flags & kGapLine) {
        out += "gap::line";
        co_return;
      }
      out += "gap::has(";
      break;
    case ImageKind::gapIs:
      if (_image.nodes[child(0)].kind == ImageKind::epsilon) {
        out += "gap::none";
        co_return;
      }
      out += "gap::is(";
      break;
    case ImageKind::implied:
    case ImageKind::through:
      out += n.kind == ImageKind::implied ? "implied(" : "through(";
      break;
    case ImageKind::mark:
      out += "mark<";
      out += n.text;
      out += ">(";
      break;
    case ImageKind::scan:
    case ImageKind::same:
      out += n.kind == ImageKind::scan ? "scan<" : "same<";
      out += n.text;
      out += '>';
      co_return;
    case ImageKind::seq:
      // A create takes the rest of its sequence, so `{T} a b` needs no
      // brackets; one in mid-sequence is bracketed whole: `x ({T} a b) c`.
      for (std::uint32_t i = 0; i < n.count; ++i) {
        if (i != 0) out += ' ';
        co_await item(
            out, child(i),
            _image.nodes[child(i)].kind == ImageKind::create && i + 1 != n.count
                ? Prec::Item
                : Prec::Sequence);
      }
      co_return;
    case ImageKind::choice:
      co_await emit_choice(out, n);
      co_return;
    case ImageKind::unordered:
      out += "unordered(";
      break;
    case ImageKind::repeat:
      co_await emit_repeat(out, n);
      co_return;
    case ImageKind::notOf:
    case ImageKind::andOf:
      out += n.kind == ImageKind::notOf ? '!' : '&';
      co_await item(out, child(0), Prec::Prefix);
      co_return;
    case ImageKind::trigger:
    case ImageKind::embed:
      co_await emit(out, child(0));
      co_return;
    case ImageKind::ref:
      out += reference_name(n.a);
      co_return;
    case ImageKind::create:
      out += '{';
      out += n.text;
      out += "} ";
      co_await item(out, child(0), Prec::Sequence);
      co_return;
    case ImageKind::nest:
      out += '{';
      out += n.text;
      out += '.';
      out += n.text2;
      out += "=} ";
      co_await item(out, child(0), Prec::Item);
      co_return;
    case ImageKind::assign:
      co_await emit_write(out, n, "=");
      co_return;
    case ImageKind::append:
      co_await emit_write(out, n, "+=");
      co_return;
    case ImageKind::flag:
      co_await emit_write(out, n, "?=");
      co_return;
    case ImageKind::skip:
      if (n.trivia->ignored.empty() && n.trivia->hidden.empty()) {
        out += "tight(";
      } else {
        // `ignore` and `hide` are variable templates: the chevron form is
        // the one that re-reads as the grammar that produced it.
        out += "skip(ignore<";
        name_list(out, n.trivia->ignored);
        out += ">, hide<";
        name_list(out, n.trivia->hidden);
        out += ">, ";
      }
      break;
  }
  for (std::uint32_t i = 0; i < n.count; ++i) {
    if (i) out += ", ";
    co_await emit(out, child(i));
  }
  out += ')';
}

std::string GrammarPrinter::spell(ImageId node) {
  std::string out;
  descend(emit(out, node));
  return out;
}

void GrammarPrinter::grammar_line(std::string& out, std::uint32_t rule) {
  out += "grammar ";
  out += _entries[index_for(rule)].name;
  const auto& section = *_image.rules[rule]->section;
  trivia(out, section.trivia);
  out += ": ";
  out += reference_name(_image.rule(*section.entry));
  out += ';';
}

// An infix rule prints its node and the three members it writes: nothing else
// in the dump says what it builds.
void GrammarPrinter::infix_line(std::string& out, std::uint32_t rule) {
  const ImageStageDesc& stage = _image.stage(rule);
  const bool folded = _image.folds(rule);
  out += "infix ";
  out += _entries[index_for(rule)].name;
  out += " returns ";
  out += stage.returns;
  out += '(';
  out += stage.members[0];
  out += ", ";
  out += stage.members[1];
  out += ", ";
  out += stage.members[2];
  out += ") on ";
  out += reference_name(_image.rule(*stage.primary));
  out += ": ";
  // A single level prints without `levels(...)`, as it is written.
  if (stage.levelCount > 1) out += "levels(";
  for (std::uint32_t i = 0; i < stage.levelCount; ++i) {
    if (i != 0) out += ", ";
    const ImageStageLevelDesc& level = stage.levels[i];
    switch (level.group) {
      case Association::Right:
        out += "right(";
        break;
      case Association::Non:
        out += "nonassoc(";
        break;
      case Association::Prefix:
      case Association::Postfix: {
        // A postfix level names its operand first, as the text reads.
        const bool prefix = level.group == Association::Prefix;
        out += prefix ? "prefix " : "postfix ";
        out += level.returns;
        out += '(';
        out += level.members[prefix ? 0 : 1];
        out += ", ";
        out += level.members[prefix ? 1 : 0];
        out += ")(";
        break;
      }
      default:
        out += "left(";
    }
    for (std::uint32_t op = 0; op < level.count; ++op) {
      if (op != 0) out += " | ";
      const ImageId id = _image.node(*level.ops[op].op, folded);
      descend(item(out, id, Prec::Sequence));
    }
    out += ')';
  }
  if (stage.levelCount > 1) out += ')';
  out += ';';
}

void GrammarPrinter::line(std::string& out, std::uint32_t rule) {
  const ImageRuleDesc& desc = *_image.rules[rule];
  if (desc.section != nullptr) {
    grammar_line(out, rule);
    return;
  }
  const ImageStageDesc& stage = *desc.stage;
  if (stage.kind == ImageRuleKind::infix) {
    infix_line(out, rule);
    return;
  }
  if (stage.kind == ImageRuleKind::terminal)
    out += "terminal ";
  else if (stage.kind == ImageRuleKind::data)
    out += "data ";
  out += _entries[index_for(rule)].name;
  if (!stage.returns.empty()) {
    out += " returns ";
    out += stage.returns;
  }
  out += ": ";
  const ImageId body = _image.node(*stage.body, _image.folds(rule));
  descend(emit(out, body));
  out += ';';
}

GrammarDump GrammarPrinter::dump(std::uint32_t root) {
  std::string text;
  index_for(root == kNoImage ? 0 : root);
  if (root == kNoImage) {
    text = "grammar ";
    text += _image.name;
    trivia(text, _image.trivia);
    text += ";\n";
  }
  for (std::uint32_t i = 0; i < _entries.size(); ++i) {
    if (i != 0) text += '\n';
    _entries[i].begin = text.size();
    if (root == kNoImage && i == 0) text += "entry ";
    _current = i;
    line(text, _entries[i].imageRule);
    _entries[i].size = text.size() - _entries[i].begin;
  }
  return {std::move(text), std::move(_entries)};
}

}  // namespace cursiv::engine::gen
