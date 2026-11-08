#include "cursiv/shared/gen/UnaryLevelFault.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/utils/CaseFold.hpp"

namespace cursiv {

namespace {

// One token of an operator: a literal's text, or the terminal it reads where
// that terminal is no single literal.
struct Token {
  std::string_view text{};
  std::uint32_t rule = detail::kNoImage;
  bool folded = false;
};
using Spelling = std::vector<Token>;

// An operator spelled more ways than this is not listed.
constexpr std::size_t kSpellings = 64;

Token literal(const detail::ImageNode& n) {
  return Token{n.text, detail::kNoImage, (n.flags & detail::kLitFolded) != 0};
}

class Speller {
 public:
  explicit Speller(detail::GrammarImage& image) : _im(image) {}

  // The token strings `id` spells, or false where a part has no fixed
  // spelling. Conditions spell nothing, and a data rule is read through.
  bool spell(detail::ImageId id, std::vector<Spelling>& out) {
    const detail::ImageNode n = _im.nodes[id];
    switch (n.kind) {
      case detail::ImageKind::lit:
        out.assign(1, Spelling{literal(n)});
        return true;
      case detail::ImageKind::andOf:
      case detail::ImageKind::notOf:
      case detail::ImageKind::gapHas:
      case detail::ImageKind::gapIs:
      case detail::ImageKind::eof:
        out.assign(1, Spelling{});
        return true;
      case detail::ImageKind::doc:
        return spell(_im.kids[n.first], out);
      case detail::ImageKind::ref:
        return spell_rule(n.a, out);
      case detail::ImageKind::seq:
        return spell_seq(n, out);
      case detail::ImageKind::choice: {
        out.clear();
        std::vector<Spelling> part;
        for (std::uint32_t i = 0; i < n.count; ++i) {
          if (!spell(_im.kids[n.first + i], part)) return false;
          out.insert(out.end(), part.begin(), part.end());
          if (out.size() > kSpellings) return false;
        }
        return true;
      }
      default:
        return false;
    }
  }

 private:
  bool spell_seq(const detail::ImageNode& n, std::vector<Spelling>& out) {
    out.assign(1, Spelling{});
    std::vector<Spelling> part, next;
    for (std::uint32_t i = 0; i < n.count; ++i) {
      if (!spell(_im.kids[n.first + i], part) ||
          out.size() * part.size() > kSpellings)
        return false;
      next.clear();
      for (const Spelling& a : out)
        for (const Spelling& b : part) {
          next.push_back(a);
          next.back().insert(next.back().end(), b.begin(), b.end());
        }
      out.swap(next);
    }
    return true;
  }

  bool spell_rule(std::uint32_t rule, std::vector<Spelling>& out) {
    const detail::ImageFace f = _im.ready(rule).face[1];
    if (f.kind == detail::ImageRuleKind::terminal) {
      const detail::ImageNode body = _im.nodes[f.body];
      // A choice of words is spelled by its words, as if written in the table.
      const auto first = _im.kids.begin() + body.first;
      if (body.kind == detail::ImageKind::choice &&
          std::all_of(first, first + body.count, [&](detail::ImageId kid) {
            return _im.nodes[kid].kind == detail::ImageKind::lit;
          }))
        return spell(f.body, out);
      out.assign(1, Spelling{body.kind == detail::ImageKind::lit
                                 ? literal(body)
                                 : Token{.rule = rule}});
      return true;
    }
    if (f.kind != detail::ImageRuleKind::data ||
        std::ranges::count(_calling, rule) != 0)
      return false;
    _calling.push_back(rule);
    const bool ok = spell(f.body, out);
    _calling.pop_back();
    return ok;
  }

  detail::GrammarImage& _im;
  std::vector<std::uint32_t> _calling;
};

bool same_token(const Token& a, const Token& b) {
  if (a.rule != b.rule) return false;
  if (a.rule != detail::kNoImage) return true;
  if (!a.folded && !b.folded) return a.text == b.text;
  return folded_equal(a.text, b.text);
}

// The first string both operators spell.
const Spelling* shared(const std::vector<Spelling>& a,
                       const std::vector<Spelling>& b) {
  for (const Spelling& x : a)
    for (const Spelling& y : b)
      if (!x.empty() && std::ranges::equal(x, y, same_token)) return &x;
  return nullptr;
}

std::string spelled(const detail::GrammarImage& image, const Spelling& s) {
  std::string out;
  for (const Token& t : s) {
    if (!out.empty()) out += ' ';
    out += t.rule == detail::kNoImage ? t.text : image.rules[t.rule].name;
  }
  return out;
}

bool unary(detail::ImageGroup g) {
  return g == detail::ImageGroup::prefix || g == detail::ImageGroup::postfix;
}

}  // namespace

GrammarFault unary_level_fault(detail::GrammarImage& image,
                               std::span<const std::uint32_t> rules) {
  Speller speller(image);
  std::vector<std::vector<Spelling>> words;
  std::vector<bool> known;
  for (std::size_t r = 0; r < rules.size(); ++r) {
    const detail::ImageFace f = image.ready(rules[r]).face[1];
    if (f.kind != detail::ImageRuleKind::infix) continue;
    const std::vector<detail::ImageLevel> levels(
        image.levels.begin() + f.levelsAt,
        image.levels.begin() + f.levelsAt + f.levelCount);
    if (std::ranges::none_of(
            levels, [](const detail::ImageLevel& l) { return unary(l.group); }))
      continue;
    words.assign(levels.size(), {});
    known.assign(levels.size(), false);
    for (std::size_t i = 0; i < levels.size(); ++i)
      known[i] = speller.spell(levels[i].op, words[i]);
    for (std::size_t a = 0; a < levels.size(); ++a) {
      if (!unary(levels[a].group)) continue;
      for (std::size_t b = 0; b < levels.size(); ++b) {
        const bool twice = b > a && levels[b].group == levels[a].group;
        const bool binary = levels[a].group == detail::ImageGroup::postfix &&
                            !unary(levels[b].group);
        if (!twice && !binary) continue;
        // Texts the speller cannot list may be shared: refused alike.
        const bool listed = known[a] && known[b];
        const Spelling* const both =
            listed ? shared(words[a], words[b]) : nullptr;
        if (listed && both == nullptr) continue;
        GrammarFault fault{.code = binary ? GrammarFaultCode::postfixBinaryClash
                                          : GrammarFaultCode::precedenceClash,
                           .rule = static_cast<std::uint16_t>(r + 1),
                           .count = kSpellings,
                           .gated = !listed};
        const std::string at = std::to_string(a + 1);
        const std::string to = std::to_string(b + 1);
        if (!listed && binary)
          fault.part = "the postfix operator on level " + at +
                       " and the binary operator on level " + to;
        else if (!listed)
          fault.part = std::string(levels[a].group == detail::ImageGroup::prefix
                                       ? "the prefix"
                                       : "the postfix") +
                       " operators on levels " + at + " and " + to;
        else if (binary)
          fault.part = "`" + spelled(image, *both) +
                       "` as a postfix operator on level " + at +
                       " and as a binary operator on level " + to;
        return fault;
      }
    }
  }
  return {};
}

}  // namespace cursiv
