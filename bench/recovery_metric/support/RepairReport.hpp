// The report: one table per grammar, one row per fault class, and the same
// counts as JSON. No total across classes or grammars and no pass mark: a
// class is read against itself from one run to the next.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "recovery_metric/support/FaultClass.hpp"
#include "recovery_metric/support/RepairTally.hpp"

namespace cursiv::recovery_metric {

struct RepairReport {
  struct Row {
    FaultClass cls;
    RepairTally tally;
  };
  struct Section {
    std::string name;
    unsigned twins = 0, deepened = 0;
    std::vector<Row> rows;
  };

  unsigned docs = 0;
  std::uint64_t seed = 0;
  // How the cost column was counted, or why it is empty.
  std::string counter;
  // How the parser was asked to repair.
  std::string repair;
  std::vector<std::string_view> ceilings;
  std::vector<Section> sections;

  void text(std::FILE* out) const {
    std::fprintf(out,
                 "recovery metric: up to %u documents per class, seed %#llx, "
                 "cost %s%s%s\n",
                 docs, static_cast<unsigned long long>(seed), counter.c_str(),
                 repair.empty() ? "" : ", repair by ", repair.c_str());
    std::fprintf(
        out,
        "  found/miss: faults a diagnostic answers one to one, in %% of "
        "faults (silent docs are misses)\n"
        "  sil: docs accepted with no diagnostic; noop: silent docs with the "
        "twin's own tree, set apart from every column\n"
        "  para: diagnostics answering no fault, per fault\n"
        "  kept/del/aband/other: the author's bytes no fault touches, in %%\n"
        "  rest/inv: tokens the repair made that restore a twin token, or "
        "invent one, per fault\n"
        "  ast: twin nodes no fault touches, kept; whole: docs keeping all\n"
        "  diag/f: diagnostics per fault, then docs by ratio "
        "[0 <1 =1 <=2 <=4 >4]\n"
        "  cost: instructions of parse(faulted) over parse(valid)\n"
        "  caps: docs whose parse reached a ceiling more often than the "
        "valid twin's");
    for (std::size_t i = 0; i < ceilings.size(); ++i)
      std::fprintf(out, "%s%.*s", i == 0 ? " (" : " ",
                   static_cast<int>(ceilings[i].size()), ceilings[i].data());
    std::fprintf(out, "%s\n", ceilings.empty() ? "" : ")");
    for (const Section& s : sections) {
      std::fprintf(out, "\n== %s: %u valid documents, %u deepened\n",
                   s.name.c_str(), s.twins, s.deepened);
      std::fprintf(out,
                   "%-11s %5s %6s %6s %6s %5s %4s %4s | %5s %5s %5s %5s %5s "
                   "%5s | %6s %5s | %6s %4s %4s %4s %4s %4s %4s | %6s %7s "
                   "%7s | %s\n",
                   "class", "docs", "faults", "found", "miss", "para",
                   "sil", "noop", "kept", "del", "aband", "other", "rest", "inv",
                   "ast", "whole", "diag/f", "0", "<1", "=1", "<=2", "<=4",
                   ">4", "p50", "p99", "max", "caps");
      for (const Row& r : s.rows) {
        const RepairTally& t = r.tally;
        const std::string_view name = fault_class_name(r.cls);
        if (t.docs == 0) {
          std::fprintf(out, "%-11.*s n/a: %s\n", static_cast<int>(name.size()),
                       name.data(),
                       t.noop == 0 ? "no site in these documents"
                                   : "every draw left the tree unchanged");
          continue;
        }
        std::fprintf(
            out,
            "%-11.*s %5u %6u %5.1f%% %5.1f%% %5.2f %4u %4u | %5.1f %5.1f "
            "%5.1f %5.1f %5.2f %5.2f | %5.1f%% %5u | %6.2f %4u %4u %4u %4u "
            "%4u %4u | %6s %7s %7s | %u",
            static_cast<int>(name.size()), name.data(), t.docs, t.faults,
            pct(t.found, t.faults), pct(t.missed, t.faults),
            ratio(t.parasites, t.faults), t.silent, t.noop,
            pct(t.kept, t.eligible),
            pct(t.deleted, t.eligible), pct(t.abandoned, t.eligible),
            pct(t.other, t.eligible), ratio(t.restored, t.faults),
            ratio(t.invented, t.faults), pct(t.astKept, t.astTotal),
            t.astWhole, ratio(t.diagnostics, t.faults), t.perFault[0],
            t.perFault[1], t.perFault[2], t.perFault[3], t.perFault[4],
            t.perFault[5], cost(t.cost(0.5)).c_str(),
            cost(t.cost(0.99)).c_str(), cost(t.cost(1.0)).c_str(), t.machine);
        for (std::size_t i = 0; i < t.ceilings.size() && i < ceilings.size();
             ++i)
          if (t.ceilings[i] != 0)
            std::fprintf(out, " %.*s:%u", static_cast<int>(ceilings[i].size()),
                         ceilings[i].data(), t.ceilings[i]);
        if (t.refused != 0) std::fprintf(out, " refused:%u", t.refused);
        if (t.treeless != 0) std::fprintf(out, " no-ast:%u", t.treeless);
        if (t.astUnaligned != 0)
          std::fprintf(out, " ast-unaligned:%llu",
                       static_cast<unsigned long long>(t.astUnaligned));
        std::fprintf(out, "\n");
      }
    }
  }

