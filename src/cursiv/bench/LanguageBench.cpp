// Language benchmarks: one document build and its LSP providers per input. Time
// and throughput for each, on a ladder from 16 KiB to 128 MiB.
//
//   language_bench [options] [filter]
//   <language>_bench [options] file [file ...]
//     --full                 the whole ladder (adds 32 MiB and 128 MiB)
//     --sizes 16K,4M,...     an explicit ladder
//     --iterations N         override the per-size repetition count
//     --json PATH            machine-readable results
//     --csv PATH             the same rows, one line each
//     --dump-corpus DIR      write the payloads and a manifest, measure nothing
//     --corpus DIR           read the payloads from DIR instead of generating
//     --multi / --no-multi   the multi-document lane alone / not at all
//     --multi-bytes N        total bytes per shape (default 16M, 64M with
//     --full)
//     --workers 1,2,4,8      the scaling curve's points
//     --rss                  one language, one cold build: what it makes
//                            resident, phase by phase, and its peak
//     --list                 what would run
//     --file PATH            measure an existing file (repeatable)
//
// Two lanes: one document through every stage on a size ladder, and N
// documents through one `DocumentBuilder::build` at several pool widths --
// N separate builds would measure N serial workspaces, not parallelism.
//
// Env: CURSIV_BENCH_FILTER, CURSIV_BENCH_ITERATIONS, CURSIV_BENCH_BYTES (pins
// the ladder to one size, which is what an instruction count needs),
// CURSIV_BENCH_FULL, CURSIV_BENCH_JSON, CURSIV_IR_STAGE=api + CURSIV_IR_REPS
// (the instruction-count harness: the whole parse, N times, no clock),
// CURSIV_BENCH_DUMP=DIR (each build's diagnostics and resolutions, one file
// per row -- what an identity check diffs).
//
// On Linux, with a reachable PMU, every stage line also carries its window
// cost in hardware: cycles and instructions per byte, around the build
// call alone. No PMU, no columns -- see PerfCounters.
//
// One process, one framework, best of a few rounds -- a row here is
// comparable to another row here, and to nothing else.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef __linux__
#include <sched.h>
#endif

#include "cursiv/bench/LanguageBench.hpp"
#include "cursiv/core/execution/TaskScheduler.hpp"

