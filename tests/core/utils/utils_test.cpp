// The utilities the tree leans on: folded comparison and hashing, the folder
// tree, the one form a URI is keyed in, and the caches that must die with the
// build they answer for.

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cursiv/core/services/CoreServices.hpp"
#include "cursiv/core/services/ServiceRegistry.hpp"
#include "cursiv/core/services/SharedCoreServices.hpp"
#include "cursiv/core/utils/CaseFold.hpp"
#include "cursiv/core/utils/DocumentCache.hpp"
#include "cursiv/core/utils/EditDistance.hpp"
#include "cursiv/core/utils/SimpleCache.hpp"
#include "cursiv/core/utils/UriTrie.hpp"
#include "cursiv/core/utils/UriUtils.hpp"
#include "cursiv/core/utils/WorkspaceCache.hpp"
#include "cursiv/core/workspace/DefaultDocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentBuilder.hpp"
#include "cursiv/core/workspace/DocumentFactory.hpp"
#include "cursiv/core/workspace/Documents.hpp"
#include "cursiv/core/workspace/EmptyFileSystemProvider.hpp"
#include "cursiv/test/Check.hpp"
#include "workspace/generated/Workspace/Module.hpp"

#if !defined(__wasi__) || defined(_REENTRANT)
#include <atomic>
#include <barrier>
#include <thread>
#include <vector>
#endif

// Pins the folded comparison and hash across every split a word boundary
// makes: the hash reads the last word of a name a word long overlapping the
// one before it.
namespace case_fold {

namespace {

// At every length and every byte, a case change keeps the name and any other
// change loses it.
void every_byte_is_compared_folded() {
  for (std::size_t size = 1; size <= 24; ++size) {
    std::string name;
    for (std::size_t i = 0; i < size; ++i)
      name += static_cast<char>('a' + static_cast<char>(i % 26));
    for (std::size_t at = 0; at < size; ++at) {
      std::string upper = name;
      upper[at] = static_cast<char>(upper[at] - 'a' + 'A');
      CHECK(cursiv::folded_equal(name, upper));
      CHECK(cursiv::folded_hash(name) == cursiv::folded_hash(upper));
      std::string other = name;
      other[at] = '_';
      CHECK(!cursiv::folded_equal(name, other));
      CHECK(!cursiv::folded_equal(other, upper));
    }
  }
}

}  // namespace

void run() {
  every_byte_is_compared_folded();
}

}  // namespace case_fold

namespace edit_distance {

void run() {
  const auto unlimited = std::numeric_limits<std::size_t>::max();
  CHECK(cursiv::edit_distance("a", "b", unlimited) == 1);
  CHECK(cursiv::edit_distance("", "abc", unlimited) == 3);
  CHECK(cursiv::edit_distance("abcd", "a", unlimited) == 3);
  CHECK(cursiv::edit_distance("a", "abcd", unlimited - 1) == 3);
  CHECK(cursiv::edit_distance(std::string(65, 'a'), "a", unlimited) ==
        unlimited);
}

}  // namespace edit_distance

