#include "cursiv/shared/gen/LevelFault.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cursiv/shared/gen/OperatorWords.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/text/scalar_at.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/utils/StrongComponents.hpp"

namespace cursiv::engine {

namespace {

constexpr std::uint16_t kNone = 0xFFFFu;

// One token of an operator: a literal, or a terminal that reads a pattern.
struct Token {
  std::string_view text{};
  std::uint32_t rule = kNoImage;
  bool folded = false;
  bool operator==(const Token& o) const {
    return rule == o.rule && folded == o.folded &&
           (folded ? folded_equal(text, o.text) : text == o.text);
  }
};

// What an operator spells, read off the image. A repetition with no maximum,
// a rule that reaches itself and a part with no listed text are known by their
// first tokens. A rule is read once where none of its cycle is on the path.
class Reader {
 public:
  Reader(GrammarImage& image, std::span<const ImageLevel> levels) : _im(image) {
    const std::vector<std::vector<std::uint16_t>> calls = collect(levels);
    _scc = StrongComponents<>::of(calls);
    _walking.assign(calls.size(), false);
    _onPath.assign(calls.size(), 0);
    _known.resize(calls.size());
  }

  Descent<OperatorWords> words(ImageId id);

  std::string spelled(const std::vector<std::uint16_t>& word) const {
    std::string out;
    for (const std::uint16_t t : word) {
      if (!out.empty()) out += ' ';
      const Token& k = _tokens[t];
      out += k.rule == kNoImage ? k.text : _im.rules[k.rule].name;
    }
    return out;
  }

 private:
  ImageNode at(ImageId id) const { return _im.nodes[id]; }
  std::uint16_t token(const Token& t) {
    const auto it = std::ranges::find(_tokens, t);
    const auto i = static_cast<std::uint16_t>(it - _tokens.begin());
    if (it == _tokens.end()) _tokens.push_back(t);
    return i;
  }
  // A terminal of one literal or a choice of literals reads as those
  // literals; any other terminal is one token.
  bool literals(ImageId body) const {
    const auto lit = [&](ImageId k) { return at(k).kind == ImageKind::lit; };
    return lit(body) || (at(body).kind == ImageKind::choice &&
                         std::ranges::all_of(_im.shape_children(body), lit));
  }
  // The children a walk reads: what consumes text, never what a condition
  // tests.
  std::span<const ImageId> parts(ImageId id) const {
    const ImageNode n = at(id);
    switch (n.kind) {
      case ImageKind::andOf:
      case ImageKind::notOf:
      case ImageKind::through:
      case ImageKind::embed:
      case ImageKind::ref:
        return {};
      case ImageKind::implied:
        return {_im.children.data() + n.first, 1};
      default:
        return _im.shape_children(id);
    }
  }
  std::vector<std::vector<std::uint16_t>> collect(
      std::span<const ImageLevel> levels);