namespace cursiv::bench {

void parse_probe(const char* name, const cursiv::Parser& engine,
                 const std::string& src) {
  {
    const cursiv::ParseResult r = engine.parse(src);
    if (!r.cst().accepted()) refused(name, "refused by the parser");
    if (!r || r.value == nullptr) refused(name, "refused by the builder");
  }

  // Instruction-count mode (experiment harness, off unless asked): the whole
  // parse, `api`, N repetitions, no clock -- `perf stat -e instructions:u`
  // counts the process. No other stage is isolated here, so no other name
  // is taken.
  if (const char* stage = std::getenv("CURSIV_IR_STAGE")) {
    if (std::string_view(stage) != "api")
      refused(name,
              "CURSIV_IR_STAGE names a stage this harness does not "
              "isolate -- `api` is the whole parse");
    int reps = 1;
    if (const char* r = std::getenv("CURSIV_IR_REPS"))
      reps = std::max(1, std::atoi(r));
    std::size_t sink = 0;
    for (int i = 0; i < reps; ++i) {
      const cursiv::ParseResult r = engine.parse(src);
      if (!r) refused(name, "refused mid-measurement");
      sink += reinterpret_cast<std::uintptr_t>(r.value) & 1u;
    }
    std::printf("[ir] %s api reps=%d sink=%zu\n", name, reps, sink);
    return;
  }
}

// The binary states its own age: a build that is not refreshed measures
// yesterday's tree, and the mtime of the running image is the one
// identity that cannot lie.
void print_binary_age() {
  std::error_code ec;
  const auto t = std::filesystem::last_write_time("/proc/self/exe", ec);
  if (ec) return;
  // `clock_cast` is absent from libc++, so the two epochs are bridged by the
  // difference between one reading of each -- the same answer to the second,
  // which is all a staleness banner needs.
  const auto sys =
      std::chrono::system_clock::now() +
      std::chrono::duration_cast<std::chrono::system_clock::duration>(
          t - std::filesystem::file_time_type::clock::now());
  const std::time_t tt = std::chrono::system_clock::to_time_t(sys);
  std::tm local{};
#ifdef _WIN32
  if (localtime_s(&local, &tt) != 0) return;
#else
  if (localtime_r(&tt, &local) == nullptr) return;
#endif
  char buf[32];
  if (std::strftime(buf, sizeof buf, "%F %T", &local) != 0)
    std::printf(
        "binary built %s -- stale unless newer than your last change"
        " (`cmake --build build --target bench`)\n",
        buf);
}

namespace {

std::size_t parse_size(std::string_view text) {
  char* end = nullptr;
  const std::string owned(text);
  const unsigned long long n = std::strtoull(owned.c_str(), &end, 10);
  auto bytes = static_cast<std::size_t>(n);
  if (end != nullptr && (*end == 'k' || *end == 'K'))
    bytes <<= 10;
  else if (end != nullptr && (*end == 'm' || *end == 'M'))
    bytes <<= 20;
  else if (end != nullptr && (*end == 'g' || *end == 'G'))
    bytes <<= 30;
  return bytes;
}

[[noreturn]] void usage_error(const std::string& message) {
  std::fprintf(stderr, "language_bench: %s\n", printable(message).c_str());
  std::fprintf(stderr,
               "usage: language_bench [--full] [--sizes L] [--iterations N]"
               " [--json P] [--csv P] [--dump-corpus D] [--corpus D]"
               " [--multi|--no-multi] [--multi-bytes N] [--workers L]"
               " [--rss]"
               " [--list] [--file PATH] [filter]\n"
               "       <language>_bench [options] file [file ...]\n");
  std::exit(2);
}

const char* env(const char* name) { return std::getenv(name); }

Options options(int argc, char** argv, std::span<const Language> languages) {
  Options o;
  std::vector<std::size_t> explicitSizes;
  const bool fileInputs =
      languages.size() == 1 && languages.front().corpus == nullptr;
  if (fileInputs) o.multi = false;

  const auto value = [&](int& i) -> std::string {
    if (i + 1 >= argc) usage_error(std::string(argv[i]) + " needs an argument");
    return argv[++i];
  };
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--help" || arg == "-h")
      o.help = true;
    else if (arg == "--file")
      o.files.push_back(value(i));
    else if (arg == "--") {
      while (++i < argc) o.files.emplace_back(argv[i]);
    } else if (arg == "--full")
      o.full = true;
    else if (arg == "--list")
      o.list = true;
    else if (arg == "--no-lsp")
      o.lsp = false;
    else if (arg == "--iterations")
      o.iterations = std::atoi(value(i).c_str());
    else if (arg == "--json")
      o.jsonPath = value(i);
    else if (arg == "--csv")
      o.csvPath = value(i);
    else if (arg == "--dump-corpus")
      o.dumpDir = value(i);
    else if (arg == "--corpus")
      o.corpusDir = value(i);
    else if (arg == "--rss") {
      o.rss = true;
      o.multi = false;
      o.iterations = 1;
    } else if (arg == "--exact")
      o.exact = true;
    else if (arg == "--no-multi")
      o.multi = false;
    else if (arg == "--multi") {
      o.stages = false;
      o.multi = true;
    } else if (arg == "--multi-bytes")
      o.multiBytes = parse_size(value(i));
    else if (arg == "--workers") {
      std::stringstream in(value(i));
      std::string token;
      while (std::getline(in, token, ','))
        if (!token.empty())
          o.workers.push_back(static_cast<std::size_t>(std::stoull(token)));
    } else if (arg == "--sizes") {
      std::stringstream in(value(i));
      std::string token;
      while (std::getline(in, token, ','))
        if (!token.empty()) explicitSizes.push_back(parse_size(token));
    } else if (arg.starts_with("--")) {
      usage_error("unknown option " + std::string(arg));
    } else if (fileInputs) {
      o.files.emplace_back(arg);
    } else if (o.filter.empty()) {
      o.filter = arg;
    } else {
      usage_error("more than one filter given");
    }
  }

  if (o.filter.empty())
    if (const char* v = env("CURSIV_BENCH_FILTER")) o.filter = v;
  if (o.iterations <= 0)
    if (const char* v = env("CURSIV_BENCH_ITERATIONS"))
      o.iterations = std::atoi(v);
  if (o.jsonPath.empty())
    if (const char* v = env("CURSIV_BENCH_JSON")) o.jsonPath = v;
  if (!o.full && env("CURSIV_BENCH_FULL") != nullptr) o.full = true;

  if (o.help) return o;
  if (!o.files.empty()) {
    if (!o.dumpDir.empty() || !o.corpusDir.empty())
      usage_error(
          "input files cannot be combined with --corpus or --dump-corpus");
    if (!o.stages)
      usage_error("input files are measured individually; use --no-multi");
    o.multi = false;
    if (o.rss && o.files.size() != 1)
      usage_error("--rss measures one input file per process");
  }
  bool any = false;
  bool anyMulti = false;
  for (const Language& language : languages) {
    if (!selected(o, language.name)) continue;
    any = true;
    anyMulti |= language.multi != nullptr;
    if (!o.list && o.files.empty() && o.corpusDir.empty() &&
        language.corpus == nullptr)
      usage_error(
          "this language needs input files: <language>_bench [options] file "
          "[file ...]");
    if (!o.dumpDir.empty() && language.corpus == nullptr)
      usage_error("--dump-corpus requires a corpus generator");
    if (o.rss && language.workspace == nullptr)
      usage_error("--rss requires workspace stages on this target");
  }
  if (!any) usage_error("the filter selects no language");
  if (!o.stages && !anyMulti)
    usage_error("--multi requires a multi-document corpus generator");
  if (!anyMulti) o.multi = false;