// Pins the folder tree: the exact query, the prefix at two depths, and the
// erasure that takes a subtree with it -- the claims the registry makes.
namespace uri_trie {

namespace {

std::vector<int> all(const cursiv::UriTrie<int>& trie, std::string_view uri) {
  return trie.findAll(uri);
}

void moving_reparents_the_root_children() {
  cursiv::UriTrie<int> moved;
  {
    cursiv::UriTrie<int> original;
    original.insert("mem:///leaf", 1);
    cursiv::UriTrie<int> temporary(std::move(original));
    moved = std::move(temporary);
  }
  CHECK(moved.has("mem:///leaf"));
  // Pruning the last value climbs all the way back to the moved root.
  moved.eraseValue("mem:///leaf");
  CHECK(moved.all().empty());
  moved.insert("mem:///another", 2);
  CHECK(moved.find("mem:///another") != nullptr);
  CHECK(moved.all() == std::vector<int>{2});
}

void exact_prefix_and_subtree_erasure() {
  cursiv::UriTrie<int> trie;
  trie.insert("file:///w/a/x.ws", 1);
  trie.insert("file:///w/a/y.ws", 2);
  trie.insert("file:///w/b/z.ws", 3);

  // Exact.
  CHECK(all(trie, "file:///w/a/x.ws") == std::vector<int>{1});
  // Prefix, two depths.
  CHECK(all(trie, "file:///w/a").size() == 2);
  CHECK(all(trie, "file:///w").size() == 3);
  // A sibling name is not a prefix: `/w/a` must not answer for `/w/ab`.
  trie.insert("file:///w/ab/q.ws", 4);
  CHECK(all(trie, "file:///w/a").size() == 2);

  // Erasing the folder takes its subtree, and nobody else's.
  trie.erase("file:///w/a");
  CHECK(all(trie, "file:///w/a").empty());
  CHECK(all(trie, "file:///w").size() == 2);  // b/z and ab/q remain

  // Erasing one value keeps the neighbours.
  trie.eraseValue("file:///w/b/z.ws");
  CHECK(all(trie, "file:///w/b").empty());
  CHECK(all(trie, "file:///w/ab").size() == 1);

  // The root is not erasable: nothing above it to erase it from.
  trie.erase("");
  CHECK(all(trie, "file:///w/ab").size() == 1);
  trie.clear();
  CHECK(all(trie, "file:///w").empty());
}

}  // namespace

void run() {
  exact_prefix_and_subtree_erasure();
  moving_reparents_the_root_children();
}

}  // namespace uri_trie