  GrammarImage& _im;
  std::vector<Token> _tokens;
  // The rules the operators read through, by their number here.
  std::unordered_map<std::uint32_t, std::uint16_t> _local;
  std::vector<ImageId> _body;
  StrongComponents<> _scc;
  std::vector<bool> _walking;
  std::vector<std::uint16_t> _onPath;
  std::vector<std::optional<OperatorWords>> _known;
};

// The rules the operators read through, a terminal of literals among them,
// and the rules each calls.
std::vector<std::vector<std::uint16_t>> Reader::collect(
    std::span<const ImageLevel> levels) {
  std::vector<std::vector<std::uint16_t>> calls;
  std::vector<std::pair<ImageId, std::uint16_t>> todo;
  for (const ImageLevel& l : levels) todo.emplace_back(l.op, kNone);
  while (!todo.empty()) {
    const auto [id, owner] = todo.back();
    todo.pop_back();
    if (const ImageNode n = at(id); n.kind == ImageKind::ref) {
      const ImageFace f = _im.ready(n.a).face[1];
      if (f.body == kNoImage ||
          (f.kind == ImageRuleKind::terminal && !literals(f.body)))
        continue;
      const auto [it, fresh] =
          _local.try_emplace(n.a, static_cast<std::uint16_t>(_body.size()));
      if (owner != kNone) calls[owner].push_back(it->second);
      if (!fresh) continue;
      _body.push_back(f.body);
      calls.emplace_back();
      todo.emplace_back(f.body, it->second);
      continue;
    }
    for (const ImageId k : parts(id)) todo.emplace_back(k, owner);
  }
  return calls;
}

Descent<OperatorWords> Reader::words(ImageId id) {
  const ImageNode n = at(id);
  if (n.kind == ImageKind::ref) {
    const auto local = _local.find(n.a);
    if (local == _local.end())
      co_return _im.ready(n.a).face[1].kind == ImageRuleKind::terminal
          ? OperatorWords::token(token({.rule = n.a}))
          : OperatorWords::unlisted(OperatorWords::none());
    const std::uint16_t r = local->second;
    if (_walking[r]) co_return OperatorWords::unlisted(OperatorWords::none());
    const std::uint32_t part = _scc.component[r];
    const bool cacheable = _onPath[part] == 0;
    if (cacheable && _known[r]) co_return *_known[r];
    _walking[r] = true;
    ++_onPath[part];
    OperatorWords out = co_await words(_body[r]);
    _walking[r] = false;
    --_onPath[part];
    if (cacheable) _known[r] = out;
    co_return out;
  }
  if (n.kind == ImageKind::lit)
    co_return n.text.empty()
        ? OperatorWords::nothing()
        : OperatorWords::token(
              token({n.text, kNoImage, (n.flags & kLitFolded) != 0}));
  bool seq = false, unlisted = false;
  OperatorWords out;
  switch (n.kind) {
    case ImageKind::seq:
    case ImageKind::commit:
      seq = true;
      [[fallthrough]];
    case ImageKind::implied:
    case ImageKind::andOf:
    case ImageKind::notOf:
    case ImageKind::gapHas:
    case ImageKind::gapIs:
    case ImageKind::eof:
    case ImageKind::epsilon:
      out = OperatorWords::nothing();
      break;
    case ImageKind::repeat:
      unlisted = n.n > OperatorWords::kLimit;
      break;
    case ImageKind::choice:
    case ImageKind::doc:
    case ImageKind::assign:
    case ImageKind::append:
    case ImageKind::flag:
    case ImageKind::create:
    case ImageKind::nest:
    case ImageKind::skip:
      break;
    default:
      unlisted = true;
  }
  for (std::size_t i = 0; i < parts(id).size(); ++i) {
    const OperatorWords w = co_await words(parts(id)[i]);
    if (seq)
      out = out.then(w);
    else
      out.unite(w);
  }
  if (unlisted) co_return OperatorWords::unlisted(out);
  if (n.kind != ImageKind::repeat) co_return out;
  // `m` to `n` turns of the item.
  OperatorWords turns = OperatorWords::nothing(), all = OperatorWords::none();
  for (std::uint64_t k = 0;; ++k) {
    if (k >= n.m) all.unite(turns);
    if (k >= n.n) co_return all;
    turns = turns.then(out);
  }
}

// A level's operators, one per alternative, as the parser reads them.
std::vector<ImageId> alternatives(const GrammarImage& image, ImageId op) {
  std::vector<ImageId> out, todo{op};
  while (!todo.empty()) {
    const ImageId id = todo.back();
    todo.pop_back();
    const std::span<const ImageId> children = image.shape_children(id);
    if (image.nodes[id].kind == ImageKind::choice)
      todo.insert(todo.end(), children.rbegin(), children.rend());
    else
      out.push_back(id);
  }
  return out;
}

bool unary(ImageGroup g) {
  return g == ImageGroup::prefix || g == ImageGroup::postfix;
}

}  // namespace

GrammarFault level_fault(GrammarImage& image,
                         std::span<const std::uint32_t> rules) {
  for (std::size_t r = 0; r < rules.size(); ++r) {
    const ImageFace f = image.ready(rules[r]).face[1];
    // A table of more than 255 levels is refused for that.
    if (f.kind != ImageRuleKind::infix || f.levelCount > 255) continue;
    const std::vector<ImageLevel> levels(
        image.levels.begin() + f.levelsAt,
        image.levels.begin() + f.levelsAt + f.levelCount);
    Reader reader(image, levels);
    std::vector<std::vector<OperatorWords>> words(levels.size());
    for (std::size_t i = 0; i < levels.size(); ++i)
      for (const ImageId op : alternatives(image, levels[i].op))
        words[i].push_back(descend(reader.words(op)));
    for (std::size_t a = 0; a < levels.size(); ++a) {
      const ImageGroup ga = levels[a].group;
      for (std::size_t b = 0; b < levels.size(); ++b) {
        const ImageGroup gb = levels[b].group;
        const bool twice = b > a && (unary(ga) ? gb == ga : !unary(gb));
        const bool binary = ga == ImageGroup::postfix && !unary(gb);
        if (!twice && !binary) continue;
        for (const OperatorWords& x : words[a])
          for (const OperatorWords& y : words[b]) {
            if (!x.overlaps(y)) continue;
            const bool listed = !x.open() && !y.open();
            GrammarFault fault{
                .code = binary ? GrammarFaultCode::postfixBinaryClash
                               : GrammarFaultCode::precedenceClash,
                .rule = static_cast<std::uint16_t>(r + 1),
                .count = static_cast<std::uint16_t>(OperatorWords::kLimit),
                .gated = !listed};
            const std::string at = std::to_string(a + 1);
            const std::string to = std::to_string(b + 1);
            if (binary && listed)
              fault.part = quote(reader.spelled(*x.common(y)), '`') +
                           " as a postfix operator on level " + at +
                           " and as a binary operator on level " + to;
            else if (binary)
              fault.part = "the postfix operator on level " + at +
                           " and the binary operator on level " + to;
            else if (!listed)
              fault.part =
                  std::string(ga == ImageGroup::prefix    ? "the prefix"
                              : ga == ImageGroup::postfix ? "the postfix"
                                                          : "the binary") +
                  " operators on levels " + at + " and " + to;
            return fault;
          }
      }
    }
  }
  return {};
}

}  // namespace cursiv::engine