  // CURSIV_BENCH_BYTES pins the ladder to one size: one corpus, one figure,
  // the only shape an instruction count can be read from.
  if (!explicitSizes.empty()) {
    o.sizes = std::move(explicitSizes);
  } else if (const char* v = env("CURSIV_BENCH_BYTES")) {
    // A corpus smaller than this measures the generator's preamble, not the
    // language: statemachine alone opens with 70 declarations.
    o.sizes = {std::max<std::size_t>(
        static_cast<std::size_t>(std::strtoull(v, nullptr, 10)), 16u << 10)};
  } else if (o.full) {
    o.sizes.assign(std::begin(kFullSizes), std::end(kFullSizes));
  } else {
    o.sizes.assign(std::begin(kDefaultSizes), std::end(kDefaultSizes));
  }

  if (o.multiBytes == 0)
    o.multiBytes = o.full ? kFullMultiBytes : kDefaultMultiBytes;
  if (o.workers.empty()) {
    // 0, 1, 2, 4, 8, and the machine -- the last being what
    // `TaskScheduler::defaultWorkerCount()` would have chosen, so the widest
    // point is the one a real host runs.
    //
    // Zero is the serial baseline: the submitting thread works alongside the
    // pool, so `w` workers run `w + 1` documents at a time, and efficiency
    // divides by threads, not by `w`.
    const std::size_t machine = cursiv::TaskScheduler::defaultWorkerCount();
    o.workers.push_back(0);
    for (const std::size_t w :
         {std::size_t{1}, std::size_t{2}, std::size_t{4}, std::size_t{8}})
      if (w < machine) o.workers.push_back(w);
    if (machine != 0) o.workers.push_back(machine);
  }
  // One language, and the mode refuses rather than reports: the second
  // language of a run builds on an allocator the first already grew, so
  // its figures read low and look like a result.
  if (o.rss) {
    o.iterations = 1;
    if (o.files.empty() && o.sizes.size() != 1 && !o.list)
      usage_error("--rss needs exactly one corpus size (--sizes 16K)");
    // A name spelled whole is the answer, even where it is also the
    // beginning of another.
    for (const Language& language : languages)
      if (o.filter == language.name) o.filter = language.name;
    std::size_t n = 0;
    const char* first = nullptr;
    for (const Language& language : languages)
      if (o.filter == language.name) {
        n = 1;
        first = language.name;
        break;
      }
    if (n == 0)
      for (const Language& language : languages)
        if (selected(o, language.name)) {
          ++n;
          if (first == nullptr) first = language.name;
        }
    if (n == 0) usage_error("--rss: the filter selects no language");
    if (n > 1)
      usage_error(std::string("--rss measures one language at a time, and \"") +
                  o.filter + "\" selects " + std::to_string(n) +
                  " -- name one exactly (e.g. " + first + ")");
    o.filter = first;
    o.exact = true;
  }
  return o;
}

std::string corpus_path(const Options& o, const Language& language,
                        const std::string& label) {
  const std::string dir = o.corpusDir.empty() ? o.dumpDir : o.corpusDir;
  // Portable names: ASCII, one hyphen, the language's own extension.
  // Nothing a shell, a Windows path or a JSON string has to escape.
  return dir + "/" + language.name + "-" + label + language.extension;
}

std::string read_file(const std::string& path) {
  std::ifstream in(checked_path(path), std::ios::binary);
  if (!in) {
    std::fprintf(stderr, "language_bench: cannot read '%s': %s\n",
                 printable(path).c_str(), std::strerror(errno));
    std::exit(1);
  }
  std::ostringstream out;
  out << in.rdbuf();
  return out.str();
}

void write_file(const std::string& path, std::string_view content) {
  std::ofstream out(checked_path(path), std::ios::binary);
  if (!out) {
    std::fprintf(stderr, "language_bench: cannot write '%s': %s\n",
                 printable(path).c_str(), std::strerror(errno));
    std::exit(1);
  }
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

double throughput(std::size_t bytes, double ms) {
  return ms > 0.0
             ? (static_cast<double>(bytes) / (1024.0 * 1024.0)) / (ms / 1000.0)
             : 0.0;
}

// Group large counts without changing the numeric JSON and CSV exports.
std::string grouped(std::uint64_t value) {
  const std::string digits = std::to_string(value);
  std::string out;
  for (std::size_t i = 0; i < digits.size(); ++i) {
    if (i != 0 && (digits.size() - i) % 3 == 0) out += ',';
    out += digits[i];
  }
  return out;
}

void print_document_counts(std::size_t references, std::size_t resolved,
                           std::size_t diagnostics) {
  std::printf("\n  Document\n");
  std::printf("    %-22s %s / %s\n", "resolved references",
              grouped(resolved).c_str(), grouped(references).c_str());
  std::printf("    %-22s %s\n", "diagnostics", grouped(diagnostics).c_str());
}

void print_perf(const Row& row) {
  const PerfSample& p = row.perf[kBuild];
  if (!p.valid || row.bytes == 0) return;
  const auto bytes = static_cast<double>(row.bytes);
  std::printf("\n  Hardware (build)%s\n",
              p.scaled ? " — multiplexed counters, scaled" : "");
  std::printf("    %-22s %18s\n", "cycles", grouped(p.cycles).c_str());
  std::printf("    %-22s %18s\n", "instructions",
              grouped(p.instructions).c_str());
  std::printf("    %-22s %18.3f\n", "IPC",
              p.cycles != 0 ? static_cast<double>(p.instructions) /
                                  static_cast<double>(p.cycles)
                            : 0.0);
  std::printf("    %-22s %18.4f\n", "branch misses / byte",
              static_cast<double>(p.branchMisses) / bytes);
  std::printf("    %-22s %18.5f\n", "LLC misses / byte",
              static_cast<double>(p.cacheMisses) / bytes);
}

// Each resident delta is an interval of the same cold build.
void print_rss_row(const Row& row) {
  std::printf("\n[rss] %s-%s  %s B  one cold build\n", row.language.c_str(),
              printable(row.label).c_str(), grouped(row.bytes).c_str());
  std::printf("\n  %-14s %16s\n", "Stage", "resident (MiB)");
  for (std::size_t stage = 0; stage < kStageCount; ++stage) {
    if (!row.rss[stage].valid) continue;
    if (stage == kBuild) std::printf("\n");
    std::printf(
        "  %-14s %+16.1f\n", kStageName[stage],
        static_cast<double>(row.rss[stage].current) / (1024.0 * 1024.0));
  }
  const RssSample& r = row.rss[kBuild];
  if (r.valid && row.bytes != 0) {
    std::printf("\n  %-22s %12.1f MiB\n", "peak",
                static_cast<double>(r.peak) / (1024.0 * 1024.0));
    std::printf(
        "  %-22s %12.2f\n", "resident / byte",
        static_cast<double>(r.current) / static_cast<double>(row.bytes));
  }
}

void print_row(const Row& row) {
  std::printf("\n[bench] %s-%s  %s B  %d iteration%s\n", row.language.c_str(),
              printable(row.label).c_str(), grouped(row.bytes).c_str(),
              row.iterations, row.iterations == 1 ? "" : "s");
  const bool hardware =
      row.bytes != 0 && std::ranges::any_of(row.perf, [](const PerfSample& p) {
        return p.valid;
      });
  std::printf("\n  Pipeline\n");
  std::printf("    %-14s %12s %12s", "Stage", "best (ms)", "MiB/s");
  if (hardware) std::printf(" %12s %12s", "cycles/B", "instr/B");
  std::printf("\n");
  for (std::size_t stage = 0; stage < kStageCount; ++stage) {
    if (row.ms[stage] < 0.0) continue;
    if (stage == kBuild) std::printf("\n");
    std::printf("    %-14s %12.3f %12.1f", kStageName[stage], row.ms[stage],
                throughput(row.bytes, row.ms[stage]));
    if (const PerfSample& p = row.perf[stage]; hardware && p.valid)
      std::printf(
          " %12.2f %12.2f",
          static_cast<double>(p.cycles) / static_cast<double>(row.bytes),
          static_cast<double>(p.instructions) / static_cast<double>(row.bytes));
    std::printf("\n");
  }
  print_document_counts(row.references, row.resolved, row.diagnostics);
  print_perf(row);
  if (std::ranges::any_of(row.lsp, [](const ServiceMeasurement& service) {
        return service.ms >= 0;
      })) {
    std::printf("\n  LSP providers (validated document, direct calls)\n");
    const bool repeated = std::ranges::any_of(
        row.lsp,
        [](const ServiceMeasurement& service) { return service.samples > 1; });
    std::printf("    %-26s %s\n", "Service",
                repeated ? "duration (ms; min / avg / max)" : "duration (ms)");
    for (const ServiceMeasurement& service : row.lsp) {
      if (service.ms < 0) continue;
      std::printf("    %-26s %10.3f", service.name.c_str(),
                  service.samples > 1 ? service.min : service.ms);
      if (service.samples > 1)
        std::printf(" / %10.3f / %10.3f", service.ms, service.worst);
      std::printf("\n");
    }
  }
  std::printf("\n");
}

// The affinity mask, which the pool ignores: under `taskset` every width
// shares the pinned CPUs. Zero where the platform cannot say.
std::size_t usable_cpus() {
#ifdef __linux__
  if (cpu_set_t set; sched_getaffinity(0, sizeof set, &set) == 0)
    return static_cast<std::size_t>(CPU_COUNT(&set));
#endif
  return 0;
}

// The submitting thread is one of them; zero workers is one thread, inline.
double threads_of(std::size_t workers) {
  return static_cast<double>(workers == 0 ? 1 : workers + 1);
}

// The scaling curve: `speedup` against the serial point, `efficiency` =
// speedup / threads. Near 1 means the phase really is spread; a curve
// that flattens says where it stops being.
// `phase1` is `total - link`, a subtraction of two measured windows, not
// a third clock: both halves are whole builds through the real builder.
void print_multi(const MultiRow& row) {
  std::printf("[multi] %s/%s refs=%s files=%zu bytes=%zu iterations=%d\n",
              row.language.c_str(), row.shape.c_str(), refs_name(row.refs),
              row.files, row.bytes, row.iterations);
  std::printf("  %7s %11s %13s %11s %11s %9s %11s\n", "workers", "total(ms)",
              "total(MiB/s)", "phase1(ms)", "link(ms)", "speedup",
              "efficiency");
  if (row.points.empty()) return;
  const double baseTotal = row.points.front().totalMs;
  const double baseLink = row.points.front().linkMs;
  const double baseThreads = threads_of(row.points.front().workers);
  for (const MultiPoint& point : row.points) {
    const double speedup =
        point.totalMs > 0.0 ? baseTotal / point.totalMs : 0.0;
    const double width = threads_of(point.workers) / baseThreads;
    std::printf("  %7zu %11.2f %13.1f %11.2f %11.2f %9.2f %11.2f\n",
                point.workers, point.totalMs,
                throughput(row.bytes, point.totalMs),
                point.totalMs - point.linkMs, point.linkMs, speedup,
                width > 0.0 ? speedup / width : 0.0);
  }
  const MultiPoint& widest = row.points.back();
  std::printf("\n  Link speedup: %.2fx at %zu workers\n",
              widest.linkMs > 0.0 ? baseLink / widest.linkMs : 0.0,
              widest.workers);
  std::printf("  Phase-1 speedup: %.2fx\n",
              (widest.totalMs - widest.linkMs) > 0.0
                  ? (baseTotal - baseLink) / (widest.totalMs - widest.linkMs)
                  : 0.0);
  print_document_counts(row.references, row.resolved, row.diagnostics);
  std::printf("\n");
}

std::string multi_json(const std::vector<MultiRow>& rows) {
  std::string out = "  \"multi\": [\n";
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const MultiRow& row = rows[i];
    out += R"(    {"language": ")" + row.language + R"(", "shape": ")" +
           row.shape + R"(", "refs": ")" + refs_name(row.refs) +
           R"(", "files": )" + std::to_string(row.files) +
           ", \"bytes\": " + std::to_string(row.bytes) +
           ", \"iterations\": " + std::to_string(row.iterations) +
           ", \"references\": " + std::to_string(row.references) +
           ", \"resolved\": " + std::to_string(row.resolved) +
           ", \"diagnostics\": " + std::to_string(row.diagnostics) +
           ", \"points\": [";
    for (std::size_t p = 0; p < row.points.size(); ++p) {
      char buf[128];
      std::snprintf(buf, sizeof buf,
                    R"(%s{"workers": %zu, "total_ms": %.6f, "link_ms": %.6f})",
                    p == 0 ? "" : ", ", row.points[p].workers,
                    row.points[p].totalMs, row.points[p].linkMs);
      out += buf;
    }
    out += "]}";
    if (i + 1 != rows.size()) out += ",";
    out += "\n";
  }
  out += "  ]";
  return out;
}