// Pins the one form every URI is compared and keyed in. Two spellings of one
// file must normalize to the same string: they name the same document.
namespace uri_utils {

namespace {

void the_scheme_is_recognized_whatever_its_case() {
  CHECK(cursiv::is_file_uri("file:///a/b"));
  CHECK(cursiv::is_file_uri("FILE:///a/b"));
  CHECK(!cursiv::is_file_uri("http://x/a"));
  CHECK(cursiv::is_file_uri("file:/a"));
  CHECK(cursiv::is_file_uri("file:/"));
}

// `file:/path` has no authority at all (RFC 8089): a Java client sends it
// where the folder scan sends `file:///path`, and both name one document.
void a_single_slash_names_the_same_file() {
  CHECK(cursiv::file_uri_to_path("file:/a/b/c.txt").value() == "/a/b/c.txt");
  CHECK(cursiv::normalize_uri("file:/a/b/c.txt") == "file:///a/b/c.txt");
  CHECK(cursiv::equals_uri("file:/a/b/c.txt", "file:///a/b/c.txt"));
  CHECK(cursiv::normalize_uri("file:/a/b.ws?rev=2#name") ==
        "file:///a/b.ws?rev=2#name");
  CHECK(cursiv::contains_uri("file:/a", "file:///a/b"));
  CHECK(!cursiv::file_uri_to_path("file:a/b").has_value());
}

// A name is read as vscode-uri reads it, escapes decoded: an escaped dot is
// still the dot of an extension.
void an_escape_does_not_hide_an_extension() {
  CHECK(cursiv::uri_extension("file:///a/b%2Ecalc") == ".calc");
  CHECK(cursiv::uri_file_name("file:///a/b%2Ecalc") == "b.calc");
  CHECK(cursiv::uri_file_name("mem:///w/a%20b.ws") == "a b.ws");
  CHECK(cursiv::uri_file_name("file:///a/c%2Fd") == "d");
  // A query is cut where it starts, before anything is decoded.
  CHECK(cursiv::uri_file_name("file:///a/b%3Fc.ws?x#y") == "b?c.ws");
}

// The authority is written in lower case, userinfo apart: two spellings of one
// host are one place.
void a_host_is_lower_case() {
  CHECK(cursiv::normalize_uri("http://Host:80/A") == "http://host:80/A");
  CHECK(cursiv::normalize_uri("http://User@Host/a") == "http://User@host/a");
  CHECK(cursiv::normalize_uri("file://OtherHost/a") == "file://otherhost/a");
  CHECK(cursiv::equals_uri("http://HOST/a", "http://host/a"));
  CHECK(cursiv::contains_uri("http://Host/a", "http://host/a/b"));
}

void an_empty_path_names_no_file() {
  bool threw = false;
  try {
    (void)cursiv::path_to_file_uri("");
  } catch (const std::invalid_argument&) {
    threw = true;
  }
  CHECK(threw);
}

void uri_suffixes_are_not_file_names() {
  CHECK(cursiv::file_uri_to_path("file:///a.ws?rev=2#name") == "/a.ws");
  CHECK(cursiv::file_uri_to_path("file:///a%3Frev%23name.ws") == "/a?rev#name.ws");
  const std::string canonical = cursiv::normalize_uri("file:///w/../a.ws?rev=2#name");
  CHECK(canonical == "file:///a.ws?rev=2#name");
  CHECK(cursiv::uri_extension(canonical) == ".ws");
  CHECK(!cursiv::equals_uri("file:///a.ws?rev=1", "file:///a.ws?rev=2"));
  CHECK(cursiv::contains_uri("http://host?query", "http://host/child"));
}

void a_path_makes_a_uri_and_back() {
  CHECK(cursiv::path_to_file_uri("/a/b/c.txt") == "file:///a/b/c.txt");
  CHECK(cursiv::file_uri_to_path("file:///a/b/c.txt").value() == "/a/b/c.txt");
  // Only `localhost` is a host this process can read a file from.
  CHECK(cursiv::file_uri_to_path("file://localhost/a").value() == "/a");
  CHECK(!cursiv::file_uri_to_path("file://other-host/a").has_value());
  CHECK(!cursiv::file_uri_to_path("http://x/a").has_value());
}

// A byte outside the unreserved set travels percent-encoded, and comes back.
void what_is_escaped_comes_back() {
  const std::string uri = cursiv::path_to_file_uri("/a/b c/d%e");
  CHECK(uri == "file:///a/b%20c/d%25e");
  CHECK(cursiv::file_uri_to_path(uri).value() == "/a/b c/d%e");
  // A UTF-8 byte is never unreserved, whatever the locale says.
  CHECK(cursiv::path_to_file_uri("/\xC3\xA9") == "file:///%C3%A9");
  CHECK(cursiv::file_uri_to_path("file:///%C3%A9").value() == "/\xC3\xA9");
  // A colon is not unreserved in a path: it travels as vscode-uri sends it.
  CHECK(cursiv::path_to_file_uri("/a:b") == "file:///a%3Ab");
  CHECK(cursiv::file_uri_to_path("file:///a%3Ab").value() == "/a:b");
}

// The whole point: one file, one key.
void one_file_normalizes_to_one_string() {
  CHECK(cursiv::normalize_uri("file:///a/./b/../b/c") == "file:///a/b/c");
  CHECK(cursiv::equals_uri("file:///a/b/c", "file:///a/x/../b/c"));
  CHECK(cursiv::equals_uri("file:///a/b c", "file:///a/b%20c"));
  CHECK(!cursiv::equals_uri("file:///a/b", "file:///a/c"));
  // Not a file URI: left alone, and still comparable.
  CHECK(cursiv::normalize_uri("http://x/a") == "http://x/a");
  CHECK(cursiv::equals_uri("http://x/a", "http://x/a"));
  // ...but for its scheme, which has no case (RFC 3986): one spelling, the
  // lower one, which is how a client's URI already arrives. The path keeps its
  // case, and a colon after a `/` is no scheme.
  CHECK(cursiv::normalize_uri("MEM:///A.ws") == "mem:///A.ws");
  CHECK(cursiv::equals_uri("MEM:///A.ws", "mem:///A.ws"));
  CHECK(!cursiv::equals_uri("mem:///A.ws", "mem:///a.ws"));
  CHECK(cursiv::normalize_uri("dir/A:b.ws") == "dir/A:b.ws");
}

// Segment by segment: `/a/bc` does not live under `/a/b`.
void containment_counts_segments() {
  CHECK(cursiv::contains_uri("file:///a", "file:///a/b/c"));
  CHECK(cursiv::contains_uri("file:///a", "file:///a"));
  CHECK(!cursiv::contains_uri("file:///a/b", "file:///a/bc"));
  CHECK(!cursiv::contains_uri("file:///a/b/c", "file:///a/b"));
  CHECK(cursiv::contains_uri("file:///a/", "file:///a/b"));
  // Any scheme, on its path -- but one scheme and one authority.
  CHECK(cursiv::contains_uri("http://x/a", "http://x/a/b"));
  CHECK(!cursiv::contains_uri("http://x/a", "https://x/a/b"));
  CHECK(!cursiv::contains_uri("http://x/a", "http://y/a/b"));
}

void a_relative_path_walks_back_up() {
  CHECK(cursiv::relative_uri("file:///a/b", "file:///a/b/c") == "c");
  CHECK(cursiv::relative_uri("file:///a/b/c", "file:///a/d") == "../../d");
  CHECK(cursiv::relative_uri("mem:///w/a", "mem:///w/b/c") == "../b/c");
  CHECK(cursiv::relative_uri("file:///a/b", "file:///a/b").empty());
  CHECK(cursiv::relative_uri("dir/a:one.ws", "dir/b:two.ws") == "../b:two.ws");
  // Two places: the absolute normalized target.
  CHECK(cursiv::relative_uri("http://x/a", "file:///a/b") == "file:///a/b");
  CHECK(cursiv::relative_uri("//first.host/a", "//second.host/b") ==
        "//second.host/b");
}

void a_name_is_what_follows_the_last_separator() {
  CHECK(cursiv::uri_file_name("file:///a/b.ws?x#y") == "b.ws");
  CHECK(cursiv::uri_file_name("file:///a/") == "a");
  CHECK(cursiv::uri_extension("file:///a/b.WS") == ".ws");
  CHECK(cursiv::uri_extension("file:///a.dir/b").empty());
  CHECK(cursiv::uri_extension("file:///a/b").empty());
}

void names_and_extensions_follow_the_uri_path() {
  struct Example {
    std::string_view uri;
    std::string_view name;
    std::string_view extension;
  };
  const Example examples[] = {
      {"untitled:Makefile", "Makefile", ""},
      {"untitled:Makefile?other.ws#name", "Makefile", ""},
      {"mem://my.host", "", ""},
      {"mem://my.host?other.ws#name", "", ""},
      {"file://my.host", "", ""},
      {"//my.host", "", ""},
      {"//my.host:9000/a.ws?other#name", "a.ws", ".ws"},
      {"dir/a:b.ws", "a:b.ws", ".ws"},
      {":", ":", ""},
      {":name.ws", ":name.ws", ".ws"},
      {"mem:foo///", "foo", ""},
      {"file:///a/", "a", ""},
      {"mem:///a.ws///", "a.ws", ".ws"},
      {"file:///", "", ""},
      {"mem:///", "", ""},
      {"mem:///a/.env", ".env", ""},
      {"mem:///a/.", ".", ""},
      {"mem:///a/..", "..", ""},
      {"mem:///a/.env.ws", ".env.ws", ".ws"},
      {"mem:///a/..env", "..env", ".env"},
      {"mem:///a/...", "...", "."},
      {"mem:///a/name.", "name.", "."},
      {"mem:///a/name.WS", "name.WS", ".ws"},
      {"mem:///a%2Fb%2Ews%2F", "b.ws", ".ws"},
      {"mem:///a%3Ab.ws", "a:b.ws", ".ws"},
      {"mem:///a/b%3Fc.ws?other#name", "b?c.ws", ".ws"},
  };
  for (const Example& example : examples) {
    CHECK(cursiv::uri_file_name(example.uri) == example.name);
    CHECK(cursiv::uri_extension(example.uri) == example.extension);
  }
}

void an_extension_wears_its_dot() {
  CHECK(cursiv::normalize_extension("calc") == ".calc");
  CHECK(cursiv::normalize_extension(".calc") == ".calc");
  CHECK(cursiv::normalize_extension("").empty());
}

}  // namespace

// A Windows drive letter is a syntactic fact of the URI: the three spellings
// a client may send land on one, whatever platform the server runs on.
void a_drive_letter_normalizes_on_any_host() {
  const std::string canonical = cursiv::normalize_uri("file:///c:/w/a.ws");
  CHECK(cursiv::normalize_uri("file:///C:/w/a.ws") == canonical);
  CHECK(cursiv::normalize_uri("file:///c%3A/w/a.ws") == canonical);
  CHECK(canonical.starts_with("file:///c%3A/"));
  // Any first segment `<letter>:` is a drive, as vscode-uri reads it: the
  // letter is lowered and the colon escaped, on every host.
  CHECK(cursiv::normalize_uri("file:///X:foo/bar") == "file:///x%3Afoo/bar");
}

void run() {
  the_scheme_is_recognized_whatever_its_case();
  a_single_slash_names_the_same_file();
  an_escape_does_not_hide_an_extension();
  a_host_is_lower_case();
  an_empty_path_names_no_file();
  a_path_makes_a_uri_and_back();
  uri_suffixes_are_not_file_names();
  what_is_escaped_comes_back();
  one_file_normalizes_to_one_string();
  containment_counts_segments();
  a_relative_path_walks_back_up();
  an_extension_wears_its_dot();
  a_name_is_what_follows_the_last_separator();
  names_and_extensions_follow_the_uri_path();
  a_drive_letter_normalizes_on_any_host();
}

}  // namespace uri_utils

