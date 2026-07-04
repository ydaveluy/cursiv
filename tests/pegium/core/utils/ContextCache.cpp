#include <gtest/gtest.h>

#include <optional>
#include <string>

#include <pegium/core/utils/Caching.hpp>

#include "CacheKeyTestUtils.hpp"

namespace pegium::utils {
namespace {

TEST(ContextCacheTest, SeparatesEntriesByContextAndCanClearOneContext) {
  ContextCache<std::string, std::string, std::string> cache;

  cache.set("doc-1", "key", "first");
  cache.set("doc-2", "key", "second");

  EXPECT_EQ(cache.get("doc-1", "key"), std::optional<std::string>("first"));
  EXPECT_EQ(cache.get("doc-2", "key"), std::optional<std::string>("second"));

  cache.clear("doc-1");
  EXPECT_FALSE(cache.has("doc-1", "key"));
  EXPECT_EQ(cache.get("doc-2", "key"), std::optional<std::string>("second"));
}

TEST(ContextCacheTest, SupportsCustomHashAndEqualityForContextAndKey) {
  using Cache =
      ContextCache<test::NamedKey, test::NamedKey, std::string,
                   test::NamedKey, test::NamedKeyHash,
                   test::NamedKeyEqual, test::NamedKeyHash,
                   test::NamedKeyEqual>;
  Cache cache;

  cache.set({.value = "doc-1"}, {.value = "key"}, "first");
  cache.set({.value = "doc-2"}, {.value = "key"}, "second");

  EXPECT_EQ(cache.get({.value = "doc-1"}, {.value = "key"}),
            std::optional<std::string>("first"));
  EXPECT_EQ(cache.get({.value = "doc-2"}, {.value = "key"}),
            std::optional<std::string>("second"));

  cache.clear({.value = "doc-1"});
  EXPECT_FALSE(cache.has({.value = "doc-1"}, {.value = "key"}));
  EXPECT_EQ(cache.get({.value = "doc-2"}, {.value = "key"}),
            std::optional<std::string>("second"));
}

} // namespace
} // namespace pegium::utils
