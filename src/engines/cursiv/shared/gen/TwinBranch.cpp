#include "cursiv/shared/gen/TwinBranch.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>

#include "cursiv/grammar/GrammarPrinter.hpp"
#include "cursiv/utils/StrongComponents.hpp"
#include "cursiv/shared/gen/WarningFound.hpp"
#include "cursiv/shared/gen/WrittenReads.hpp"
#include "cursiv/shared/gen/optional_shape.hpp"

namespace cursiv {
namespace {

// The first word of a key's shape.
enum : std::uint64_t { kEmpty = 1, kSeq, kChoice, kRegion, kOther };

// A write or prose over an operand reads what the operand reads.
bool reads_operand(detail::ImageKind kind) {
  switch (kind) {
    case detail::ImageKind::assign:
    case detail::ImageKind::append:
    case detail::ImageKind::flag:
    case detail::ImageKind::create:
    case detail::ImageKind::nest:
    case detail::ImageKind::doc:
      return true;
    default:
      return false;
  }
}

// How deep a run read by its first item present may nest runs of its own.
constexpr unsigned kRunDepth = 16;
// The most positions a call opens into: a longer body is compared whole.
constexpr std::size_t kCallItems = 16;

}  // namespace

TwinReader::TwinReader(detail::GrammarImage& image, DataBody dataBody)
    : _im(image), _dataBody(std::move(dataBody)) {}

TwinReader TwinReader::resolved(detail::GrammarImage& image) {
  return TwinReader(image, [&image](std::uint32_t rule) {
    if (image.rules[rule].foreign) return detail::kNoImage;
    const detail::ImageFace face = image.ready(rule).face[1];
    return face.kind == detail::ImageRuleKind::data ? face.body
                                                    : detail::kNoImage;
  });
}

std::vector<detail::ImageId> TwinReader::kids(detail::ImageId id) const {
  const detail::ImageNode& n = _im.nodes[id];
  return {_im.kids.begin() + n.first, _im.kids.begin() + n.first + n.count};
}

std::uint32_t TwinReader::intern(std::vector<std::uint64_t> shape) {
  const auto next = static_cast<std::uint32_t>(_shapes.size());
  return _shapes.try_emplace(std::move(shape), next).first->second;
}

std::uint32_t TwinReader::text(std::string_view s) {
  const auto next = static_cast<std::uint32_t>(_texts.size());
  return _texts.try_emplace(s, next).first->second;
}

std::uint32_t TwinReader::key(detail::ImageId id) {
  if (id < _keys.size() && _keys[id] != 0) return _keys[id] - 1;
  const std::uint32_t k = shape(id);
  if (_keys.size() <= id)
    _keys.resize(std::max<std::size_t>(id + 1u, _im.nodes.size()), 0);
  _keys[id] = k + 1;
  return k;
}

std::uint32_t TwinReader::shape(detail::ImageId id) {
  const detail::ImageNode n = _im.nodes[id];
  if (reads_operand(n.kind)) return key(kid(id, 0));
  switch (n.kind) {
    case detail::ImageKind::epsilon:
      return intern({kEmpty});
    case detail::ImageKind::seq: {
      std::vector<Item> items;
      flatten(id, items);
      if (items.empty()) return intern({kEmpty});
      if (items.size() == 1) return items[0].key;
      std::vector<std::uint64_t> s{kSeq};
      for (const Item& i : items) s.push_back(i.key);
      return intern(std::move(s));
    }
    case detail::ImageKind::choice: {
      const std::vector<std::uint32_t> arms = branch_keys(id);
      if (arms.size() == 1) return arms[0];
      std::vector<std::uint64_t> s{kChoice};
      s.insert(s.end(), arms.begin(), arms.end());
      return intern(std::move(s));
    }
    case detail::ImageKind::skip: {
      // A region is its lists, whichever order they were written in.
      const auto list = [&](std::uint64_t at, std::uint64_t count) {
        std::vector<std::uint64_t> rules(_im.ruleLists.begin() + at,
                                         _im.ruleLists.begin() + at + count);
        std::ranges::sort(rules);
        return rules;
      };
      std::vector<std::uint64_t> s{kRegion, n.m, n.n};
      for (const auto& part : {list(n.a, n.m), list(n.a + n.m, n.n)})
        s.insert(s.end(), part.begin(), part.end());
      for (const detail::ImageId k : kids(id)) s.push_back(key(k));
      return intern(std::move(s));
    }
    case detail::ImageKind::ref:
      if (const detail::ImageId body = expanded(n.a); body != detail::kNoImage)
        return key(body);
      break;
    default:
      break;
  }
  std::vector<std::uint64_t> s{kOther,
                               static_cast<std::uint64_t>(n.kind),
                               n.flags,
                               n.a,
                               n.m,
                               n.n,
                               text(n.text),
                               text(n.text2),
                               reinterpret_cast<std::uintptr_t>(n.row),
                               reinterpret_cast<std::uintptr_t>(n.tag)};
  for (const detail::ImageId k : kids(id)) s.push_back(key(k));
  return intern(std::move(s));
}

// Each distinct key once, in the order written.
std::vector<std::uint32_t> TwinReader::branch_keys(detail::ImageId choice) {
  std::vector<std::uint32_t> out;
  for (const detail::ImageId k : kids(choice)) {
    const std::uint32_t key_ = key(k);
    if (std::ranges::find(out, key_) == out.end()) out.push_back(key_);
  }
  return out;
}

detail::ImageId TwinReader::expanded(std::uint32_t rule) {
  if (!_dataRules.contains(rule)) analyse_calls(rule);
  const DataRule& data = _dataRules.at(rule);
  return data.cyclic ? detail::kNoImage : data.body;
}

// Discover each reachable rule and expression once, then settle their cycles
// together. Bodies are lazy and may grow the image. A previous batch already
// explored its whole closure, so an edge into it cannot return to this batch.
// Expressions have vertices too: shared bodies are never scanned per caller.
void TwinReader::analyse_calls(std::uint32_t rule) {
  struct Vertex {
    std::uint64_t key;
    detail::ImageId body = detail::kNoImage;
  };
  const std::uint64_t root = std::uint64_t{rule} << 1 | 1u;
  std::unordered_map<std::uint64_t, std::uint32_t> ids{{root, 0}};
  std::vector<Vertex> vertices{{root}};
  std::vector<std::uint32_t> starts{0}, edges;
  const auto enter = [&](std::uint32_t id, bool isRule) {
    if (isRule ? _dataRules.contains(id)
               : id < _analysedNodes.size() && _analysedNodes[id])
      return;
    const std::uint64_t key = std::uint64_t{id} << 1 | (isRule ? 1u : 0u);
    const auto [at, fresh] =
        ids.try_emplace(key, static_cast<std::uint32_t>(vertices.size()));
    if (fresh) vertices.push_back({key});
    edges.push_back(at->second);
  };
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const std::uint64_t key = vertices[i].key;
    const auto id = static_cast<std::uint32_t>(key >> 1);
    if ((key & 1u) != 0) {
      const detail::ImageId body = _dataBody(id);
      vertices[i].body = body;
      // Keep answers only after the whole batch is discovered: another rule
      // in this batch may lead back to this vertex.
      if (body != detail::kNoImage) enter(body, false);
    } else {
      const detail::ImageNode n = _im.nodes[id];
      for (std::uint32_t k = 0; k < n.count; ++k)
        enter(_im.kids[n.first + k], false);
      if (n.kind == detail::ImageKind::ref ||
          n.kind == detail::ImageKind::closed)
        enter(n.a, true);
    }
    starts.push_back(static_cast<std::uint32_t>(edges.size()));
  }
  const auto components = StrongComponents<>::build(starts, edges);
  _analysedNodes.resize(_im.nodes.size());
  for (std::size_t i = 0; i < vertices.size(); ++i) {
    const auto id = static_cast<std::uint32_t>(vertices[i].key >> 1);
    if ((vertices[i].key & 1u) != 0)
      _dataRules.emplace(id, DataRule{vertices[i].body, components.cyclic[i]});
    else
      _analysedNodes[id] = true;
  }
}

