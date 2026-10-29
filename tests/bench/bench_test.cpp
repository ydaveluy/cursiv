// The harness's own sums: the counters a hybrid CPU splits, a service's
// samples, and the rows of a language whose parser keeps no reference.
#include <array>
#include <memory>
#include <string_view>
#include <utility>

#include "cursiv/bench/LanguageBench.hpp"
#include "cursiv/core/parser/Parser.hpp"
#include "cursiv/test/Check.hpp"
#include "workspace/lsp/LspModule.hpp"

using namespace cursiv::bench;

namespace {

PerfReading reading(std::uint64_t enabled, std::uint64_t running,
                    std::uint64_t instructions) {
  PerfReading r;
  r.enabled = enabled;
  r.running = running;
  r.instructions = instructions;
  r.cycles = instructions * 2;
  r.branchMisses = instructions / 10;
  r.cacheMisses = instructions / 20;
  r.valid = true;
  return r;
}

PerfSample hybrid(PerfReading core, PerfReading atom) {
  PerfSample s;
  s.valid = true;
  s.pmuCount = 2;
  s.pmu = {core, atom};
  return s;
}

void hybrid_coverage() {
  const PerfSample start = hybrid(reading(0, 0, 0), reading(0, 0, 0));
  // Running entirely on either CPU type must not look like 50% PMU coverage.
  for (const bool onAtom : {false, true}) {
    const auto end = onAtom
                         ? hybrid(reading(100, 0, 0), reading(100, 100, 100))
                         : hybrid(reading(100, 100, 100), reading(100, 0, 0));
    const PerfSample delta = perf_delta(start, end);
    CHECK(delta.valid);
    CHECK(!delta.scaled);
    CHECK(delta.instructions == 100);
    CHECK(delta.cycles == 200);
  }

  // A migration counts both portions once, without extrapolating each PMU
  // independently to the entire thread interval.
  const PerfSample migrated =
      perf_delta(start, hybrid(reading(100, 40, 40), reading(100, 60, 60)));
  CHECK(migrated.valid);
  CHECK(!migrated.scaled);
  CHECK(migrated.instructions == 100);
  CHECK(migrated.cycles == 200);
  CHECK(migrated.branchMisses == 10);
  CHECK(migrated.cacheMisses == 5);

  // Multiplexing still needs correction, using combined running coverage.
  const PerfSample contended =
      perf_delta(start, hybrid(reading(100, 20, 20), reading(100, 30, 30)));
  CHECK(contended.valid);
  CHECK(contended.scaled);
  CHECK(contended.instructions == 100);
  CHECK(contended.cycles == 200);

  const PerfSample unscheduled =
      perf_delta(start, hybrid(reading(100, 0, 0), reading(100, 0, 0)));
  CHECK(!unscheduled.valid);
}

void cumulative_intervals() {
  const PerfSample start =
      hybrid(reading(1000, 400, 400), reading(1000, 600, 600));
  const PerfSample end =
      hybrid(reading(1100, 440, 440), reading(1100, 660, 660));
  const PerfSample delta = perf_delta(start, end);
  CHECK(delta.valid);
  CHECK(!delta.scaled);
  CHECK(delta.instructions == 100);
  CHECK(delta.running == 100);
  CHECK(delta.enabled == 100);

  // A missing read or a reset between snapshots is not a zero-cost region.
  auto missing = end;
  missing.pmu[1].valid = false;
  CHECK(!perf_delta(start, missing).valid);
  CHECK(!perf_delta(end, start).valid);

  PerfSample genericStart, genericEnd;
  static_cast<PerfReading&>(genericStart) = reading(100, 100, 100);
  static_cast<PerfReading&>(genericEnd) = reading(200, 150, 150);
  const PerfSample generic = perf_delta(genericStart, genericEnd);
  CHECK(generic.valid);
  CHECK(generic.scaled);
  CHECK(generic.instructions == 100);
}

void keep_valid_repetition() {
  Row row;
  std::array<PerfSample, kStageCount> first{};
  for (std::size_t stage = 0; stage < kStageCount; ++stage)
    static_cast<PerfReading&>(first[stage]) = reading(100, 100, 100 + stage);
  row.record_perf(10.0, first);

  std::array<PerfSample, kStageCount> absent{};
  row.record_perf(5.0, absent);
  CHECK(row.perfBuildMs == 10.0);
  CHECK(row.perf[kBuild].valid);
  CHECK(row.perf[kBuild].instructions == first[kBuild].instructions);

  auto next = first;
  for (auto& sample : next) sample.instructions += 200;
  row.record_perf(12.0, next);
  CHECK(row.perfBuildMs == 10.0);
  CHECK(row.perf[kBuild].instructions == first[kBuild].instructions);

  row.record_perf(8.0, next);
  CHECK(row.perfBuildMs == 8.0);
  for (std::size_t stage = 0; stage < kStageCount; ++stage)
    CHECK(row.perf[stage].instructions == next[stage].instructions);
}

void every_sample_counts() {
  ServiceMeasurement single;
  CHECK(single.samples == 0);
  CHECK(single.min == kAbsent);
  CHECK(single.ms == kAbsent);
  CHECK(single.worst == kAbsent);

  single.record(9.0);
  CHECK(single.samples == 1);
  CHECK(single.min == 9.0);
  CHECK(single.ms == 9.0);
  CHECK(single.worst == 9.0);

  // Include every repetition of every probe, even slower calls and zeroes.
  ServiceMeasurement sweep;
  for (const double elapsed : {3.0, 9.0, 6.0, 30.0, 12.0, 24.0})
    sweep.record(elapsed);
  CHECK(sweep.samples == 6);
  CHECK(sweep.min == 3.0);
  CHECK(sweep.ms == 14.0);
  CHECK(sweep.worst == 30.0);
  sweep.record(0.0);
  CHECK(sweep.samples == 7);
  CHECK(sweep.min == 0.0);
  CHECK(sweep.ms == 12.0);
  CHECK(sweep.worst == 30.0);
}

class WithoutReferences final : public cursiv::Parser {
 public:
  explicit WithoutReferences(std::unique_ptr<const cursiv::Parser> parser)
      : parser_(std::move(parser)) {}

