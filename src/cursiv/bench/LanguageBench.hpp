// Reusable language benchmarks: document builds and installed LSP providers.
#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <ratio>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "cursiv/bench/PerfCounters.hpp"
#include "cursiv/bench/Rss.hpp"
#include "cursiv/core/execution/TaskScheduler.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/references/ScopeComputation.hpp"
#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/Diagnostic.hpp"
#include "cursiv/core/services/LanguageMetaData.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/Cst.hpp"
#include "cursiv/core/syntax-tree/CstNode.hpp"
#include "cursiv/core/syntax-tree/CstUtils.hpp"
#include "cursiv/core/syntax-tree/DocumentReferences.hpp"
#include "cursiv/core/syntax-tree/ReferenceInfo.hpp"
#include "cursiv/core/validation/ValidationRegistry.hpp"
#include "cursiv/core/workspace/DefaultDocumentBuilder.hpp"
#include "cursiv/core/workspace/Document.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/WorkspaceLock.hpp"
#include "cursiv/lsp/services/LspServices.hpp"
#include "cursiv/lsp/services/SharedLspServices.hpp"

namespace cursiv::bench {

// The stages, in the order the builder runs them -- each an interval of one
// cold build, read at a phase drain through `onBuildPhase`:
//
//   parse+index  the `IndexedContent` drain: parse plus this document's
//                exported content (no separate `index`: the builder has no
//                barrier between parsing and exporting)
//   scopes       `IndexedContent` -> `ComputedScopes`: local symbols
//   link         `ComputedScopes` -> `IndexedReferences`: linking and the
//                references published to the index
//   validate     `IndexedReferences` -> `Validated`
//   build        the call to the `Validated` drain -- the total
//
// One column per barrier: a column per finer-grained rung would read zero
// and say nothing, since `runPhase` publishes merged rungs back to back.
//
// `build` is not the sum of the rows above it -- it also carries the parse,
// the root indexing of adoption, and the scheduler's own dispatch, and that
// difference is meant to be read.
enum Stage : std::size_t {
  kIndex,
  kScopes,
  kLink,
  kValidate,
  kBuild,
  kStageCount,
};

inline constexpr const char* kStageName[kStageCount] = {
    "parse+index", "scopes", "link", "validate", "build"};

// The language module installs a `DocumentValidator`, so
// the phase always runs -- but with an empty registry and every reference
// resolved it walks nothing and publishes nothing. Real as soon as it does
// either: turning thousands of broken references into diagnostics is work.
inline constexpr const char* kValidateAbsentWhy =
    "this language registers no validation check and publishes no "
    "diagnostic -- the phase has nothing to do";

// A stage that did not run is negative, never zero: zero is a legitimate
// measurement at 16 KiB and would print as a stage that took no time.
inline constexpr double kAbsent = -1.0;

struct ServiceMeasurement {
  std::string name;
  double min = kAbsent;
  double ms = kAbsent;     // mean of all timed calls, excluding warmups
  double worst = kAbsent;  // maximum of all timed calls
  std::size_t samples = 0;
  std::size_t results = 0;  // items returned over one sweep
  std::size_t probes = 0;

  void record(double elapsed) {
    if (samples == 0) {
      min = ms = worst = elapsed;
      samples = 1;
      return;
    }
    min = std::min(min, elapsed);
    worst = std::max(worst, elapsed);
    ms += (elapsed - ms) / static_cast<double>(++samples);
  }
};

struct Row {
  std::string language;
  std::string label;   // "16KiB", "128MiB"
  std::string source;  // "generated" | "disk:<path>"
  std::size_t bytes = 0;
  int iterations = 0;
  double ms[kStageCount] = {kAbsent, kAbsent, kAbsent, kAbsent, kAbsent};
  // What the link stage actually did. A `link` figure with zero references is
  // a figure for an empty loop, and it must be visible as one.
  std::size_t references = 0;
  std::size_t resolved = 0;
  std::size_t diagnostics = 0;
  // What the stage cost in hardware, all of it read off one build -- see
  // `ws_stages`. Hardware columns are hidden when counters are unavailable.
  PerfSample perf[kStageCount] = {};
  double perfBuildMs = kAbsent;