// The positions `id` reads in a row: writes and prose drop away, sequences
// and calls read in place open, and a choice whose branches all read alike is
// that branch.
void TwinReader::flatten(detail::ImageId id, std::vector<Item>& out) {
  const detail::ImageNode n = _im.nodes[id];
  if (reads_operand(n.kind)) return flatten(kid(id, 0), out);
  switch (n.kind) {
    case detail::ImageKind::epsilon:
      return;
    case detail::ImageKind::seq:
      for (const detail::ImageId k : kids(id)) flatten(k, out);
      return;
    case detail::ImageKind::ref:
      if (const std::vector<Item>* body = called(n.a); body != nullptr) {
        out.insert(out.end(), body->begin(), body->end());
        return;
      }
      break;
    case detail::ImageKind::choice:
      if (branch_keys(id).size() == 1) {
        flatten(kid(id, 0), out);
        return;
      }
      break;
    default:
      break;
  }
  out.push_back({key(id), id, optional(id) ? kid(id, 0) : detail::kNoImage});
}

// The positions a call reads in place, or null where it is compared whole.
const std::vector<TwinReader::Item>* TwinReader::called(std::uint32_t rule) {
  if (const auto it = _calls.find(rule); it != _calls.end())
    return it->second.empty() ? nullptr : &it->second;
  std::vector<Item> items;
  if (const detail::ImageId body = expanded(rule); body != detail::kNoImage) {
    flatten(body, items);
    if (items.size() > kCallItems) items.clear();
  }
  const auto& at = _calls.try_emplace(rule, std::move(items)).first->second;
  return at.empty() ? nullptr : &at;
}

