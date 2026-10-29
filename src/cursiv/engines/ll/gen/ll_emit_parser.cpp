// The descent as source: one step per rule, the program's shape spelled
// straight out. A choice one token separates is a switch on that token; every
// other one asks the prediction. A callee that can finish without a call of
// its own runs in place, its frame on the native stack; any other call goes to
// the machine, which runs it and resumes the step at a label. A rule that
// reaches no climb and no rule that calls itself never resumes: it is a plain
// function with no frame, unless too many of them would stand on one another.
#include "cursiv/engines/ll/gen/ll_emit_parser.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cursiv/engines/ll/gen/LlFinalProgram.hpp"
#include "cursiv/engines/ll/gen/ll_emit_walk.hpp"
#include "cursiv/engines/shared/gen/EmitOutput.hpp"
#include "cursiv/engines/shared/gen/LowerVerdicts.hpp"
#include "cursiv/engines/shared/table/LexNode.hpp"

namespace cursiv {
namespace {

using Built = LlBuiltProgram;

// Where a record opened: the names of the tape cursor, the text offset and the
// record count taken there. Empty while none stands.
struct Anchor {
  std::string open{}, begin{}, count{};
  bool live() const { return !count.empty(); }
};

// A slot holds `value` for a scope and gives back what it held; with no
// value it only gives back what the scope leaves in it.
class Anchored {
 public:
  explicit Anchored(Anchor& slot) : _slot(slot), _outer(slot) {}
  Anchored(Anchor& slot, Anchor value) : _slot(slot), _outer(std::move(slot)) {
    _slot = std::move(value);
  }
  ~Anchored() { _slot = std::move(_outer); }
  Anchored(const Anchored&) = delete;
  Anchored& operator=(const Anchored&) = delete;

 private:
  Anchor& _slot;
  Anchor _outer;
};

// Shared by every rule's emitter: which rules are plain, whether a position
// may suspend and the first mode it reads do not depend on the rule. A search
// stamps the rules it visits by round, so the next one starts clean without
// clearing.
struct Memos {
  std::vector<std::uint8_t> plain{};
  std::vector<std::int8_t> suspends{};
  std::vector<std::int8_t> clean{};
  std::vector<std::uint32_t> visited{};
  std::uint32_t round = 0;
};

struct Emit : EmitOutput {
  const Built& a;
  const LlStampPlan& plan;
  Memos& memo;
  std::uint16_t inRule = 0;
  unsigned ind = 2;
  unsigned uniq = 0;
  // A level opens or closes in no byte: a turn moves the cursor or a level.
  bool levels = false;
  Anchor foldAt{};
  // The record a nest turn reaches back to: the sequence it stands in, with
  // the tape length where that sequence opened. A group whose position is
  // such a sequence hands its own opening over.
  Anchor regionAt{};
  std::uint16_t ownRegion = kNoRow;
  Anchor ownAt{};
  bool inTurn = false;
  // The body of a fold step: past its first reading, a nest wraps what the
  // fold has read.
  bool inStep = false;
  // What outlives a call goes in the rule's frame; the calls are numbered by
  // the label each resumes at.
  std::vector<std::pair<std::string, std::string>> fields{};
  // The fields the open scopes hold, and the ones closed scopes gave back. A
  // field is named only inside the scope that took it, so a later scope may
  // take it again: positions that are never live at once share frame bytes.
  std::vector<std::size_t> inUse{}, spare{};
  unsigned resumes = 0;
  // A counted tail of nest turns is spelt one turn inside the other, a block
  // deeper each: past this many the source outgrows the nesting a compiler
  // must take, and the walk that writes it its stack.
  static constexpr unsigned kMaxNestedTurns = 64;
  unsigned nestedTurns = 0;
  bool exits = false;
  std::string frame{};

  void over_fold() {
    if (foldAt.live()) regionAt = foldAt;
  }

  // A spare field of the type if one is left, a new one otherwise.
  std::string field(std::string_view type, std::string_view stem) {
    for (std::size_t k = spare.size(); k-- > 0;)
      if (fields[spare[k]].first == type) {
        inUse.push_back(spare[k]);
        spare.erase(spare.begin() + static_cast<std::ptrdiff_t>(k));
        return "f." + fields[inUse.back()].second;
      }
    inUse.push_back(fields.size());
    fields.emplace_back(std::string(type), name(stem));
    return "f." + fields.back().second;
  }
  // A field where the scope may suspend, a local otherwise.
  std::string local(bool held, std::string_view type, std::string_view stem) {
    return held ? field(type, stem) : name(stem);
  }

  // Open while one position's code is written: the fields taken meanwhile go
  // back to the pool when it closes.
  class FieldScope {
   public:
    explicit FieldScope(Emit& e) : _e(e), _mark(e.inUse.size()) {}
    ~FieldScope() {
      for (std::size_t k = _mark; k < _e.inUse.size(); ++k)
        _e.spare.push_back(_e.inUse[k]);
      _e.inUse.resize(_mark);
    }
    FieldScope(const FieldScope&) = delete;
    FieldScope& operator=(const FieldScope&) = delete;

   private:
    Emit& _e;
    std::size_t _mark;
  };

  // Past this depth a block deepens the code and not its indentation, whose
  // bytes would grow with the square of a nest's depth.
  static constexpr unsigned kMaxIndent = 48;
  void pad() { out.append(std::min(ind, kMaxIndent), ' '); }
  // A block, with the line that opens it and the one that closes it, if any.
  void open(std::string_view t = {}) {
    if (!t.empty()) line(t);
    ind += 2;
  }
  void close(std::string_view t = {}) {
    if (ind < 2)
      throw std::logic_error("cursiv: unbalanced LL source indentation");
    ind -= 2;
    if (!t.empty()) line(t);
  }
  void line(std::string_view t) {
    pad();
    EmitOutput::line(t);
  }
  template <class... A>
  void put(std::format_string<A...> f, A&&... x) {
    pad();
    fmt(f, std::forward<A>(x)...);
    out += '\n';
  }
  std::string name(std::string_view stem) {
    return std::format("{}{}", stem, uniq++);
  }

  const LlNode& at(std::uint16_t id) const { return a.cfg.nodes[id]; }
  std::uint16_t kid(const LlNode& n, unsigned i) const {
    return a.cfg.kids[n.sym + i];
  }

  std::uint16_t first_mode(std::uint16_t id) const;
  std::uint16_t rule_mode(std::uint16_t r) const;
  void new_search() const {
    if (memo.visited.size() != a.cfg.nrules)
      memo.visited.assign(a.cfg.nrules, 0);
    ++memo.round;
  }
  std::uint16_t mode_of(std::uint16_t id) const {
    new_search();
    const std::uint16_t m = first_mode(id);
    return m == kNoRow ? a.errMode : m;
  }

