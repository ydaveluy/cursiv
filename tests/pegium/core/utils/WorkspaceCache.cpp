#include <gtest/gtest.h>

#include <optional>
#include <string>

#include <pegium/core/utils/Caching.hpp>

#include "CacheTestUtils.hpp"
#include "CacheKeyTestUtils.hpp"

namespace pegium::utils {
namespace {

using test::make_cache_shared_services;

TEST(WorkspaceCacheTest, InvalidatesWholeCacheOnUpdateAndDeletion) {
  test::RecordingEventDocumentBuilder *builder = nullptr;
  auto shared = make_cache_shared_services(builder);
  WorkspaceCache<std::string, std::string> cache(*shared);

  cache.set("key", "value");
  builder->emitUpdate({1}, {});
  EXPECT_FALSE(cache.has("key"));

  cache.set("key", "value");
  builder->emitUpdate({}, {1});
  EXPECT_FALSE(cache.has("key"));
}

TEST(WorkspaceCacheTest, InvalidatesOnTrackedBuildStateAndDisposesCleanly) {
  test::RecordingEventDocumentBuilder *builder = nullptr;
  auto shared = make_cache_shared_services(builder);
  WorkspaceCache<std::string, std::string> cache(
      *shared, workspace::DocumentState::Linked);

  EXPECT_EQ(builder->buildPhaseListenerCount(), 1u);
  EXPECT_EQ(builder->updateListenerCount(), 1u);

  cache.set("key", "value");
  builder->emitBuildPhase(workspace::DocumentState::ComputedScopes);
  EXPECT_EQ(cache.get("key"), std::optional<std::string>("value"));

  builder->emitBuildPhase(workspace::DocumentState::Linked);
  EXPECT_FALSE(cache.has("key"));

  cache.dispose();
  EXPECT_EQ(builder->buildPhaseListenerCount(), 0u);
  EXPECT_EQ(builder->updateListenerCount(), 0u);
}

TEST(WorkspaceCacheTest, SupportsCustomHashAndEqualityForKeys) {
  test::RecordingEventDocumentBuilder *builder = nullptr;
  auto shared = make_cache_shared_services(builder);
  WorkspaceCache<test::NamedKey, std::string, test::NamedKeyHash,
                 test::NamedKeyEqual>
      cache(*shared);

  cache.set({.value = "key"}, "value");

  EXPECT_TRUE(cache.has({.value = "key"}));
  EXPECT_EQ(cache.get({.value = "key"}), std::optional<std::string>("value"));
  EXPECT_TRUE(cache.erase({.value = "key"}));
}

} // namespace
} // namespace pegium::utils