  // A faster repetition without PMU coverage must not erase valid counts.
  // Keep all phases from the fastest build with a measured total.
  void record_perf(double buildMs,
                   const std::array<PerfSample, kStageCount>& samples) {
    if (!samples[kBuild].valid || (perfBuildMs >= 0 && buildMs >= perfBuildMs))
      return;
    std::copy(samples.begin(), samples.end(), perf);
    perfBuildMs = buildMs;
  }

  // ...and what it made resident: the stage figure is what the phase asked
  // the OS for, the peak is the build's own ceiling. Comparable for one
  // language across binaries, never between languages of one run.
  RssSample rss[kStageCount] = {};
  std::vector<ServiceMeasurement> lsp;
};

// One workspace, N documents, the real builder: `DefaultDocumentBuilder`
// spreads each phase across the documents on its `TaskScheduler`, so N files
// handed to one `build` is the only way to ask whether it parallelises.
// Equal byte budget across shapes: the rows only compare if the total work
// is the same, so a shape is `budget` bytes cut differently, never a
// different amount of text.
enum class Shape : std::size_t { ManySmall, Medium, FewLarge, Mixed, Count };

inline constexpr const char* kShapeName[] = {"many-small", "medium",
                                             "few-large", "mixed"};

// How the reference graph of a shape's corpus is wired. Both regimes drive
// the link phase for real -- an intra-document reference through local
// scopes is still work -- but a curve averaging them would describe neither.
enum class Refs : std::uint8_t { Intra, Cross };

inline constexpr const char* refs_name(Refs r) {
  return r == Refs::Cross ? "cross" : "intra";
}

// The shapes, as file sizes summing to `budget`.
//   many-small  16 KiB files      -- dispatch-bound: the most documents, the
//                                    least work in each
//   medium      1 MiB files       -- the shape a real workspace has
//   few-large   budget/4, 4 files -- fewer documents than workers, which is
//                                    where a per-document fan-out stops being
//                                    able to help
//   mixed       80 % of the bytes in 16 KiB files, 20 % in 1 MiB ones -- the
//               realistic tail, and the one an even split would flatter
inline std::vector<std::size_t> shape_files(Shape shape, std::size_t budget) {
  constexpr std::size_t kSmall = std::size_t{16} << 10;
  constexpr std::size_t kMedium = std::size_t{1} << 20;
  std::vector<std::size_t> files;
  // At least one file per component, or a small budget silently drops the
  // tail of the mixed shape. The budget then overshoots by less than one
  // file, which the row reports as its real byte count.
  const auto fill = [&](std::size_t each, std::size_t bytes) {
    if (bytes == 0 || each == 0) return;
    const std::size_t count = std::max<std::size_t>(bytes / each, 1);
    for (std::size_t i = 0; i < count; ++i) files.push_back(each);
  };
  switch (shape) {
    case Shape::ManySmall:
      fill(kSmall, budget);
      break;
    case Shape::Medium:
      fill(kMedium, budget);
      break;
    case Shape::FewLarge:
      fill(budget / 4, budget);
      break;
    case Shape::Mixed:
      fill(kSmall, budget * 4 / 5);
      fill(kMedium, budget / 5);
      break;
    case Shape::Count:
      break;
  }
  if (files.empty()) files.push_back(budget);
  return files;
}

// One point of a scaling curve.
struct MultiPoint {
  std::size_t workers = 0;
  double totalMs = 0.0;  // the whole build, from Changed
  double linkMs =
      0.0;  // everything after phase 1: IndexedContent -> Validated drain
};

struct MultiRow {
  std::string language;
  std::string shape;
  Refs refs = Refs::Intra;
  std::size_t files = 0;
  std::size_t bytes = 0;
  int iterations = 0;
  std::vector<MultiPoint> points;
  std::size_t references = 0;
  std::size_t resolved = 0;
  std::size_t diagnostics = 0;
};

struct Options {
  std::vector<std::size_t> sizes;
  int iterations = 0;  // 0 -> per-size default, see iterations_for
  std::string filter;
  std::string jsonPath;
  std::string csvPath;
  std::string dumpDir;
  std::string corpusDir;
  std::vector<std::string> files;
  bool help = false;
  bool lsp = true;
  bool full = false;
  bool list = false;
  // The multi-document lane. It runs by default; `--no-multi` skips it and
  // `--multi` runs it alone.
  bool multi = true;
  bool stages = true;
  std::size_t multiBytes = 0;  // 0 -> kDefaultMultiBytes / kFullMultiBytes
  std::vector<std::size_t> workers;
  // The resident lane, and it is a mode, not a column: resident bytes
  // answer only on an allocator that does not already hold what the build
  // needs, and this binary warms it well before the ladder. `--rss`
  // therefore runs one language, skips the parse lane, and builds once.
  bool rss = false;
  // The filter is a whole name and not a beginning -- what `--rss` narrows to
  // once it has decided which language it measures.
  bool exact = false;
};

inline constexpr std::size_t kDefaultMultiBytes = std::size_t{16} << 20;
inline constexpr std::size_t kFullMultiBytes = std::size_t{64} << 20;

// The ladder, five orders of magnitude. A few kB is what an editor
// reparses on a keystroke and where the fixed costs show; 128 MiB is a
// build-system input and shows whether anything grows worse than linearly.
inline constexpr std::size_t kDefaultSizes[] = {
    std::size_t{16} << 10, std::size_t{256} << 10, std::size_t{4} << 20};
inline constexpr std::size_t kFullSizes[] = {
    std::size_t{16} << 10, std::size_t{256} << 10, std::size_t{4} << 20,
    std::size_t{32} << 20, std::size_t{128} << 20};

// Repetitions shrink as the corpus grows, so the default sweep stays
// bounded: the whole ladder is minutes, not an afternoon. `--iterations`
// overrides.
inline int iterations_for(std::size_t bytes) {
  if (bytes <= (std::size_t{256} << 10)) return 5;
  if (bytes <= (std::size_t{4} << 20)) return 3;
  if (bytes <= (std::size_t{32} << 20)) return 2;
  return 1;
}

inline std::string size_label(std::size_t bytes) {
  char buf[32];
  if (bytes >= (std::size_t{1} << 20) && bytes % (std::size_t{1} << 20) == 0)
    std::snprintf(buf, sizeof buf, "%zuMiB", bytes >> 20);
  else if (bytes >= (std::size_t{1} << 10) &&
           bytes % (std::size_t{1} << 10) == 0)
    std::snprintf(buf, sizeof buf, "%zuKiB", bytes >> 10);
  else
    std::snprintf(buf, sizeof buf, "%zuB", bytes);
  return buf;
}

inline bool selected(const Options& o, std::string_view name) {
  if (o.exact) return name == o.filter;
  return o.filter.empty() || name.find(o.filter) != std::string_view::npos;
}

inline double ms_since(std::chrono::steady_clock::time_point t0) {
  return std::chrono::duration<double, std::milli>(
             std::chrono::steady_clock::now() - t0)
      .count();
}

[[noreturn]] inline void refused(const char* name, const char* what) {
  std::fprintf(stderr, "[bench] %s: the corpus was %s -- no figure\n", name,
               what);
  std::exit(1);
}

// A descriptor for one language, used by single-language executables and
// by the example driver. All callbacks use the language server registration.
struct Language {
  const char* name;
  const char* languageId;
  const char* extension;
  std::string (*corpus)(std::size_t bytes);
  void (*parse)(const std::string& src);
  // Workspace callbacks are null on threadless cross builds.
  void (*workspace)(int iterations, bool resident, const std::string& src,
                    Row&);