// Whether a position reads a token: one that only tests the text, or the end
// of it, reads none.
bool TwinReader::reads(const std::vector<Item>& items) const {
  return std::ranges::any_of(
      items, [&](const Item& i) { return !is_condition_node(_im, i.node); });
}

// What a row of positions reads, an optional one taken and left. Past the
// gate an optional position is compared as written.
std::vector<TwinReader::Reading> TwinReader::readings(
    const std::vector<Item>& items) {
  std::vector<std::size_t> open;
  for (std::size_t i = 0; i < items.size() && open.size() < kOptionalGate; ++i)
    if (items[i].operand != detail::kNoImage) open.push_back(i);
  std::vector<std::vector<Item>> taken(open.size());
  for (std::size_t j = 0; j < open.size(); ++j)
    flatten(items[open[j]].operand, taken[j]);
  std::vector<Reading> out;
  for (unsigned mask = 0; mask < (1u << open.size()); ++mask) {
    Reading r;
    for (std::size_t i = 0, j = 0; i < items.size(); ++i) {
      if (j < open.size() && open[j] == i) {
        if (((mask >> j) & 1u) != 0)
          for (const Item& t : taken[j]) r.push_back(t.key);
        ++j;
        continue;
      }
      r.push_back(items[i].key);
    }
    out.push_back(std::move(r));
  }
  return out;
}

bool TwinReader::optional(detail::ImageId id) const {
  const detail::ImageNode& n = _im.nodes[id];
  return n.kind == detail::ImageKind::repeat && n.m == 0 && n.n == 1 &&
         n.count == 1;
}

// Whether a fold of a left recursion stands in the expression: the election
// ranks its arms by an order of its own, so the reader leaves them as laid.
bool TwinReader::folds(detail::ImageId id) {
  if (const auto it = _folds.find(id); it != _folds.end()) return it->second;
  const detail::ImageKind kind = _im.nodes[id].kind;
  bool out =
      kind == detail::ImageKind::closed || kind == detail::ImageKind::turns;
  for (const detail::ImageId k : kids(id)) out = out || folds(k);
  _folds.emplace(id, out);
  return out;
}

// Two optional items whose operands read the same tokens.
bool TwinReader::alike(detail::ImageId a, detail::ImageId b) {
  if (key(kid(a, 0)) != key(kid(b, 0)) || folds(kid(a, 0))) return false;
  std::vector<Item> items;
  flatten(kid(a, 0), items);
  return reads(items);
}

detail::ImageId TwinReader::drop(detail::ImageId body,
                                 std::vector<TwinFound>* found) {
  _found = found;
  const detail::ImageId out = drop_node(body);
  _found = nullptr;
  return out;
}