// Pins the three caches: a hit never recomputes, and above all when an entry
// dies -- per update, per rung, per document -- because a cache that outlives
// its build answers from a world that no longer exists.
namespace caching {

namespace {

struct World {
  cursiv::SharedCoreServices shared;

  World() {
    shared.fileSystemProvider =
        std::make_unique<cursiv::EmptyFileSystemProvider>();
    cursiv::install_default_shared_core_services(shared);
    auto language = std::make_unique<cursiv::CoreServices>(shared);
    language->languageMetaData = {"workspace", {".ws"}, {}};
    language->parser = ws::generated::Workspace::create_parser();
    language->astReflection =
        ws::generated::Workspace::create_ast_reflection();
    cursiv::install_default_core_services(*language);
    shared.serviceRegistry->registerServices(std::move(language));
  }

  cursiv::Document* add(const std::string& uri, std::string text) const {
    auto document =
        shared.documentFactory->fromString(std::move(text), std::string(uri));
    cursiv::Document* const raw = document.get();
    shared.documents->addDocument(std::move(document));
    return raw;
  }
};

// The one question: a miss computes, a hit never does, `clear` starts over.
void a_hit_never_recomputes() {
  cursiv::SimpleCache<int, std::string> cache;
  int computed = 0;
  const auto provider = [&] {
    ++computed;
    return std::string("v") + std::to_string(computed);
  };
  CHECK(cache.get(1, provider) == "v1");
  CHECK(cache.get(1, provider) == "v1");
  CHECK(computed == 1);
  cache.clear();
  CHECK(cache.get(1, provider) == "v2");
  CHECK(computed == 2);
}

// A value that counts its copies: what a fill pays is told by how often the
// cache copies its table.
struct Counted {
  int value = 0;
  static inline std::size_t copies = 0;
  Counted() = default;
  explicit Counted(int v) : value(v) {}
  Counted(const Counted& other) : value(other.value) { ++copies; }
  Counted(Counted&& other) noexcept = default;
  Counted& operator=(const Counted&) = default;
  Counted& operator=(Counted&&) noexcept = default;
};

// Filling a cache key by key costs a constant number of copies a key: a copy
// of the whole table at each miss would make a workspace-wide cache quadratic.
void filling_the_cache_is_linear() {
  constexpr int kKeys = 2000;
  cursiv::SimpleCache<int, Counted> cache;
  Counted::copies = 0;
  for (int k = 0; k < kKeys; ++k)
    CHECK(cache.get(k, [k] { return Counted(k); }).value == k);
  CHECK(Counted::copies < 40u * kKeys);
  for (int k = 0; k < kKeys; ++k)
    CHECK(cache.get(k, [] { return Counted(-1); }).value == k);
}

void providers_can_reenter_the_cache() {
  cursiv::SimpleCache<int, int> cache;
  CHECK(cache.get(1, [&] { return cache.get(2, [] { return 3; }) + 4; }) == 7);
  CHECK(cache.get(2, [] { return 0; }) == 3);
  CHECK(cache.get(3, [&] { return cache.get(3, [] { return 5; }) + 1; }) == 5);
}

#if !defined(__wasi__) || defined(_REENTRANT)
void concurrent_providers_do_not_wait_on_each_other() {
  cursiv::SimpleCache<int, int> cache;
  std::barrier entered(4);
  std::atomic<int> calls{0};
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
    threads.emplace_back([&] {
      const int value = cache.get(1, [&] {
        ++calls;
        entered.arrive_and_wait();
        return 42;
      });
      if (value != 42) ++wrong;
    });
  for (auto& thread : threads) thread.join();
  CHECK(calls.load() == 4);
  CHECK(wrong.load() == 0);
}

void an_evicted_provider_does_not_replace_the_new_generation() {
  cursiv::SimpleCache<int, int> cache;
  std::barrier entered(2);
  std::barrier resume(2);
  int previous = 0;
  std::thread provider([&] {
    previous = cache.get(1, [&] {
      entered.arrive_and_wait();
      resume.arrive_and_wait();
      return 1;
    });
  });
  entered.arrive_and_wait();
  cache.clear();
  CHECK(cache.get(1, [] { return 2; }) == 2);
  resume.arrive_and_wait();
  provider.join();
  CHECK(previous == 1);
  CHECK(cache.get(1, [] { return 3; }) == 2);
}
#endif

void a_failed_provider_can_be_retried() {
  cursiv::SimpleCache<int, int> cache;
  bool threw = false;
  try {
    (void)cache.get(1, []() -> int { throw std::runtime_error("provider"); });
  } catch (const std::runtime_error&) {
    threw = true;
  }
  CHECK(threw);
  CHECK(cache.get(1, [] { return 42; }) == 42);
}

void a_failed_subscription_leaves_no_listener() {
  struct ThrowingBuilder : cursiv::DefaultDocumentBuilder {
    using cursiv::DefaultDocumentBuilder::DefaultDocumentBuilder;
    int buildListeners = 0;
    int documentListeners = 0;

    void addBuildPhaseListener(cursiv::BuildPhaseListener&) override {
      ++buildListeners;
    }
    void removeBuildPhaseListener(cursiv::BuildPhaseListener&) override {
      --buildListeners;
    }
    void addDocumentPhaseListener(cursiv::DocumentPhaseListener&) override {
      ++documentListeners;
    }
    void removeDocumentPhaseListener(cursiv::DocumentPhaseListener&) override {
      --documentListeners;
    }
    void addDocumentUpdateListener(cursiv::DocumentUpdateListener&) override {
      throw std::runtime_error("subscription");
    }
  };

  cursiv::SharedCoreServices shared;
  auto builder = std::make_unique<ThrowingBuilder>(shared);
  const auto* const tracked = builder.get();
  shared.documentBuilder = std::move(builder);
  bool workspaceThrew = false;
  try {
    cursiv::WorkspaceCache<int, int> cache(shared);
  } catch (const std::runtime_error&) {
    workspaceThrew = true;
  }
  CHECK(workspaceThrew);
  CHECK(tracked->buildListeners == 0);

  bool documentThrew = false;
  try {
    cursiv::DocumentCache<int, int> cache(shared, cursiv::DocumentState::Linked);
  } catch (const std::runtime_error&) {
    documentThrew = true;
  }
  CHECK(documentThrew);
  CHECK(tracked->documentListeners == 0);
}

// Without a rung, any announced update empties the workspace cache.
void a_stateless_workspace_cache_empties_on_any_update() {
  World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};
  world.shared.documentBuilder->build(set);