  // Null on both when the language has no per-file generator: a workspace of N
  // copies of the same single-document corpus is N duplicate symbol sets, and
  // that is not a workspace.
  std::string (*multiFile)(std::size_t fileIndex, std::size_t bytes);
  void (*multi)(int iterations, const std::vector<std::string>& files,
                const std::vector<std::size_t>& workers, MultiRow&);
  Refs multiRefs;
  void (*lsp)(int iterations, const std::string& src, Row&) = nullptr;
};

// The same driver serves cursiv's examples and an embedded consumer. A null
// corpus callback means that the caller supplies files on the command line.
int run(int argc, char** argv, std::span<const Language> languages);

// Check that the input parses without recovery before the timed build. The
// instruction-count mode repeats the parser API without timing workspace work.
void parse_probe(const char* name, const Parser& parser,
                 const std::string& src);

// Every workspace interval is measured between the real builder's phase drains.
// The clock on the ladder: one instant per rung, stamped on the build's own
// thread the moment the phase has drained, after the fan-out has joined and
// before the next phase starts.
// A cancelled phase never drains, so it never stamps; the bench refuses
// such a build outright rather than report a partial ladder.
class PhaseClock final : public cursiv::BuildPhaseListener {
 public:
  using Clock = std::chrono::steady_clock;

