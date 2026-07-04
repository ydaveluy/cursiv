#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include <pegium/core/utils/Caching.hpp>

#include "CacheTestUtils.hpp"
#include "CacheKeyTestUtils.hpp"

namespace pegium::utils {
namespace {

using test::make_cache_shared_services;
using test::make_document;
using test::replace_document_text;

TEST(DocumentCacheTest, InvalidatesChangedDocumentOnUpdate) {
  test::RecordingEventDocumentBuilder *builder = nullptr;
  auto shared = make_cache_shared_services(builder);
  DocumentCache<std::string, std::string> cache(*shared);

  const auto uri1 = test::make_file_uri("cache-document-1.test");
  const auto uri2 = test::make_file_uri("cache-document-2.test");
  const auto document1 = make_document(*shared, uri1, "first");
  const auto document2 = make_document(*shared, uri2, "second");

  cache.set(document1->id, "key", "first");
  cache.set(document2->id, "key", "second");

  replace_document_text(*document1, "updated");
  builder->emitUpdate({document1->id}, {});

  EXPECT_FALSE(cache.has(document1->id, "key"));
  EXPECT_EQ(cache.get(document2->id, "key"),
            std::optional<std::string>("second"));
}

TEST(DocumentCacheTest, RecomputesValueAfterUpdate) {
  test::RecordingEventDocumentBuilder *builder = nullptr;
  auto shared = make_cache_shared_services(builder);
  DocumentCache<std::string, std::string> cache(*shared);

  const auto uri = test::make_file_uri("cache-document-recompute.test");
  const auto document = make_document(*shared, uri, "first");
  std::size_t calls = 0;

  EXPECT_EQ(cache.get(document->id, "key", [&]() {
              ++calls;
              return std::string("first");
            }),
            "first");
  EXPECT_EQ(cache.get(document->id, "key", [&]() {
              ++calls;
              return std::string("second");
            }),
            "first");

  replace_document_text(*document, "second");
  builder->emitUpdate({document->id}, {});

  EXPECT_EQ(cache.get(document->id, "key", [&]() {
              ++calls;
              return std::string("second");
            }),
            "second");
  EXPECT_EQ(calls, 2u);
}

TEST(DocumentCacheTest, ClearsDeletedDocumentOnUpdate) {
  test::RecordingEventDocumentBuilder *builder = nullptr;
  auto shared = make_cache_shared_services(builder);
  DocumentCache<std::string, std::string> cache(*shared);

  const auto uri = test::make_file_uri("cache-document-deleted.test");
  const auto document = make_document(*shared, uri);

  cache.set(document->id, "key", "value");
  EXPECT_FALSE(cache.erase(document->id, "missing"));
  cache.set(document->id, "key", "value");

  EXPECT_TRUE(shared->workspace.documents->deleteDocument(uri) != nullptr);
  builder->emitUpdate({}, {document->id});

  EXPECT_FALSE(cache.erase(document->id, "key"));
  EXPECT_FALSE(cache.get(document->id, "key").has_value());
}

TEST(DocumentCacheTest, DisposeRemovesListenersAndRejectsFurtherAccess) {
  test::RecordingEventDocumentBuilder *builder = nullptr;
  auto shared = make_cache_shared_services(builder);
  DocumentCache<std::string, std::string> cache(*shared);

  const auto document =
      make_document(*shared, test::make_file_uri("cache-disposed.test"));

  EXPECT_EQ(builder->updateListenerCount(), 1u);

  cache.dispose();

  EXPECT_EQ(builder->updateListenerCount(), 0u);
  EXPECT_THROW((void)cache.has(document->id, "key"), std::runtime_error);
  EXPECT_THROW(cache.set(document->id, "key", "value"), std::runtime_error);
  EXPECT_THROW((void)cache.get(document->id, "key"), std::runtime_error);
  EXPECT_THROW((void)cache.erase(document->id, "key"), std::runtime_error);
  EXPECT_THROW(cache.clear(document->id), std::runtime_error);
  EXPECT_THROW(cache.clear(), std::runtime_error);
}

TEST(DocumentCacheTest, SupportsCustomHashAndEqualityForKeys) {
  test::RecordingEventDocumentBuilder *builder = nullptr;
  auto shared = make_cache_shared_services(builder);
  DocumentCache<test::NamedKey, std::string, test::NamedKeyHash,
                test::NamedKeyEqual>
      cache(*shared);

  const auto document =
      make_document(*shared, test::make_file_uri("cache-custom-key.test"));

  cache.set(document->id, {.value = "key"}, "value");

  EXPECT_TRUE(cache.has(document->id, {.value = "key"}));
  EXPECT_EQ(cache.get(document->id, {.value = "key"}),
            std::optional<std::string>("value"));
  EXPECT_TRUE(cache.erase(document->id, {.value = "key"}));
}

} // namespace
} // namespace pegium::utils
