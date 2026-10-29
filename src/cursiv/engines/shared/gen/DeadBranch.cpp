#include "cursiv/engines/shared/gen/DeadBranch.hpp"

#include <algorithm>
#include <vector>

#include "cursiv/core/grammar/GrammarPrinter.hpp"
#include "cursiv/engines/shared/gen/WarningFound.hpp"

namespace cursiv {
namespace {

// What an element contributes to its sequence: nothing, a token, an optional
// token, the end assertion, a choice one branch of which is that assertion
// alone, or a symbol that may derive nothing.
enum class Read : std::uint8_t { none, token, option, end, either, other };

struct Arm {
  std::uint16_t at = 0, of = 0;
};

class Dead {
 public:
  explicit Dead(WarningFound& w) : _w(w), _im(w.image()) {}

  void rule(std::uint32_t rule) {
    const ImageId body = _w.body(rule);
    if (!eof_in(body)) return;
    _rule = _w.name(rule);
    walk(body, Arm{});
  }

  std::vector<DeadBranch> found;

 private:
  const ImageNode& node(ImageId id) const { return _im.nodes[id]; }
  ImageId kid(const ImageNode& n, std::uint32_t i) const {
    return _im.kids[n.first + i];
  }

  bool is_eof(ImageId id) const { return node(id).kind == ImageKind::eof; }

  // Conditions consume no token in their containing sequence.
  bool is_cond(ImageId id) const {
    const ImageNode& n = node(id);
    switch (n.kind) {
      case ImageKind::andOf:
      case ImageKind::notOf:
      case ImageKind::gapHas:
      case ImageKind::gapIs:
        return true;
      case ImageKind::choice:
      case ImageKind::seq: {
        bool any = false;
        for (std::uint32_t i = 0; i < n.count; ++i) {
          const ImageId k = kid(n, i);
          if (!is_cond(k) && !is_eof(k)) return false;
          any = any || is_cond(k);
        }
        return any;
      }
      default:
        return false;
    }
  }

  // One for a branch that is `eof` bare, two for one under a directive.
  unsigned end_arm(ImageId id) const {
    const ImageNode& n = node(id);
    switch (n.kind) {
      case ImageKind::assign:
      case ImageKind::append:
      case ImageKind::flag:
      case ImageKind::create:
      case ImageKind::nest:
        return end_arm(kid(n, 0)) ? 2u : 0u;
      case ImageKind::skip:
      case ImageKind::doc:
        return end_arm(kid(n, 0));
      default:
        return is_eof(id) ? 1u : 0u;
    }
  }

  // A write over a creation stands on a rule of its own.
  bool creates(ImageId id) const {
    const ImageNode& n = node(id);
    switch (n.kind) {
      case ImageKind::create:
        return true;
      case ImageKind::nest:
      case ImageKind::skip:
      case ImageKind::doc:
        return creates(kid(n, 0));
      case ImageKind::repeat:
        return n.n == 1 && creates(kid(n, 0));
      default:
        return false;
    }
  }

  // `Written`: a directive stands over the element, and spreads over a
  // choice.
  Read read(ImageId id, bool written) {
    const ImageNode n = node(id);
    switch (n.kind) {
      case ImageKind::eof:
        return Read::end;
      case ImageKind::ref:
        return _im.ready(n.a).face[1].kind == ImageRuleKind::terminal
                   ? Read::token
                   : Read::other;
      case ImageKind::lit:
      case ImageKind::cpset:
      case ImageKind::byte:
      case ImageKind::through:
      case ImageKind::indent:
      case ImageKind::dedent:
      case ImageKind::implied:
      case ImageKind::embed:
        return Read::token;
      case ImageKind::epsilon:
        return Read::none;
      case ImageKind::doc:
      case ImageKind::skip:
        return read(kid(n, 0), written);
      case ImageKind::create:
      case ImageKind::nest:
        return read(kid(n, 0), true);
      case ImageKind::assign:
      case ImageKind::append:
      case ImageKind::flag:
        return creates(kid(n, 0)) ? Read::other : read(kid(n, 0), true);
      case ImageKind::repeat: {
        const Read k = read(kid(n, 0), written);
        if (n.n != 1) return Read::other;
        if (n.m != 0) return k;
        if (k == Read::token) return Read::option;
        return k == Read::option || k == Read::none ? k : Read::other;
      }
      case ImageKind::choice: {
        unsigned ends = 0;
        bool wrote = written;
        for (std::uint32_t i = 0; i < n.count; ++i) {
          const unsigned e = end_arm(kid(n, i));
          ends += e != 0 ? 1u : 0u;
          wrote = wrote || e == 2;
        }
        return is_cond(id) || ends != 1 || wrote ? Read::other : Read::either;
      }
      default:
        return Read::other;
    }
  }

  // The choice an `either` element stands for, under what keeps it one.
  ImageId choice_of(ImageId id) const {
    if (const ImageNode& n = node(id);
        n.kind == ImageKind::skip || n.kind == ImageKind::doc ||
        (n.kind == ImageKind::repeat && n.m == 1 && n.n == 1))
      return choice_of(kid(n, 0));
    return id;
  }

