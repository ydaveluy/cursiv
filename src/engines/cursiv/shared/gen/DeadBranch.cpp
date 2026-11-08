#include "cursiv/shared/gen/DeadBranch.hpp"

#include <algorithm>
#include <vector>

#include "cursiv/grammar/GrammarPrinter.hpp"
#include "cursiv/shared/gen/WarningFound.hpp"

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
    const detail::ImageId body = _w.body(rule);
    if (!eof_in(body)) return;
    _rule = _w.name(rule);
    walk(body, Arm{});
  }

  std::vector<DeadBranch> found;

 private:
  const detail::ImageNode& node(detail::ImageId id) const {
    return _im.nodes[id];
  }
  detail::ImageId kid(const detail::ImageNode& n, std::uint32_t i) const {
    return _im.kids[n.first + i];
  }

  bool is_eof(detail::ImageId id) const {
    return node(id).kind == detail::ImageKind::eof;
  }

  // Conditions consume no token in their containing sequence.
  bool is_cond(detail::ImageId id) const {
    const detail::ImageNode& n = node(id);
    switch (n.kind) {
      case detail::ImageKind::andOf:
      case detail::ImageKind::notOf:
      case detail::ImageKind::gapHas:
      case detail::ImageKind::gapIs:
        return true;
      case detail::ImageKind::choice:
      case detail::ImageKind::seq: {
        bool any = false;
        for (std::uint32_t i = 0; i < n.count; ++i) {
          const detail::ImageId k = kid(n, i);
          const bool condition = is_cond(k);
          if (!condition && !is_eof(k)) return false;
          any = any || condition;
        }
        return any;
      }
      default:
        return false;
    }
  }

  // One for a branch that is `eof` bare, two for one under a directive.
  unsigned end_arm(detail::ImageId id) const {
    const detail::ImageNode& n = node(id);
    switch (n.kind) {
      case detail::ImageKind::assign:
      case detail::ImageKind::append:
      case detail::ImageKind::flag:
      case detail::ImageKind::create:
      case detail::ImageKind::nest:
        return end_arm(kid(n, 0)) ? 2u : 0u;
      case detail::ImageKind::skip:
      case detail::ImageKind::doc:
        return end_arm(kid(n, 0));
      default:
        return is_eof(id) ? 1u : 0u;
    }
  }

  // A write over a creation stands on a rule of its own.
  bool creates(detail::ImageId id) const {
    const detail::ImageNode& n = node(id);
    switch (n.kind) {
      case detail::ImageKind::create:
        return true;
      case detail::ImageKind::nest:
      case detail::ImageKind::skip:
      case detail::ImageKind::doc:
        return creates(kid(n, 0));
      case detail::ImageKind::repeat:
        return n.n == 1 && creates(kid(n, 0));
      default:
        return false;
    }
  }

  // `Written`: a directive stands over the element, and spreads over a
  // choice.
  Read read(detail::ImageId id, bool written) {
    const detail::ImageNode n = node(id);
    switch (n.kind) {
      case detail::ImageKind::eof:
        return Read::end;
      case detail::ImageKind::ref:
        return _im.ready(n.a).face[1].kind == detail::ImageRuleKind::terminal
                   ? Read::token
                   : Read::other;
      case detail::ImageKind::lit:
      case detail::ImageKind::cpset:
      case detail::ImageKind::byte:
      case detail::ImageKind::through:
      case detail::ImageKind::indent:
      case detail::ImageKind::dedent:
      case detail::ImageKind::implied:
      case detail::ImageKind::embed:
        return Read::token;
      case detail::ImageKind::epsilon:
        return Read::none;
      case detail::ImageKind::doc:
      case detail::ImageKind::skip:
        return read(kid(n, 0), written);
      case detail::ImageKind::create:
      case detail::ImageKind::nest:
        return read(kid(n, 0), true);
      case detail::ImageKind::assign:
      case detail::ImageKind::append:
      case detail::ImageKind::flag:
        return creates(kid(n, 0)) ? Read::other : read(kid(n, 0), true);
      case detail::ImageKind::repeat: {
        const Read k = read(kid(n, 0), written);
        if (n.n != 1) return Read::other;
        if (n.m != 0) return k;
        if (k == Read::token) return Read::option;
        return k == Read::option || k == Read::none ? k : Read::other;
      }
      case detail::ImageKind::choice: {
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
  detail::ImageId choice_of(detail::ImageId id) const {
    if (const detail::ImageNode& n = node(id);
        n.kind == detail::ImageKind::skip || n.kind == detail::ImageKind::doc ||
        (n.kind == detail::ImageKind::repeat && n.m == 1 && n.n == 1))
      return choice_of(kid(n, 0));
    return id;
  }

  // Whether `eof` stands as a position anywhere under the expression: a
  // condition holds none, and a rule called is walked on its own.
  bool eof_in(detail::ImageId id) {
    if (_eof.size() <= id) _eof.resize(_im.nodes.size(), 0);
    if (_eof[id] != 0) return _eof[id] == 2;
    const detail::ImageNode& n = node(id);
    bool holds = n.kind == detail::ImageKind::eof;
    if (!holds && n.kind != detail::ImageKind::notOf &&
        n.kind != detail::ImageKind::andOf)
      for (const detail::ImageId k : _im.shape_kids(id))
        if (eof_in(k)) {
          holds = true;
          break;
        }
    _eof[id] = holds ? 2 : 1;
    return holds;
  }

  std::vector<Arm> arms_of(const detail::ImageNode& choice) const {
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

  void walk_kid(detail::ImageId id, Arm a) {
    if (eof_in(id)) walk(id, a);
  }

  void walk(detail::ImageId id, Arm a) {
    const detail::ImageNode n = node(id);
    switch (n.kind) {
      // A directive, prose or region over a branch is still that branch.
      case detail::ImageKind::assign:
      case detail::ImageKind::append:
      case detail::ImageKind::flag:
      case detail::ImageKind::create:
      case detail::ImageKind::nest:
      case detail::ImageKind::doc:
      case detail::ImageKind::skip:
        walk_kid(kid(n, 0), a);
        return;
      case detail::ImageKind::choice: {
        const std::vector<Arm> arms = arms_of(n);
        for (std::uint32_t i = 0; i < n.count; ++i)
          walk_kid(kid(n, i), arms[i]);
        return;
      }
      case detail::ImageKind::seq:
        walk_seq(id, a);
        return;
      default: {
        const std::vector<detail::ImageId> kids(_im.shape_kids(id).begin(),
                                                _im.shape_kids(id).end());
        for (const detail::ImageId k : kids) walk_kid(k, Arm{});
      }
    }
  }

  // A sequence drops each reading that keeps a token past an end assertion:
  // an `eof`, or the `eof` branch of an `either`.
  void walk_seq(detail::ImageId id, Arm a) {
    const detail::ImageNode n = node(id);
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
        const detail::ImageId k = kid(n, i);
        if (reads[i] == Read::option && end < i) {
          add(_w.spell(k), {}, {}, {});
        } else if (reads[i] == Read::either && token_after[i] < count) {
          const detail::ImageId c = choice_of(k);
          const detail::ImageNode choice = node(c);
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
  detail::GrammarImage& _im;
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