void write_multi_csv(const std::string& path,
                     const std::vector<MultiRow>& rows) {
  std::string out =
      "language,shape,refs,files,bytes,iterations,workers,"
      "total_ms,link_ms,phase1_ms,total_mibps,speedup,efficiency,"
      "references,resolved,diagnostics\n";
  for (const MultiRow& row : rows) {
    if (row.points.empty()) continue;
    const double baseTotal = row.points.front().totalMs;
    const double baseThreads = threads_of(row.points.front().workers);
    for (const MultiPoint& point : row.points) {
      const double speedup =
          point.totalMs > 0.0 ? baseTotal / point.totalMs : 0.0;
      const double width = threads_of(point.workers) / baseThreads;
      char buf[512];
      std::snprintf(buf, sizeof buf,
                    "%s,%s,%s,%zu,%zu,%d,%zu,%.6f,%.6f,%.6f,%.3f,%.4f,%.4f,"
                    "%zu,%zu,%zu\n",
                    row.language.c_str(), row.shape.c_str(),
                    refs_name(row.refs), row.files, row.bytes, row.iterations,
                    point.workers, point.totalMs, point.linkMs,
                    point.totalMs - point.linkMs,
                    throughput(row.bytes, point.totalMs), speedup,
                    width > 0.0 ? speedup / width : 0.0, row.references,
                    row.resolved, row.diagnostics);
      out += buf;
    }
  }
  write_file(path, out);
}