  // A read made at a rung lands in the next interval, several microseconds
  // of syscalls: a build that reads counters or resident bytes is not timed.
  void onBuildPhase(cursiv::DocumentState reached,
                    std::span<cursiv::Document* const>,
                    const cursiv::CancellationToken&) override {
    const std::size_t rung = static_cast<std::size_t>(reached);
    _at[rung] = Clock::now();
    if (_perf != nullptr) _sample[rung] = _perf->read();
    if (_resident) _rss[rung] = rss_now();
  }

  void start(const PerfCounters* perf = nullptr,
             bool resident = false) noexcept {
    for (Clock::time_point& t : _at) t = Clock::time_point{};
    for (PerfSample& s : _sample) s = PerfSample{};
    for (RssSample& s : _rss) s = RssSample{};
    _perf = perf;
    _resident = resident;
    _startSample = perf != nullptr ? perf->read() : PerfSample{};
    _startRss = resident ? rss_now() : RssSample{};
    _start = Clock::now();
  }

  [[nodiscard]] bool reached(cursiv::DocumentState s) const noexcept {
    return _at[static_cast<std::size_t>(s)] != Clock::time_point{};
  }
  // The interval between two rungs, in milliseconds. `from` absent means "from
  // the call that started the build".
  [[nodiscard]] double span(cursiv::DocumentState to) const {
    return ms_between(_start, _at[static_cast<std::size_t>(to)]);
  }
  [[nodiscard]] double span(cursiv::DocumentState from,
                            cursiv::DocumentState to) const {
    return ms_between(_at[static_cast<std::size_t>(from)],
                      _at[static_cast<std::size_t>(to)]);
  }
  // The same two windows, in hardware counts.
  [[nodiscard]] PerfSample counts(cursiv::DocumentState to) const {
    return perf_delta(_startSample, _sample[static_cast<std::size_t>(to)]);
  }
  [[nodiscard]] PerfSample counts(cursiv::DocumentState from,
                                  cursiv::DocumentState to) const {
    return perf_delta(_sample[static_cast<std::size_t>(from)],
                      _sample[static_cast<std::size_t>(to)]);
  }
  // ...and in resident bytes.
  [[nodiscard]] RssSample held(cursiv::DocumentState to) const {
    return rss_delta(_startRss, _rss[static_cast<std::size_t>(to)]);
  }
  [[nodiscard]] RssSample held(cursiv::DocumentState from,
                               cursiv::DocumentState to) const {
    return rss_delta(_rss[static_cast<std::size_t>(from)],
                     _rss[static_cast<std::size_t>(to)]);
  }

 private:
  [[nodiscard]] static double ms_between(Clock::time_point a,
                                         Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  }

