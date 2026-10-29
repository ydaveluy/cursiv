#include "cursiv/core/grammar/GrammarPrinter.hpp"

#include <algorithm>
#include <utility>

#include "cursiv/core/grammar/expr.hpp"
#include "cursiv/core/grammar/unicode/UnicodeProperty.hpp"
#include "cursiv/core/grammar/word_final_in.hpp"

namespace cursiv {
namespace detail {

std::string decimal(std::size_t v) { return std::to_string(v); }

void quote(std::string& out, std::string_view text, char q) {
  out += q;
  for (const char ch : text) {
    if (ch == q) {
      out += '\\';
      out += ch;
      continue;
    }
    switch (ch) {
      case '\\':
        out += R"(\\)";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += ch;
    }
  }
  out += q;
}

// A character class, rendered as the ranges it holds rather than as the spec
// that produced it: a computed set has no spec, and printing the two forms
// differently would hide the very thing a merging pass does. The rendering
// reads back as that set under `cr<>`'s syntax.
void charset(std::string& out, const CharSet& set) {
  const auto emit = [&out](int c) {
    switch (c) {
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\v':
        out += "\\v";
        break;
      case ']':
        out += "\\]";
        break;
      case '\\':
        out += R"(\\)";
        break;
      case '-':
        out += "\\-";
        break;
      case '^':
        out += "\\^";
        break;
      default:
        if (c >= 32 && c < 127) {
          out += static_cast<char>(c);
        } else {
          constexpr char kHex[] = "0123456789abcdef";
          out += "\\x";
          out += kHex[(c >> 4) & 0xF];
          out += kHex[c & 0xF];
        }
    }
  };
  out += '[';
  for (int i = 0; i < 256;) {
    if (!set.test(static_cast<unsigned char>(i))) {
      ++i;
      continue;
    }
    int j = i;
    while (j + 1 < 256 && set.test(static_cast<unsigned char>(j + 1))) ++j;
    emit(i);
    if (j == i + 1) {
      emit(j);
    } else if (j > i) {
      out += '-';
      emit(j);
    }
    i = j + 1;
  }
  out += ']';
}

}  // namespace detail

namespace {

// Prose, on the one line the dump gives it: a raw terminator would split a
// rule in two and the source's line index would name a rule that is not there.
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

bool same_class(const ImageClass& a, const ImageNode& b) {
  return a.props == b.m && a.complement == ((b.flags & kCpNot) != 0) &&
         a.scalars == b.text;
}

}  // namespace

std::size_t GrammarPrinter::index_for(std::uint32_t rule) {
  for (std::size_t i = 0; i < _entries.size(); ++i)
    if (_entries[i].rule == rule) return i;
  const ImageRule& r = _image.rules[rule];
  if (r.anonymous) {
    _entries.push_back(
        {"$" + detail::decimal(static_cast<std::size_t>(_synth++)), rule, {}});
    return _entries.size() - 1;
  }
  bool taken = false;
  for (const Entry& e : _entries)
    if (e.name == r.name) {
      taken = true;
      break;
    }
  _entries.push_back({std::string(taken ? r.qualified : r.name), rule, {}});
  return _entries.size() - 1;
}

std::string GrammarPrinter::name_for(std::uint32_t rule) {
  return _entries[index_for(rule)].name;
}

void GrammarPrinter::note_edge(std::size_t target) {
  if (_current >= _entries.size()) return;
  auto& edges = _entries[_current].dependencies;
  if (std::ranges::find(edges, target) == edges.end()) edges.push_back(target);
}

std::string GrammarPrinter::reference_name(std::uint32_t rule) {
  const std::size_t target = index_for(rule);
  note_edge(target);
  return _entries[target].name;
}

void GrammarPrinter::name_list(std::string& out, std::uint32_t at,
                               std::uint32_t count) {
  for (std::uint32_t i = 0; i < count; ++i) {
    if (i != 0) out += ", ";
    out += reference_name(_image.ruleLists[at + i]);
  }
}

Prec GrammarPrinter::prec(ImageId id) const {
  const ImageNode& n = _image.nodes[id];
  switch (n.kind) {
    case ImageKind::seq:
      return Prec::Sequence;
    case ImageKind::choice:
      return implied_pair(n) ? Prec::Item : Prec::Choice;
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
      return prec(_image.kids[n.first]);
    default:
      return Prec::Item;
  }
}

void GrammarPrinter::item(std::string& out, ImageId id, Prec here) {
  const bool need = prec(id) < here;
  if (need) out += '(';
  emit(out, id);
  if (need) out += ')';
}

bool GrammarPrinter::implied_pair(const ImageNode& n) const {
  if (n.kind != ImageKind::choice || n.count != 2) return false;
  const ImageNode& second = _image.nodes[_image.kids[n.first + 1]];
  return second.kind == ImageKind::implied &&
         _image.kids[second.first] == _image.kids[n.first];
}

bool GrammarPrinter::printed_by_next(const ImageNode& n,
                                     std::uint32_t arm) const {
  if (arm + 1 >= n.count) return false;
  const ImageNode& next = _image.nodes[_image.kids[n.first + arm + 1]];
  return next.kind == ImageKind::implied &&
         _image.kids[next.first] == _image.kids[n.first + arm];
}

void GrammarPrinter::emit_lit(std::string& out, const ImageNode& n) {
  enum class Guard { none, word, bound, expr };
  Guard guard = Guard::none;
  ImageId expression = kNoImage;
  if (n.flags & kLitGuardExpr) {
    guard = Guard::expr;
    expression = _image.kids[n.first];
  } else if (n.flags & kLitGuardWord) {
    guard = Guard::word;
    if (_view == ImageView::resolved && _image.bound.known)
      guard = detail::word_final_in(
                  n.text, _image.bound.props, _image.bound.complement,
                  _image.bound.scalars, (n.flags & kLitFolded) != 0)
                  ? Guard::bound
                  : Guard::none;
  }
  detail::quote(out, n.text, guard == Guard::none ? '\'' : '"');
  if (n.flags & kLitFolded) out += '~';
  if (guard == Guard::bound) {
    if (!(_image.bound.props == _image.plain.props &&
          _image.bound.complement == _image.plain.complement &&
          _image.bound.scalars == _image.plain.scalars)) {
      out += "\\b";
      ImageNode cls;
      cls.kind = ImageKind::cpset;
      cls.m = _image.bound.props;
      cls.flags = _image.bound.complement ? kCpNot : 0;
      cls.text = _image.bound.scalars;
      emit_class(out, cls);
    }
  } else if (guard == Guard::expr) {
    const ImageNode& g = _image.nodes[expression];
    const ImageNode* cls = nullptr;
    if (g.kind == ImageKind::notOf) {
      const ImageNode& inner = _image.nodes[_image.kids[g.first]];
      if (inner.kind == ImageKind::cpset) cls = &inner;
    }
    if (cls != nullptr) {
      if (!same_class(_image.plain, *cls)) {
        out += "\\b";
        emit_class(out, *cls);
      }
    } else {
      out += "\\b(";
      emit(out, expression);
      out += ')';
    }
  }
}

void GrammarPrinter::emit_class(std::string& out, const ImageNode& n) const {
  const bool complement = (n.flags & kCpNot) != 0;
  if (n.m == 0 && complement && n.text.empty()) {
    out += '.';
  } else if (n.m == 0 && !complement && !n.text.empty() &&
             detail::cp_ascii_of(n.text)) {
    detail::charset(out, detail::parse_charset(n.text));
  } else {
    detail::cp_name_emit(n.m, complement, n.text, [&out](char c) { out += c; });
  }
}

void GrammarPrinter::emit_choice(std::string& out, const ImageNode& n) {
  if (implied_pair(n)) {
    emit(out, _image.kids[n.first + 1]);
    return;
  }
  bool first = true;
  for (std::uint32_t arm = 0; arm < n.count; ++arm) {
    if (printed_by_next(n, arm)) continue;
    if (!first) out += " | ";
    first = false;
    item(out, _image.kids[n.first + arm], Prec::Sequence);
  }
}

void GrammarPrinter::emit_repeat(std::string& out, const ImageNode& n) {
  if (const ImageId kid = _image.kids[n.first];
      _image.nodes[kid].kind == ImageKind::repeat) {
    out += '(';
    emit(out, kid);
    out += ')';
  } else {
    item(out, kid, Prec::Item);
  }
  const auto unbounded = static_cast<std::uint64_t>(kUnbounded);
  if (n.m == 0 && n.n == unbounded)
    out += '*';
  else if (n.m == 1 && n.n == unbounded)
    out += '+';
  else if (n.m == 0 && n.n == 1)
    out += '?';
  else {
    out += '{';
    out += detail::decimal(n.m);
    if (n.m != n.n) {
      out += ',';
      if (n.n != unbounded) out += detail::decimal(n.n);
    }
    out += '}';
  }
}

void GrammarPrinter::emit_write(std::string& out, const ImageNode& n,
                                std::string_view op) {
  out += n.text;
  if (!n.text2.empty()) {
    out += '[';
    out += n.text2;
    out += ']';
  }
  out += op;
  const ImageId kid = _image.kids[n.first];
  // A created payload brackets as a whole -- `m=({T} a b)` -- because the
  // create would otherwise read as absorbing past the assignment.
  item(out, kid,
       _image.nodes[kid].kind == ImageKind::create ? Prec::Item : Prec::Prefix);
}

void GrammarPrinter::emit(std::string& out, ImageId id) {
  const ImageNode n = _image.nodes[id];
  const auto kid = [&](std::uint32_t i) { return _image.kids[n.first + i]; };
  switch (n.kind) {
    case ImageKind::epsilon:
      return;
    case ImageKind::lit:
      emit_lit(out, n);
      return;
    case ImageKind::doc:
      out += "/** ";
      one_line(out, n.text);
      out += " */ ";
      emit(out, kid(0));
      return;
    case ImageKind::cpset:
      emit_class(out, n);
      return;
    case ImageKind::byte:
      out += "\\C";
      return;
    case ImageKind::eof:
      out += "!.";
      return;
    case ImageKind::indent:
      out += "indent";
      return;
    case ImageKind::dedent:
      out += "dedent";
      return;
    case ImageKind::gapHas:
      if (n.flags & kGapLine) {
        out += "gap::line";
        return;
      }
      out += "gap::has(";
      item(out, kid(0), Prec::Choice);
      out += ')';
      return;
    case ImageKind::gapIs:
      if (_image.nodes[kid(0)].kind == ImageKind::epsilon) {
        out += "gap::none";
        return;
      }
      out += "gap::is(";
      item(out, kid(0), Prec::Choice);
      out += ')';
      return;
    case ImageKind::implied:
      out += "implied(";
      item(out, kid(0), Prec::Choice);
      out += ", ";
      item(out, kid(1), Prec::Choice);
      out += ')';
      return;
    case ImageKind::through:
      out += "through(";
      item(out, kid(0), Prec::Choice);
      out += ", ";
      item(out, kid(1), Prec::Choice);
      out += ')';
      return;
    case ImageKind::mark:
      out += "mark<";
      out += n.text;
      out += ">(";
      emit(out, kid(0));
      out += ')';
      return;
    case ImageKind::same:
      out += "same<";
      out += n.text;
      out += '>';
      return;
    case ImageKind::seq:
      // A create absorbs the rest of its sequence, so `{T} a b` needs no
      // brackets -- except when it does not: a bounded mid-sequence create is
      // bracketed whole (`x ({T} a b) c`), not on its payload.
      for (std::uint32_t i = 0; i < n.count; ++i) {
        if (i != 0) out += ' ';
        item(out, kid(i),
             _image.nodes[kid(i)].kind == ImageKind::create && i + 1 != n.count
                 ? Prec::Item
                 : Prec::Sequence);
      }
      return;
    case ImageKind::choice:
      emit_choice(out, n);
      return;
    case ImageKind::unordered:
      out += "unordered(";
      for (std::uint32_t i = 0; i < n.count; ++i) {
        if (i != 0) out += ", ";
        item(out, kid(i), Prec::Choice);
      }
      out += ')';
      return;
    case ImageKind::repeat:
      emit_repeat(out, n);
      return;
    case ImageKind::notOf:
      out += '!';
      item(out, kid(0), Prec::Prefix);
      return;
    case ImageKind::andOf:
      out += '&';
      item(out, kid(0), Prec::Prefix);
      return;
    case ImageKind::trigger:
    case ImageKind::embed:
      emit(out, kid(0));
      return;
    case ImageKind::ref:
      out += reference_name(n.a);
      return;
    case ImageKind::create:
      out += '{';
      out += n.text;
      out += "} ";
      item(out, kid(0), Prec::Sequence);
      return;
    case ImageKind::nest:
      out += '{';
      out += n.text;
      out += '.';
      out += n.text2;
      out += "=} ";
      item(out, kid(0), Prec::Item);
      return;
    case ImageKind::assign:
      emit_write(out, n, "=");
      return;
    case ImageKind::append:
      emit_write(out, n, "+=");
      return;
    case ImageKind::flag:
      emit_write(out, n, "?=");
      return;
    case ImageKind::commit:
    case ImageKind::closed:
    case ImageKind::turns:
    case ImageKind::call:
    case ImageKind::sectionClose:
    case ImageKind::throughNested:
      return;
    case ImageKind::skip:
      if (n.m == 0 && n.n == 0) {
        out += "tight(";
      } else {
        // `ignore` and `hide` are variable templates: the chevron form is
        // the one that re-reads as the grammar that produced it.
        out += "skip(ignore<";
        name_list(out, n.a, static_cast<std::uint32_t>(n.m));
        out += ">, hide<";
        name_list(out, n.a + static_cast<std::uint32_t>(n.m),
                  static_cast<std::uint32_t>(n.n));
        out += ">, ";
      }
      item(out, kid(0), Prec::Choice);
      out += ')';
      return;
  }
}

std::string GrammarPrinter::spell(ImageId node) {
  std::string out;
  emit(out, node);
  return out;
}

void GrammarPrinter::line(std::string& out, std::uint32_t rule) {
  const ImageFace f = _image.ready(rule).face[static_cast<int>(_view)];
  if (f.kind == ImageRuleKind::grammar) {
    out += "grammar ";
    out += name_for(rule);
    if (f.ignoreCount != 0) {
      out += " skip ";
      name_list(out, f.ignoreAt, f.ignoreCount);
    }
    if (f.hiddenCount != 0) {
      out += " hidden ";
      name_list(out, f.hiddenAt, f.hiddenCount);
    }
    out += ": ";
    out += reference_name(f.entry);
    out += ';';
  } else if (f.kind == ImageRuleKind::infix) {
    // The node it builds and the three members it writes are as much the
    // rule as its level table -- nothing else in the dump says what an infix
    // rule produces.
    out += "infix ";
    out += name_for(rule);
    out += " returns ";
    out += f.returns;
    out += '(';
    out += f.members[0];
    out += ", ";
    out += f.members[1];
    out += ", ";
    out += f.members[2];
    out += ") on ";
    out += reference_name(f.primary);
    out += ": ";
    // A table of one is its level alone, as it is spelled.
    if (f.levelCount > 1) out += "levels(";
    for (std::uint32_t i = 0; i < f.levelCount; ++i) {
      if (i != 0) out += ", ";
      const ImageLevel level = _image.levels[f.levelsAt + i];
      switch (level.group) {
        case ImageGroup::right:
          out += "right(";
          break;
        case ImageGroup::non:
          out += "nonassoc(";
          break;
        case ImageGroup::prefix:
        case ImageGroup::postfix:
          // A unary level builds a class of its own: `prefix<U(op, operand)>`.
          out += level.group == ImageGroup::prefix ? "prefix<" : "postfix<";
          out += level.node;
          out += '(';
          out += level.members[0];
          out += ", ";
          out += level.members[1];
          out += ")>(";
          break;
        default:
          out += "left(";
      }
      emit(out, level.op);
      out += ')';
    }
    if (f.levelCount > 1) out += ')';
    out += ';';
  } else {
    if (f.kind == ImageRuleKind::terminal)
      out += "terminal ";
    else if (f.kind == ImageRuleKind::data)
      out += "data ";
    out += name_for(rule);
    if (!f.returns.empty()) {
      out += " returns ";
      out += f.returns;
    }
    out += ": ";
    emit(out, f.body);
    out += ';';
  }
}

std::string GrammarPrinter::rule_line(std::uint32_t rule) {
  std::string out;
  line(out, rule);
  return out;
}

std::string GrammarPrinter::closure(std::uint32_t rule) {
  std::string out;
  index_for(rule);
  line(out, rule);
  for (std::size_t i = 1; i < _entries.size(); ++i) {
    out += '\n';
    line(out, _entries[i].rule);
  }
  return out;
}

GrammarDump GrammarPrinter::render() {
  index_for(_image.entry);
  std::string head = "grammar ";
  head += _image.name;
  if (_image.ignoreCount != 0) {
    head += " skip ";
    name_list(head, _image.ignoreAt, _image.ignoreCount);
  }
  if (_image.hiddenCount != 0) {
    head += " hidden ";
    name_list(head, _image.hiddenAt, _image.hiddenCount);
  }
  head += ";\n";

  GrammarDump result{std::move(head), {}};
  for (std::size_t i = 0; i < _entries.size(); ++i) {
    if (i != 0) result.text += '\n';
    const std::size_t begin = result.text.size();
    if (i == 0) result.text += "entry ";
    _current = i;
    // Printing may append to the entries, so keep no reference across it.
    line(result.text, _entries[i].rule);
    result.rules.emplace_back(_entries[i].name, begin,
                              result.text.size() - begin,
                              std::move(_entries[i].dependencies));
  }
  return result;
}

void GrammarPrinter::prime() {
  render();
  _current = static_cast<std::size_t>(-1);
}

}  // namespace cursiv
