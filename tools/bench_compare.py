#!/usr/bin/env python3
"""Compare the benchmarks of two builds of Cursiv.

    tools/bench_compare.py BASE HEAD [--rounds N] [--summary FILE]

BASE and HEAD are directories holding `language_bench` and, when built,
`recover_bench`, compiled with the same configuration.
Both builds read the corpus the base dumps. After a warm-up round, each round
runs every benchmark once per build, alternating which build goes first. A
delta is the median of the per-round head/base ratios with a distribution-free
confidence interval. It counts as a change only when the interval excludes zero
and the delta passes the metric's threshold.
"""

import argparse
import json
import math
import os
import platform
import re
import signal
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

SIDES = ("base", "head")
METRICS = ("time", "instructions", "cycles", "memory")
# The smallest delta reported as a change, per metric.
THRESHOLDS = {"time": 0.03, "instructions": 0.005, "cycles": 0.02,
              "memory": 0.01}
# Shorter times are shown but never reported: they mostly measure code layout.
MIN_TIME_MS = 0.05
STAGES = ("parse+index", "scopes", "link", "validate", "build")
RECOVERY = {"recover_bench": ("arithmetics", "domainmodel")}
SUITES = {
    "build": "Document builds (language_bench)",
    "multi": "Workspace builds (language_bench --multi)",
    "lsp": "Editor requests (language_bench)",
    "recover_bench": "Error recovery (recover_bench)",
}
LEGEND = {
    "build": "Time: fastest of the run's builds, on a pool of every CPU of "
             "the run. Instructions and cycles: fewest-cycle counted build, "
             "on one thread. Memory: peak resident bytes of one cold build "
             "on the pool.",
    "multi": "Time: fastest of the run's builds of the whole workspace, on "
             "one thread and on every CPU of the run.",
    "lsp": "Time: mean of one request over the run's calls, on the largest "
           "document.",
    "recover_bench": "Per document: fastest repetition. Whole run: fastest "
                     "repetition of all documents, and the process's peak "
                     "resident bytes.",
}
RECOVERY_LINE = re.compile(
    r"^(\S+)\s+\d+ faults (\S+) ms/doc \S+ us/fault (\S+) notes/doc"
    r"(?: (\S+) instr/doc (\S+) cycles/doc)?$")
RECOVERY_TOTAL = re.compile(r"^\S+ \d+ docs (\d+) bytes best (\S+) s sink")


class Failed(Exception):
    pass


def run(argv, log, timeout):
    """Runs `argv` with its output in `log`; returns its resource usage."""
    timed_out = False

    def stop(*_):
        nonlocal timed_out
        timed_out = True
        proc.kill()

    with open(log, "wb") as out:
        proc = subprocess.Popen([str(a) for a in argv], stdout=out,
                                stderr=subprocess.STDOUT)
    previous = signal.signal(signal.SIGALRM, stop)
    signal.alarm(timeout)
    try:
        _, status, usage = os.wait4(proc.pid, 0)
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, previous)
    proc.returncode = os.waitstatus_to_exitcode(status)
    if timed_out or proc.returncode != 0:
        tail = Path(log).read_text(errors="replace").splitlines()[-15:]
        what = (f"timed out after {timeout} s" if timed_out
                else f"exited with {proc.returncode}")
        raise Failed(f"`{' '.join(map(str, argv[1:]))}` {what}\n" +
                     "\n".join(tail))
    return usage


class Job:
    """One benchmark command, run once per build and round."""

    def __init__(self, suite, name, argv, read):
        self.suite, self.name = suite, name
        self.argv, self.read = argv, read
        self.failure = None


