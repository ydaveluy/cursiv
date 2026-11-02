// What a lane is written with: one assertion, the counter it feeds, the verdict
// `main` returns, and the whole-file read a corpus needs.
#pragma once

#include <cstdio>
#include <string>

// `failures` is one inline variable for the whole program, so a lane split
// over several translation units counts into the same one.

// Variadic, so a condition may spell a template-id: `CHECK(f<A, B>())` hands
// the macro two arguments, and one parameter would reject it.
#define CHECK(...)                                                       \
  do {                                                                   \
    if (!(__VA_ARGS__)) {                                                \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #__VA_ARGS__); \
      ++failures;                                                        \
    }                                                                    \
  } while (0)

namespace cursiv::test {

inline int failures = 0;

// The verdict, and the exit code: `main` ends with `return
// cursiv::test::report(...)` so the line a runner greps for is the same in
// every suite.
inline int report(const char* suite) {
  if (failures != 0) {
    std::printf("%s: %d failure(s)\n", suite, failures);
    return 1;
  }
  std::printf("%s: all checks passed\n", suite);
  return 0;
}

// The test module's verdicts, counted like a check. `cursiv::test` throws on
// a mismatch unless a hook says otherwise, and a lane that counts must reach
// its end: installed first thing in `main`, this one reports and keeps going.
void count_expectations();

// A whole file, or the empty string when it cannot be read -- what a missing
// corpus must look like to the check that reads it.
std::string read_file(const std::string& path);

}  // namespace cursiv::test

using cursiv::test::failures;