  cursiv::WorkspaceCache<int, int> cache(world.shared);
  int computed = 0;
  const auto provider = [&] { return ++computed; };
  CHECK(cache.get(1, provider) == 1);
  const std::string_view changed[] = {"file:///a.ws"};
  world.shared.documentBuilder->update(changed, {});
  CHECK(cache.get(1, provider) == 2);  // the update emptied it
}

// With a rung, a change empties at the rung the rebuild climbs through, and
// a deletion empties at the announcement (a deleted document reaches no
// rung). The two variants below tell the staged mode from the stateless one.
void a_staged_workspace_cache_empties_at_its_rung() {
  World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};
  world.shared.documentBuilder->build(set);

  cursiv::WorkspaceCache<int, int> cache(world.shared,
                                         cursiv::DocumentState::Linked);
  int computed = 0;
  const auto provider = [&] { return ++computed; };
  CHECK(cache.get(1, provider) == 1);
  const std::string_view changed[] = {"file:///a.ws"};
  world.shared.documentBuilder->update(changed, {});
  CHECK(cache.get(1, provider) == 2);
  (void)cache.get(1, provider);
  const std::string_view gone[] = {"file:///a.ws"};
  world.shared.documentBuilder->update({}, gone);
  CHECK(cache.get(1, provider) == 3);
}

