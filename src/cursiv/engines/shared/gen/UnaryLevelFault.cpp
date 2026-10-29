#include "cursiv/engines/shared/gen/UnaryLevelFault.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace cursiv {

namespace {

// One token of an operator: a literal's text, or the terminal it reads where
// that terminal is no single literal.
struct Token {
  std::string_view text{};
  std::uint32_t rule = kNoImage;
  bool folded = false;
};
using Spelling = std::vector<Token>;

// An operator spelled more ways than this is not listed.
constexpr std::size_t kSpellings = 64;

Token literal(const ImageNode& n) {
  return Token{n.text, kNoImage, (n.flags & kLitFolded) != 0};
}

class Speller {
 public:
  explicit Speller(GrammarImage& image) : _im(image) {}

  // The token strings `id` spells, or false where a part has no fixed
  // spelling. Conditions spell nothing, and a data rule is read through.
  bool spell(ImageId id, std::vector<Spelling>& out) {
    const ImageNode n = _im.nodes[id];
    switch (n.kind) {
      case ImageKind::lit:
        out.assign(1, Spelling{literal(n)});
        return true;
      case ImageKind::andOf:
      case ImageKind::notOf:
      case ImageKind::gapHas:
      case ImageKind::gapIs:
      case ImageKind::eof:
        out.assign(1, Spelling{});
        return true;
      case ImageKind::doc:
        return spell(_im.kids[n.first], out);
      case ImageKind::ref:
        return spell_rule(n.a, out);
      case ImageKind::seq:
        return spell_seq(n, out);
      case ImageKind::choice: {
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
  bool spell_seq(const ImageNode& n, std::vector<Spelling>& out) {
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
    const ImageFace f = _im.ready(rule).face[1];
    if (f.kind == ImageRuleKind::terminal) {
      const ImageNode body = _im.nodes[f.body];
      out.assign(1,
                 Spelling{body.kind == ImageKind::lit ? literal(body)
                                                      : Token{.rule = rule}});
      return true;
    }
    if (f.kind != ImageRuleKind::data ||
        std::ranges::count(_calling, rule) != 0)
      return false;
    _calling.push_back(rule);
    const bool ok = spell(f.body, out);
    _calling.pop_back();
    return ok;
  }

  GrammarImage& _im;
  std::vector<std::uint32_t> _calling;
};

char lower(char c) {
  return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool same_token(const Token& a, const Token& b) {
  if (a.rule != b.rule) return false;
  if (a.rule != kNoImage) return true;
  if (!a.folded && !b.folded) return a.text == b.text;
  return std::ranges::equal(
      a.text, b.text, [](char x, char y) { return lower(x) == lower(y); });
}

// The first string both operators spell.
const Spelling* shared(const std::vector<Spelling>& a,
                       const std::vector<Spelling>& b) {
  for (const Spelling& x : a)
    for (const Spelling& y : b)
      if (!x.empty() && std::ranges::equal(x, y, same_token)) return &x;
  return nullptr;
}

std::string spelled(const GrammarImage& image, const Spelling& s) {
  std::string out;
  for (const Token& t : s) {
    if (!out.empty()) out += ' ';
    out += t.rule == kNoImage ? t.text : image.rules[t.rule].name;
  }
  return out;
}

bool unary(ImageGroup g) {
  return g == ImageGroup::prefix || g == ImageGroup::postfix;
}

}  // namespace

GrammarFault unary_level_fault(GrammarImage& image,
                               std::span<const std::uint32_t> rules) {
  Speller speller(image);
  std::vector<std::vector<Spelling>> words;
  std::vector<bool> known;
  for (std::size_t r = 0; r < rules.size(); ++r) {
    const ImageFace f = image.ready(rules[r]).face[1];
    if (f.kind != ImageRuleKind::infix) continue;
    const std::vector<ImageLevel> levels(
        image.levels.begin() + f.levelsAt,
        image.levels.begin() + f.levelsAt + f.levelCount);
    if (std::ranges::none_of(
            levels, [](const ImageLevel& l) { return unary(l.group); }))
      continue;
    words.assign(levels.size(), {});
    known.assign(levels.size(), false);
    for (std::size_t i = 0; i < levels.size(); ++i)
      known[i] = speller.spell(levels[i].op, words[i]);
    for (std::size_t a = 0; a < levels.size(); ++a) {
      if (!unary(levels[a].group)) continue;
      for (std::size_t b = 0; b < levels.size(); ++b) {
        const bool twice = b > a && levels[b].group == levels[a].group;
        const bool binary =
            levels[a].group == ImageGroup::postfix && !unary(levels[b].group);
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
                       " with the binary operator on level " + to;
        else if (!listed)
          fault.part = std::string(levels[a].group == ImageGroup::prefix
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