  // Whether `eof` stands as a position anywhere under the expression: a
  // condition holds none, and a rule called is walked on its own.
  bool eof_in(ImageId id) {
    if (_eof.size() <= id) _eof.resize(_im.nodes.size(), 0);
    if (_eof[id] != 0) return _eof[id] == 2;
    const ImageNode& n = node(id);
    bool found = n.kind == ImageKind::eof;
    if (!found && n.kind != ImageKind::notOf && n.kind != ImageKind::andOf)
      for (const ImageId k : _im.shape_kids(id))
        if (eof_in(k)) {
          found = true;
          break;
        }
    _eof[id] = found ? 2 : 1;
    return found;
  }

  std::vector<Arm> arms_of(const ImageNode& choice) const {
    const std::vector<std::uint16_t> places = printed_places(_im, choice);
    std::vector<Arm> arms;
    for (const std::uint16_t at : places) arms.push_back({at, places.back()});
    return arms;
  }

  void add(std::string branch, std::string choice, Arm a, std::string after) {
    DeadBranch d{_rule, std::move(branch), std::move(choice), a.at,
                 a.of,  std::move(after)};
    if (std::ranges::find(found, d) == found.end())
      found.push_back(std::move(d));
  }

  void walk_kid(ImageId id, Arm a) {
    if (eof_in(id)) walk(id, a);
  }

  void walk(ImageId id, Arm a) {
    const ImageNode n = node(id);
    switch (n.kind) {
      // A directive, prose or region over a branch is still that branch.
      case ImageKind::assign:
      case ImageKind::append:
      case ImageKind::flag:
      case ImageKind::create:
      case ImageKind::nest:
      case ImageKind::doc:
      case ImageKind::skip:
        walk_kid(kid(n, 0), a);
        return;
      case ImageKind::choice: {
        const std::vector<Arm> arms = arms_of(n);
        for (std::uint32_t i = 0; i < n.count; ++i)
          walk_kid(kid(n, i), arms[i]);
        return;
      }
      case ImageKind::seq:
        walk_seq(id, a);
        return;
      default: {
        const std::vector<ImageId> kids(_im.shape_kids(id).begin(),
                                        _im.shape_kids(id).end());
        for (const ImageId k : kids) walk_kid(k, Arm{});
      }
    }
  }

  // A sequence drops each reading that keeps a token past an end assertion:
  // an `eof`, or the `eof` branch of an `either`.
  void walk_seq(ImageId id, Arm a) {
    const ImageNode n = node(id);
    std::vector<Read> reads;
    for (std::uint32_t i = 0; i < n.count; ++i)
      reads.push_back(read(kid(n, i), false));
    const std::uint32_t count = n.count;
    std::vector<std::uint32_t> token_after(count);
    for (std::uint32_t i = count, next = count; i-- > 0;) {
      token_after[i] = next;
      if (reads[i] == Read::token) next = i;
    }
    std::uint32_t end = 0;
    while (end < count && reads[end] != Read::end) ++end;
    if (end < count && token_after[end] < count) {
      add(_w.spell(id), {}, a, _w.spell(kid(n, token_after[end])));
    } else {
      for (std::uint32_t i = 0; i < count; ++i) {
        const ImageId k = kid(n, i);
        if (reads[i] == Read::option && end < i) {
          add(_w.spell(k), {}, {}, {});
        } else if (reads[i] == Read::either && token_after[i] < count) {
          const ImageId c = choice_of(k);
          const ImageNode choice = node(c);
          std::uint32_t at = 0;
          while (at < choice.count && end_arm(kid(choice, at)) == 0) ++at;
          add(_w.spell(kid(choice, at)), _w.spell(c), arms_of(choice)[at],
              _w.spell(kid(n, token_after[i])));
        }
      }
    }
    for (std::uint32_t i = 0; i < count; ++i) walk_kid(kid(n, i), Arm{});
  }

  WarningFound& _w;
  GrammarImage& _im;
  std::string _rule;
  std::vector<std::uint8_t> _eof;
};

}  // namespace

std::vector<DeadBranch> dead_branches(WarningFound& found) {
  Dead dead(found);
  for (const std::uint32_t rule : found.rules()) dead.rule(rule);
  return std::move(dead.found);
}

std::string dead_branch_text(const DeadBranch& d) {
  std::string out = "rule `" + d.rule + "`: ";
  if (d.arms != 0)
    out += "branch " + detail::decimal(d.arm) + " of " +
           detail::decimal(d.arms) + ' ';
  out += '`' + d.branch + '`';
  if (!d.choice.empty()) out += " in `" + d.choice + '`';
  out += " can never match -- ";
  out += d.after.empty() ? std::string("it") : '`' + d.after + '`';
  return out + " comes after `eof`, and nothing can follow the end of input";
}

}  // namespace cursiv