def language_jobs(corpus, sizes, multi_bytes, iterations, workers,
                  languages):
    def stages(bindir, out):
        return [bindir / "language_bench", "--corpus", corpus, "--sizes",
                ",".join(sizes), "--no-multi", "--single-workers", workers,
                "--iterations", iterations, "--json", out]

    def multi(bindir, out):
        return [bindir / "language_bench", "--corpus", corpus, "--multi",
                "--multi-bytes", multi_bytes, "--workers",
                ",".join(map(str, sorted({0, workers}))),
                "--iterations", iterations, "--json", out]

    def read_multi(out, _log, _usage):
        for row in json.loads(Path(out).read_text())["multi"]:
            work = (row["references"], row["resolved"], row["diagnostics"])
            for point in row["points"]:
                threads = point["workers"] + 1
                key = ("multi", f"{row['language']} {row['shape']}, "
                                f"{threads} thread{'s' if threads > 1 else ''}")
                yield key, "time", point["total_ms"], work

    def read_stages(out, _log, _usage):
        rows = json.loads(Path(out).read_text())["rows"]
        largest = {}
        for row in rows:
            if row["bytes"] > largest.get(row["language"], (0,))[0]:
                largest[row["language"]] = (row["bytes"], row["label"])
        for row in rows:
            name = f"{row['language']} {row['label']}"
            work = (row["references"], row["resolved"], row["diagnostics"])
            perf = row.get("perf") or {}
            for stage in STAGES:
                if row["stages"].get(stage) is None:
                    continue
                key = ("build", f"{name} {stage}")
                yield key, "time", row["stages"][stage], work
                if perf.get(stage):
                    yield key, "instructions", perf[stage]["instructions"], work
                    yield key, "cycles", perf[stage]["cycles"], work
            if row["label"] != largest[row["language"]][1]:
                continue
            for service, m in row["lsp"].items():
                if m["available"]:
                    yield ("lsp", f"{name} {service}"), "time", m["ms"], \
                        m["results"]

    def resident(language, size):
        def argv(bindir, out):
            return [bindir / "language_bench", "--corpus", corpus, "--sizes",
                    size, "--rss", "--single-workers", workers, "--json", out,
                    language]
        return argv

    def read_resident(out, _log, _usage):
        for row in json.loads(Path(out).read_text())["rows"]:
            peak = (row.get("rss") or {}).get("peak")
            if peak:
                key = ("build", f"{row['language']} {row['label']} build")
                yield key, "memory", peak, None

    yield Job("build", "language_bench", stages, read_stages)
    yield Job("multi", "language_bench --multi", multi, read_multi)
    for language in languages:
        for size in sizes:
            yield Job("build", f"language_bench --rss {language} {size}",
                      resident(language, size), read_resident)


def recovery_jobs(binary, languages, args):
    def argv_of(language):
        return lambda bindir, _out: [bindir / binary, language, *args]

    def read_of(language):
        def read(_out, log, usage):
            for line in Path(log).read_text().splitlines():
                if m := RECOVERY_LINE.match(line):
                    key = (binary, f"{language} {m[1]}")
                    yield key, "time", float(m[2]), m[3]
                    if m[4] is not None:
                        yield key, "instructions", float(m[4]), m[3]
                        yield key, "cycles", float(m[5]), m[3]
                elif m := RECOVERY_TOTAL.match(line):
                    key = (binary, f"{language} whole run")
                    yield key, "time", float(m[2]) * 1e3, m[1]
                    yield key, "memory", usage.ru_maxrss * 1024, m[1]
        return read

    for language in languages:
        yield Job(binary, f"{binary} {language}", argv_of(language),
                  read_of(language))


def confidence_rank(n, confidence):
    """The 1-based rank k such that [x(k), x(n+1-k)] covers the median."""
    tail = (1 - confidence) / 2
    k, mass = 0, 0.0
    for j in range(n):
        mass += math.comb(n, j) / 2 ** n
        if mass > tail:
            break
        k = j + 1
    return k


def spread(values):
    """The robust coefficient of variation: scaled MAD over the median."""
    middle = statistics.median(values)
    if middle <= 0:
        return 0.0
    return 1.4826 * statistics.median(abs(v - middle) for v in values) / middle


def compare(samples, metric, confidence):
    base, head = samples["base"], samples["head"]
    rounds = sorted(r for r in set(base) & set(head) if base[r] > 0)
    if not rounds:
        return None
    ratios = sorted(head[r] / base[r] for r in rounds)
    n = len(ratios)
    k = confidence_rank(n, confidence)
    c = {
        "n": n,
        "base": statistics.median(base[r] for r in rounds),
        "head": statistics.median(head[r] for r in rounds),
        "delta": statistics.median(ratios) - 1,
        "ci": (ratios[k - 1] - 1, ratios[n - k] - 1) if k else None,
        "spread": (spread([base[r] for r in rounds]),
                   spread([head[r] for r in rounds])),
        "verdict": "same",
    }
    t = THRESHOLDS[metric]
    if c["ci"] is None:
        c["verdict"] = "unknown"
    elif metric == "time" and c["base"] < MIN_TIME_MS:
        c["verdict"] = "short"
    elif c["delta"] > t and c["ci"][0] > 0:
        c["verdict"] = "worse"
    elif c["delta"] < -t and c["ci"][1] < 0:
        c["verdict"] = "better"
    return c


def percent(x, metric):
    return f"{x * 100:+.{2 if metric == 'instructions' else 1}f}"


def amount(x, metric):
    if metric == "time":
        return f"{x:.4g} ms"
    if metric == "memory":
        return f"{x / 2 ** 20:.1f} MiB"
    for scale, unit in ((1e9, "G"), (1e6, "M"), (1e3, "k")):
        if x >= scale:
            return f"{x / scale:.3f} {unit}"
    return f"{x:.0f}"


