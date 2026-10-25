// recovery_metric -- the quality of error repair, per grammar and per class
// of fault: valid documents, faults planted in them from a seed, and every
// faulted copy weighed against its twin by the benchmark's support code.
//
//   recovery_metric [--docs N] [--seed S] [--reps R] [--only a,b]
//                   [--json PATH] [--cpu N | --no-cost] [--trace]
//                   [--budget FLOOR,PERBYTE,PERSEARCH]

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sched.h>
#endif

#include "cursiv/bench/PerfCounters.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/core/syntax-tree/AstReflection.hpp"
#include "cursiv/core/syntax-tree/ParseDiagnostic.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/engines/shared/runtime/RepairBudget.hpp"
#include "recovery_metric/support/FaultClass.hpp"
#include "recovery_metric/support/FaultCorpus.hpp"
#include "recovery_metric/support/FaultWindow.hpp"
#include "recovery_metric/support/RepairCase.hpp"
#include "recovery_metric/support/RepairFault.hpp"
#include "recovery_metric/support/RepairJudge.hpp"
#include "recovery_metric/support/RepairReport.hpp"
#include "recovery_metric/support/RepairTally.hpp"
#include "recovery_metric/support/RepairTwin.hpp"
#include "recovery_metric/support/RepairVerdict.hpp"
#include "recovery_metric/MetricLanguage.hpp"