// Conditions, calls, regions of another grammar and the folds a lowering
// makes are read as written.
detail::ImageId TwinReader::drop_node(detail::ImageId id) {
  if (const auto it = _dropped.find(id); it != _dropped.end())
    return it->second;
  const detail::ImageNode n = _im.nodes[id];
  detail::ImageId out = id;
  switch (n.kind) {
    case detail::ImageKind::choice:
      out = drop_choice(id);
      break;
    case detail::ImageKind::seq:
      out = drop_sequence(id);
      break;
    case detail::ImageKind::repeat:
    case detail::ImageKind::unordered:
    case detail::ImageKind::skip:
    case detail::ImageKind::doc:
    case detail::ImageKind::assign:
    case detail::ImageKind::append:
    case detail::ImageKind::flag:
    case detail::ImageKind::create:
    case detail::ImageKind::nest: {
      const std::vector<detail::ImageId> was = kids(id);
      std::vector<detail::ImageId> now;
      for (const detail::ImageId k : was) now.push_back(drop_node(k));
      if (now != was) out = _im.derive(n, now);
      break;
    }
    default:
      break;
  }
  _dropped.emplace(id, out);
  return out;
}

// A branch is dropped where each of its readings is a reading of an earlier
// branch. A branch that reads no token, as `eof` alone, stays, and so do the
// branches of a choice that holds a fold.
detail::ImageId TwinReader::drop_choice(detail::ImageId id) {
  const detail::ImageNode n = _im.nodes[id];
  const std::vector<detail::ImageId> was = kids(id);
  const bool folded =
      std::ranges::any_of(was, [&](detail::ImageId k) { return folds(k); });
  std::map<Reading, std::uint32_t> first;
  std::vector<detail::ImageId> kept;
  std::vector<TwinFound> twins;
  for (std::uint32_t i = 0; i < was.size(); ++i) {
    std::vector<Item> items;
    flatten(was[i], items);
    const std::vector<Reading> rs = readings(items);
    TwinFound twin{.at = id, .arm = i, .over = {}, .item = false};
    bool repeats = i != 0 && !folded && reads(items);
    for (std::size_t r = 0; r < rs.size() && repeats; ++r) {
      const auto it = first.find(rs[r]);
      repeats = it != first.end();
      if (repeats) twin.over.push_back(it->second);
    }
    for (const Reading& r : rs) first.try_emplace(r, i);
    if (!repeats) {
      kept.push_back(was[i]);
      continue;
    }
    std::ranges::sort(twin.over);
    twin.over.erase(std::ranges::unique(twin.over).begin(), twin.over.end());
    twins.push_back(std::move(twin));
  }
  if (_found != nullptr)
    _found->insert(_found->end(), twins.begin(), twins.end());
  bool changed = !twins.empty();
  for (detail::ImageId& k : kept) {
    const detail::ImageId d = drop_node(k);
    changed = changed || d != k;
    k = d;
  }
  if (!twins.empty() && kept.size() == 1) return kept[0];
  return changed ? _im.derive(n, kept) : id;
}

// A run of optional items, two of which read alike, is read by its first item
// present from the first of those on.
detail::ImageId TwinReader::drop_sequence(detail::ImageId id) {
  const detail::ImageNode n = _im.nodes[id];
  const std::vector<detail::ImageId> was = kids(id);
  std::vector<detail::ImageId> now;
  for (const detail::ImageId k : was) now.push_back(drop_node(k));
  std::vector<detail::ImageId> out;
  for (std::size_t s = 0; s < was.size();) {
    std::size_t e = s;
    while (e < was.size() && optional(was[e])) ++e;
    if (e == s) {
      out.push_back(now[s++]);
      continue;
    }
    const auto twinned = [&](std::size_t a) {
      for (std::size_t b = a + 1; b < e; ++b)
        if (alike(was[a], was[b])) return true;
      return false;
    };
    std::size_t i = s;
    while (i < e && !twinned(i)) ++i;
    out.insert(out.end(), now.begin() + static_cast<std::ptrdiff_t>(s),
               now.begin() + static_cast<std::ptrdiff_t>(i));
    if (i < e && _runs < kRunDepth) {
      for (std::size_t j = i + 1; _found != nullptr && j < e; ++j)
        for (std::size_t t = i; t < j; ++t)
          if (alike(was[t], was[j])) {
            _found->push_back({.at = id,
                               .arm = static_cast<std::uint32_t>(j),
                               .over = {static_cast<std::uint32_t>(t)},
                               .item = true});
            break;
          }
      out.push_back(
          first_present({now.begin() + static_cast<std::ptrdiff_t>(i),
                         now.begin() + static_cast<std::ptrdiff_t>(e)}));
    } else {
      out.insert(out.end(), now.begin() + static_cast<std::ptrdiff_t>(i),
                 now.begin() + static_cast<std::ptrdiff_t>(e));
    }
    s = e;
  }
  return out == was ? id : _im.derive(n, out);
}