// `--csv PATH` also writes `PATH` with `-multi` before its extension: the
// two tables have different shapes, and one file carrying both is a file
// no CSV reader can open.
std::string companion_csv_path(const std::string& path,
                               std::string_view suffix) {
  const std::size_t dot = path.find_last_of('.');
  if (const std::size_t slash = path.find_last_of('/');
      dot == std::string::npos || (slash != std::string::npos && dot < slash))
    return path + std::string(suffix);
  return path.substr(0, dot) + std::string(suffix) + path.substr(dot);
}

// Paths and file names belong to the input, including quotes and backslashes.
std::string json_escape(std::string_view text) {
  std::string out;
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 0x20) {
      char buf[7];
      std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned int>(c));
      out += buf;
    } else {
      out += static_cast<char>(c);
    }
  }
  return out;
}

std::string csv_field(std::string_view text) {
  if (text.find_first_of(",\"\r\n") == std::string_view::npos)
    return std::string(text);
  std::string out = "\"";
  for (const char c : text) {
    if (c == '"') out += '"';
    out += c;
  }
  return out + '"';
}

void write_json(const std::string& path, const std::vector<Row>& rows,
                const std::vector<MultiRow>& multiRows) {
  std::string out = "{\n  \"tool\": \"language_bench\",\n  \"rows\": [\n";
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    out += R"(    {"language": ")" + json_escape(row.language) +
           R"(", "label": ")" + json_escape(row.label) + R"(", "bytes": )" +
           std::to_string(row.bytes) +
           ", \"iterations\": " + std::to_string(row.iterations) +
           R"(, "source": ")" + json_escape(row.source) + R"(", "stages": {)";
    for (std::size_t stage = 0; stage < kStageCount; ++stage) {
      if (stage != 0) out += ", ";
      out += "\"";
      out += kStageName[stage];
      out += "\": ";
      if (row.ms[stage] < 0.0) {
        out += "null";
      } else {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.6f", row.ms[stage]);
        out += buf;
      }
    }
    out += "}, \"references\": " + std::to_string(row.references) +
           ", \"resolved\": " + std::to_string(row.resolved) +
           ", \"diagnostics\": " + std::to_string(row.diagnostics) +
           ", \"lsp\": {";
    for (std::size_t s = 0; s < row.lsp.size(); ++s) {
      const auto& service = row.lsp[s];
      if (s != 0) out += ", ";
      out += '"' + json_escape(service.name) + R"(": {"available": )";
      out += service.ms < 0
                 ? R"(false, "min_ms": null, "ms": null, "worst_ms": null)"
                 : "true";
      if (service.ms >= 0) {
        char buf[192];
        std::snprintf(buf, sizeof buf,
                      R"(, "min_ms": %.6f, "ms": %.6f, "worst_ms": %.6f)",
                      service.min, service.ms, service.worst);
        out += buf;
      }
      out += ", \"samples\": " + std::to_string(service.samples) +
             ", \"probes\": " + std::to_string(service.probes) +
             ", \"results\": " + std::to_string(service.results) + "}";
    }
    out += "}}";
    if (i + 1 != rows.size()) out += ",";
    out += "\n";
  }
  out += "  ],\n";
  out += multi_json(multiRows);
  out += ",\n  \"validate_absent\": \"";
  out += kValidateAbsentWhy;
  out += "\"\n}\n";
  write_file(path, out);
}