namespace {

struct Options {
  unsigned docs = 40;
  std::uint64_t seed = 0x5eed;
  int reps = 2;
  int cpu = -2;  // -2: the first performance core, -1: unpinned
  bool cost = true;
  // One line per faulted document on stderr: its faults, windows, and what
  // was said.
  bool trace = false;
  // What a parse's repairs may spend, where the language's engine takes it.
  cursiv::RepairBudget budget;
  std::vector<std::string> only;
  std::string json;
};

// The nesting a deepened twin adds: past the deepest truncation class.
constexpr unsigned kDeepen = 18;

// The first core of the performance type on a hybrid part: pinned there,
// the counter runs for the whole window instead of being scaled.
int performance_core() {
  std::ifstream f("/sys/devices/cpu_core/cpus");
  int cpu = -1;
  if (f >> cpu) return cpu;
  return -1;
}

bool pin(int cpu) {
#ifdef __linux__
  if (cpu < 0) return false;
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return sched_setaffinity(0, sizeof set, &set) == 0;
#else
  (void)cpu;
  return false;
#endif
}

// The fewest instructions of `reps` parses, 0 when no clean count was read;
// the result of the last one is kept for the verdict.
std::uint64_t parse(const cursiv::bench::PerfCounters& pc, int reps,
                    const cursiv::Parser& parser, std::string_view text,
                    std::optional<cursiv::ParseResult>& out) {
  std::uint64_t best = 0;
  for (int r = 0; r < std::max(reps, 1); ++r) {
    out.reset();
    const cursiv::bench::PerfSample from = pc.read();
    pc.enable();
    out.emplace(parser.parse(text));
    pc.disable();
    const cursiv::bench::PerfSample d =
        cursiv::bench::perf_delta(from, pc.read());
    if (d.valid && !d.scaled && d.instructions != 0 &&
        (best == 0 || d.instructions < best))
      best = d.instructions;
  }
  return best;
}

bool is_eof(cursiv::FaultClass k) {
  return k == cursiv::FaultClass::eof1 || k == cursiv::FaultClass::eof3 ||
         k == cursiv::FaultClass::eof7 || k == cursiv::FaultClass::eof20;
}

void escaped(std::string_view s) {
  for (const char ch : s)
    if (ch >= 0x20 && ch < 0x7f)
      std::fputc(ch, stderr);
    else
      std::fprintf(stderr, "\\x%02x", static_cast<unsigned char>(ch));
}

void trace(std::string_view lang, cursiv::FaultClass cls,
           const cursiv::RepairTwin& twin, const cursiv::RepairCase& c,
           const cursiv::ParseResult& got, const cursiv::RepairVerdict& v) {
  constexpr std::string_view kKind[] = {"ins", "del",  "rep",
                                        "inc", "conv", "bnd"};
  const std::string_view name = cursiv::fault_class_name(cls);
  std::fprintf(stderr, "%.*s %.*s |", static_cast<int>(lang.size()),
               lang.data(), static_cast<int>(name.size()), name.data());
  const auto windows = cursiv::FaultWindow::of(twin.tokens, twin.hidden, c);
  for (std::size_t i = 0; i < c.faults.size(); ++i) {
    const cursiv::RepairFault& f = c.faults[i];
    std::fprintf(stderr, " @%u-%u+\"", f.at, f.removed);
    escaped(f.inserted);
    std::fprintf(stderr, "\" w[%u,%u]", windows[i].lo, windows[i].hi);
  }
  std::fprintf(stderr, " | said");
  for (const cursiv::ParseDiagnostic& d : got.cst().diagnostics())
    std::fprintf(stderr, " %.*s[%u,%u)",
                 static_cast<int>(kKind[static_cast<int>(d.kind)].size()),
                 kKind[static_cast<int>(d.kind)].data(), d.begin, d.end);
  std::fprintf(stderr,
               " | found %u miss %u para %u%s | text %llu/%llu del %llu aband "
               "%llu other %llu rest %u inv %u | ast %llu/%llu",
               v.found, v.missed, v.parasites, v.noop ? " noop" : "",
               static_cast<unsigned long long>(v.text.kept),
               static_cast<unsigned long long>(v.text.eligible),
               static_cast<unsigned long long>(v.text.deleted),
               static_cast<unsigned long long>(v.text.abandoned),
               static_cast<unsigned long long>(v.text.other), v.text.restored,
               v.text.invented, static_cast<unsigned long long>(v.ast.kept),
               static_cast<unsigned long long>(v.ast.total));
  std::fprintf(stderr, " | caps");
  for (std::size_t i = 0;
       i < v.ceilings.size() && i < recovery_metric::kCeilings.size(); ++i)
    if (v.ceilings[i] != 0)
      std::fprintf(stderr, " %.*s:%u",
                   static_cast<int>(recovery_metric::kCeilings[i].size()),
                   recovery_metric::kCeilings[i].data(), v.ceilings[i]);
  std::fputc('\n', stderr);
}

struct Twin {
  std::unique_ptr<cursiv::RepairTwin> twin;
  std::uint64_t instructions = 0;
  // What the valid text alone makes the parser count: a ceiling is the
  // repair's only past it.
  recovery_metric::Ceilings ceilings{};
};

cursiv::RepairReport::Section run(const recovery_metric::MetricLanguage& lang,
                                  const Options& o,
                                  const cursiv::bench::PerfCounters& pc) {
  cursiv::RepairReport::Section s;
  s.name = std::string(lang.name);
  const std::unique_ptr<cursiv::Parser> parser =
      lang.searching != nullptr ? lang.searching(o.budget) : lang.parser();
  const std::unique_ptr<cursiv::AstReflection> reflection = lang.reflection();
  std::vector<Twin> twins, deep;
  std::optional<cursiv::ParseResult> scratch;
  for (std::string& text : lang.documents()) {
    if (text.empty()) continue;
    auto t = cursiv::RepairTwin::read(*parser, std::move(text));
    if (t == nullptr) {
      std::fprintf(stderr, "%s: a valid document is not taken clean, skipped\n",
                   s.name.c_str());
      continue;
    }
    twins.push_back(Twin{std::move(t), 0, {}});
  }
  for (std::size_t i = 0; i < twins.size() && deep.size() < 2; ++i)
    if (auto t = cursiv::FaultCorpus::deepen(*parser, *twins[i].twin, kDeepen,
                                             o.seed + i))
      deep.push_back(Twin{std::move(t), 0, {}});
  s.twins = static_cast<unsigned>(twins.size());
  s.deepened = static_cast<unsigned>(deep.size());
  for (std::vector<Twin>* pool : {&twins, &deep})
    for (Twin& t : *pool) {
      if (o.cost) {
        (void)parse(pc, 1, *parser, t.twin->text, scratch);
        t.instructions = parse(pc, o.reps + 1, *parser, t.twin->text, scratch);
      }
      if (lang.ceilings != nullptr)
        t.ceilings = lang.ceilings(t.twin->text, o.budget);
    }

  const cursiv::RepairJudge judge{reflection.get(), lang.actions};
  for (std::size_t k = 0; k < cursiv::kFaultClasses; ++k) {
    const auto cls = static_cast<cursiv::FaultClass>(k);
    std::vector<const Twin*> pool;
    for (const Twin& t : twins) pool.push_back(&t);
    if (is_eof(cls))
      for (const Twin& t : deep) pool.push_back(&t);
    cursiv::RepairTally tally;
    if (!pool.empty()) {
      // Each twin in turn, so a twin short of sites leaves its share to the
      // others rather than to nobody.
      std::vector<std::vector<cursiv::RepairCase>> drawn;
      for (std::size_t i = 0; i < pool.size(); ++i)
        drawn.push_back(cursiv::FaultCorpus::draw(*pool[i]->twin, cls, o.docs,
                                                  o.seed + 0x100 * i));
      unsigned taken = 0;
      for (std::size_t j = 0; j < o.docs && taken < o.docs; ++j)
        for (std::size_t i = 0; i < pool.size() && taken < o.docs; ++i) {
          if (j >= drawn[i].size()) continue;
          const cursiv::RepairCase& c = drawn[i][j];
          const std::uint64_t ins =
              parse(pc, o.cost ? o.reps : 1, *parser, c.text, scratch);
          cursiv::RepairVerdict v = judge.judge(*pool[i]->twin, c, *scratch);
          if (ins != 0 && pool[i]->instructions != 0)
            v.cost = static_cast<double>(ins) /
                     static_cast<double>(pool[i]->instructions);
          if (lang.ceilings != nullptr) {
            const recovery_metric::Ceilings hit =
                lang.ceilings(c.text, o.budget);
            for (std::size_t n = 0; n < hit.size(); ++n)
              v.ceilings.push_back(hit[n] > pool[i]->ceilings[n]
                                       ? hit[n] - pool[i]->ceilings[n]
                                       : 0);
          }
          if (o.trace) trace(lang.name, cls, *pool[i]->twin, c, *scratch, v);
          tally.add(v);
          ++taken;
        }
    }
    s.rows.push_back(cursiv::RepairReport::Row{cls, std::move(tally)});
  }
  return s;
}

// `FLOOR,PERBYTE,PERSEARCH,PERPARSE`, each a count or `inf`.
bool read_budget(std::string_view arg, cursiv::RepairBudget& b) {
  std::uint64_t v[4];
  std::stringstream ss{std::string(arg)};
  std::string part;
  for (std::uint64_t& x : v) {
    if (!std::getline(ss, part, ',') || part.empty()) return false;
    char* end = nullptr;
    x = part == "inf" ? ~std::uint64_t{0}
                      : std::strtoull(part.c_str(), &end, 0);
    if (part != "inf" && *end != '\0') return false;
  }
  b = {v[0], v[1], v[2], v[3]};
  return !std::getline(ss, part);
}

std::string budget_text(std::uint64_t v) {
  return v == ~std::uint64_t{0} ? "inf" : std::to_string(v);
}

}  // namespace

