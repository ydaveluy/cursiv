// What the shared cases check a reading with: a text against the one a case
// expects, shown with the engine and the input where they differ, and a value
// of exactly one class.
#pragma once

#include <cstdio>
#include <string_view>

#include "cursiv/core/syntax-tree/AstNode.hpp"
#include "cursiv/core/syntax-tree/ParseResult.hpp"
#include "cursiv/test/Check.hpp"

inline void same_text(const char* engine, std::string_view input,
                      std::string_view got, std::string_view expected) {
  if (got != expected)
    std::printf("  [%s] %.*s\n    got      %.*s\n    expected %.*s\n", engine,
                static_cast<int>(input.size()), input.data(),
                static_cast<int>(got.size()), got.data(),
                static_cast<int>(expected.size()), expected.data());
  CHECK(got == expected);
}

template <class T>
const T* exactly(const cursiv::ParseResult& r) {
  return r.value != nullptr && r.value->type == &cursiv::ast_type_tag<T>
             ? static_cast<const T*>(r.value)
             : nullptr;
}