void write_csv(const std::string& path, const std::vector<Row>& rows) {
  std::string out = "language,label,bytes,iterations";
  for (const char* name : kStageName) {
    out += ",";
    out += name;
    out += "_ms";
  }
  for (const char* name : kStageName) {
    out += ",";
    out += name;
    out += "_mibps";
  }
  out += ",references,resolved,diagnostics\n";
  for (const Row& row : rows) {
    out += csv_field(row.language) + "," + csv_field(row.label) + "," +
           std::to_string(row.bytes) + "," + std::to_string(row.iterations);
    char buf[32];
    for (double m : row.ms) {
      if (m < 0.0) {
        out += ",";
        continue;
      }
      std::snprintf(buf, sizeof buf, ",%.6f", m);
      out += buf;
    }
    for (double m : row.ms) {
      if (m < 0.0) {
        out += ",";
        continue;
      }
      std::snprintf(buf, sizeof buf, ",%.3f", throughput(row.bytes, m));
      out += buf;
    }
    out += "," + std::to_string(row.references) + "," +
           std::to_string(row.resolved) + "," +
           std::to_string(row.diagnostics) + "\n";
  }
  write_file(path, out);
}

void write_lsp_csv(const std::string& path, const std::vector<Row>& rows) {
  std::string out =
      "language,label,source,service,available,min_ms,ms,worst_ms,samples,"
      "probes,results\n";
  for (const Row& row : rows)
    for (const auto& service : row.lsp) {
      out += csv_field(row.language) + "," + csv_field(row.label) + "," +
             csv_field(row.source) + "," + csv_field(service.name) + ",";
      if (service.ms < 0)
        out += "false,,,";
      else {
        char buf[192];
        std::snprintf(buf, sizeof buf, "true,%.6f,%.6f,%.6f", service.min,
                      service.ms, service.worst);
        out += buf;
      }
      out += "," + std::to_string(service.samples) + "," +
             std::to_string(service.probes) + "," +
             std::to_string(service.results) + "\n";
    }
  write_file(path, out);
}

std::string multi_set_dir(const Language& language, Shape shape) {
  return std::string("multi/") + language.name + "-" + shape_name(shape);
}

std::string multi_file_path(const std::string& root, const Language& language,
                            Shape shape, std::size_t index) {
  return root + "/" + multi_set_dir(language, shape) + "/" +
         std::to_string(index) + language.extension;
}

// Generated, or read back from the dump -- the same set either way, because the
// shape is a pure function of (budget, shape) and the generator of (index,
// size). Nothing here samples a clock or a random source.
std::vector<std::string> multi_texts(const Options& o, const Language& language,
                                     Shape shape) {
  const std::vector<std::size_t> sizes = shape_files(shape, o.multiBytes);
  std::vector<std::string> files;
  files.reserve(sizes.size());
  for (std::size_t i = 0; i < sizes.size(); ++i)
    files.push_back(
        o.corpusDir.empty()
            ? language.multiFile(i, sizes[i])
            : read_file(multi_file_path(o.corpusDir, language, shape, i)));
  return files;
}