int main(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string_view a = argv[i];
    const bool more = i + 1 < argc;
    if (a == "--docs" && more)
      o.docs = static_cast<unsigned>(std::strtoul(argv[++i], nullptr, 0));
    else if (a == "--seed" && more)
      o.seed = std::strtoull(argv[++i], nullptr, 0);
    else if (a == "--reps" && more)
      o.reps = std::atoi(argv[++i]);
    else if (a == "--cpu" && more)
      o.cpu = std::atoi(argv[++i]);
    else if (a == "--no-cost")
      o.cost = false;
    else if (a == "--trace")
      o.trace = true;
    else if (a == "--budget" && more && read_budget(argv[i + 1], o.budget))
      ++i;
    else if (a == "--json" && more)
      o.json = argv[++i];
    else if (a == "--only" && more) {
      std::stringstream ss(argv[++i]);
      for (std::string name; std::getline(ss, name, ',');)
        o.only.push_back(name);
    } else {
      std::fprintf(stderr,
                   "usage: %s [--docs N] [--seed S] [--reps R] [--only a,b] "
                   "[--json PATH] [--cpu N | --no-cost] [--trace] "
                   "[--budget FLOOR,PERBYTE,PERSEARCH,PERPARSE]\n",
                   argv[0]);
      return 2;
    }
  }

  cursiv::RepairReport report;
  report.docs = o.docs;
  report.repair = "search, budget " + budget_text(o.budget.floor) + " + " +
                  budget_text(o.budget.perByte) + "/byte up to " +
                  budget_text(o.budget.perParse) + ", " +
                  budget_text(o.budget.perSearch) + " per search";
  report.seed = o.seed;
  for (const std::string_view name : recovery_metric::kCeilings)
    report.ceilings.push_back(name);
  const int cpu = o.cpu == -2 ? performance_core() : o.cpu;
  const bool pinned = o.cost && pin(cpu);
  const cursiv::bench::PerfCounters pc;
  if (!o.cost)
    report.counter = "not measured (--no-cost)";
  else if (!pc.active())
    report.counter = "unavailable (no hardware counter)";
  else
    report.counter =
        "instructions:u, fewest of " + std::to_string(std::max(o.reps, 1)) +
        " parses" +
        (pinned ? ", pinned on cpu " + std::to_string(cpu) : ", unpinned");

  std::vector<recovery_metric::MetricLanguage> langs =
      recovery_metric::languages();
  std::sort(langs.begin(), langs.end(),
            [](const auto& a, const auto& b) { return a.rank < b.rank; });
  for (const recovery_metric::MetricLanguage& l : langs) {
    if (!o.only.empty() &&
        std::find(o.only.begin(), o.only.end(), l.name) == o.only.end())
      continue;
    report.sections.push_back(run(l, o, pc));
  }
  if (report.sections.empty()) {
    std::fprintf(stderr, "no language selected\n");
    return 2;
  }
  report.text(stdout);
  if (!o.json.empty()) {
    std::FILE* f = std::fopen(o.json.c_str(), "w");
    if (f == nullptr) {
      std::fprintf(stderr, "cannot write %s\n", o.json.c_str());
      return 1;
    }
    report.json(f);
    std::fclose(f);
  }
  return 0;
}