  // A token already read, and what the point that read it proved about it:
  // `kNoRow` for neither, `kAnyToken` for one in hand the switch did not
  // pin down. The descent reads once per point, as the table engine does --
  // an arm that re-lexed the token its own choice had just read would answer
  // a second question nobody asked.
  static constexpr std::uint16_t kAnyToken = 0xFFFEu;
  void node(std::uint16_t id, std::uint16_t hand = kNoRow);
  void leaf(std::uint16_t id, std::uint16_t hand, std::uint16_t row);
  void group(std::uint16_t id, std::uint16_t hand = kNoRow);
  void decision(std::uint16_t id);
  void sequence(std::uint16_t id, std::uint16_t hand);
  void repetition(std::uint16_t id, std::uint16_t body = kNoRow);
  bool ladder_flat(std::uint16_t id) const;
  void ladder(std::uint16_t id);
  void call_site(std::uint16_t id);
  bool may_suspend(std::uint16_t id);
  bool turns(std::uint16_t id) const { return plan.turns(id); }
  bool head_turns(std::uint16_t id) const { return plan.head_turns(id); }
  bool reaches_back(std::uint16_t id) const {
    return !inTurn && plan.reaches_back(id);
  }
  // A rule that opens no frame and never resumes.
  bool plain(std::uint16_t r) const { return memo.plain[r] != 0; }
  bool clean(std::uint16_t id);
  bool finishes(std::uint16_t r);
  void invoke(std::uint16_t callee, std::string_view arg);
  void climb(std::uint16_t r);
  void rule(std::uint16_t r);
  void join(std::uint32_t base, std::uint32_t count, std::string_view from);
};

std::uint16_t Emit::rule_mode(std::uint16_t r) const {
  if (r >= a.cfg.nrules || memo.visited[r] == memo.round) return kNoRow;
  memo.visited[r] = memo.round;
  const LlRuleInfo& u = a.cfg.rules[r];
  if (u.climb != kNoRow) {
    const LlClimb& cl = a.cfg.climbs[u.climb];
    return cl.primary != kNoRow ? rule_mode(cl.primary)
                                : a.cfg.nodes[cl.primaryNode].mode;
  }
  return u.body == kNoRow ? kNoRow : first_mode(u.body);
}

// The alphabet the first token of this position is read with: a record opened
// over the position begins where that token does, so the peek comes first and
// the trivia in front of it stays outside.
std::uint16_t Emit::first_mode(std::uint16_t id) const {
  const LlNode& n = at(id);
  switch (n.kind) {
    case LlShape::term:
    case LlShape::endMark:
    case LlShape::condition:
    case LlShape::implied:
      return n.mode;
    case LlShape::call:
      return n.mode != kNoRow ? n.mode : rule_mode(n.sym);
    case LlShape::alt:
      return a.cfg.decisions[n.decision].mode;
    case LlShape::rep:
      return a.headMode[n.decision];
    case LlShape::seq: {
      for (unsigned i = 0; i < n.count; ++i) {
        const std::uint16_t m = first_mode(kid(n, i));
        if (m != kNoRow) return m;
      }
      return kNoRow;
    }
    default:
      return kNoRow;
  }
}

void Emit::join(std::uint32_t base, std::uint32_t count,
                std::string_view from) {
  if (!ll_fuse_tabled(count, false)) {
    if (count == 1)
      put("::cursiv::ll_join_one<kTable>(c, {}, {}u);", from, a.fuse[base].to);
    return;
  }
  put("::cursiv::ll_join<kTable>(c, {}, kFuse + {}u, {}u);", from, base, count);
}

void Emit::call_site(std::uint16_t id) {
  const FieldScope scope(*this);
  const LlNode& n = at(id);
  if (n.mode != kNoRow) put("::cursiv::ll_peek<kTable, {}u>(c);", n.mode);
  const std::uint32_t base = a.fuseBase[id];
  const std::uint32_t count = a.fuseBase[id + 1] - base;
  // A section reads its grammar alone, and what its entry left is dropped
  // up to the token that closes it.
  const std::uint8_t section = a.cfg.nodeBuild[id].section;
  // What the site keeps across a plain callee ends with it, as a group's
  // locals do.
  const bool held = may_suspend(id);
  const bool scoped = !held && (count != 0 || section != 0);
  if (scoped) open("{");
  const std::string was =
      count == 0 ? std::string{} : local(held, "::cursiv::NodeId", "w");
  if (count != 0)
    put("{}{} = c.last;", held ? "" : "const ::cursiv::NodeId ", was);
  std::string outer;
  if (section != 0) {
    outer = local(held, "::cursiv::LlSection", "sec");
    put("{}{} = ::cursiv::ll_section_open<kTable>(c, {}u);",
        held ? "" : "const ::cursiv::LlSection ", outer, section);
  }
  put("c.stack.push_back({}u, c.tape.stream().records());", a.nodeFollow[id]);
  invoke(n.sym, "1u");
  line("::cursiv::ll_return(c);");
  if (section != 0) put("::cursiv::ll_section_close<kTable>(c, {});", outer);
  join(base, count, was);
  if (scoped) close("}");
}

// Whether a position calls, anywhere inside, a rule that is not plain: the
// call may leave the step to resume at a label, so a record open across it
// keeps its handles in the frame.
bool Emit::may_suspend(std::uint16_t id) {
  if (memo.suspends.empty()) memo.suspends.assign(a.cfg.nnodes, -1);
  if (memo.suspends[id] >= 0) return memo.suspends[id] != 0;
  const LlNode& n = at(id);
  bool any = n.kind == LlShape::call && !plain(n.sym);
  if (n.kind == LlShape::seq || n.kind == LlShape::alt)
    for (unsigned i = 0; i < n.count; ++i) any = may_suspend(kid(n, i)) || any;
  if (n.kind == LlShape::rep) {
    // A tail of counted options is walked, never descended: it may be as
    // deep as the count.
    std::uint16_t turn = id;
    while (a.cfg.nodeBuild[turn].ladder &&
           a.cfg.nodeBuild[at(turn).sym].ladder) {
      any = may_suspend(kid(at(at(turn).sym), 0)) || any;
      turn = kid(at(at(turn).sym), 1);
    }
    any = may_suspend(at(turn).sym) || any;
  }
  memo.suspends[id] = any ? 1 : 0;
  return any;
}

// Whether a position has a reading that calls no rule with a frame.
bool Emit::clean(std::uint16_t id) {
  if (memo.clean.empty()) memo.clean.assign(a.cfg.nnodes, -1);
  if (memo.clean[id] >= 0) return memo.clean[id] != 0;
  const LlNode& n = at(id);
  bool any = true;
  if (n.kind == LlShape::call) any = plain(n.sym);
  if (n.kind == LlShape::seq)
    for (unsigned i = 0; i < n.count && any; ++i) any = clean(kid(n, i));
  if (n.kind == LlShape::alt) {
    any = false;
    for (unsigned i = 0; i < n.count && !any; ++i) any = clean(kid(n, i));
  }
  if (n.kind == LlShape::rep && n.min != 0) any = clean(n.sym);
  memo.clean[id] = any ? 1 : 0;
  return any;
}

// Whether a rule can finish where its caller runs it: a reading of it calls no
// rule that would hand its frame over. A climb calls its primary first.
bool Emit::finishes(std::uint16_t r) {
  const LlRuleInfo& u = a.cfg.rules[r];
  if (u.climb != kNoRow) {
    const std::uint16_t primary = a.cfg.climbs[u.climb].primary;
    return primary == kNoRow || plain(primary);
  }
  return u.body == kNoRow || clean(u.body);
}

// A callee that can finish in place runs there; one that calls in turn, or
// one that cannot finish there, leaves the step to return the call to the
// machine, its frame saying where it resumes: the machine runs the call, then
// comes back to the label. A local declared before the label and read after it
// is a field of the frame: a jump to the label skips every initialisation in
// front of it. A plain callee is a plain call. The frame names the
// label in sixteen bits: a rule whose labels outrun them is refused.
void Emit::invoke(std::uint16_t callee, std::string_view arg) {
  if (plain(callee)) {
    put("s{}(c);", callee);
    return;
  }
  if (resumes == 0xFFFFu)
    refuse_lowering(std::format("rule `{}` {}", a.cfg.rule_name(inRule),
                                lower_fail_text(LowerFail::tooManyPositions)));
  if (a.cfg.rules[callee].climb != kNoRow) put("c.arg = {};", arg);
  const unsigned k = ++resumes;
  put("f.pc = {}u;", k);
  if (finishes(callee)) {
    put("if (const ::std::uint16_t next = "
        "::cursiv::ll_call<F{0}, &s{0}>(c, {0}u);",
        callee);
    put("    next != ::cursiv::kLlDone)");
    put("  return next;");
  } else {
    put("return {}u;", callee);
  }
  put("L{}:;", k);
}

// A group that owns a record: the token in front of it is read first, so the
// trivia that precedes it lands outside and the record begins where the text
// does.
void Emit::group(std::uint16_t id, std::uint16_t hand) {
  const FieldScope scope(*this);
  const LlNode& n = at(id);
  // An empty position has no token to wait for: it lays its own record.
  if (!n.owns || n.kind == LlShape::empty) {
    node(id, hand);
    return;
  }
  const bool held = may_suspend(id);
  const std::uint32_t ob = a.ownBase[id];
  const std::uint32_t oc = a.ownBase[id + 1] - ob;
  const bool step = a.cfg.nodeBuild[id].foldStep;
  if (step && foldAt.open.empty())
    throw std::logic_error("cursiv: LL fold step has no prefix region");
  // A nest turn's record begins at its receiver, its first child; its
  // position says it has one.
  const bool turn = turns(id) && regionAt.live();
  // With no receiver to wrap, the nest rides its lone terminal.
  if (turns(id) && !turn && a.cfg.nodeBuild[id].leafRow != kNoRow) {
    leaf(kid(n, 0), hand, a.cfg.nodeBuild[id].leafRow);
    return;
  }
  const bool region = !turns(id) && reaches_back(id);
  const bool folds = n.kind == LlShape::seq && n.count == 2 &&
                     a.cfg.nodeBuild[kid(n, 1)].leftFold;
  const bool counted = oc != 0 || region || folds;
  const std::string o = local(held, "::cursiv::CstBuilder::Open", "o");
  const std::string b = local(held, "::cursiv::TextOffset", "b");
  const std::string f = local(held && counted, "::cursiv::NodeCount", "f");
  const std::string l =
      !turn ? std::string("0") : local(held, "::std::uint16_t", "l");
  if (hand == kNoRow) put("::cursiv::ll_peek<kTable, {}u>(c);", mode_of(id));
  line("::cursiv::ll_flush_trivia<kTable>(c);");
  // Its locals end with it: a label further on in the block may not cross
  // their initialisation.
  if (!held) open("{");
  if (turn) {
    put("{}{} = ::cursiv::ll_nest_receiver<kTable>(c, {}, {}, {});",
        held ? "" : "const ::std::uint16_t ", l, regionAt.open, regionAt.begin,
        regionAt.count);
    put("{}{} = {} != 0 ? {} : c.tape.enter();", held ? "" : "const auto ", o,
        l, regionAt.open);
    if (counted)
      put("{}{} = {} != 0 ? {} : c.tape.stream().records();",
          held ? "" : "const ::cursiv::NodeCount ", f, l, regionAt.count);
    put("{}{} = {} != 0 ? {} : c.la.begin;",
        held ? "" : "const ::cursiv::TextOffset ", b, l, regionAt.begin);
  } else {
    put("{}{} = {};", held ? "" : "const auto ", o,
        step ? foldAt.open : "c.tape.enter()");
    // The tape's length is kept only where a ride or a turn may read it back.
    if (counted)
      put("{}{} = c.tape.stream().records();",
          held ? "" : "[[maybe_unused]] const ::cursiv::NodeCount ", f);
    put("{}{} = {};", held ? "" : "const ::cursiv::TextOffset ", b,
        step ? foldAt.begin : "c.la.begin");
  }
  const Anchor here{o, b, f};
  const Anchored foldHere = folds ? Anchored(foldAt, here) : Anchored(foldAt);
  // A record stands between its turns and any region outside it, and a turn
  // holds the turns its absorbed tail reads, which close inside it: those
  // begin where their own text does.
  const bool outerTurn = inTurn;
  if (region) {
    ownRegion = id;
    ownAt = here;
  }
  inTurn = inTurn || turns(id);
  const Anchored regionHere(regionAt, {});
  const bool outerStep = inStep;
  inStep = step;
  node(id, hand);
  inStep = outerStep;
  inTurn = outerTurn;
  // The hand-over ends with the group, whose fields go back to the pool.
  if (region) {
    ownRegion = kNoRow;
    ownAt = {};
  }
  // A record that opens no node states over one its body builds.
  const std::string closer =
      a.writes.chainBuild[n.row].creates ? "::cursiv::ll_close<kTable>("
                                         : "::cursiv::ll_close_over<kTable>(";
  if (!ll_fuse_tabled(oc, true)) {
    put("c.last = {}c, {}, {}, kTable.stamp.make({}u, {}));", closer, o, b,
        n.row,
        plan.stamps(id) ? std::to_string(LlStampPlan::state(inRule)) : l);
  } else {
    // A turn that reached back covers more than one subtree: no ride there.
    if (turn)
      put("if ({} != 0 || !::cursiv::ll_ride<kTable>(c, {}, kFuse + {}u, {}u))",
          l, f, ob, oc);
    else
      put("if (!::cursiv::ll_ride<kTable>(c, {}, kFuse + {}u, {}u))", f, ob,
          oc);
    open();
    put("c.last = {}c, {}, {}, kTable.stamp.make({}u, {}));", closer, o, b,
        n.row, l);
    close();
  }
  if (!held) close("}");
}

// Only the first position stands where the token was read. A head reaches
// back to where its caller's region opened, a later position that turns to
// where the sequence did -- or to the caller's, where the head may have
// reached back over it -- short of a nest, which holds what follows it.
void Emit::sequence(std::uint16_t id, std::uint16_t hand) {
  const FieldScope scope(*this);
  const LlNode& n = at(id);
  const bool region = reaches_back(id);
  // A fold that owns no record anchors its steps where its base begins.
  const bool folds =
      !n.owns && n.count == 2 && a.cfg.nodeBuild[kid(n, 1)].leftFold;
  const Anchor outer = regionAt;
  const bool over = inStep && !outer.live() && foldAt.live();
  const Anchor& reach = over ? foldAt : outer;
  Anchor start;
  bool scoped = false;
  if (region && reach.live() && head_turns(kid(n, 0))) {
    start = reach;
  } else if (region && ownRegion == id) {
    start = ownAt;
  } else if (region || folds) {
    const bool held = may_suspend(id);
    if (hand == kNoRow) put("::cursiv::ll_peek<kTable, {}u>(c);", mode_of(id));
    line("::cursiv::ll_flush_trivia<kTable>(c);");
    if (!held) {
      open("{");
      scoped = true;
    }
    start.open = local(held, "::cursiv::CstBuilder::Open", "o");
    start.begin = local(held, "::cursiv::TextOffset", "b");
    start.count = local(held, "::cursiv::NodeCount", "f");
    const std::string decl =
        held ? "" : region ? "[[maybe_unused]] const " : "const ";
    put("{}{}{} = c.tape.enter();", decl, held ? "" : "auto ", start.open);
    put("{}{} = c.tape.stream().records();",
        held ? "" : "[[maybe_unused]] const ::cursiv::NodeCount ", start.count);
    put("{}{}{} = c.la.begin;", decl, held ? "" : "::cursiv::TextOffset ",
        start.begin);
  }
  const Anchored foldHere = folds ? Anchored(foldAt, start) : Anchored(foldAt);
  const Anchored regionHere(regionAt);
  bool held = false;
  for (unsigned k = 0; k < n.count; ++k) {
    const std::uint16_t x = kid(n, k);
    const bool from = k == 0 || (region && !held && head_turns(x));
    regionAt = k == 0 ? outer : from ? start : Anchor{};
    if (!from && inStep) over_fold();
    group(x, k == 0 ? hand : kNoRow);
    held = held || turns(x);
  }
  if (scoped) close("}");
}

// The arms of a choice, and the token that picks one. What one token settles
// is a case; what it does not is asked of the prediction -- and only for the
// tokens that leave more than one arm standing.
void Emit::decision(std::uint16_t id) {
  const LlNode& n = at(id);
  const LlDecision& dec = a.cfg.decisions[n.decision];
  const LlDecisionBuild& db = a.cfg.decisionBuild[n.decision];
  // Which arms one token admits: an arm alone on a token is a case of its own.
  std::vector<std::vector<std::uint16_t>> armOf(a.cfg.nterms);
  for (std::uint16_t i = 0; i < db.nalts; ++i)
    for (std::uint16_t t = 0; t < a.cfg.nterms; ++t)
      if (a.laSet[dec.set + i].test(t)) armOf[t].push_back(i);
  std::vector<std::vector<std::uint16_t>> byArm(db.nalts);
  bool shared = false;
  for (std::uint16_t t = 0; t < a.cfg.nterms; ++t) {
    if (armOf[t].size() == 1)
      byArm[armOf[t][0]].push_back(t);
    else if (armOf[t].size() > 1)
      shared = true;
  }
  // A token alone cannot establish a predicate on the gap before it.
  if ((a.cfg.ngapConds != 0 && dec.how != LlPredict::single) ||
      a.modeMulti[dec.mode] == 1) {
    shared = true;
    for (auto& arm : byArm) arm.clear();
  }
  // Where the text ends on the token, the prediction reads what else it may
  // be: the token settles the arm everywhere else.
  const bool ended = a.decEnded[n.decision] != 0;
  if (ended) shared = true;
  // A token no arm admits, still there once the skip has had its say. The
  // prediction is not asked then: it chooses BETWEEN readings, and where none
  // reads the token it answers with the arm the grammar wrote first -- which
  // for a rule that reaches itself is the one that invents its way down.
  const bool asks =
      (dec.how != LlPredict::single || ended) && shared && db.repairCost <= 1;
  const bool direct = !ended && (dec.how == LlPredict::single || !shared);
  const std::string none = name("none");
  const std::string k = name("k");
  put("::cursiv::ll_peek<kTable, {}u>(c);", dec.mode);
  if (a.decStands[n.decision] != 0) put("::cursiv::ll_hold<kTable>(c);");
  if (asks) {
    put("bool {};", none);
    put("{} = false;", none);
  }
  if (!direct) {
    put("::std::uint16_t {};", k);
    put("{} = 0xFFFFu;", k);
  }
  // A token no arm takes: the repair names the arm it enters with the token
  // it owes invented. Every arm is spelt once, and entered there.
  std::vector<std::string> entry(db.nalts);
  put("if (!kTable.holds(kTable.dec_first({}u), c.la.sym)) {{", n.decision);
  open();
  const std::string fix =
      std::format("::cursiv::ll_choice_fix<kTable>(c, {}u, {}u)",
                  dec.mode, n.decision);
  if (direct) {
    open(std::format("switch ({}) {{", fix));
    for (std::uint16_t i = 0; i < db.nalts; ++i) {
      if (byArm[i].empty()) continue;
      entry[i] = name("A");
      put("case {}u: goto {};", i, entry[i]);
    }
    line("default: break;");
    close("}");
  } else {
    put("{} = {};", k, fix);
  }
  if (asks)
    put("{} = !kTable.holds(kTable.dec_first({}u), c.la.sym);", none,
        n.decision);
  close("}");
  if (direct) {
    open("switch (c.la.sym) {");
  } else {
    open(ended ? "switch (c.la.end == c.len ? 0xFFFFu : c.la.sym) {"
               : "switch (c.la.sym) {");
    for (std::uint16_t i = 0; i < db.nalts; ++i)
      for (std::uint16_t t : byArm[i])
        put("case {}u: {} = {}u; break;", t, k, i);
    if (asks)
      put("default: if ({} == 0xFFFFu) {} = {} ? {}u : "
          "::cursiv::ll_predict<kTable>(c, {}u); break;",
          k, k, none, dec.repair, n.decision);
    else
      put("default: if ({} == 0xFFFFu) {} = "
          "::cursiv::ll_predict<kTable>(c, {}u); break;",
          k, k, n.decision);
    close("}");
    open(std::format("switch ({}) {{", k));
  }
  for (std::uint16_t i = 0; i < db.nalts; ++i) {
    if (direct) {
      if (byArm[i].empty()) continue;
      std::string labels;
      for (std::uint16_t t : byArm[i]) labels += std::format("case {}u: ", t);
      if (!entry[i].empty()) labels += entry[i] + ": ";
      open(labels + "{");
    } else {
      entry[i] = name("R");
      open(std::format("case {}u: {}: {{", i, entry[i]));
    }
    // One label, one terminal: the arm knows which token it holds. Several,
    // and it knows only that it holds one.
    const std::uint16_t hand =
        direct ? (byArm[i].size() == 1 ? byArm[i][0] : kAnyToken) : kAnyToken;
    group(kid(n, i), hand);
    line("break;");
    close("}");
  }
  // A choice no arm answers, even after the skip. One invented token is a
  // repair -- the member the choice stood for gets its value; a whole
  // construct invented is a second document, and the choice matches nothing
  // instead.
  // ...unless an arm reads the token in hand and only what follows it is
  // refused: that arm is entered, and repaired where the refusal stands.
  const auto reread = [&] {
    if (direct) return;
    put("switch (::cursiv::ll_reading_fix<kTable>(c, {}u)) {{", n.decision);
    open();
    for (std::uint16_t i = 0; i < db.nalts; ++i)
      put("case {}u: goto {};", i, entry[i]);
    line("default: break;");
    close("}");
  };
  // The arm invented is entered where the switch spells it, if it does.
  const auto invent = [&] {
    if (!entry[dec.repair].empty())
      put("goto {};", entry[dec.repair]);
    else
      group(kid(n, dec.repair), kNoRow);
  };
  if (db.repairCost <= 1) {
    open("default: {");
    reread();
    open("if (c.invented < ::cursiv::kLlInventRun) {");
    invent();
    close("}");
    line("break;");
    close("}");
  } else {
    // ...and a choice too dear to invent still owes the caller a step.
    // Reading nothing leaves the loop it stands in with nothing to turn on,
    // so one member no arm could name would cost every member after it. The
    // token goes instead, and what stands behind it is read. Where the text
    // has ended no token is left to go: the arm is invented whole.
    open("default:");
    reread();
    if (db.repairCost != kLlRepairCostless) {
      open("if (::cursiv::ll_ends<kTable>(c, c.la.sym) && "
           "c.invented < ::cursiv::kLlInventRun) {");
      invent();
      close();
      open("} else {");
      line("::cursiv::ll_drop<kTable>(c);");
      close("}");
    } else {
      line("::cursiv::ll_drop<kTable>(c);");
    }
    line("break;");
    close();
  }
  close("}");
}

// One turn of a loop, or with `body` the turn of a flat chain: the choice to
// take it, then what it reads, under the loop the chain stands in.
void Emit::repetition(std::uint16_t id, std::uint16_t body) {
  const FieldScope scope(*this);
  const LlNode& n = at(id);
  const LlDecision& dec = a.cfg.decisions[n.decision];
  const bool flat = body != kNoRow;
  if (!flat && a.cfg.nodeBuild[id].ladder && ladder_flat(id)) {
    ladder(id);
    return;
  }
  const bool nested = !flat && a.cfg.nodeBuild[id].ladder;
  if (nested && ++nestedTurns > kMaxNestedTurns)
    refuse_lowering(std::format(
        "rule `{}` repeats a `nest` more than {} times in a row, more than "
        "the LL parser supports -- narrow the range, or use `many` and check "
        "the count in validation",
        a.cfg.rule_name(inRule), kMaxNestedTurns));
  const bool held = !flat && may_suspend(n.sym);
  // The turns are counted wherever a bound reads them: a floor guards the
  // choice, a ceiling ends the loop.
  const bool counts = !flat && (n.min != 0 || n.max != kEndless);
  const std::string i = local(held && counts, "unsigned", "i");
  if (!held) open("{");
  if (counts) put("{}{} = 0;", held ? "" : "unsigned ", i);
  // The first turn's gap may be read under another configuration than the
  // gap between two turns.
  const std::uint16_t head = a.headMode[n.decision];
  const bool split = !flat && n.min == 0 && head != dec.mode;
  const std::string first =
      !split ? std::string{} : local(held, "bool", "first");
  if (split) put("{}{} = true;", held ? "" : "bool ", first);
  const std::string mode =
      flat    ? std::format("{}u", head)
      : split ? std::format("({} ? {}u : {}u)", first, head, dec.mode)
              : std::format("{}u", dec.mode);
  if (!flat) open("for (;;) {");
  if (!flat && n.max != kEndless) put("if ({} == {}u) break;", i, n.max);
  const bool guarded = n.min != 0;
  if (guarded) put("if ({} >= {}u) {{", i, n.min);
  if (guarded) open();
  if (split)
    put("::cursiv::ll_peek<kTable>(c, {});", mode);
  else
    put("::cursiv::ll_peek<kTable, {}>(c);", mode);
  if (a.decStands[n.decision] != 0) put("::cursiv::ll_hold<kTable>(c);");
  // A token neither turn nor exit admits ends the loop where it stands, and
  // everything after it is text nobody reads again -- one misspelt keyword
  // at the head of a document would cost every construct below it. The skip
  // stops at the first token a turn could start on, or at one the caller can
  // carry on from, so a loop that was really over is over.
  line("::std::uint8_t fix;");
  line("fix = ::cursiv::kLlLoopNone;");
  put("if (!kTable.holds(kTable.dec_first({}u), c.la.sym)) {{", n.decision);
  open();
  put("fix = ::cursiv::ll_loop_fix<kTable>(c, {}, {}u);", mode, n.decision);
  line("if (fix == ::cursiv::kLlLoopLeave) break;");
  close("}");
  line("bool mend;");
  line("mend = fix == ::cursiv::kLlLoopTurn;");
  // Arm 0 takes a turn, arm 1 leaves: the sets were written in that order.
  const auto byToken = [&] {
    line("bool go;");
    line("go = false;");
    open("switch (c.la.sym) {");
    std::string labels;
    for (std::uint16_t t = 0; t < a.cfg.nterms; ++t)
      if (a.laSet[dec.set].test(t)) labels += std::format("case {}u: ", t);
    if (!labels.empty()) put("{}go = true; break;", labels);
    line("default: break;");
    close("}");
    line("if (!go && mend) go = true;");
    line("if (!go) break;");
  };
  const auto byPrediction = [&] {
    const std::string k = name("k");
    put("::std::uint16_t {};", k);
    put("{} = ::cursiv::ll_predict<kTable>(c, {}u);", k, n.decision);
    // A prediction no arm answers is not the loop's end: the text is broken
    // where a turn would have started. LEAVING IS ONLY AN ANSWER WHERE A RULE
    // ABOVE CAN CARRY ON FROM THE TOKEN -- otherwise it costs the author
    // every construct below, so the loop turns and its body repairs.
    open(std::format("if ({} != 0) {{", k));
    put("if ({} == ::cursiv::kNoRow && !mend &&", k);
    put("    !::cursiv::ll_loop_leaves<kTable>(c, {}u)) {{", n.decision);
    open();
    put("fix = ::cursiv::ll_loop_fix<kTable>(c, {}, {}u);", mode,
        n.decision);
    line("if (fix == ::cursiv::kLlLoopLeave) break;");
    line("mend = fix == ::cursiv::kLlLoopTurn;");
    put("{} = ::cursiv::ll_predict<kTable>(c, {}u);", k, n.decision);
    put("if ({} == ::cursiv::kNoRow) mend = true;", k);
    close("}");
    put("if ({} != 0 && !mend) break;", k);
    close("}");
  };
  if (dec.how != LlPredict::single) {
    byPrediction();
  } else if (a.decEnded[n.decision] != 0) {
    open("if (c.la.end == c.len) {");
    byPrediction();
    close();
    open("} else {");
    byToken();
    close("}");
  } else {
    byToken();
  }
  if (guarded) close("}");
  if (flat) {
    if (inStep) over_fold();
    group(body, kAnyToken);
    close("}");
    return;
  }
  if (split) put("{} = false;", first);
  const std::string at = local(held, "::cursiv::TextOffset", "at");
  put("{}{} = c.at;", held ? "" : "const ::cursiv::TextOffset ", at);
  const std::string level =
      !levels ? std::string{} : local(held, "::cursiv::IndentAt", "lv");
  if (levels) put("{}{} = c.indent;", held ? "" : "const auto ", level);
  if (inStep) over_fold();
  group(n.sym, guarded ? kNoRow : kAnyToken);
  if (nested) --nestedTurns;
  // A turn that gave up hands the text back to the loop, which takes another
  // on it. A turn that read nothing would turn for ever: a repair inside it
  // may have left the cursor where it found it.
  line("if (c.abandonDepth != ~::std::size_t{0}) break;");
  line("c.give = false;");
  put("if (c.at == {}{}) break;", at,
      levels ? std::format(" && c.indent == {}", level) : std::string{});
  if (counts) put("++{};", i);
  close("}");
  if (!held) close("}");
}

// The optional tail of a counted repetition is nested options, each turn one
// block deeper than the last. Written flat, one loop holds the turns in a row
// and the first that is not taken leaves it: the same reading, at a size that
// grows with the count alone.
bool Emit::ladder_flat(std::uint16_t id) const {
  for (;;) {
    const LlNode& n = at(id);
    if (!a.cfg.nodeBuild[id].ladder || a.cfg.nodeBuild[id].leftFold || n.owns ||
        n.row != kNoRow || n.min != 0 || n.max != 1)
      return false;
    const LlNode& in = at(n.sym);
    if (!a.cfg.nodeBuild[n.sym].ladder) return true;
    if (in.owns || in.row != kNoRow || plan.reaches_back(n.sym)) return false;
    id = kid(in, 1);
  }
}

void Emit::ladder(std::uint16_t id) {
  const FieldScope scope(*this);
  std::vector<std::pair<std::uint16_t, std::uint16_t>> turns;
  bool held = false;
  for (;;) {
    const std::uint16_t next = at(id).sym;
    const bool more = a.cfg.nodeBuild[next].ladder;
    const std::uint16_t body = more ? kid(at(next), 0) : next;
    turns.emplace_back(id, body);
    held = may_suspend(body) || held;
    if (!more) break;
    id = kid(at(next), 1);
  }
  const std::string taken = local(held, "bool", "t");
  const Anchored regionHere(regionAt);
  if (!held) open("{");
  put("{}{} = false;", held ? "" : "bool ", taken);
  open("for (;;) {");
  for (std::size_t k = 0; k < turns.size(); ++k) {
    if (k == 1) regionAt = {};
    repetition(turns[k].first, turns[k].second);
    line("if (c.abandonDepth != ~::std::size_t{0}) break;");
    if (k == 0) put("{} = true;", taken);
  }
  line("break;");
  close("}");
  put("if ({0} && c.abandonDepth == ~::std::size_t{{0}}) c.give = false;", taken);
  if (!held) close("}");
}

// The token the point that read it pinned down is taken as read; anything
// else, or the token a repair entered the arm with, is matched or repaired.
void Emit::leaf(std::uint16_t id, std::uint16_t hand, std::uint16_t row) {
  const LlNode& n = at(id);
  if (hand == n.sym)
    put("if (c.la.sym == {}u) [[likely]] ::cursiv::ll_consume<kTable>(c, {}u, "
        "{}u); else",
        n.sym, row, id + 1);
  put("{}::cursiv::ll_expect<kTable, {}u, {}u>(c, {}u, {}u);",
      hand == n.sym ? "  " : "", n.mode, n.sym, row, id + 1);
}

void Emit::node(std::uint16_t id, std::uint16_t hand) {
  const LlNode& n = at(id);
  switch (n.kind) {
    case LlShape::term:
      leaf(id, hand, n.row);
      return;
    case LlShape::endMark:
      put("::cursiv::ll_end<kTable>(c, {}u);", n.mode);
      put("::cursiv::ll_flush_trivia<kTable>(c);");
      return;
    case LlShape::condition:
      if (a.cut[n.sym].none())
        put("::cursiv::ll_condition_node<kTable>(c, {}u, {}u, {}u);", n.mode,
            n.sym, id);
      return;
    case LlShape::implied:
      put("::cursiv::ll_implied<kTable>(c, {}u, {}u, {}u, {}u, {}u);", n.mode,
          n.sym, n.min, n.row, id + 1);
      return;
    case LlShape::empty:
      // Past the trivia a parent already wrote: the run's ends never decrease.
      if (n.owns)
        put("c.last = ::cursiv::ll_close<kTable>(c, c.tape.enter(), "
            "::std::max(c.at, c.triviaAt), kTable.stamp.make({}u, 0));",
            n.row);
      return;
    case LlShape::call:
      call_site(id);
      return;
    case LlShape::seq:
      sequence(id, hand);
      return;
    case LlShape::alt:
      decision(id);
      return;
    case LlShape::rep:
      repetition(id);
      return;
  }
}

// A precedence table, climbed: the operand, then a turn per operator that
// binds at least as tight as the one the caller stands under. The record of a
// join spans from the left operand's first byte, so the open is taken before
// it and re-closed at every turn. The right operand is the climb again, one
// step tighter, called through the machine like any rule.
void Emit::climb(std::uint16_t r) {
  const LlClimb& cl = a.cfg.climbs[a.cfg.rules[r].climb];
  const bool select =
      cl.selectNode != kNoRow &&
      (a.cfg.decisions[a.cfg.nodes[cl.selectNode].decision].how !=
           LlPredict::single ||
       a.modeMulti[cl.mode] == 1 ||
       a.decEnded[a.cfg.nodes[cl.selectNode].decision] != 0);
  // A step that groups nothing leaves the next operator of the chain to the
  // caller, and the caller does not take what the step refused either: after a
  // turn only an operator on a looser step goes on. Without a right-grouping
  // operator the operand is always one step tighter, and the frame keeps no
  // flag for it.
  bool anyNon = false, anyRight = false, bodies = false, lays = false;
  for (std::uint16_t k = 0; k < cl.opCount; ++k) {
    const LlClimbOp& op = a.cfg.climbOps[cl.opBase + k];
    anyNon = anyNon || op.assoc == Association::non;
    anyRight = anyRight || op.assoc == Association::right;
    const bool atomic = a.cfg.atomic_operator(op.body) != kNoRow;
    bodies = bodies || !atomic;
    lays = lays || atomic;
  }
  for (const auto& [type, n] : {std::pair{"unsigned", "least"},
                                {"::cursiv::CstBuilder::Open", "o"},
                                {"::cursiv::TextOffset", "b"},
                                {"::cursiv::NodeId", "w0"},
                                {"::cursiv::TextOffset", "before"},
                                {"unsigned", "prec"}})
    fields.emplace_back(type, n);
  if (anyRight) fields.emplace_back("bool", "right");
  fields.emplace_back("::cursiv::NodeId", "w1");
  if (anyNon) {
    fields.emplace_back("unsigned", "limit");
    fields.emplace_back("bool", "non");
  }
  // The statements, each led by a space, that state how an operator's step
  // groups.
  const auto grouping = [&](const LlClimbOp& op) {
    std::string stated;
    if (anyRight)
      stated += std::format(" f.right = {};", op.assoc == Association::right);
    if (anyNon)
      stated += std::format(" f.non = {};", op.assoc == Association::non);
    return stated;
  };
  line("f.least = c.arg;");
  if (anyNon) line("f.limit = 255u;");
  new_search();
  const std::uint16_t m = cl.primary != kNoRow
                              ? rule_mode(cl.primary)
                              : a.cfg.nodes[cl.primaryNode].mode;
  put("::cursiv::ll_peek<kTable, {}u>(c);", m == kNoRow ? a.errMode : m);
  line("::cursiv::ll_flush_trivia<kTable>(c);");
  line("f.o = c.tape.enter();");
  line("f.b = c.la.begin;");
  line("f.w0 = c.last;");
  if (cl.primary == kNoRow) {
    const LlNode& q = a.cfg.nodes[cl.primaryNode];
    put("::cursiv::ll_expect<kTable, {}u, {}u>(c, {}u, {}u);", q.mode, q.sym,
        q.row, cl.primaryNode + 1);
  } else {
    put("c.stack.push_back({}u, c.tape.stream().records());", cl.atnEntry);
    invoke(cl.primary, "1u");
    line("::cursiv::ll_return(c);");
  }
  exits = true;
  line("if (c.give) goto done;");
  open("for (;;) {");
  const std::uint16_t selectDec =
      cl.selectNode != kNoRow ? a.cfg.nodes[cl.selectNode].decision : kNoRow;
  line("f.before = c.at;");
  put("::cursiv::ll_peek<kTable, {}u>(c);", cl.mode);
  if (selectDec != kNoRow && a.decStands[selectDec] != 0)
    put("::cursiv::ll_hold<kTable>(c);");
  // Prediction can keep the first unfinished arm at the end of a document.
  // An infix has no operator there: consuming that zero-width EOF would
  // reset the repair budget and recursively invent another operand forever.
  line("if (c.la.sym == ::cursiv::kEofSymbol) break;");
  line("f.prec = 0;");
  if (anyRight) line("f.right = false;");
  if (anyNon) line("f.non = false;");
  lays = lays && selectDec != kNoRow;
  const std::string invented = name("M");
  // The operator a repair invents: a terminal is laid where the text stops
  // and the right operand is read on from there; a rule's body invents its
  // own head. What the repair does not name leaves the climb.
  const auto mend = [&](std::string_view into) {
    put("{} = ::cursiv::ll_climb_fix<kTable>(c, {}u, {}u);", into, cl.mode,
        selectDec);
    put("if ({} == ::cursiv::kLlClimbAgain) continue;", into);
    open(std::format("switch ({}) {{", into));
    for (std::uint16_t k = 0; k < cl.opCount; ++k) {
      const LlClimbOp& op = a.cfg.climbOps[cl.opBase + k];
      const auto leaf = a.cfg.atomic_operator(op.body);
      if (leaf == kNoRow) {
        if (!select)
          put("case {}u: f.prec = {}u;{} body = {}u; break;", k, op.prec,
              grouping(op), k);
        continue;
      }
      open(std::format("case {}u:", k));
      put("f.prec = {}u;", op.prec);
      if (const std::string stated = grouping(op); !stated.empty())
        line(std::string_view(stated).substr(1));
      line("if (f.prec < f.least) break;");
      join(cl.leftBase, cl.leftCount, "f.w0");
      const auto& token = at(leaf);
      put("::cursiv::ll_insert_planned<kTable>(c, {}u, {}u, {}u);",
          token.sym, token.row, leaf + 1);
      put("goto {};", invented);
      close();
    }
    line("default: break;");
    close("}");
  };
  if (!select) {
    line("::cursiv::ElementId row;");
    line("row = 0xFFFFu;");
    line("::std::uint16_t pos;");
    line("pos = 0;");
    if (bodies) {
      line("::std::uint16_t body;");
      line("body = ::cursiv::kNoRow;");
    }
    open("switch (c.la.sym) {");
  } else {
    line("::std::uint16_t op;");
    line("bool bad;");
    put("bad = !kTable.holds(kTable.dec_first({}u), c.la.sym);", selectDec);
    open("if (!bad) {");
    put("op = ::cursiv::ll_predict<kTable>(c, {}u);", selectDec);
    put("if (op >= {}u && !::cursiv::ll_climb_exits<kTable>(c, c.la.sym))",
        cl.opCount);
    put("  op = ::cursiv::ll_reading_fix<kTable>(c, {}u);", selectDec);
    line("bad = false;");
    close("}");
    open("if (bad) {");
    mend("op");
    close("}");
    open("switch (op) {");
  }
  for (std::uint16_t k = 0; k < cl.opCount; ++k) {
    const LlClimbOp& op = a.cfg.climbOps[cl.opBase + k];
    if (select) {
      put("case {}u: f.prec = {}u;{} break;", k, op.prec, grouping(op));
      continue;
    }
    const auto leaf = a.cfg.atomic_operator(op.body);
    if (leaf == kNoRow) {
      const LlDecision& choice =
          a.cfg.decisions[a.cfg.nodes[cl.selectNode].decision];
      std::string labels;
      for (std::uint16_t t = 0; t < a.cfg.nterms; ++t)
        if (a.laSet[choice.set + k].test(t))
          labels += std::format("case {}u: ", t);
      put("{}f.prec = {}u;{} body = {}u; break;", labels, op.prec,
          grouping(op), k);
      continue;
    }
    // A guarded operator leaves its choice to a closure, never to one token.
    if (leaf != op.body)
      throw std::logic_error("cursiv: an LL operator switch reads a condition");
    const auto& token = at(leaf);
    put("case {}u:", token.sym);
    put("f.prec = {}u;{} row = {}u; pos = {}u; break;", op.prec, grouping(op),
        token.row, leaf + 1);
  }
  if (select || selectDec == kNoRow) {
    line("default: break;");
  } else {
    // A token neither an operator nor one a caller goes on from: the climb
    // is where an operator missing in front of it is invented.
    open("default:");
    put("if (kTable.holds(kTable.dec_first({}u), c.la.sym) && "
        "::cursiv::ll_climb_exits<kTable>(c, c.la.sym)) break;",
        selectDec);
    line("::std::uint16_t fix;");
    mend("fix");
    line("break;");
    close();
  }
  close("}");
  line(anyNon
           ? "if (f.prec == 0 || f.prec < f.least || f.prec > f.limit) break;"
           : "if (f.prec == 0 || f.prec < f.least) break;");
  join(cl.leftBase, cl.leftCount, "f.w0");
  if (!select && !bodies) {
    line("::cursiv::ll_consume<kTable>(c, row, pos);");
  } else {
    open(select ? "switch (op) {" : "switch (body) {");
    for (std::uint16_t k = 0; k < cl.opCount; ++k) {
      const auto& op = a.cfg.climbOps[cl.opBase + k];
      const auto leaf = a.cfg.atomic_operator(op.body);
      if (!select && leaf != kNoRow) continue;
      open(std::format("case {}u: {{", k));
      if (leaf != kNoRow) {
        // Prediction chooses an operator arm but does not replace the token
        // held by the wider climb alphabet. Its terminal position reads the
        // selected spelling again before consuming the operator's own span.
        const auto& token = at(leaf);
        put("::cursiv::ll_expect<kTable, {}u, {}u>(c, {}u, {}u);", token.mode,
            token.sym, token.row, leaf + 1);
      } else {
        group(op.body, kAnyToken);
      }
      line("break;");
      close("}");
    }
    line(select ? "default: break;"
                : "default: ::cursiv::ll_consume<kTable>(c, row, pos); break;");
    close("}");
  }
  line("if (c.give || c.at == f.before) break;");
  if (lays) put("{}:;", invented);
  line("f.w1 = c.last;");
  invoke(r, anyRight ? "f.right ? f.prec : f.prec + 1" : "f.prec + 1");
  if (anyNon) line("f.limit = f.non ? f.prec - 1u : f.prec;");
  join(cl.rightBase, cl.rightCount, "f.w1");
  put("c.last = ::cursiv::ll_close<kTable>(c, f.o, f.b, kTable.stamp.make({}u, "
      "0));",
      cl.node);
  line("if (c.give || c.at == f.before) break;");
  close("}");
}

// One rule, as a step: its frame holds what outlives a call, and the step
// jumps back to the label a call resumes at. A plain rule is a function with
// no frame. A rule entered after the rule in hand gave up opens
// nothing.
void Emit::rule(std::uint16_t r) {
  const LlRuleInfo& u = a.cfg.rules[r];
  ind = 2;
  inRule = r;
  if (u.climb != kNoRow)
    climb(r);
  else if (u.body != kNoRow)
    group(u.body);
  std::string body = std::move(out);
  out.clear();
  if (plain(r)) {
    if (resumes != 0 || !fields.empty() || exits)
      throw std::logic_error("cursiv: a plain LL rule resumes");
    frame.clear();
    out += std::format("void s{}(::cursiv::LlContext& c) {{\n", r);
    out += "  if (c.give) {\n    c.repaired.mark();\n    return;\n  }\n";
    out += body;
    out += "}\n\n";
    return;
  }
  frame = std::format("struct F{} {{\n  ::std::uint16_t pc;\n", r);
  for (const auto& [type, n] : fields)
    frame += std::format("  {} {};\n", type, n);
  frame += "};\n";
  out += std::format(
      "::std::uint16_t s{}(::cursiv::LlContext& c, void* frame) {{\n", r);
  out += std::format("  auto& f = *::std::launder(static_cast<F{}*>(frame));\n",
                   r);
  out += "  switch (f.pc) {\n";
  for (unsigned k = 1; k <= resumes; ++k)
    out += std::format("    case {}u: goto L{};\n", k, k);
  out += "    default: break;\n  }\n";
  out += "  if (c.give) {\n    c.repaired.mark();\n"
       "    return ::cursiv::kLlDone;\n  }\n";
  out += body;
  if (exits) out += "done:\n";
  out += "  return ::cursiv::kLlDone;\n}\n\n";
}

}  // namespace

// How many plain rules stand on the native stack from each rule down, zero for
// a rule that is not plain: one with a climb, one that calls a rule that is
// not plain -- so none reaches a rule that calls itself -- or one that would
// stack more than `kLlPlainDepth`. Callees settle first, with no recursion: a
// chain of rules may be as long as the grammar.
std::vector<std::uint8_t> ll_plain_depths(const LlLowCfg& c) {
  std::vector<std::vector<std::uint16_t>> callers(c.nrules);
  std::vector<std::uint16_t> waits(c.nrules, 0), ready, nodes;
  for (std::uint16_t r = 0; r < c.nrules; ++r) {
    if (c.rules[r].climb == kNoRow && c.rules[r].body != kNoRow)
      nodes.push_back(c.rules[r].body);
    while (!nodes.empty()) {
      const LlNode& n = c.nodes[nodes.back()];
      nodes.pop_back();
      if (n.kind == LlShape::call) callers[n.sym].push_back(r), ++waits[r];
      for (const auto k : c.children(n)) nodes.push_back(k);
    }
    if (waits[r] == 0) ready.push_back(r);
  }
  // A rule that is not plain weighs as one at the limit.
  std::vector<std::uint8_t> depth(c.nrules, 0), below(c.nrules, 0);
  while (!ready.empty()) {
    const std::uint16_t r = ready.back();
    ready.pop_back();
    if (c.rules[r].climb == kNoRow && below[r] < kLlPlainDepth)
      depth[r] = below[r] + 1;
    for (const auto p : callers[r]) {
      below[p] = std::max(below[p], depth[r] == 0 ? kLlPlainDepth : depth[r]);
      if (--waits[p] == 0) ready.push_back(p);
    }
  }
  return depth;
}

std::string ll_emit_parser_decl(const std::string& ns) {
  std::string s;
  s += "// GENERATED by the predictive engine -- do not edit.\n";
  s += "#pragma once\n\n";
  s += "namespace cursiv {\nstruct LlContext;\n}  // namespace cursiv\n\n";
  s += std::format("namespace {} {{\n", ns);
  s += "void parse_entry(::cursiv::LlContext&);\n";
  s +=
      "inline constexpr void (*kDrive)(::cursiv::LlContext&) = &parse_entry;\n";
  s += std::format("}}  // namespace {}\n", ns);
  return s;
}

std::vector<std::pair<std::string, std::string>> ll_emit_parser_files(
    const LlFinalProgram& done, const std::string& ns) {
  const Built& a = done.program();
  const LlStampPlan& plan = done.plan();
  // A bounded set of translation units keeps the optimizer from repeatedly
  // opening an entire language. The frames are declared once, so the
  // machine's unit sizes every one of them.
  constexpr unsigned units = 8;
  constexpr std::string_view banner =
      "// GENERATED by the predictive engine -- do not edit.\n";
  bool levels = false;
  for (std::uint16_t t = 1; t < a.cfg.nterms; ++t)
    levels = levels || a.cfg.terms[t].laid == kLaidIndent ||
             a.cfg.terms[t].laid == kLaidDedent;
  Memos memo{.plain = ll_plain_depths(a.cfg)};
  std::string declarations(banner);
  declarations +=
      "#pragma once\n\n#include <cstdint>\n\n#include \"Parser.hpp\"\n"
      "#include \"cursiv/core/syntax-tree/CstBuilder.hpp\"\n"
      "#include \"cursiv/engines/ll/runtime/LlSection.hpp\"\n";
  if (levels)
    declarations +=
        "#include \"cursiv/engines/shared/runtime/IndentLevel.hpp\"\n";
  declarations += std::format("\nnamespace {} {{\n", ns);
  std::vector<std::string> bodies;
  std::size_t bytes = 0;
  // The machine steps the rules that resume, and the entry whatever it is:
  // a plain rule is only ever called in place.
  std::vector<bool> stepped(a.cfg.nrules, false);
  for (std::uint16_t r = 1; r < a.cfg.nrules; ++r)
    if (a.liveRules[r]) {
      Emit e{{}, a, plan, memo};
      e.levels = levels;
      e.rule(r);
      stepped[r] = !e.frame.empty();
      if (stepped[r])
        declarations += e.frame + std::format(
            "::std::uint16_t s{}(::cursiv::LlContext&, void*);\n", r);
      else
        declarations += std::format("void s{}(::cursiv::LlContext&);\n", r);
      bytes += e.out.size();
      bodies.push_back(std::move(e.out));
    }
  declarations += std::format("}}  // namespace {}\n", ns);
  std::string steps, sizes;
  const std::uint16_t entry = a.cfg.entryRule;
  for (std::uint16_t r = 1; r < a.cfg.nrules; ++r)
    if (stepped[r])
      steps += std::format("    case {}u: return s{}(c, f);\n", r, r);
    else if (r == entry)
      steps += std::format(
          "    case {}u: s{}(c); return ::cursiv::kLlDone;\n", r, r);
  for (std::uint16_t r = 0; r < a.cfg.nrules; ++r)
    sizes += stepped[r]  ? std::format("sizeof(F{}), ", r)
             : r == entry ? "sizeof(::std::uint16_t), "
                          : "0u, ";
  const std::size_t budget =
      std::max<std::size_t>(32768, (bytes + units - 1) / units);
  std::vector<std::pair<std::string, std::string>> files;
  files.emplace_back("ParserRules.hpp", std::move(declarations));
  files.emplace_back(
      "ParserStates.cpp",
      std::string(banner) +
          "#include <cstdint>\n\n#include \"ParserRules.hpp\"\n" +
          "#include \"cursiv/engines/ll/runtime/LlContext.hpp\"\n" +
          "#include \"cursiv/engines/ll/runtime/LlMachine.hpp\"\n" +
          ll_emit_walk_includes(a) + "\n" +
          std::format("namespace {} {{\n\n", ns) + ll_emit_walk(a) +
          std::format(
              "\nnamespace {{\n"
              "::std::uint16_t step(::std::uint16_t r, ::cursiv::LlContext& c, "
              "[[maybe_unused]] void* f) {{\n  switch (r) {{\n{}"
              "    default: return ::cursiv::kLlDone;\n  }}\n}}\n"
              "constexpr ::std::uint32_t kFrameBytes[] = {{{}}};\n"
              "}}  // namespace\n"
              "void parse_entry(::cursiv::LlContext& c) {{\n{}"
              "  c.document = true;\n"
              "  ::cursiv::LlMachine machine;\n"
              "  machine.run<&step>(c, {}u, kFrameBytes);\n"
              "  c.document = false;\n}}\n"
              "}}  // namespace {}\n",
              steps, sizes, "  c.stack.reserve(64);\n", entry, ns));
  std::size_t next = 0;
  for (unsigned u = 0; u < units; ++u) {
    std::string source(banner);
    // Empty units are deliberately emitted too: the CMake outputs are fixed
    // before a generator runs and must survive grammars that grow or shrink.
    if (next < bodies.size()) {
      source +=
          "#include <cstdint>\n#include <new>\n\n#include \"ParserRules.hpp\"\n"
          "#include \"Table.hpp\"\n"
          "#include \"cursiv/engines/ll/runtime/LlMachine.hpp\"\n"
          "#include \"cursiv/engines/ll/runtime/ll_predictor.hpp\"\n"
          "#include \"cursiv/engines/ll/runtime/ll_repair.hpp\"\n";
      if (a.cfg.ngapConds != 0)
        source += "#include \"cursiv/engines/ll/runtime/ll_conditions.hpp\"\n";
      source += std::format("\nnamespace {} {{\n\n", ns);
      std::size_t used = 0;
      do {
        used += bodies[next].size();
        source += bodies[next++];
      } while (next < bodies.size() && (used < budget || u + 1 == units));
      source += std::format("}}  // namespace {}\n", ns);
    }
    files.emplace_back(std::format("ParserRules{}.cpp", u), std::move(source));
  }
  return files;
}

}  // namespace cursiv