// The payloads are written once and read by both engines. The manifest
// is the contract between them: the harness and the comparison script
// read it rather than reconstructing a file name.
int dump_corpus(const Options& o, std::span<const Language> languages) {
  std::error_code ec;
  std::filesystem::create_directories(checked_path(o.dumpDir), ec);
  if (ec) {
    std::fprintf(stderr, "language_bench: cannot create '%s': %s\n",
                 printable(o.dumpDir).c_str(), ec.message().c_str());
    return 1;
  }
  std::string manifest =
      "{\n  \"tool\": \"language_bench --dump-corpus\",\n  \"payloads\": [\n";
  bool first = true;
  for (const Language& language : languages) {
    if (!selected(o, language.name)) continue;
    for (const std::size_t bytes : o.sizes) {
      const std::string label = size_label(bytes);
      const std::string text = language.corpus(bytes);
      const std::string path = corpus_path(o, language, label);
      write_file(path, text);
      std::fprintf(stderr, "  wrote %s (%zu B)\n", printable(path).c_str(),
                   text.size());
      if (!first) manifest += ",\n";
      first = false;
      manifest += R"(    {"language": ")" + std::string(language.name) +
                  R"(", "languageId": ")" + language.languageId +
                  R"(", "extension": ")" + language.extension +
                  R"(", "label": ")" + label + R"(", "bytes": )" +
                  std::to_string(text.size()) + R"(, "file": ")" +
                  std::string(language.name) + "-" + label +
                  language.extension + "\"}";
    }
  }
  manifest += "\n  ],\n  \"sets\": [\n";

  // The multi-document sets, one directory per (language, shape), on disk
  // so a single-threaded, async builder -- no worker curve of its
  // own -- can still be handed the same N files in one build.
  bool firstSet = true;
  if (o.multi) {
    for (const Language& language : languages) {
      if (!selected(o, language.name) || language.multiFile == nullptr)
        continue;
      for (std::size_t s = 0; s < static_cast<std::size_t>(Shape::Count); ++s) {
        const auto shape = static_cast<Shape>(s);
        const std::string dir =
            o.dumpDir + "/" + multi_set_dir(language, shape);
        std::filesystem::create_directories(checked_path(dir), ec);
        if (ec) {
          std::fprintf(stderr, "language_bench: cannot create '%s': %s\n",
                       printable(dir).c_str(), ec.message().c_str());
          return 1;
        }
        const std::vector<std::string> files = multi_texts(o, language, shape);
        std::size_t bytes = 0;
        for (std::size_t i = 0; i < files.size(); ++i) {
          write_file(multi_file_path(o.dumpDir, language, shape, i), files[i]);
          bytes += files[i].size();
        }
        std::fprintf(stderr, "  wrote %s (%zu files, %zu B)\n",
                     printable(dir).c_str(), files.size(), bytes);
        if (!firstSet) manifest += ",\n";
        firstSet = false;
        manifest += R"(    {"language": ")" + std::string(language.name) +
                    R"(", "languageId": ")" + language.languageId +
                    R"(", "extension": ")" + language.extension +
                    R"(", "shape": ")" + shape_name(shape) + R"(", "refs": ")" +
                    refs_name(language.multiRefs) + R"(", "files": )" +
                    std::to_string(files.size()) +
                    ", \"bytes\": " + std::to_string(bytes) + R"(, "dir": ")" +
                    multi_set_dir(language, shape) + "\"}";
      }
    }
  }
  manifest += "\n  ]\n}\n";
  write_file(o.dumpDir + "/manifest.json", manifest);
  std::fprintf(stderr, "  wrote %s/manifest.json\n",
               printable(o.dumpDir).c_str());
  return 0;
}

}  // namespace
}  // namespace cursiv::bench