// Partitioned by document: one document's rebuild empties its own entries and
// nobody else's; `Validated` is refused as an eviction rung.
void a_document_cache_evicts_per_document() {
  World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const b = world.add("file:///b.ws", "datatype B\n");
  cursiv::Document* const set[] = {a, b};
  world.shared.documentBuilder->build(set);

  cursiv::DocumentCache<int, int> cache(world.shared);
  int computed = 0;
  const auto provider = [&] { return ++computed; };
  CHECK(cache.get(a->id, 1, provider) == 1);
  CHECK(cache.get(b->id, 1, provider) == 2);
  const std::string_view changed[] = {"file:///a.ws"};
  world.shared.documentBuilder->update(changed, {});
  CHECK(cache.get(a->id, 1, provider) == 3);  // a's entry died with a
  CHECK(cache.get(b->id, 1, provider) == 2);  // b's survived

  bool refused = false;
  try {
    cursiv::DocumentCache<int, int> bad(world.shared,
                                        cursiv::DocumentState::Validated);
  } catch (const std::invalid_argument&) {
    refused = true;
  }
  CHECK(refused);
}

}  // namespace

void run() {
  a_hit_never_recomputes();
  filling_the_cache_is_linear();
  providers_can_reenter_the_cache();
#if !defined(__wasi__) || defined(_REENTRANT)
  concurrent_providers_do_not_wait_on_each_other();
  an_evicted_provider_does_not_replace_the_new_generation();
#endif
  a_failed_provider_can_be_retried();
  a_failed_subscription_leaves_no_listener();
  a_stateless_workspace_cache_empties_on_any_update();
  a_staged_workspace_cache_empties_at_its_rung();
  a_document_cache_evicts_per_document();
}

}  // namespace caching

int main() {
  case_fold::run();
  edit_distance::run();
  uri_trie::run();
  uri_utils::run();
  caching::run();
  return cursiv::test::report("utils");
}