  const cursiv::Grammar& getGrammar() const noexcept override {
    return parser_->getGrammar();
  }

  cursiv::ParseResult parse(std::string_view text,
                            const cursiv::CancellationToken& cancel =
                                cursiv::kNoCancel) const override {
    auto result = parser_->parse(text, cancel);
    CHECK(result.references && result.references->handles.empty());
    result.references.reset();
    return result;
  }

 private:
  std::unique_ptr<const cursiv::Parser> parser_;
};

bool register_language(cursiv::SharedCoreServices& shared) {
  auto services = ws::create_lsp_services(
      dynamic_cast<ws::WorkspaceSharedServices&>(shared));
  services->parser =
      std::make_unique<WithoutReferences>(std::move(services->parser));
  shared.serviceRegistry->registerServices(std::move(services));
  return true;
}

void a_parser_without_references_counts_none() {
  {
    cursiv::bench::Row row;
    row.references = row.resolved = 99;
    cursiv::bench::ws_stages<ws::WorkspaceSharedServices>(
        1, false, "workspace", ".ws", "datatype lowercase", row,
        register_language);
    CHECK(row.references == 0);
    CHECK(row.resolved == 0);
    CHECK(row.diagnostics == 1);
    CHECK(row.ms[cursiv::bench::kIndex] >= 0);
  }
  {
    cursiv::bench::MultiRow row;
    row.references = row.resolved = 99;
    cursiv::bench::multi_stages<ws::WorkspaceSharedServices>(
        1, "workspace", ".ws", {"datatype Text", "datatype lowercase"}, {0},
        row, register_language);
    CHECK(row.references == 0);
    CHECK(row.resolved == 0);
    CHECK(row.diagnostics == 1);
    CHECK(row.points.size() == 1);
  }
}

}  // namespace

int main() {
  hybrid_coverage();
  cumulative_intervals();
  keep_valid_repetition();
  every_sample_counts();
  a_parser_without_references_counts_none();
  return cursiv::test::report("bench_test");
}