def cell(c, metric):
    if c is None:
        return "—"
    text = percent(c["delta"], metric)
    if c["ci"] is not None:
        lo, hi = (percent(x, metric) for x in c["ci"])
        text += f" ({lo} … {hi})"
    icon = {"worse": "🔴 ", "better": "🟢 "}.get(c["verdict"], "")
    return f"{icon}**{text}**" if icon else text


def report(results, work, failures, info, confidence):
    keys = sorted(results)
    changes = sorted(((key, metric, c) for key in keys
                      for metric, c in results[key].items()
                      if c is not None and c["verdict"] in ("worse", "better")),
                     key=lambda change: -change[2]["delta"])
    compared = sum(c is not None for r in results.values() for c in r.values())
    worse = sum(c["verdict"] == "worse" for _, _, c in changes)
    better = len(changes) - worse
    level = f"{confidence * 100:g} %"
    lines = [f"## Benchmarks: base {info['base']} → merge {info['head']}", ""]
    if changes:
        lines.append(f"**{worse} slower, {better} faster** out of {compared} "
                     "comparisons.")
    else:
        lines.append(f"**No significant change** in {compared} comparisons.")
    lines += ["", "| | |", "|---|---|"]
    lines += [f"| {k} | {v} |" for k, v in info["table"]]
    lines.append(
        "| Reported | a median delta beyond " +
        ", ".join(f"±{THRESHOLDS[m] * 100:g} % ({m})" for m in METRICS) +
        f" whose {level} interval excludes zero; times under "
        f"{MIN_TIME_MS:g} ms are not judged |")
    if failures:
        lines += ["", "### Failures", ""]
        for job, side, message in failures:
            lines += [f"**{job}** on {side}:", "", "```", message, "```", ""]
    if changes:
        lines += ["", "### Significant changes", "",
                  f"| Benchmark | Metric | Base | Head | Δ % ({level} CI) "
                  "| Spread base / head |",
                  "|---|---|---:|---:|---:|---:|"]
        for key, metric, c in changes:
            lines.append(
                f"| {SUITES[key[0]].split(' (')[0]}: {mark(key, work)} "
                f"| {metric} | {amount(c['base'], metric)} "
                f"| {amount(c['head'], metric)} | {cell(c, metric)} "
                f"| {c['spread'][0] * 100:.1f} % / "
                f"{c['spread'][1] * 100:.1f} % |")
    for suite, title in SUITES.items():
        rows = [key for key in keys if key[0] == suite]
        if not rows:
            continue
        shown = [m for m in METRICS
                 if any(results[key].get(m) for key in rows)]
        lines += ["", f"<details><summary>{title}: {len(rows)} benchmarks"
                  "</summary>", "",
                  "| Benchmark | Base time | " +
                  " | ".join(f"Δ {m} %" for m in shown) + " |",
                  "|---|---:|" + "---:|" * len(shown)]
        for key in rows:
            base_time = results[key].get("time")
            lines.append(
                f"| {mark(key, work)} | "
                f"{amount(base_time['base'], 'time') if base_time else '—'} | " +
                " | ".join(cell(results[key].get(m), m) for m in shown) + " |")
        lines += ["", LEGEND[suite], "", "</details>"]
    if any(differs(key, work) for key in keys):
        lines += ["", "⚠ The two builds produced different output (diagnostics, "
                  "references, results or repairs): the delta compares "
                  "different work."]
    return "\n".join(lines) + "\n"


def differs(key, work):
    seen = work.get(key, {})
    return len(seen) == 2 and seen["base"] != seen["head"]


def mark(key, work):
    return ("⚠ " if differs(key, work) else "") + key[1]


def cpu_list(text):
    cpus = set()
    for part in text.split(","):
        first, _, last = part.partition("-")
        cpus.update(range(int(first), int(last or first) + 1))
    return sorted(cpus)


def cpu_ranges(cpus):
    spans = []
    for cpu in cpus:
        if spans and spans[-1][1] == cpu - 1:
            spans[-1][1] = cpu
        else:
            spans.append([cpu, cpu])
    return ",".join(f"{a}-{b}" if a != b else f"{a}" for a, b in spans)