// `x? y? z?` is `(x y? z? | y z? | z)?`: each branch opens on its own first
// item present, so one that reads only what an earlier one reads is dropped
// as any other twin branch, and the readings that kept it go with it. What
// this spells is read again without a word: the warning names the run.
detail::ImageId TwinReader::first_present(
    const std::vector<detail::ImageId>& run) {
  std::vector<detail::ImageId> arms;
  for (std::size_t k = 0; k < run.size(); ++k) {
    std::vector parts{kid(run[k], 0)};
    parts.insert(parts.end(), run.begin() + static_cast<std::ptrdiff_t>(k + 1),
                 run.end());
    arms.push_back(
        parts.size() == 1
            ? parts[0]
            : _im.derive(detail::ImageNode{.kind = detail::ImageKind::seq},
                         parts));
  }
  const detail::ImageId choice =
      _im.derive(detail::ImageNode{.kind = detail::ImageKind::choice}, arms);
  const detail::ImageId option = _im.derive(
      detail::ImageNode{.kind = detail::ImageKind::repeat, .m = 0, .n = 1},
      std::span(&choice, 1));
  std::vector<TwinFound>* const found = std::exchange(_found, nullptr);
  ++_runs;
  const detail::ImageId out = drop_node(option);
  --_runs;
  _found = found;
  return out;
}

std::vector<TwinBranch> twin_branches(WarningFound& found) {
  detail::GrammarImage& image = found.image();
  TwinReader reader = TwinReader::resolved(image);
  std::vector<TwinBranch> out;
  std::vector<TwinFound> hits;
  for (const std::uint32_t rule : found.rules()) {
    hits.clear();
    reader.drop(found.body(rule), &hits);
    for (const TwinFound& hit : hits) {
      const detail::ImageNode at = image.nodes[hit.at];
      const auto part = [&](std::uint32_t i) {
        return image.kids[at.first + i];
      };
      TwinBranch t;
      t.rule = found.name(rule);
      t.branch = found.spell(part(hit.arm));
      if (hit.item) {
        t.earlier = found.spell(part(hit.over.front()));
      } else {
        const std::vector<std::uint16_t> places = printed_places(image, at);
        t.arm = places[hit.arm];
        t.arms = places.back();
        for (const std::uint32_t o : hit.over)
          if (std::ranges::find(t.over, places[o]) == t.over.end())
            t.over.push_back(places[o]);
      }
      if (std::ranges::find(out, t) == out.end()) out.push_back(std::move(t));
    }
  }
  return out;
}

std::string twin_branch_text(const TwinBranch& t) {
  std::string out = "rule `" + t.rule + "`: ";
  if (t.arms == 0)
    return out + '`' + t.branch +
           "` matches the same tokens as the earlier `" + t.earlier +
           "`, so it is never chosen in place of that item, and that choice "
           "is dropped -- merge the two items, or nest the second in the "
           "first, as `option(a + option(b))`";
  out += "branch " + detail::decimal(t.arm) + " of " + detail::decimal(t.arms) +
         " `" + t.branch + "` matches the same tokens as ";
  out += t.over.size() == 1 ? "branch " : "branches ";
  for (std::size_t i = 0; i < t.over.size(); ++i) {
    if (i != 0) out += i + 1 == t.over.size() ? " and " : ", ";
    out += detail::decimal(t.over[i]);
  }
  return out +
         ", so it is never chosen and is dropped -- merge the branches, or "
         "reorder them";
}

}  // namespace cursiv