  Clock::time_point _start{};
  std::array<Clock::time_point, cursiv::kDocumentStateCount> _at{};
  const PerfCounters* _perf = nullptr;
  bool _resident = false;
  PerfSample _startSample{};
  std::array<PerfSample, cursiv::kDocumentStateCount> _sample{};
  RssSample _startRss{};
  std::array<RssSample, cursiv::kDocumentStateCount> _rss{};
};

// Prepare bytes and routing outside the clock, leaving parsing to the measured
// build. The factory's fromString already parses and returns `Parsed`, so using
// it here would exclude parsing from both `parse+index` and `build`.
inline std::shared_ptr<cursiv::Document> unparsed_input(
    const cursiv::CoreServices& core, std::string uri,
    const std::string& text) {
  auto document =
      std::make_shared<cursiv::Document>(cursiv::TextDocument::create(
          std::move(uri), core.languageMetaData.languageId, 0, text));
  document->language = &core;
  // The supplied buffer is authoritative; no disk read belongs in this lane.
  document->isOpen = true;
  return document;
}

// `CURSIV_BENCH_DUMP=DIR`: what one build published, for an identity check
// across binaries -- every diagnostic whole, every edge with its target, every
// failed reference. Written off the clock, after the last repetition.
inline void dump_document(std::FILE* out, const cursiv::Document& d) {
  using namespace cursiv;
  std::fprintf(out, "doc %s\n", d.uri.c_str());
  for (const Diagnostic& g : d.diagnostics) {
    std::string code = "-";
    if (g.code && g.code->isString()) code = g.code->string();
    if (g.code && g.code->isInteger()) code = std::to_string(g.code->integer());
    const std::string data =
        g.data ? g.data->toJsonString({.pretty = false}) : "-";
    std::fprintf(out, "diag %u %u %d %s %.*s | %s | %s\n", g.range.begin,
                 g.range.end, static_cast<int>(g.severity), code.c_str(),
                 static_cast<int>(g.source.size()), g.source.data(),
                 g.message.c_str(), data.c_str());
  }
  const DocumentReferences* const references = d.parseResult.references.get();
  if (references == nullptr) return;
  references->visitResolved(
      d.parseResult.cst(),
      [&](TextOffset begin, TextOffset end, const AstNode& target, bool multi) {
        const Document* const home = target.getDocument();
        const CstNode* const at =
            home != nullptr && target.cstNodeId != kNoNode
                ? &home->parseResult.cst()[target.cstNodeId]
                : nullptr;
        std::fprintf(out, "ref %u %u %d -> %s %.*s %u %u\n", begin, end,
                     multi ? 1 : 0, home != nullptr ? home->uri.c_str() : "?",
                     static_cast<int>(target.type->name.size()),
                     target.type->name.data(), at != nullptr ? at->begin : 0,
                     at != nullptr ? at->end : 0);
      });
  references->visitUnresolved(
      d.parseResult.cst(), [&](RefState why, TextOffset begin, TextOffset end,
                               const ReferenceInfo& site) {
        std::fprintf(out, "unresolved %d %u %u %.*s\n", static_cast<int>(why),
                     begin, end, static_cast<int>(site.referenceText.size()),
                     site.referenceText.data());
      });
}

inline void dump_build(const std::string& name,
                       std::span<cursiv::Document* const> documents) {
  const char* const dir = std::getenv("CURSIV_BENCH_DUMP");
  if (dir == nullptr) return;
  const std::string path = std::string(dir) + "/" + name + ".txt";
  std::FILE* const out = std::fopen(path.c_str(), "w");
  if (out == nullptr) refused(name.c_str(), "not dumped: the file won't open");
  for (const cursiv::Document* d : documents) dump_document(out, *d);
  std::fclose(out);
}

// `resident` is `--rss`: one build, read for resident bytes alone. Otherwise
// each repetition is a counted build, when the PMU answers, then a timed one.
template <class Shared>
void ws_stages(int iterations, bool resident, const char* languageId,
               const char* extension, const std::string& src, Row& row,
               bool (*register_)(cursiv::SharedCoreServices&)) {
  using namespace cursiv;
  enum class Probe : std::uint8_t { Clock, Counters, Resident };

  double best[kStageCount];
  for (double& v : best) v = 1e30;
  bool timed = false;
  bool validated = false;
  PerfCounters perf;
  const int builds = resident ? 1 : perf.active() ? 2 * iterations : iterations;

  for (int n = 0; n < builds; ++n) {
    const Probe probe = resident                      ? Probe::Resident
                        : perf.active() && n % 2 == 0 ? Probe::Counters
                                                      : Probe::Clock;
    Shared shared;
    // One document never fans out, and a pool raised and joined around each
    // build leaves the next one on cold caches.
    shared.taskScheduler = std::make_unique<TaskScheduler>(0);
    install_default_shared_lsp_services(shared);
    if (!register_(shared)) refused(languageId, "not registered");
    const CoreServices& core = shared.serviceRegistry->getServices(
        std::string("file:///bench/main") + extension);

    Documents& docs = *shared.documents;
    const DocumentBuilder& builder = *shared.documentBuilder;
    const std::shared_ptr<Document> held = unparsed_input(
        core, std::string("file:///bench/main") + extension, src);
    docs.addDocument(held);
    Document& d = *held;
    Document* one = held.get();
    const std::span<Document* const> all(&one, 1);

    PhaseClock clock;
    shared.documentBuilder->addBuildPhaseListener(clock);
    if (probe == Probe::Counters) {
      perf.reset();
      perf.enable();
    }
    // Brought down so the peak is this build's, not the process's.
    if (probe == Probe::Resident) rss_reset();
    clock.start(probe == Probe::Counters ? &perf : nullptr,
                probe == Probe::Resident);
    builder.build(all);
    if (probe == Probe::Counters) perf.disable();
    shared.documentBuilder->removeBuildPhaseListener(clock);
    if (d.state.load() != DocumentState::Validated)
      refused(languageId, "left below Validated by the builder");
    if (!clock.reached(DocumentState::Validated))
      refused(languageId, "the build drained no Validated phase");
    if (!d.parseResult.diagnostics.empty())
      refused(languageId, "refused by the parser inside the workspace");

    switch (probe) {
      case Probe::Counters:
        row.record_perf(clock.span(DocumentState::Validated),
                        {clock.counts(DocumentState::IndexedContent),
                         clock.counts(DocumentState::IndexedContent,
                                      DocumentState::ComputedScopes),
                         clock.counts(DocumentState::ComputedScopes,
                                      DocumentState::IndexedReferences),
                         clock.counts(DocumentState::IndexedReferences,
                                      DocumentState::Validated),
                         clock.counts(DocumentState::Validated)});
        break;
      case Probe::Resident:
        row.rss[kIndex] = clock.held(DocumentState::IndexedContent);
        row.rss[kScopes] = clock.held(DocumentState::IndexedContent,
                                      DocumentState::ComputedScopes);
        row.rss[kLink] = clock.held(DocumentState::ComputedScopes,
                                    DocumentState::IndexedReferences);
        row.rss[kValidate] = clock.held(DocumentState::IndexedReferences,
                                        DocumentState::Validated);
        row.rss[kBuild] = clock.held(DocumentState::Validated);
        break;
      case Probe::Clock:
        timed = true;
        best[kBuild] =
            std::min(best[kBuild], clock.span(DocumentState::Validated));
        best[kIndex] =
            std::min(best[kIndex], clock.span(DocumentState::IndexedContent));
        best[kScopes] =
            std::min(best[kScopes], clock.span(DocumentState::IndexedContent,
                                               DocumentState::ComputedScopes));
        best[kLink] =
            std::min(best[kLink], clock.span(DocumentState::ComputedScopes,
                                             DocumentState::IndexedReferences));
        // A language with no check and nothing to publish has no validation
        // to time -- see `kValidateAbsentWhy`.
        if (const ValidationRegistry* registry = core.validationRegistry.get();
            core.documentValidator != nullptr &&
            ((registry != nullptr && registry->checkCount() != 0) ||
             !d.diagnostics.empty())) {
          best[kValidate] = std::min(
              best[kValidate], clock.span(DocumentState::IndexedReferences,
                                          DocumentState::Validated));
          validated = true;
        }
        break;
    }

    if (n + 1 == builds) {
      const DocumentReferences* references = d.parseResult.references.get();
      std::size_t resolved = 0;
      if (references != nullptr)
        references->visitResolved(
            d.parseResult.cst(),
            [&](TextOffset, TextOffset, const AstNode&, bool) { ++resolved; });
      row.references = references != nullptr ? references->handles.size() : 0;
      row.resolved = resolved;
      row.diagnostics = d.diagnostics.size();
      dump_build(std::string(languageId) + "-" + std::to_string(src.size()),
                 all);
    }
  }

  if (!timed) return;
  row.ms[kIndex] = best[kIndex];
  row.ms[kScopes] = best[kScopes];
  // Zero references make the stage an empty loop whose throughput would read
  // as millions of MiB/s: absent, like `validate`.
  row.ms[kLink] = row.references == 0 ? kAbsent : best[kLink];
  row.ms[kBuild] = best[kBuild];
  row.ms[kValidate] = validated ? best[kValidate] : kAbsent;
}

// N documents, one build, one pool width per point. The two numbers per
// point are intervals of the same cold build, read at its phase boundaries:
//
//   total  the whole build from `Changed` to the `Validated` drain
//   link   everything after phase 1 -- `IndexedContent` to `Validated`:
//          scopes, linking, validation
//
// The two halves scale differently: phase 1 is per document and reads
// nothing else, phase 2 reaches the shared index.
//
// The pool width is installed, not wished for: it lives on the workspace's
// shared scheduler, installed at a known width before the defaults fill --
// affinity would not do, `hardware_concurrency()` ignores it.
//
// `register_` is the same hook the single-document lane takes, passed here
// too on purpose: `linkMs` spans validation as well as linking, so a
// language whose server runs checks must run them here too.
// A worker on a wasm host is an isolate that boots after the pool has
// returned, tens of milliseconds later: the clock starts once every one of
// them has run a part, or the lane would time workers still starting.
inline void warm_pool(cursiv::TaskScheduler& scheduler, std::size_t workers) {
  if (workers == 0) return;
  const cursiv::CancellationToken token;
  const std::vector<int> range(64, 0);
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  for (;;) {
    std::mutex lock;
    std::set<std::thread::id> seen;
    scheduler.parallelFor(token, range, [&](int) {
      const auto until =
          std::chrono::steady_clock::now() + std::chrono::microseconds(500);
      while (std::chrono::steady_clock::now() < until) {
      }
      const std::lock_guard guard(lock);
      seen.insert(std::this_thread::get_id());
    });
    if (seen.size() > workers || std::chrono::steady_clock::now() > deadline)
      return;
  }
}

template <class Shared>
void multi_stages(int iterations, const char* languageId, const char* extension,
                  const std::vector<std::string>& files,
                  const std::vector<std::size_t>& workerPoints, MultiRow& row,
                  bool (*register_)(cursiv::SharedCoreServices&)) {
  using namespace cursiv;

  for (const std::size_t workers : workerPoints) {
    double bestTotal = 1e30;
    double bestLink = 1e30;
    for (int i = 0; i < iterations; ++i) {
      Shared shared;
      shared.taskScheduler = std::make_unique<TaskScheduler>(workers);
      warm_pool(*shared.taskScheduler, workers);
      install_default_shared_lsp_services(shared);
      if (!register_(shared)) refused(languageId, "not registered");

      Documents& docs = *shared.documents;
      const DocumentBuilder& builder = *shared.documentBuilder;
      std::vector<std::shared_ptr<Document>> held;
      std::vector<Document*> all;
      held.reserve(files.size());
      all.reserve(files.size());
      for (std::size_t f = 0; f < files.size(); ++f) {
        std::string uri = "file:///bench/ws/" + std::to_string(f) + extension;
        const CoreServices& core = shared.serviceRegistry->getServices(uri);
        held.push_back(unparsed_input(core, std::move(uri), files[f]));
        docs.addDocument(held.back());
        all.push_back(held.back().get());
      }

      {
        PhaseClock clock;
        shared.documentBuilder->addBuildPhaseListener(clock);
        clock.start();
        builder.build(all);
        shared.documentBuilder->removeBuildPhaseListener(clock);
        for (const Document* d : all)
          if (d->state.load() != DocumentState::Validated)
            refused(languageId, "left below Validated by the builder");
        if (!clock.reached(DocumentState::Validated))
          refused(languageId, "the build drained no Validated phase");
        bestTotal = std::min(bestTotal, clock.span(DocumentState::Validated));
        bestLink = std::min(bestLink, clock.span(DocumentState::IndexedContent,
                                                 DocumentState::Validated));
      }
      // One file per pool width: the widths must publish the same thing.
      if (i + 1 == iterations)
        dump_build(std::string("multi-") + languageId + "-" +
                       std::to_string(files.size()) + "-w" +
                       std::to_string(workers),
                   all);

      if (i + 1 == iterations && workers == workerPoints.front()) {
        // Counted once, on the narrowest point: the graph does not depend on
        // the pool, and a row that reported it per point would invite the idea
        // that it might.
        std::size_t total = 0;
        std::size_t resolved = 0;
        std::size_t diagnostics = 0;
        for (const Document* d : all) {
          if (const DocumentReferences* references =
                  d->parseResult.references.get()) {
            total += references->handles.size();
            references->visitResolved(
                d->parseResult.cst(),
                [&](TextOffset, TextOffset, const AstNode&, bool) {
                  ++resolved;
                });
          }
          diagnostics += d->diagnostics.size();
        }
        row.references = total;
        row.resolved = resolved;
        row.diagnostics = diagnostics;
      }
    }
    row.points.push_back(MultiPoint{
        .workers = workers, .totalMs = bestTotal, .linkMs = bestLink});
  }
}

// Providers run on one validated document under the workspace read lock.
// Building, position selection and lock acquisition are outside timed calls.
void lsp_stages(int iterations, SharedLspServices& shared,
                const LspServices& services, const Document& document,
                Row& row);

template <class Shared, bool (*register_)(SharedCoreServices&)>
std::unique_ptr<Shared> language_host() {
  auto shared = std::make_unique<Shared>();
  install_default_shared_lsp_services(*shared);
  if (!register_(*shared) || shared->serviceRegistry->all().empty())
    refused("language", "not registered for the benchmark");
  return shared;
}

template <class Shared, bool (*register_)(SharedCoreServices&)>
void measure_lsp(int iterations, const std::string& src, Row& row) {
  auto shared = language_host<Shared, register_>();
  const CoreServices& core = *shared->serviceRegistry->all().front();
  const auto* services = dynamic_cast<const LspServices*>(&core);
  if (services == nullptr)
    refused(row.language.c_str(), "missing LSP services");
  const auto& meta = core.languageMetaData;
  const std::string extension =
      meta.fileExtensions.empty() ? "" : meta.fileExtensions.front();
  auto held = unparsed_input(core, "file:///bench/main" + extension, src);
  shared->documents->addDocument(held);
  Document* document = held.get();
  shared->documentBuilder->build(std::span<Document* const>(&document, 1));
  if (document->state.load() != DocumentState::Validated ||
      document->parseRecovered())
    refused(meta.languageId.c_str(), "not validated before the LSP probes");
  if (!shared->workspaceLock->read(
          [&] { lsp_stages(iterations, *shared, *services, *document, row); }))
    refused(meta.languageId.c_str(), "cancelled during the LSP probes");
}

// Metadata strings must outlive the descriptor. The registration hook is the
// language server's hook, including its custom providers and validation checks.
template <class Shared, bool (*register_)(SharedCoreServices&)>
Language make_language(const char* name, const char* languageId,
                       const char* extension,
                       std::string (*corpus)(std::size_t) = nullptr,
                       std::string (*multiFile)(std::size_t,
                                                std::size_t) = nullptr,
                       Refs multiRefs = Refs::Intra) {
  Language language{name,
                    languageId,
                    extension,
                    corpus,
                    +[](const std::string& src) {
                      auto shared = language_host<Shared, register_>();
                      const CoreServices& core =
                          *shared->serviceRegistry->all().front();
                      parse_probe(core.languageMetaData.languageId.c_str(),
                                  *core.parser, src);
                    },
                    nullptr,
                    multiFile,
                    nullptr,
                    multiRefs};
#ifndef CURSIV_BENCH_PARSE_ONLY
  language.workspace = +[](int iterations, bool resident,
                           const std::string& src, Row& row) {
    auto shared = language_host<Shared, register_>();
    const auto& meta = shared->serviceRegistry->all().front()->languageMetaData;
    const char* ext =
        meta.fileExtensions.empty() ? "" : meta.fileExtensions.front().c_str();
    ws_stages<Shared>(iterations, resident, meta.languageId.c_str(), ext, src,
                      row, register_);
  };
  if (multiFile != nullptr)
    language.multi =
        +[](int iterations, const std::vector<std::string>& files,
            const std::vector<std::size_t>& workers, MultiRow& row) {
          auto shared = language_host<Shared, register_>();
          const auto& meta =
              shared->serviceRegistry->all().front()->languageMetaData;
          const char* ext = meta.fileExtensions.empty()
                                ? ""
                                : meta.fileExtensions.front().c_str();
          multi_stages<Shared>(iterations, meta.languageId.c_str(), ext, files,
                               workers, row, register_);
        };
  language.lsp = &measure_lsp<Shared, register_>;
#endif
  return language;
}

// A language executable needs only its shared services and registration hook.
// Optional generators enable the size ladder and multi-document worker curve.
template <class Shared, bool (*register_)(SharedCoreServices&)>
int run_language(int argc, char** argv,
                 std::string (*corpus)(std::size_t) = nullptr,
                 std::string (*multiFile)(std::size_t, std::size_t) = nullptr,
                 Refs multiRefs = Refs::Intra) {
  auto host = language_host<Shared, register_>();
  const auto& metadata = host->serviceRegistry->all().front()->languageMetaData;
  const char* extension = metadata.fileExtensions.empty()
                              ? ""
                              : metadata.fileExtensions.front().c_str();
  const Language language = make_language<Shared, register_>(
      metadata.languageId.c_str(), metadata.languageId.c_str(), extension,
      corpus, multiFile, multiRefs);
  return run(argc, argv, std::span<const Language>(&language, 1));
}

}  // namespace cursiv::bench