  void json(std::FILE* out) const {
    std::fprintf(out, "{\"docs_per_class\":%u,\"seed\":%llu,\"counter\":", docs,
                 static_cast<unsigned long long>(seed));
    quoted(out, counter);
    std::fprintf(out, ",\"ceilings\":[");
    for (std::size_t i = 0; i < ceilings.size(); ++i) {
      if (i != 0) std::fputc(',', out);
      quoted(out, ceilings[i]);
    }
    std::fprintf(out, "],\"grammars\":[");
    for (std::size_t g = 0; g < sections.size(); ++g) {
      const Section& s = sections[g];
      if (g != 0) std::fputc(',', out);
      std::fprintf(out, "\n{\"name\":");
      quoted(out, s.name);
      std::fprintf(out, ",\"twins\":%u,\"deepened\":%u,\"classes\":[",
                   s.twins, s.deepened);
      for (std::size_t c = 0; c < s.rows.size(); ++c) {
        const RepairTally& t = s.rows[c].tally;
        if (c != 0) std::fputc(',', out);
        std::fprintf(out, "\n {\"class\":");
        quoted(out, fault_class_name(s.rows[c].cls));
        std::fprintf(
            out,
            ",\"docs\":%u,\"faults\":%u,\"diagnostics\":%u,\"found\":%u,"
            "\"missed\":%u,\"parasites\":%u,\"silent\":%u,\"noop\":%u,"
            "\"refused\":%u,"
            "\"treeless\":%u,\"text\":{\"eligible\":%llu,\"kept\":%llu,"
            "\"deleted\":%llu,\"abandoned\":%llu,\"other\":%llu,"
            "\"restored\":%u,\"invented\":%u},\"ast\":{\"total\":%llu,"
            "\"kept\":%llu,\"whole\":%u,\"unaligned\":%llu},\"per_fault\":[%u,%u,%u,%u,%u,%u],"
            "\"cost\":",
            t.docs, t.faults, t.diagnostics, t.found, t.missed, t.parasites,
            t.silent, t.noop, t.refused, t.treeless,
            static_cast<unsigned long long>(t.eligible),
            static_cast<unsigned long long>(t.kept),
            static_cast<unsigned long long>(t.deleted),
            static_cast<unsigned long long>(t.abandoned),
            static_cast<unsigned long long>(t.other), t.restored, t.invented,
            static_cast<unsigned long long>(t.astTotal),
            static_cast<unsigned long long>(t.astKept), t.astWhole,
            static_cast<unsigned long long>(t.astUnaligned),
            t.perFault[0], t.perFault[1], t.perFault[2], t.perFault[3],
            t.perFault[4], t.perFault[5]);
        if (t.costs.empty())
          std::fprintf(out, "null");
        else
          std::fprintf(out,
                       "{\"measured\":%zu,\"p50\":%.6g,\"p99\":%.6g,"
                       "\"max\":%.6g}",
                       t.costs.size(), t.cost(0.5), t.cost(0.99), t.cost(1.0));
        std::fprintf(out, ",\"ceilings\":[");
        for (std::size_t i = 0; i < ceilings.size(); ++i)
          std::fprintf(out, "%s%u", i == 0 ? "" : ",",
                       i < t.ceilings.size() ? t.ceilings[i] : 0u);
        std::fprintf(out, "],\"machine\":%u}", t.machine);
      }
      std::fprintf(out, "]}");
    }
    std::fprintf(out, "\n]}\n");
  }

 private:
  [[nodiscard]] static double pct(std::uint64_t a, std::uint64_t b) {
    return b == 0 ? 0.0 : 100.0 * static_cast<double>(a) / static_cast<double>(b);
  }
  [[nodiscard]] static double ratio(std::uint64_t a, std::uint64_t b) {
    return b == 0 ? 0.0 : static_cast<double>(a) / static_cast<double>(b);
  }
  [[nodiscard]] static std::string cost(double v) {
    if (v < 0) return "-";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
  }
  static void quoted(std::FILE* out, std::string_view s) {
    std::fputc('"', out);
    for (const char ch : s) {
      if (ch == '"' || ch == '\\')
        std::fprintf(out, "\\%c", ch);
      else if (static_cast<unsigned char>(ch) < 0x20)
        std::fprintf(out, "\\u%04x", static_cast<unsigned char>(ch));
      else
        std::fputc(ch, out);
    }
    std::fputc('"', out);
  }
};

}  // namespace cursiv::recovery_metric
