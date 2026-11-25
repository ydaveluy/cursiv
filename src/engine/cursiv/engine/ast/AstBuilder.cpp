#include "cursiv/engine/ast/AstBuilder.hpp"

#include <algorithm>
#include <functional>
#include <iterator>
#include <new>
#include <utility>

#include "cursiv/engine/parser/Language.hpp"
#include "cursiv/engine/parser/Machine.hpp"
#include "cursiv/syntax-tree/AstBuildError.hpp"
#include "cursiv/syntax-tree/ParseDiagnostic.hpp"

namespace cursiv::engine {

namespace {

// Thrown at the record that shows a tape these tables did not write.
struct BrokenTape {
  NodeId id;
};

}  // namespace

struct AstBuilder::Reading {
  CstRun unused;
  Machine machine;
  Reading(const Tables& tables, std::string_view source, const CancellationToken& stop)
      : machine(tables, source, unused, stop) {}
};

AstBuilder::AstBuilder(const AstTables& tables, ParseResult& result, const CancellationToken& cancelToken)
    : tables(tables),
      out(result, *tables.meta),
      cst(out.cst()),
      repaired(cst.recovered()),
      stop(cancelToken) {
  _small[0] = {kNoNode, kNoAction, detail::kNoIndex, nullptr};
}

bool AstBuilder::invented(NodeId id) const {
  const auto first = std::ranges::lower_bound(_invented, cst.subtreeBegin(id));
  return first != _invented.end() && *first <= id;
}

AstBuilder::~AstBuilder() {
  if (_base != _small) ::operator delete(_base);
}

void AstBuilder::grow() {
  const auto depth = static_cast<std::size_t>(_top - _base);
  auto* const wider = static_cast<Pending*>(::operator new(2 * depth * sizeof(Pending)));
  std::copy_n(_base, depth, wider);
  if (_base != _small) ::operator delete(_base);
  _base = wider;
  _top = wider + depth;
  _limit = wider + 2 * depth;
}

void AstBuilder::broken(NodeId id) const { throw BrokenTape{id}; }

ParseResult AstBuilder::build(const AstTables& tables, Cst&& tree,
                              const CancellationToken& cancelToken) {
  throwIfCancelled(cancelToken);
  ParseResult result(std::move(tree));
  {
    AstBuilder self(tables, result, cancelToken);
    const Cst& cst = self.cst;
    if (!cst.hasEntry()) {
      // A parse that reported its own faults has said why no tree stands.
      if (cst.diagnostics().empty()) {
        ParseDiagnostic nothing;
        nothing.kind = DiagnosticKind::ConversionError;
        nothing.message = "the entry rule matched nothing to build from";
        self.out.report(std::move(nothing));
      }
      self.out.finish(nullptr, AstBuildError::NoEntry);
      return result;
    }
    const NodeId root = cst.entryRoot();
    AstNode* value = nullptr;
    AstBuildError error = AstBuildError::None;
    try {
      NodeId first = cst.subtreeBegin(root);
      // An extent that reaches past the tape's start wraps above the root.
      if (first > root) [[unlikely]] self.broken(root);
      const auto count = root - first + 1;
      // Sample more of a large tree before projecting its node and reference counts.
      const auto sample = std::min(count, std::clamp<NodeCount>(count / 32, 256, kStopRecords));
      self.out.reserve(sample / 4, sample / 16);
      if (count > sample) {
        self.run(first, first + sample - 1);
        const auto project = [count, sample](std::uint64_t made) {
          return (made + made / 8) * count / sample;
        };
        self.out.reserve(project(result.nodes().size()),
                         result.references ? project(result.references->handles.size()) : 0);
        first += sample;
      }
      self.run(first, root);
      // The entry's node is the one value left.
      for (const Pending* left = self._base + 1; left != self._top; ++left)
        if (left->action == kNoAction && left->node != nullptr) value = left->node;
      if (value == nullptr) self.broken(root);
      if (!cst.accepted()) error = AstBuildError::NotAccepted;
    } catch (const BrokenTape& tape) {
      error = AstBuildError::MalformedCst;
      self.fail("the AST could not be materialised from this node", tape.id);
    }
    self.out.finish(value, error);
  }
  throwIfCancelled(cancelToken);
  return result;
}

NodeId AstBuilder::payload_below(NodeId id) const {
  NodeId only = kNoNode;
  for (const NodeId child : cst.reverseChildren(id)) {
    // Trivia and text a repair skipped are no value.
    if (cst[child].element() >= kSkippedElement) continue;
    if (only != kNoNode) return id;
    only = child;
  }
  return only != kNoNode ? only : id;
}

std::size_t AstBuilder::waiting(const Pending& item) const {
  const Pending* at = &item;
  while (at != _top && at->member == item.member) ++at;
  return static_cast<std::size_t>(at - &item);
}

bool AstBuilder::parts(NodeId id, std::span<const std::string_view>& out) {
  _parts.clear();
  _gathering.clear();
  const auto texts = tables.texts;
  // A rule read inside whose conversion takes no input has no parts.
  const auto open = [&](NodeId record, bool reads, bool grouped) {
    const CstNode& node = cst[record];
    _gathering.push_back({record, record - (node.subtreeSize - 1),
                          reads ? node.subtreeSize - 1 : 0, grouped, _parts.size()});
    if (reads && node.subtreeSize == 1 && node.begin != node.end) _parts.push_back(cst.text(node));
  };
  open(id, true, true);
  for (NodeCount steps = 0;; ++steps) {
    if (steps == kStopRecords) [[unlikely]] {
      throwIfCancelled(stop);
      steps = 0;
    }
    Gathering& at = _gathering.back();
    if (at.left == 0) {
      // Its parts are whole, the later first.
      auto first = _parts.begin() + static_cast<std::ptrdiff_t>(at.base);
      auto last = _parts.end();
      while (static_cast<std::size_t>(last - first) > 2 * kStopRecords) {
        std::swap_ranges(first, first + kStopRecords, std::make_reverse_iterator(last));
        first += kStopRecords;
        last -= kStopRecords;
        throwIfCancelled(stop);
      }
      std::reverse(first, last);
      if (_gathering.size() == 1) break;
      // A rule read inside another is one part of it: converted where its
      // element converts text, else its own parts joined.
      const NodeId record = at.record;
      const std::size_t base = at.base;
      const std::span<const std::string_view> own = std::span(_parts).subspan(base);
      std::string_view text;
      if (const AstText convert = texts ? texts[element(cst[record])] : AstText{};
          convert.convert != nullptr) {
        if (!convert.convert(*this, record, own, text)) return false;
      } else {
        text = join(own);
      }
      _gathering.pop_back();
      _parts.resize(base);
      _parts.push_back(text);
      continue;
    }
    const NodeId record = at.begin + at.left - 1;
    const CstNode& node = cst[record];
    // Trivia and text a repair skipped are no part.
    if (node.element() >= kSkippedElement) {
      --at.left;
      continue;
    }
    // Under a record whose text is read, a record with an element is a
    // token or the call of a rule: a call is one part, even one that read
    // nothing, and so is a token its terminal converts.
    const std::uint32_t named = element(node);
    const bool call = named != tables.elementCount && (node.subtreeSize != 1 || node.begin == node.end);
    const AstText text = texts ? texts[named] : AstText{};
    const bool converts = text.convert != nullptr && !text.joined;
    if (converts || (at.grouped && call)) {
      at.left -= node.subtreeSize;
      open(record, text.reads, converts);
      continue;
    }
    if (node.subtreeSize == 1 && node.begin != node.end) _parts.push_back(cst.text(node));
    --at.left;
  }
  out = _parts;
  return true;
}

std::string_view AstBuilder::join(std::span<const std::string_view> parts) const {
  if (parts.empty()) return {};
  while (!parts.empty() && parts.front().empty()) parts = parts.subspan(1);
  if (parts.empty()) return "";
  while (parts.back().empty()) parts = parts.first(parts.size() - 1);
  if (parts.size() == 1) return parts.front();
  const auto source = cst.text();
  const char* first = parts.front().data();
  const char* end = source.data() + source.size();
  if (std::less{}(first, source.data()) || !std::less{}(first, end)) return out.keepText(parts);
  auto tail = source.substr(static_cast<std::size_t>(first - source.data()));
  for (const auto part : parts) {
    if (part.empty()) continue;
    if (part.data() != tail.data() || part.size() > tail.size()) return out.keepText(parts);
    tail.remove_prefix(part.size());
  }
  return {first, static_cast<std::size_t>(tail.data() - first)};
}

bool AstBuilder::converted(NodeId id, std::string_view& out) {
  if (repaired && invented(id)) return false;
  const AstText convert = tables.texts[element(cst[id])];
  std::span<const std::string_view> all;
  return (!convert.reads || parts(id, all)) && convert.convert(*this, id, all, out);
}

bool AstBuilder::value(NodeId id, const char* type, void* out) {
  if (repaired && invented(id)) return false;
  for (;;) {
    if (const AstValue* const held =
            tables.values ? &tables.values[element(cst[id])] : nullptr;
        held != nullptr && held->type == type)
      return held->read(*this, id, out);
    const NodeId below = payload(id);
    if (below == id) {
      fail("this text gives no value of the type its member takes", id);
      return false;
    }
    id = below;
  }
}

bool AstBuilder::folds(NodeId id) {
  if (tables.grammar == nullptr) return false;
  const GrammarFacts& g = *tables.grammar;
  bool read = false;
  for (NodeId at = cst.subtreeBegin(id); at <= id; ++at) {
    const CstNode& node = cst[at];
    if (node.subtreeSize != 1 || node.element() >= kSkippedElement || node.begin == node.end)
      continue;
    if (node.element() >= g.elementCount) return false;
    const auto fold = g.elements[node.element()].fold;
    if (!fold) return false;
    if (fold > 1) {
      const auto text = cst.text(at);
      const auto words = std::ranges::equal_range(g.foldedSets[fold - 2], GrammarWord::key(text),
          {}, [](const FoldedWord& word) { return GrammarWord::key(word.text); });
      if (std::ranges::none_of(words, [&](const FoldedWord& word) {
            if (!foldedEqual(word.text, text)) return false;
            if (word.guard == FoldedWord::kPlain) return true;
            if (word.guard == FoldedWord::kNever) return false;
            const auto end = cst.text().data() + node.end;
            if (word.guard == FoldedWord::kWord)
              return !g.lexer->word_width(end, cst.text().data() + cst.text().size());
            if (!_reading) _reading = std::make_unique<Reading>(*g.lexer, cst.text(), stop);
            auto& m = _reading->machine;
            if (word.guard & FoldedWord::kMatcher)
              return m.match(word.guard ^ FoldedWord::kMatcher, m.src + node.begin) == end;
            return m.settles(word.guard, end);
          })) return false;
    }
    read = true;
  }
  return read;
}

bool AstBuilder::joined(NodeId id, std::string_view& out) {
  const CstNode& top = cst[id];
  // Tokens that touch are one stretch of the source: the stretches are
  // gathered in one reading, and one alone is given as it stands.
  const std::string_view text = cst.text();
  _parts.clear();
  TextOffset begin = 0;
  TextOffset end = 0;
  for (const CstNode* node = &top - (top.subtreeSize - 1); node <= &top; ++node) {
    if (node->element() >= kSkippedElement || node->subtreeSize != 1) continue;
    if (node->begin == node->end) {
      if (repaired && node->isRecovered()) return false;
      continue;
    }
    if (node->begin != end) {
      if (end != begin) _parts.push_back(text.substr(begin, end - begin));
      begin = node->begin;
    }
    end = node->end;
  }
  const std::string_view last = text.substr(begin, end - begin);
  if (_parts.empty()) {
    out = end != begin ? last : std::string_view{};
  } else {
    _parts.push_back(last);
    out = this->out.keepText(_parts);
  }
  return true;
}

void AstBuilder::fail(std::string message, NodeId at) {
  ParseDiagnostic d;
  d.kind = DiagnosticKind::ConversionError;
  d.message = std::move(message);
  if (at != kNoNode) {
    d.begin = cst[at].begin;
    d.end = cst[at].end;
  }
  out.report(std::move(d));
}

void AstBuilder::run(NodeId first, NodeId root) {
  const DeferredWrite* const deferred = tables.deferred;
  const AstRecord* const records = tables.records;
  const std::uint32_t writes = tables.writeCount;
  const std::uint32_t elements = tables.recordCount;
  NodeId id = first;
  const CstNode* node = &cst[id];
  for (; id <= root; throwIfCancelled(stop)) {
    const NodeId last = root - id < kStopRecords ? root : id + (kStopRecords - 1);
    if (repaired) {
      for (NodeId at = id; at <= last; ++at) {
        const CstNode& n = cst[at];
        if (n.isRecovered() && n.begin == n.end && n.subtreeSize == 1 &&
            n.element() != kSkippedElement)
          _invented.push_back(at);
      }
    }
    for (; id <= last; ++id, ++node) {
      // A record with no action names no element, and trivia stands past
      // every element: one test leaves both out.
      const std::uint32_t element = node->element();
      if (element < writes) {
        const auto [action, member] = deferred[element];
        push({id, action, member, nullptr});
      } else if (element < elements) {
        const AstRecord& record = records[element - writes];
        record.run(*this, id, *node, record.write);
      }
    }
  }
}

AstNode* AstBuilder::adopt(const AstTypeTag* type, Pending* first, NodeId id) {
  AstNode* owner = nullptr;
  for (Pending* item = first; item != _top && owner == nullptr; ++item) {
    if (item->action != kNoAction || item->node == nullptr) continue;
    if (!item->node->type->derivesFrom(type)) continue;
    owner = std::exchange(item->node, nullptr);
  }
  if (owner == nullptr) broken(id);
  // The node spans what its rule read.
  const auto element = cst[owner->cstNodeId].element();
  const auto* facts = tables.grammar;
  const bool filled = facts && element < facts->elementCount &&
                      facts->elements[element].fills != facts->elements[element].end;
  out.moveNode(*owner, id, filled);
  return owner;
}

AstNode* AstBuilder::wrapped(Pending* first, Pending* end, NodeId id) {
  AstNode* inner = nullptr;
  for (Pending* item = first; item != end && inner == nullptr; ++item)
    if (item->action == kNoAction && item->node != nullptr)
      inner = std::exchange(item->node, nullptr);
  if (inner == nullptr) broken(id);
  for (const Pending* item = first; item != end; ++item)
    if (item->action != kNoAction && !tables.writes[item->action](*this, inner, *item))
      broken(item->record);
  return inner;
}

AstNode* AstBuilder::take(bool (*takes)(const AstTypeTag*) noexcept, NodeId id,
                          const CstNode& node) {
  for (Pending* item = inside(id, node); item != _top; ++item) {
    if (item->action != kNoAction || item->node == nullptr) continue;
    if (!takes(item->node->type)) continue;
    return std::exchange(item->node, nullptr);
  }
  return nullptr;
}

}  // namespace cursiv::engine