int cursiv::bench::run(int argc, char** argv,
                       std::span<const Language> languages) {
  const Options o = options(argc, argv, languages);
  if (o.help) {
    std::printf(
        "usage: %s [options] %s\n"
        "  --file PATH            measure a file (repeatable)\n"
        "  --iterations N         repetitions (best pipeline, LSP "
        "min/avg/max)\n"
        "  --json PATH            write JSON results\n"
        "  --csv PATH             write CSV results\n"
        "  --rss                  one input, one cold workspace build\n"
        "  --no-lsp               skip LSP provider timings\n"
        "  --list                 list languages and inputs\n"
        "  --sizes 16K,4M,...      generated corpus sizes\n"
        "  --full                 include 32MiB and 128MiB\n"
        "  --dump-corpus DIR      write generated corpora without measuring\n"
        "  --corpus DIR           read previously dumped corpora\n"
        "  --multi / --no-multi   generated multi-document lane only / skip "
        "it\n"
        "  --multi-bytes N        byte budget per generated workspace shape\n"
        "  --workers 0,1,2,4      worker counts for the multi-document lane\n",
        argv[0],
        languages.size() == 1 && languages.front().corpus == nullptr
            ? "file [file ...]"
            : "[filter]");
    return 0;
  }
  print_binary_age();

  if (o.list) {
    for (const Language& language : languages) {
      if (!selected(o, language.name)) continue;
      std::printf("%-14s id=%-18s ext=%s\n", language.name, language.languageId,
                  language.extension);
    }
    if (o.files.empty()) {
      const bool generated =
          std::ranges::any_of(languages, [&](const Language& language) {
            return selected(o, language.name) && language.corpus != nullptr;
          });
      if (generated || !o.corpusDir.empty())
        for (const std::size_t bytes : o.sizes)
          std::printf("  size %-8s (%zu B, %d iterations)\n",
                      size_label(bytes).c_str(), bytes,
                      o.iterations > 0 ? o.iterations : iterations_for(bytes));
      else
        std::printf("  input: file [file ...]\n");
    } else {
      for (const std::string& path : o.files)
        std::printf("  file %s\n", printable(path).c_str());
    }
    if (o.multi) {
      std::printf("  multi budget %s, workers",
                  size_label(o.multiBytes).c_str());
      for (const std::size_t w : o.workers) std::printf(" %zu", w);
      std::printf("\n");
      for (std::size_t s = 0; s < static_cast<std::size_t>(Shape::Count); ++s) {
        const auto shape = static_cast<Shape>(s);
        std::printf("    %-12s %zu files\n", shape_name(shape),
                    shape_files(shape, o.multiBytes).size());
      }
    }
    return 0;
  }

  if (!o.dumpDir.empty()) return dump_corpus(o, languages);

  // Instruction-count mode repeats only the parser API, excluding workspace
  // builds and LSP probes from the process-wide count.
  const bool irMode = std::getenv("CURSIV_IR_STAGE") != nullptr;

  std::vector<Row> rows;
  for (const Language& language : languages) {
    if (!selected(o, language.name) || (!o.stages && !irMode)) continue;
    const auto measure = [&](std::string label, const std::string& source,
                             std::string origin) {
      Row row;
      row.language = language.name;
      row.label = std::move(label);
      row.source = std::move(origin);
      row.bytes = source.size();
      row.iterations =
          o.iterations > 0 ? o.iterations : iterations_for(row.bytes);

      if (irMode) {
        language.parse(source);
        return;
      }

      if (!o.rss) language.parse(source);
      if (language.workspace != nullptr)
        language.workspace(row.iterations, o.rss, source, row);
      if (!o.rss && o.lsp && language.lsp != nullptr)
        language.lsp(row.iterations, source, row);
      if (o.rss)
        print_rss_row(row);
      else
        print_row(row);
      std::fflush(stdout);
      rows.push_back(std::move(row));
    };
    if (!o.files.empty()) {
      for (const std::string& path : o.files)
        measure(std::filesystem::path(path).filename().string(),
                read_file(path), "disk:" + path);
    } else {
      for (const std::size_t bytes : o.sizes) {
        const std::string label = size_label(bytes);
        const std::string source =
            o.corpusDir.empty() ? language.corpus(bytes)
                                : read_file(corpus_path(o, language, label));
        measure(label, source,
                o.corpusDir.empty()
                    ? "generated"
                    : "disk:" + corpus_path(o, language, label));
      }
    }
  }

  // The multi-document lane runs after the stage ladder: it is the
  // expensive half, and an interrupted run should still publish the
  // cheap rows. Never in Ir mode: the instruction counter is process-wide.
  std::vector<MultiRow> multiRows;
  if (o.multi && !irMode) {
    if (const std::size_t cpus = usable_cpus(); cpus != 0) {
      const std::size_t widest = std::ranges::max(o.workers) + 1;
      if (widest > cpus)
        std::printf(
            "[multi] %zu CPU(s) usable by this process for %zu threads: the "
            "wider points time-share them and cannot scale\n",
            cpus, widest);
    }
    for (const Language& language : languages) {
      if (!selected(o, language.name) || language.multi == nullptr) continue;
      for (std::size_t s = 0; s < static_cast<std::size_t>(Shape::Count); ++s) {
        const auto shape = static_cast<Shape>(s);
        const std::vector<std::string> files = multi_texts(o, language, shape);
        MultiRow row;
        row.language = language.name;
        row.shape = shape_name(shape);
        row.refs = language.multiRefs;
        row.files = files.size();
        for (const std::string& text : files) row.bytes += text.size();
        row.iterations =
            o.iterations > 0 ? o.iterations : iterations_for(row.bytes);
        language.multi(row.iterations, files, o.workers, row);
        print_multi(row);
        std::fflush(stdout);
        multiRows.push_back(std::move(row));
      }
    }
  }

  if (!o.jsonPath.empty()) write_json(o.jsonPath, rows, multiRows);
  if (!o.csvPath.empty()) {
    write_csv(o.csvPath, rows);
    if (std::ranges::any_of(rows,
                            [](const Row& row) { return !row.lsp.empty(); })) {
      write_lsp_csv(companion_csv_path(o.csvPath, "-lsp"), rows);
    }
    if (!multiRows.empty())
      write_multi_csv(companion_csv_path(o.csvPath, "-multi"), multiRows);
  }
  return 0;
}