def cpu_model():
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or platform.machine()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("base", type=Path)
    parser.add_argument("head", type=Path)
    parser.add_argument("--rounds", type=int, default=15)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--sizes", default="256K,4M")
    parser.add_argument("--multi-bytes", default="8M",
                        help="bytes of each generated workspace")
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--recovery", default="65536 64 5",
                        help="bytes, sites and repetitions of recover_bench")
    parser.add_argument("--cpus", type=cpu_list,
                        help="the CPUs to run on, as `2-9` or `0,2,4` "
                             "(default: every allowed CPU)")
    parser.add_argument("--confidence", type=float, default=0.99)
    parser.add_argument("--timeout", type=int, default=600,
                        help="seconds allowed to one run")
    parser.add_argument("--base-label", default="")
    parser.add_argument("--head-label", default="")
    parser.add_argument("--summary", type=Path,
                        help="append the Markdown report to this file")
    args = parser.parse_args()
    if confidence_rank(args.rounds, args.confidence) == 0:
        parser.error(f"{args.rounds} rounds give no {args.confidence:g} "
                     "interval: run more")
    dirs = {"base": args.base.resolve(), "head": args.head.resolve()}
    started = time.monotonic()

    cpus = args.cpus or sorted(os.sched_getaffinity(0))
    os.sched_setaffinity(0, cpus)
    work_dir = Path(tempfile.mkdtemp(prefix="bench-compare-"))
    corpus = work_dir / "corpus"
    sizes = args.sizes.split(",")
    notes = []

    def present(binary):
        return all((dirs[s] / binary).is_file() for s in SIDES)

    if not present("language_bench"):
        sys.exit("language_bench is missing from one of the builds")
    run([dirs["base"] / "language_bench", "--dump-corpus", corpus,
         "--sizes", args.sizes, "--multi-bytes", args.multi_bytes],
        work_dir / "dump.log", args.timeout)
    manifest = json.loads((corpus / "manifest.json").read_text())
    languages = list(dict.fromkeys(p["language"] for p in manifest["payloads"]))
    jobs = list(language_jobs(corpus, sizes, args.multi_bytes,
                              args.iterations, len(cpus) - 1, languages))
    for binary, names in RECOVERY.items():
        if present(binary):
            jobs += recovery_jobs(binary, names, args.recovery.split())
        else:
            notes.append(f"`{binary}` is missing from one build: skipped.")

    samples = {}  # (suite, name) -> metric -> side -> round -> value
    work = {}  # (suite, name) -> side -> what the run produced
    failures = []
    for r in range(-args.warmup, args.rounds):
        print(f"round {r + 1}/{args.rounds}" if r >= 0 else "warm-up",
              file=sys.stderr, flush=True)
        for i, job in enumerate(jobs):
            if job.failure:
                continue
            for side in SIDES if r % 2 == 0 else SIDES[::-1]:
                out = work_dir / f"{side}-{i}.json"
                log = work_dir / f"{side}-{i}.log"
                try:
                    usage = run(job.argv(dirs[side], out), log, args.timeout)
                    rows = list(job.read(out, log, usage))
                except (Failed, OSError, ValueError, KeyError) as error:
                    job.failure = True
                    failures.append((job.name, side, str(error)))
                    print(f"{job.name} on {side}: {error}", file=sys.stderr)
                    break
                if r < 0:
                    continue
                for key, metric, value, did in rows:
                    samples.setdefault(key, {}).setdefault(
                        metric, {"base": {}, "head": {}})[side][r] = value
                    if did is not None:
                        work.setdefault(key, {}).setdefault(side, did)

    results = {key: {m: compare(s, m, args.confidence)
                     for m, s in metrics.items()}
               for key, metrics in samples.items()}
    counted = {side: any(side_samples[side]
                         for metrics in samples.values()
                         for m, side_samples in metrics.items()
                         if m == "instructions")
               for side in SIDES}
    spreads = {side: [c["spread"][i] for r in results.values()
                      if (c := r.get("time")) is not None
                      and c["verdict"] not in ("short", "unknown")]
               for i, side in enumerate(SIDES)}
    info = {
        "base": f"`{args.base_label or args.base}`",
        "head": f"`{args.head_label or args.head}`",
        "table": [
            ("Rounds", f"{args.rounds} paired runs of each benchmark after "
                       f"{args.warmup} warm-up round(s), alternating which "
                       "build runs first"),
            ("Machine", f"{cpu_model()}, runs on {len(cpus)} of "
                        f"{os.cpu_count()} CPUs ({cpu_ranges(cpus)}), Linux "
                        f"{platform.release()}"),
            ("Hardware counters",
             "counted" if all(counted.values()) else
             "reported by neither build: no instruction or cycle figures"
             if not any(counted.values()) else
             "base {}, head {}".format(
                 *("counted" if counted[s] else "unavailable"
                   for s in SIDES))),
            ("Time noise", "median spread across rounds: " + ", ".join(
                f"{side} {statistics.median(v) * 100:.1f} %"
                for side, v in spreads.items() if v)),
            ("Duration", f"{(time.monotonic() - started) / 60:.1f} min"),
        ],
    }
    text = report(results, work, failures, info, args.confidence)
    if notes:
        text += "\n" + "\n".join(notes) + "\n"
    print(text)
    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as out:
            out.write(text)


if __name__ == "__main__":
    main()
