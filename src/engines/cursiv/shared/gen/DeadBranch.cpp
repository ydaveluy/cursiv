#include "cursiv/shared/gen/DeadBranch.hpp"

#include <algorithm>
#include <vector>

#include "cursiv/shared/gen/WarningFound.hpp"
#include "cursiv/shared/utils/CoreContract.hpp"
#include "cursiv/shared/utils/GrammarPrinter.hpp"

namespace cursiv::engine {
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
    if (!descend(eof_in(body))) return;
    _rule = _w.name(rule);
    descend(walk(body, Arm{}));
  }

  std::vector<DeadBranch> found;

 private:
  const ImageNode& node(ImageId id) const { return _im.nodes[id]; }
  ImageId child_at(const ImageNode& n, std::uint32_t i) const {
    return _im.children[n.first + i];
  }

  bool is_eof(ImageId id) const { return node(id).kind == ImageKind::eof; }

  // Conditions consume no token in their containing sequence.
  Descent<bool> is_cond(ImageId id) const {
    const ImageNode& n = node(id);
    switch (n.kind) {
      case ImageKind::andOf:
      case ImageKind::notOf:
      case ImageKind::gapHas:
      case ImageKind::gapIs:
        co_return true;
      case ImageKind::choice:
      case ImageKind::seq: {
        bool any = false;
        for (std::uint32_t i = 0; i < n.count; ++i) {
          const ImageId k = child_at(n, i);
          const bool condition = co_await is_cond(k);
          if (!condition && !is_eof(k)) co_return false;
          any = any || condition;
        }
        co_return any;
      }
      default:
        co_return false;
    }
  }

  // One for a branch that is `eof` bare, two for one under a directive.
  Descent<unsigned> end_arm(ImageId id) const {
    const ImageNode& n = node(id);
    if (is_directive_kind(n.kind))
      co_return co_await end_arm(child_at(n, 0)) ? 2u : 0u;
    if (n.kind == ImageKind::skip || n.kind == ImageKind::doc)
      co_return co_await end_arm(child_at(n, 0));
    co_return is_eof(id) ? 1u : 0u;
  }

  // A write over a creation stands on a rule of its own.
  Descent<bool> creates(ImageId id) const {
    const ImageNode& n = node(id);
    switch (n.kind) {
      case ImageKind::create:
        co_return true;
      case ImageKind::nest:
      case ImageKind::skip:
      case ImageKind::doc:
        co_return co_await creates(child_at(n, 0));
      case ImageKind::repeat:
        if (n.n != 1) co_return false;
        co_return co_await creates(child_at(n, 0));
      default:
        co_return false;
    }
  }

  // `Written`: a directive stands over the element, and spreads over a
  // choice.
  Descent<Read> read(ImageId id, bool written) {
    const ImageNode n = node(id);
    switch (n.kind) {
      case ImageKind::eof:
        co_return Read::end;
      case ImageKind::ref:
        co_return _im.ready(n.a).face[1].kind == ImageRuleKind::terminal
            ? Read::token
            : Read::other;
      case ImageKind::lit:
      case ImageKind::cpset:
      case ImageKind::byte:
      case ImageKind::through:
      case ImageKind::indent:
      case ImageKind::dedent:
      case ImageKind::embed:
        co_return Read::token;
      // An implied token is inserted with no width, past the end as well.
      case ImageKind::epsilon:
      case ImageKind::implied:
        co_return Read::none;
      case ImageKind::doc:
      case ImageKind::skip:
        co_return co_await read(child_at(n, 0), written);
      case ImageKind::create:
      case ImageKind::nest:
        co_return co_await read(child_at(n, 0), true);
      case ImageKind::assign:
      case ImageKind::append:
      case ImageKind::flag:
        if (co_await creates(child_at(n, 0))) co_return Read::other;
        co_return co_await read(child_at(n, 0), true);
      case ImageKind::repeat: {
        const Read k = co_await read(child_at(n, 0), written);
        if (n.n != 1) co_return Read::other;
        if (n.m != 0) co_return k;
        if (k == Read::token) co_return Read::option;
        co_return k == Read::option || k == Read::none ? k : Read::other;
      }
      case ImageKind::choice: {
        unsigned ends = 0;
        bool wrote = written;
        for (std::uint32_t i = 0; i < n.count; ++i) {
          const unsigned e = co_await end_arm(child_at(n, i));
          ends += e != 0 ? 1u : 0u;
          wrote = wrote || e == 2;
        }
        co_return co_await is_cond(id) || ends != 1 || wrote ? Read::other
                                                             : Read::either;
      }
      default:
        co_return Read::other;
    }
  }

  // The choice an `either` element stands for, under what keeps it one.
  Descent<ImageId> choice_of(ImageId id) const {
    if (const ImageNode& n = node(id);
        n.kind == ImageKind::skip || n.kind == ImageKind::doc ||
        (n.kind == ImageKind::repeat && n.m == 1 && n.n == 1))
      co_return co_await choice_of(child_at(n, 0));
    co_return id;
  }

  // Whether `eof` stands as a position anywhere under the expression: a
  // condition holds none, and a rule called is walked on its own.
  Descent<bool> eof_in(ImageId id) {
    if (_eof.size() <= id) _eof.resize(_im.nodes.size(), 0);
    if (_eof[id] != 0) co_return _eof[id] == 2;
    const ImageNode& n = node(id);
    bool holds = n.kind == ImageKind::eof;
    if (!holds && n.kind != ImageKind::notOf && n.kind != ImageKind::andOf)
      for (const ImageId k : _im.shape_children(id))
        if (co_await eof_in(k)) {
          holds = true;
          break;
        }
    _eof[id] = holds ? 2 : 1;
    co_return holds;
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

  Descent<void> walk_child(ImageId id, Arm a) {
    if (co_await eof_in(id)) co_await walk(id, a);
  }

  Descent<void> walk(ImageId id, Arm a) {
    const ImageNode n = node(id);
    // A directive, prose or region over a branch is still that branch.
    if (is_directive_kind(n.kind) || n.kind == ImageKind::doc ||
        n.kind == ImageKind::skip) {
      co_await walk_child(child_at(n, 0), a);
      co_return;
    }
    switch (n.kind) {
      case ImageKind::choice: {
        const std::vector<Arm> arms = arms_of(n);
        for (std::uint32_t i = 0; i < n.count; ++i)
          co_await walk_child(child_at(n, i), arms[i]);
        co_return;
      }
      case ImageKind::seq:
        co_await walk_seq(id, a);
        co_return;
      default: {
        const std::vector<ImageId> children(_im.shape_children(id).begin(),
                                            _im.shape_children(id).end());
        for (const ImageId k : children) co_await walk_child(k, Arm{});
      }
    }
  }

  // A sequence drops each reading that keeps a token past an end assertion:
  // an `eof`, or the `eof` branch of an `either`.
  Descent<void> walk_seq(ImageId id, Arm a) {
    const ImageNode n = node(id);
    std::vector<Read> reads;
    for (std::uint32_t i = 0; i < n.count; ++i)
      reads.push_back(co_await read(child_at(n, i), false));
    const std::uint32_t count = n.count;
    std::vector<std::uint32_t> token_after(count);
    for (std::uint32_t i = count, next = count; i-- > 0;) {
      token_after[i] = next;
      if (reads[i] == Read::token) next = i;
    }
    std::uint32_t end = 0;
    while (end < count && reads[end] != Read::end) ++end;
    if (end < count && token_after[end] < count) {
      add(_w.spell(id), {}, a, _w.spell(child_at(n, token_after[end])));
    } else {
      for (std::uint32_t i = 0; i < count; ++i) {
        const ImageId k = child_at(n, i);
        if (reads[i] == Read::option && end < i) {
          add(_w.spell(k), {}, {}, {});
        } else if (reads[i] == Read::either && token_after[i] < count) {
          const ImageId c = co_await choice_of(k);
          const ImageNode choice = node(c);
          std::uint32_t at = 0;
          while (at < choice.count) {
            if (co_await end_arm(child_at(choice, at)) != 0) break;
            ++at;
          }
          add(_w.spell(child_at(choice, at)), _w.spell(c), arms_of(choice)[at],
              _w.spell(child_at(n, token_after[i])));
        }
      }
    }
    for (std::uint32_t i = 0; i < count; ++i)
      co_await walk_child(child_at(n, i), Arm{});
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
    out += "branch " + decimal(d.arm) + " of " + decimal(d.arms) + ' ';
  out += '`' + d.branch + '`';
  if (!d.choice.empty()) out += " in `" + d.choice + '`';
  out += " can never match -- ";
  out += d.after.empty() ? std::string("it") : '`' + d.after + '`';
  return out + " comes after `eof`, and nothing can follow the end of input";
}

}  // namespace cursiv::engine
