// The utilities the tree leans on: folded comparison and hashing, the folder
// tree, the one form a URI is keyed in, and the caches emptied with the build
// they belong to.

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(__linux__)
#include <pthread.h>
#endif

#include "cursiv/Check.hpp"
#include "cursiv/services/CoreServices.hpp"
#include "cursiv/services/SharedCoreServices.hpp"
#include "cursiv/utils/CaseFold.hpp"
#include "cursiv/utils/ConcurrentTable.hpp"
#include "cursiv/utils/DocumentCache.hpp"
#include "cursiv/utils/FunctionRef.hpp"
#include "cursiv/utils/RoundMarks.hpp"
#include "cursiv/utils/SimpleCache.hpp"
#include "cursiv/utils/UriTrie.hpp"
#include "cursiv/utils/UriUtils.hpp"
#include "cursiv/utils/WorkspaceCache.hpp"
#include "cursiv/workspace/DefaultDocumentBuilder.hpp"
#include "cursiv/workspace/DocumentBuilder.hpp"
#include "cursiv/workspace/DocumentFactory.hpp"
#include "cursiv/workspace/Documents.hpp"
#include "support/PendingTables.hpp"
#include "support/Throws.hpp"
#include "support/WorkspaceWorld.hpp"

inline constexpr bool kWindowsPaths =
    std::filesystem::path::preferred_separator == '\\';

#if !defined(__wasi__) || defined(_REENTRANT)
#include <atomic>
#include <barrier>
#include <thread>
#include <vector>
#endif

// The folded comparison and hash across every split a word boundary
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
      CHECK(cursiv::detail::name_match(name, upper) ==
            cursiv::detail::NameMatch::Folded);
      CHECK(cursiv::detail::name_match(name, std::string(name)) ==
            cursiv::detail::NameMatch::Exact);
      CHECK(cursiv::folded_hash(name) == cursiv::folded_hash(upper));
      std::string other = name;
      other[at] = '_';
      CHECK(!cursiv::folded_equal(name, other));
      CHECK(!cursiv::folded_equal(other, upper));
    }
  }
}

// Beyond ASCII, a letter matches each of its cases as long in UTF-8, and
// nothing else.
void every_case_of_a_letter_is_one_name() {
  const auto same = [](std::string_view a, std::string_view b) {
    CHECK(cursiv::folded_equal(a, b));
    CHECK(cursiv::folded_equal(b, a));
    CHECK(cursiv::folded_hash(a) == cursiv::folded_hash(b));
  };
  const auto apart = [](std::string_view a, std::string_view b) {
    CHECK(!cursiv::folded_equal(a, b));
    CHECK(!cursiv::folded_equal(b, a));
  };
  same("École", "éCOLE");
  same("ΣΟΦΊΑ", "σοφία");
  same("ΤΈΛΟΣ", "τέλοσ");  // a final sigma is a sigma
  // Cases starting on different bytes, past the first eight.
  same("ΣΣΣΣΣxyz", "σσσσςXYZ");
  same("abcdefghΣ", "ABCDEFGHς");
  same("\U00010400\U00010401abc", "\U00010428\U00010429ABC");
  same("\xff\xfe" "A", "\xff\xfe" "a");  // bytes that are no letter stay
  apart("abcdefgxΣ", "ABCDEFGHς");
  apart("STRASSE", "straße");  // one letter never matches two
  // Cases of another length in UTF-8 match only themselves.
  apart("Straße", "STRAẞE");
  apart("Ωmega", "\u2126MEGA");  // the Ohm sign
  apart("\u212Aelvin", "kelvin");  // the Kelvin sign
  apart("cla\u017F\u017F", "class");
  apart("\u023A\u023Axyz", "\u2C65\u2C65XYZ");
  apart("İstanbul", "istanbul");
  apart("ı", "i");
  apart("e\u0301", "é");  // no normalization
  apart("\xff" "A", "\xfe" "a");
}

}  // namespace

void run() {
  every_byte_is_compared_folded();
  every_case_of_a_letter_is_one_name();
}

}  // namespace case_fold

// A `function_ref` calls what it was bound to: a function, a pointer read at
// binding, a borrowed callable whose later state it sees.
namespace function_ref {

namespace {
int next(int n) { return n + 1; }
int previous(int n) noexcept { return n - 1; }
auto from_temporary_pointer() { return cursiv::function_ref<int(int)>(&next); }
struct Value {
  int offset = 4;
  int operator()(int n) const { return n + offset; }
};
}  // namespace

void run() {
  cursiv::function_ref<int(int)> direct(next);
  CHECK(direct(7) == 8);
  auto held = from_temporary_pointer();
  CHECK(held(2) == 3);
  auto pointer = &next;
  cursiv::function_ref<int(int)> copied(pointer);
  pointer = &previous;
  CHECK(copied(2) == 3);
  CHECK(cursiv::function_ref<int(int)>(&previous)(2) == 1);
  Value value;
  cursiv::function_ref<int(int)> borrowed(value);
  CHECK(borrowed(1) == 5);
  value.offset = 8;
  CHECK(borrowed(1) == 9);
  const Value constant{3};
  cursiv::function_ref<int(int)> constBorrowed(constant);
  CHECK(constBorrowed(2) == 5);
  auto mutableCallback = [count = 0]() mutable { return ++count; };
  cursiv::function_ref<int()> mutableBorrowed(mutableCallback);
  CHECK(mutableBorrowed() == 1);
  CHECK(mutableBorrowed() == 2);
  CHECK(mutableCallback() == 3);
  static_assert(!std::is_constructible_v<cursiv::function_ref<int()>,
                                         const decltype(mutableCallback)&>);
  int calls = 0;
  const auto callback = [&] { return ++calls; };
  cursiv::function_ref<void()> discard(callback);
  discard();
  CHECK(calls == 1);
  cursiv::function_ref<void(int)> discarded_function(next);
  discarded_function(3);
  static_assert(std::is_trivially_copyable_v<decltype(borrowed)>);
}

}  // namespace function_ref

// The folder tree: the exact query, the prefix at two depths, and the erasure
// that takes a subtree with it, as the registry uses them.
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
  // Pruning the last value goes all the way back up to the moved root.
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

#if defined(__linux__)
// A path far deeper than a small thread's stack could recurse through:
// listing the values and freeing the nodes walk it on the heap.
void* deep_path_on_a_small_stack(void* arg) {
  auto* const listed = static_cast<std::size_t*>(arg);
  std::string uri = "mem:///";
  for (int i = 0; i < 100000; ++i) uri += "a/";
  cursiv::UriTrie<int> trie;
  trie.insert(uri + "x", 1);
  trie.insert("mem:///a/y", 2);
  *listed = trie.all().size() + trie.findAll("mem:///a").size();
  trie.erase("mem:///a/a");
  *listed += trie.all().size();
  trie.insert(uri + "z", 3);
  return nullptr;
}

void a_deep_path_needs_no_deep_stack() {
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, std::size_t{128} << 10);
  std::size_t listed = 0;
  pthread_t thread;
  if (pthread_create(&thread, &attr, &deep_path_on_a_small_stack, &listed) ==
      0) {
    pthread_join(thread, nullptr);
    CHECK(listed == 5);
  }
  pthread_attr_destroy(&attr);
}
#endif

}  // namespace

void run() {
  exact_prefix_and_subtree_erasure();
  moving_reparents_the_root_children();
#if defined(__linux__)
  a_deep_path_needs_no_deep_stack();
#endif
}

}  // namespace uri_trie

// The one form every URI is compared and keyed in. Two spellings of one
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
  if constexpr (!kWindowsPaths)
    CHECK(cursiv::file_uri_to_path("file:/a/b/c.txt").value() == "/a/b/c.txt");
  CHECK(cursiv::normalize_uri("file:/a/b/c.txt") == "file:///a/b/c.txt");
  CHECK(cursiv::equals_uri("file:/a/b/c.txt", "file:///a/b/c.txt"));
  CHECK(cursiv::normalize_uri("file:/a/b.ws?rev=2#name") ==
        "file:///a/b.ws?rev%3D2#name");
  CHECK(cursiv::contains_uri("file:/a", "file:///a/b"));
  // No authority and no root: it is rooted, the scheme having no relative form.
  CHECK(cursiv::normalize_uri("file:a/b") == "file:///a/b");
}

// A name is read with its escapes decoded: an escaped dot is still the dot of an
// extension.
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
  CHECK(support::throws<std::invalid_argument>([&] {
    (void)cursiv::path_to_file_uri("");
  }));
}

void uri_suffixes_are_not_file_names() {
  if constexpr (!kWindowsPaths) {
    CHECK(cursiv::file_uri_to_path("file:///a.ws?rev=2#name") == "/a.ws");
    CHECK(cursiv::file_uri_to_path("file:///a%3Frev%23name.ws") ==
          "/a?rev#name.ws");
  }
  const std::string canonical = cursiv::normalize_uri("file:///a.ws?rev=2#name");
  CHECK(canonical == "file:///a.ws?rev%3D2#name");
  CHECK(cursiv::uri_extension(canonical) == ".ws");
  CHECK(!cursiv::equals_uri("file:///a.ws?rev=1", "file:///a.ws?rev=2"));
  CHECK(cursiv::contains_uri("http://host?query", "http://host/child"));
}

void a_path_makes_a_uri_and_back() {
  if constexpr (kWindowsPaths) {
    // A rooted path with no drive sits on the current one, unknown here. The
    // drive letter is spelled as `normalize_uri` spells it.
    CHECK(cursiv::path_to_file_uri("C:\\a\\b.txt") == "file:///c%3A/a/b.txt");
    CHECK(cursiv::file_uri_to_path("file:///c%3A/a/b.txt").value() ==
          "c:\\a\\b.txt");
    CHECK(cursiv::normalize_uri("file:///w/a.ws") == "file:///w/a.ws");
    CHECK(cursiv::file_uri_to_path("file://localhost/c:/a").value() == "c:\\a");
    // A share: the host stands first, and its drive-like name is no drive.
    CHECK(cursiv::file_uri_to_path("file://server/share/a.ws").value() ==
          "\\\\server\\share\\a.ws");
    CHECK(cursiv::file_uri_to_path("file://server/C:/a").value() ==
          "\\\\server\\C:\\a");
    CHECK(cursiv::file_uri_to_path("file://wsl.localhost/Ubuntu/a").value() ==
          "\\\\wsl.localhost\\Ubuntu\\a");
    CHECK(cursiv::path_to_file_uri("\\\\Server\\share\\a b") ==
          "file://server/share/a%20b");
    // Before a share, `localhost` is that share's host, not the current drive.
    CHECK(cursiv::path_to_file_uri("\\\\localhost\\share\\a") ==
          "file://localhost/share/a");
    CHECK(cursiv::file_uri_to_path("file://localhost/share/a").value() ==
          "\\\\localhost\\share\\a");
    CHECK(!cursiv::file_uri_to_path("file://server").has_value());
  } else {
    CHECK(cursiv::path_to_file_uri("/a/b/c.txt") == "file:///a/b/c.txt");
    CHECK(cursiv::file_uri_to_path("file:///a/b/c.txt").value() == "/a/b/c.txt");
    // Only `localhost` is a host this process can read a file from.
    CHECK(cursiv::file_uri_to_path("file://localhost/a").value() == "/a");
    CHECK(!cursiv::file_uri_to_path("file://other-host/a").has_value());
    // A drive letter keeps its root, and one spelling: a WASI runner on Windows
    // preopens each drive under that name.
    CHECK(cursiv::file_uri_to_path("file:///C%3A/a").value() == "/c:/a");
    CHECK(cursiv::path_to_file_uri("/C:/a") == "file:///c%3A/a");
    // A path is resolved, which a URI is not.
    CHECK(cursiv::path_to_file_uri("/a/./b/../c") == "file:///a/c");
  }
  CHECK(!cursiv::file_uri_to_path("http://x/a").has_value());
}

// A byte outside the unreserved set travels percent-encoded, and comes back.
void what_is_escaped_comes_back() {
  if constexpr (kWindowsPaths) {
    const std::string uri = cursiv::path_to_file_uri("C:\\a\\b c\\d%e");
    CHECK(uri == "file:///c%3A/a/b%20c/d%25e");
    CHECK(cursiv::file_uri_to_path(uri).value() == "c:\\a\\b c\\d%e");
  } else {
    const std::string uri = cursiv::path_to_file_uri("/a/b c/d%e");
    CHECK(uri == "file:///a/b%20c/d%25e");
    CHECK(cursiv::file_uri_to_path(uri).value() == "/a/b c/d%e");
    // A UTF-8 byte is never unreserved, whatever the locale says.
    CHECK(cursiv::path_to_file_uri("/\xC3\xA9") == "file:///%C3%A9");
    CHECK(cursiv::file_uri_to_path("file:///%C3%A9").value() == "/\xC3\xA9");
    // A colon is not unreserved in a path: it travels escaped.
    CHECK(cursiv::path_to_file_uri("/a:b") == "file:///a%3Ab");
    CHECK(cursiv::file_uri_to_path("file:///a%3Ab").value() == "/a:b");
  }
}

// Each URI and the one spelling it has, whatever the host.
void a_uri_has_one_spelling() {
  const std::pair<std::string_view, std::string_view> spelled[] = {
      {R"(file:///c%3A/Users/me/a.dsl)", R"(file:///c%3A/Users/me/a.dsl)"},
      {R"(file:///C:/Users/me/a.dsl)", R"(file:///c%3A/Users/me/a.dsl)"},
      {R"(file:///C%3a/x)", R"(file:///c%3A/x)"},
      {R"(file:///home/me/My%20File.dsl)", R"(file:///home/me/My%20File.dsl)"},
      {R"(file:///caf%C3%A9.dsl)", R"(file:///caf%C3%A9.dsl)"},
      {R"(file:///a+b/c%2Bd)", R"(file:///a%2Bb/c%2Bd)"},
      {R"(file:///a%2Fb)", R"(file:///a/b)"},
      {R"(file:///a%23b)", R"(file:///a%23b)"},
      {R"(file:///a%3Fb)", R"(file:///a%3Fb)"},
      {R"(file:///a%25b)", R"(file:///a%25b)"},
      {R"(file:///!$&'()*,;=@[])",
       R"(file:///%21%24%26%27%28%29%2A%2C%3B%3D%40%5B%5D)"},
      {R"(file:///a b)", R"(file:///a%20b)"},
      {R"(file:///a/b?x=1)", R"(file:///a/b?x%3D1)"},
      {R"(file:///a/b?a/b)", R"(file:///a/b?a%2Fb)"},
      {R"(file:///a/b#a/b)", R"(file:///a/b#a%2Fb)"},
      {R"(file:///a/b?)", R"(file:///a/b)"},
      {R"(file:///a/b#)", R"(file:///a/b)"},
      {R"(file://server/a/b)", R"(file://server/a/b)"},
      {R"(file://Host/a/b)", R"(file://host/a/b)"},
      {R"(file://wsl.localhost/a/b)", R"(file://wsl.localhost/a/b)"},
      {R"(file://localhost/a/b)", R"(file://localhost/a/b)"},
      {R"(file://LocalHost/C:/x)", R"(file://localhost/c%3A/x)"},
      {R"(file://local%68ost/a)", R"(file://localhost/a)"},
      {R"(file://server/C:/Users/me/a.dsl)",
       R"(file://server/c%3A/Users/me/a.dsl)"},
      {R"(http://Host/a/b)", R"(http://host/a/b)"},
      {R"(http://localhost/a/b)", R"(http://localhost/a/b)"},
      {R"(http://User:Pw@Host:80/a/b)", R"(http://User:Pw@host:80/a/b)"},
      {R"(http://User@Host/a/b)", R"(http://User@host/a/b)"},
      {R"(http://[::1]:8080/a/b)", R"(http://[::1]:8080/a/b)"},
      {R"(http://[::1]/a/b)", R"(http://[::1]/a/b)"},
      {R"(http://[FE80::1%2541]/a)", R"(http://[fe80::1%2541]/a)"},
      {R"(http://[fe80::1%25en1]:8080/a)", R"(http://[fe80::1%25en1]:8080/a)"},
      {R"(http://a%3Ab/a)", R"(http://a%3Ab/a)"},
      {R"(http://a%5Bb%5D:80/a)", R"(http://a%5Bb%5D:80/a)"},
      {R"(http://a%40b@c/a/b)", R"(http://a%40b@c/a/b)"},
      {R"(https://alice:p%40ss@EXAMPLE.com/a)",
       R"(https://alice:p%40ss@example.com/a)"},
      {R"(https://alice:p%3Ass@host/a)", R"(https://alice:p%3Ass@host/a)"},
      {R"(https://alice:p:ss@host/a)", R"(https://alice:p%3Ass@host/a)"},
      {R"(https://al%3Aice:pw@host/a)", R"(https://al%3Aice:pw@host/a)"},
      {R"(vscode-remote://wsl%2Bubuntu/a/b)",
       R"(vscode-remote://wsl%2Bubuntu/a/b)"},
      {R"(untitled:Untitled-1)", R"(untitled:Untitled-1)"},
      {R"(untitled:C:/x)", R"(untitled:c%3A/x)"},
      {R"(mem:///a b)", R"(mem:/a%20b)"},
  };
  for (const auto& [uri, canonical] : spelled) {
    CHECK(cursiv::normalize_uri(uri) == canonical);
    if (cursiv::normalize_uri(uri) != canonical)
      std::printf("%.*s\n", static_cast<int>(uri.size()), uri.data());
    // Read again, it does not move.
    CHECK(cursiv::normalize_uri(canonical) == canonical);
  }
  // Text, not a path: what `..` names is for the file system to say.
  CHECK(cursiv::normalize_uri("file:///a/./b/../c") == "file:///a/./b/../c");
}

// One file, one key.
void one_file_normalizes_to_one_string() {
  CHECK(cursiv::equals_uri("file:///a/b c", "file:///a/b%20c"));
  CHECK(!cursiv::equals_uri("file:///a/b", "file:///a/c"));
  // `localhost` is a host like any other: on Windows it may name a share.
  CHECK(!cursiv::equals_uri("file://localhost/a/b", "file:///a/b"));
  CHECK(!cursiv::contains_uri("file:///a", "file://localhost/a/b"));
  // Not a file URI: left alone, and still comparable.
  CHECK(cursiv::normalize_uri("http://x/a") == "http://x/a");
  CHECK(cursiv::equals_uri("http://x/a", "http://x/a"));
  // ...but for its scheme, which has no case (RFC 3986): one spelling, the
  // lower one, which is how a client's URI already arrives. The path keeps its
  // case, and a colon after a `/` is no scheme.
  CHECK(cursiv::normalize_uri("MEM:///A.ws") == "mem:/A.ws");
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
  // The answer is a decoded path; another drive, scheme or host gives the
  // absolute target.
  CHECK(cursiv::relative_uri("file:///a/b%20c", "file:///a/d%20e") == "../d e");
  CHECK(cursiv::relative_uri("file:///c%3A/a", "file:///C:/a/b") == "b");
  CHECK(cursiv::relative_uri("file:///c%3A/a", "file:///d%3A/b%20c") == "d:/b c");
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

void an_extension_gets_its_leading_dot() {
  CHECK(cursiv::normalize_extension("calc") == ".calc");
  CHECK(cursiv::normalize_extension(".calc") == ".calc");
  CHECK(cursiv::normalize_extension("").empty());
}

}  // namespace

// A Windows drive letter is a syntactic fact of the URI: the three spellings
// a client may send map to one, whatever platform the server runs on.
void a_drive_letter_normalizes_on_any_host() {
  const std::string canonical = cursiv::normalize_uri("file:///c:/w/a.ws");
  CHECK(cursiv::normalize_uri("file:///C:/w/a.ws") == canonical);
  CHECK(cursiv::normalize_uri("file:///c%3A/w/a.ws") == canonical);
  CHECK(canonical.starts_with("file:///c%3A/"));
  // Any first segment `<letter>:` is a drive: the letter is lowered and the
  // colon escaped, on every host.
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
  a_uri_has_one_spelling();
  one_file_normalizes_to_one_string();
  containment_counts_segments();
  a_relative_path_walks_back_up();
  an_extension_gets_its_leading_dot();
  a_name_is_what_follows_the_last_separator();
  names_and_extensions_follow_the_uri_path();
  a_drive_letter_normalizes_on_any_host();
}

}  // namespace uri_utils

// The three caches: a hit never recomputes, and an entry is dropped at its
// update, state or document. Every member is compiled, in both storages.
template class cursiv::detail::ConcurrentTable<int, int>;
template class cursiv::detail::ConcurrentTable<std::string, int>;
template class cursiv::detail::ConcurrentTable<int, std::string>;

namespace caching {

namespace {

using World = support::CoreWorkspace;

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

// A value that counts its copies, to tell how often a fill copies the cache's
// table.
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
  CHECK(support::throws<std::runtime_error>([&] {
    (void)cache.get(1, []() -> int { throw std::runtime_error("provider"); });
  }));
  CHECK(cache.get(1, [] { return 42; }) == 42);
}


// A value memo has no key, and nullptr is a computed answer rather than a
// miss. Consumers borrow the owning value without a copy/reference count.
void a_value_cache_keeps_null_and_borrows_without_copying() {
  cursiv::SimpleCache<const int*> empty;
  int computed = 0;
  const auto null = [&]() -> const int* { ++computed; return nullptr; };
  CHECK(empty.get(null) == nullptr);
  empty.withValue(null, [&](const int* value) { CHECK(value == nullptr); });
  CHECK(computed == 1);
  empty.clear();
  CHECK(empty.get(null) == nullptr);
  CHECK(computed == 2);

  cursiv::SimpleCache<std::shared_ptr<int>> owned;
  (void)owned.get([] { return std::make_shared<int>(42); });
  owned.withValue([] { return std::make_shared<int>(0); },
                  [&](const std::shared_ptr<int>& value) {
                    CHECK(*value == 42);
                    CHECK(value.use_count() == 1);
                  });
}

void value_providers_and_consumers_can_reenter() {
  cursiv::SimpleCache<int> cache;
  CHECK(cache.get([&] { return cache.get([] { return 5; }) + 1; }) == 5);
  cache.withValue([] { return 0; }, [&](const int& value) {
    CHECK(value == 5);
    cache.clear();
    CHECK(cache.get([] { return 7; }) == 7);
    CHECK(value == 5);  // released while this consumer still holds its guard
  });
  CHECK(cache.get([] { return 0; }) == 7);
  CHECK(support::throws<std::runtime_error>([&] {
    cache.withValue([] { return 0; }, [](const int&) {
      throw std::runtime_error("consumer");
    });
  }));
  CHECK(cache.get([] { return 0; }) == 7);

  cache.clear();
  CHECK(support::throws<std::runtime_error>([&] {
    (void)cache.get([]() -> int { throw std::runtime_error("provider"); });
  }));
  CHECK(cache.get([] { return 42; }) == 42);
}

#if !defined(__wasi__) || defined(_REENTRANT)
void value_providers_do_not_wait_and_clear_rejects_stale_publication() {
  cursiv::SimpleCache<int> cache;
  std::barrier entered(4);
  std::atomic<int> calls{0};
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  for (int i = 0; i < 4; ++i)
    threads.emplace_back([&] {
      const int value = cache.get([&] {
        ++calls;
        entered.arrive_and_wait();
        return 42;
      });
      if (value != 42) ++wrong;
    });
  for (auto& thread : threads) thread.join();
  CHECK(calls.load() == 4);
  CHECK(wrong.load() == 0);

  cache.clear();
  std::barrier computing(2), resume(2);
  int previous = 0;
  std::thread provider([&] {
    previous = cache.get([&] {
      computing.arrive_and_wait();
      resume.arrive_and_wait();
      return 1;
    });
  });
  computing.arrive_and_wait();
  cache.clear();
  CHECK(cache.get([] { return 2; }) == 2);
  resume.arrive_and_wait();
  provider.join();
  CHECK(previous == 1);
  CHECK(cache.get([] { return 3; }) == 2);
}

void a_value_consumer_keeps_its_answer_alive_during_clear() {
  cursiv::SimpleCache<std::shared_ptr<int>> cache;
  (void)cache.get([] { return std::make_shared<int>(41); });
  std::weak_ptr<int> previous;
  std::barrier reading(2), resume(2);
  int read = 0;
  std::thread consumer([&] {
    cache.withValue([] { return std::make_shared<int>(0); },
                    [&](const std::shared_ptr<int>& value) {
                      previous = value;
                      reading.arrive_and_wait();
                      resume.arrive_and_wait();
                      read = *value;
                    });
  });
  reading.arrive_and_wait();
  cache.clear();
  support::collect_pending_tables();
  CHECK(!previous.expired());
  CHECK(*cache.get([] { return std::make_shared<int>(42); }) == 42);
  resume.arrive_and_wait();
  consumer.join();
  CHECK(read == 41);
  support::collect_pending_tables();
  CHECK(previous.expired());
}
#endif

// A collected value's destructor runs with no cache lock held, so it may
// read or empty the cache, keyed or not.
void a_cleared_value_can_reenter_the_cache_when_destroyed() {
  cursiv::SimpleCache<std::shared_ptr<int>> cache;
  int released = 0, observed = 0;
  (void)cache.get([&] {
    return std::shared_ptr<int>(new int(41), [&](int* value) {
      delete value;
      ++released;
      observed = *cache.get([] { return std::make_shared<int>(7); });
    });
  });
  cache.clear();
  for (int i = 0; i < 64; ++i) cache.clear();
  support::collect_pending_tables();
  CHECK(released == 1);
  CHECK(observed == 7);

  cursiv::SimpleCache<int, std::shared_ptr<int>> keyed;
  (void)keyed.get(1, [&] {
    return std::shared_ptr<int>(new int(41), [&](int* value) {
      delete value;
      ++released;
      keyed.clear();
      observed = *keyed.get(2, [] { return std::make_shared<int>(8); });
    });
  });
  for (int i = 0; i < 64; ++i) keyed.clear();
  support::collect_pending_tables();
  CHECK(released == 2);
  CHECK(observed == 8);
}

// A clear during a provider leaves the caller its own answer, read with no
// guard: what is released meanwhile can be collected.
void a_stale_answer_holds_back_no_collection() {
  cursiv::SimpleCache<int> cache;
  cursiv::SimpleCache<std::shared_ptr<int>> other;
  std::weak_ptr<int> dropped;
  (void)other.get([&] {
    auto value = std::make_shared<int>(5);
    dropped = value;
    return value;
  });
  bool consumed = false;
  cache.withValue(
      [&] {
        cache.clear();
        return 1;
      },
      [&](const int& value) {
        consumed = value == 1;
        other.clear();
        support::collect_pending_tables();
        CHECK(dropped.expired());
      });
  CHECK(consumed);
}

// Waits past the collection delay. A loop on the clock, not a sleep, so a
// build without threads runs it too.
void wait_past_the_collection_delay() {
  const auto until =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
  while (std::chrono::steady_clock::now() < until) {
  }
}

// A first read of a document may grow the cache's table and collect; a
// value freed then may read the same cache.
void a_cleared_value_can_read_a_growing_document_cache() {
  World world;
  cursiv::DocumentCache<int, int> cache(world.shared);
  int observed = 0;
  cursiv::SimpleCache<std::shared_ptr<int>> holder;
  (void)holder.get([&] {
    return std::shared_ptr<int>(new int(0), [&](int* value) {
      delete value;
      observed = cache.get(cursiv::DocumentId{100000}, 0, [] { return 9; });
    });
  });
  // The only release since a collection, then a wait: the next release
  // collects.
  support::collect_pending_tables();
  holder.clear();
  wait_past_the_collection_delay();
  CHECK(observed == 0);
  for (std::uint32_t d = 1; d <= 64 && observed == 0; ++d)
    (void)cache.get(cursiv::DocumentId{d}, 0, [] { return 1; });
  CHECK(observed == 9);
}

void a_workspace_value_uses_the_same_invalidation_lifecycle() {
  World world;
  cursiv::Document* const document = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {document};
  world.shared.documentBuilder->build(set);
  cursiv::WorkspaceCache<int> unstaged(world.shared);
  cursiv::WorkspaceCache<int> linked(world.shared,
                                          cursiv::DocumentState::Linked);
  int first = 0, second = 0;
  const auto a = [&] { return ++first; };
  const auto b = [&] { return ++second; };
  CHECK(unstaged.get(a) == 1);
  CHECK(linked.get(b) == 1);
  world.shared.documentBuilder->build({});
  CHECK(unstaged.get(a) == 2);
  CHECK(linked.get(b) == 2);
  const std::string_view changed[] = {"file:///a.ws"};
  world.shared.documentBuilder->update(changed, {});
  CHECK(unstaged.get(a) == 3);
  CHECK(linked.get(b) == 3);
  world.shared.documentBuilder->update({}, changed);
  CHECK(unstaged.get(a) == 4);
  CHECK(linked.get(b) == 4);
}

// Value-only caches do not ask their answer to be hashable. In particular,
// constructing the default std::hash for this ordinary aggregate is disabled.
struct UnhashedValue {
  int number;
};
static_assert(!std::is_default_constructible_v<std::hash<UnhashedValue>>);

void single_value_caches_do_not_require_hashable_values() {
  World world;
  cursiv::SimpleCache<UnhashedValue> simple;
  cursiv::WorkspaceCache<UnhashedValue> workspace(world.shared);
  const auto provider = [] { return UnhashedValue{42}; };
  CHECK(simple.get(provider).number == 42);
  simple.withValue(provider,
                   [](const UnhashedValue& value) { CHECK(value.number == 42); });
  CHECK(workspace.get(provider).number == 42);
  workspace.withValue(
      provider, [](const UnhashedValue& value) { CHECK(value.number == 42); });
}

// Each document owns one answer. Looking without a provider never computes,
// and kNoDocument always computes an uncached local answer, including borrows.
void a_document_value_is_partitioned_and_can_be_read_without_computing() {
  World world;
  cursiv::DocumentCache<UnhashedValue> cache(world.shared);
  const cursiv::DocumentId a{1}, b{2};
  int computed = 0;
  const auto provider = [&] { return UnhashedValue{++computed}; };
  CHECK(!cache.get(a).has_value());
  CHECK(!cache.get(cursiv::kNoDocument).has_value());
  CHECK(cache.get(a, provider).number == 1);
  cache.withValue(a, provider, [](const UnhashedValue& value) {
    CHECK(value.number == 1);
  });
  CHECK(cache.get(b, provider).number == 2);
  CHECK(cache.get(a)->number == 1);
  CHECK(computed == 2);
  cache.clear(a);
  CHECK(!cache.get(a).has_value());
  CHECK(cache.get(b)->number == 2);
  cache.withValue(a, provider, [](const UnhashedValue& value) {
    CHECK(value.number == 3);
  });
  cache.clear();
  CHECK(!cache.get(a).has_value());
  CHECK(!cache.get(b).has_value());
  CHECK(cache.get(a, provider).number == 4);
  CHECK(cache.get(cursiv::kNoDocument, provider).number == 5);
  CHECK(cache.get(cursiv::kNoDocument, provider).number == 6);
  cache.withValue(cursiv::kNoDocument, provider,
                  [&](const UnhashedValue& value) {
                    CHECK(value.number == 7);
                    CHECK(!cache.get(cursiv::kNoDocument).has_value());
                  });
  cache.clear(cursiv::kNoDocument);
  CHECK(cache.get(a)->number == 4);
  CHECK(computed == 7);

  cursiv::DocumentCache<const int*> nullable(world.shared);
  CHECK(nullable.get(a, []() -> const int* { return nullptr; }) == nullptr);
  const auto found = nullable.get(a);
  CHECK(found.has_value());
  CHECK(*found == nullptr);

  cursiv::DocumentCache<std::shared_ptr<int>> owned(world.shared);
  (void)owned.get(a, [] { return std::make_shared<int>(42); });
  owned.withValue(a, [] { return std::make_shared<int>(0); },
                  [](const std::shared_ptr<int>& value) {
                    CHECK(*value == 42);
                    CHECK(value.use_count() == 1);
                  });
}

// A change clears an unstaged answer when the update is reported, and a staged
// one when its document reaches the cache's state. A deletion clears either at
// once; the other document keeps its answer throughout.
void document_values_follow_updates_states_and_deletions() {
  for (const cursiv::DocumentState eviction :
       {cursiv::DocumentState::Linked,
        cursiv::DocumentState::IndexedReferences}) {
    World world;
    cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
    cursiv::Document* const b = world.add("file:///b.ws", "datatype B\n");
    cursiv::Document* const documents[] = {a, b};
    world.shared.documentBuilder->build(documents);
    const cursiv::DocumentId own = a->id, other = b->id;
    cursiv::DocumentCache<int> plain(world.shared);
    cursiv::DocumentCache<int> staged(world.shared, eviction);
    CHECK(plain.get(own, [] { return 11; }) == 11);
    CHECK(plain.get(other, [] { return 12; }) == 12);
    CHECK(staged.get(own, [] { return 21; }) == 21);
    CHECK(staged.get(other, [] { return 22; }) == 22);

    struct Observer : cursiv::DocumentPhaseListener {
      cursiv::DocumentCache<int>& plain;
      cursiv::DocumentCache<int>& staged;
      const cursiv::DocumentId own;
      const cursiv::DocumentState eviction;
      int before = 0, reached = 0;
      Observer(cursiv::DocumentCache<int>& p, cursiv::DocumentCache<int>& s,
               cursiv::DocumentId d, cursiv::DocumentState r)
          : plain(p), staged(s), own(d), eviction(r) {}
      void onDocumentPhase(cursiv::Document& document,
                           cursiv::DocumentState state,
                           const cursiv::CancellationToken&) override {
        if (document.id != own) return;
        if (state == static_cast<cursiv::DocumentState>(
                         static_cast<unsigned>(eviction) - 1)) {
          CHECK(!plain.get(own).has_value());
          CHECK(staged.get(own) == 21);
          ++before;
        }
        if (state == eviction) {
          CHECK(!staged.get(own).has_value());
          ++reached;
        }
      }
    } observer(plain, staged, own, eviction);
    world.shared.documentBuilder->addDocumentPhaseListener(observer);
    const std::string_view changed[] = {"file:///a.ws"};
    world.shared.documentBuilder->update(changed, {});
    CHECK(observer.before == 1);
    CHECK(observer.reached == 1);
    CHECK(!plain.get(own).has_value());
    CHECK(!staged.get(own).has_value());
    CHECK(plain.get(other) == 12);
    CHECK(staged.get(other) == 22);
    CHECK(plain.get(own, [] { return 13; }) == 13);
    CHECK(staged.get(own, [] { return 23; }) == 23);
    world.shared.documentBuilder->update({}, changed);
    world.shared.documentBuilder->removeDocumentPhaseListener(observer);
    CHECK(!plain.get(own).has_value());
    CHECK(!staged.get(own).has_value());
    CHECK(plain.get(other) == 12);
    CHECK(staged.get(other) == 22);
    CHECK(observer.reached == 1);  // a deleted document reaches no state
  }
}

#if !defined(__wasi__) || defined(_REENTRANT)
// One partition's clear must not reject another's pending provider. Clearing
// the provider's own partition does reject its publication, while still
// returning its computed value to that caller.
void document_value_providers_are_invalidated_only_by_their_partition() {
  World world;
  cursiv::DocumentCache<int> cache(world.shared);
  const cursiv::DocumentId a{1}, b{2};
  CHECK(cache.get(b, [] { return 20; }) == 20);
  std::barrier computing(2), resume(2);
  for (int round = 1; round <= 2; ++round) {
    cache.clear(a);
    int previous = 0;
    std::thread provider([&] {
      previous = cache.get(a, [&] {
        computing.arrive_and_wait();
        resume.arrive_and_wait();
        return round;
      });
    });
    computing.arrive_and_wait();
    cache.clear(round == 1 ? b : a);
    resume.arrive_and_wait();
    provider.join();
    CHECK(previous == round);
    if (round == 1) {
      CHECK(cache.get(a) == 1);
      CHECK(!cache.get(b).has_value());
      CHECK(cache.get(b, [] { return 21; }) == 21);
    } else {
      CHECK(!cache.get(a).has_value());
      CHECK(cache.get(b) == 21);
    }
  }
}
#endif

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
  CHECK(support::throws<std::runtime_error>([&] {
    cursiv::WorkspaceCache<int, int> cache(shared);
  }));
  CHECK(tracked->buildListeners == 0);
  CHECK(support::throws<std::runtime_error>([&] {
    cursiv::WorkspaceCache<int> cache(shared);
  }));
  CHECK(tracked->buildListeners == 0);

  CHECK(support::throws<std::runtime_error>([&] {
    cursiv::DocumentCache<int, int> cache(shared, cursiv::DocumentState::Linked);
  }));
  CHECK(tracked->documentListeners == 0);
  CHECK(support::throws<std::runtime_error>([&] {
    cursiv::DocumentCache<int> cache(shared, cursiv::DocumentState::Linked);
  }));
  CHECK(tracked->documentListeners == 0);
}

// Without an eviction state, any reported update empties the workspace cache.
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

// With an eviction state, a change empties the cache when the rebuild reaches
// that state, and a deletion when it is reported (a deleted document reaches
// no state). The two variants below tell the staged mode from the stateless
// one.
void a_staged_workspace_cache_empties_at_its_state() {
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
// nobody else's; `Validated` is refused as an eviction state.
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

  CHECK(support::throws<std::invalid_argument>([&] {
    cursiv::DocumentCache<int, int> bad(world.shared,
                                        cursiv::DocumentState::Validated);
  }));
}

// A hit never recomputes, a document's entries are its own, and no document
// at all is never cached.
void a_document_cache_hits_per_document() {
  World world;
  cursiv::DocumentCache<int, int> cache(world.shared);
  const cursiv::DocumentId a{1};
  const cursiv::DocumentId b{2};
  int computed = 0;
  const auto provider = [&] { return ++computed; };
  CHECK(cache.get(a, 1, provider) == 1);
  CHECK(cache.get(a, 1, provider) == 1);
  CHECK(cache.get(a, 2, provider) == 2);
  CHECK(cache.get(b, 1, provider) == 3);
  cache.clear(a);
  CHECK(cache.get(a, 1, provider) == 4);
  CHECK(cache.get(b, 1, provider) == 3);
  cache.clear();
  CHECK(cache.get(b, 1, provider) == 5);
  CHECK(cache.get(cursiv::kNoDocument, 1, provider) == 6);
  CHECK(cache.get(cursiv::kNoDocument, 1, provider) == 7);
}

// What the standard maps accept as a hash and an equality is enough: one that
// can be copied but not swapped, or one whose assignment throws, still lets a
// document be cleared and cached again.
struct SaltedHash {
  const std::size_t salt = 7;
  std::size_t operator()(int key) const noexcept {
    return std::hash<int>{}(key) ^ salt;
  }
};
struct ThrowingAssignEqual {
  ThrowingAssignEqual() = default;
  ThrowingAssignEqual(const ThrowingAssignEqual&) = default;
  ThrowingAssignEqual& operator=(const ThrowingAssignEqual&) {
    throw std::runtime_error("assigned");
  }
  bool operator()(int a, int b) const noexcept { return a == b; }
};

void a_document_cache_clears_with_any_hash() {
  World world;
  cursiv::DocumentCache<int, int, SaltedHash> salted(world.shared);
  cursiv::DocumentCache<int, int, std::hash<int>, ThrowingAssignEqual> throwing(
      world.shared);
  const cursiv::DocumentId a{1};
  int computed = 0;
  const auto provider = [&] { return ++computed; };
  CHECK(salted.get(a, 1, provider) == 1);
  salted.clear(a);
  CHECK(salted.get(a, 1, provider) == 2);
  CHECK(throwing.get(a, 1, provider) == 3);
  CHECK(!support::throws<std::runtime_error>([&] { throwing.clear(a); }));
  CHECK(throwing.get(a, 1, provider) == 4);
  CHECK(throwing.get(a, 1, provider) == 4);
}

// A value computed across a removal of its document answers its caller but
// stays out of the cache. Each provider removes while it runs, so the race is
// played the same way every time.
void a_document_cache_keeps_out_what_a_clear_overtakes() {
  World world;
  cursiv::DocumentCache<int, int> cache(world.shared);
  const cursiv::DocumentId a{1};
  // Its own document cleared, before it has any entry.
  CHECK(cache.get(a, 1, [&] {
    cache.clear(a);
    return 1;
  }) == 1);
  CHECK(cache.get(a, 1, [] { return 2; }) == 2);
  // Everything cleared, then the document cached again: the value still
  // describes what was cleared. A document never cleared on its own, so a
  // count of its clears alone would not tell.
  const cursiv::DocumentId b{2};
  CHECK(cache.get(b, 3, [&] {
    cache.clear();
    CHECK(cache.get(b, 4, [] { return 40; }) == 40);
    return 3;
  }) == 3);
  CHECK(cache.get(b, 3, [] { return 30; }) == 30);
  CHECK(cache.get(b, 4, [] { return 0; }) == 40);
}

// The same for a deletion, with and without an eviction state, and with the
// document cached again before the value is done.
void a_document_cache_keeps_out_what_a_deletion_overtakes() {
  World world;
  cursiv::Document* const a = world.add("file:///a.ws", "datatype A\n");
  cursiv::Document* const set[] = {a};
  world.shared.documentBuilder->build(set);
  const cursiv::DocumentId id = a->id;

  cursiv::DocumentCache<int, int> plain(world.shared);
  cursiv::DocumentCache<int, int> staged(world.shared,
                                         cursiv::DocumentState::Linked);
  const std::string_view gone[] = {"file:///a.ws"};
  const int seen = plain.get(id, 1, [&] {
    return staged.get(id, 1, [&] {
      world.shared.documentBuilder->update({}, gone);
      CHECK(plain.get(id, 2, [] { return 20; }) == 20);
      CHECK(staged.get(id, 2, [] { return 20; }) == 20);
      return 1;
    });
  });
  CHECK(seen == 1);
  CHECK(plain.get(id, 1, [] { return 2; }) == 2);
  CHECK(staged.get(id, 1, [] { return 2; }) == 2);
}

// Documents may share a lock, but a clear of one costs no other document its
// computation, whatever the distance between their ids.
void a_document_cache_keeps_what_a_neighbours_clear_does_not_touch() {
  World world;
  cursiv::DocumentCache<int, int> cache(world.shared);
  constexpr std::uint32_t kOwn = 1;
  for (std::uint32_t other = 0; other < 1024; ++other) {
    if (other == kOwn) continue;
    const auto key = static_cast<int>(other);
    CHECK(cache.get(cursiv::DocumentId{kOwn}, key, [&] {
      cache.clear(cursiv::DocumentId{other});
      return key;
    }) == key);
    CHECK(cache.get(cursiv::DocumentId{kOwn}, key, [] { return -1; }) == key);
  }
}

#if !defined(__wasi__) || defined(_REENTRANT)
// Readers and clearers at once, on far and neighbouring documents: a read
// never answers from before a clear that ended before it began.
void a_document_cache_never_answers_from_before_a_finished_clear() {
  World world;
  cursiv::DocumentCache<int, std::uint64_t> cache(world.shared);
  constexpr std::uint32_t kDocuments = 6;
  const auto id = [](std::uint32_t d) {
    return cursiv::DocumentId{d % 2 == 0 ? d * 64 : d};
  };
  std::array<std::atomic<std::uint64_t>, kDocuments> clears{};
  std::array<std::atomic<std::uint64_t>, kDocuments> finished{};
  const auto raise = [](std::atomic<std::uint64_t>& to, std::uint64_t value) {
    auto at = to.load();
    while (at < value && !to.compare_exchange_weak(at, value)) {
    }
  };
  std::atomic<bool> stop{false};
  std::atomic<int> stale{0};
  std::vector<std::thread> threads;
  for (std::uint32_t t = 0; t < 4; ++t)
    threads.emplace_back([&, t] {
      std::uint32_t d = t;
      int key = 0;
      while (!stop.load()) {
        d = (d + 1) % kDocuments;
        key = (key + 1) % 8;
        const std::uint64_t floor = finished[d].load();
        const std::uint64_t value =
            cache.get(id(d), key, [&] { return clears[d].load(); });
        if (value < floor) ++stale;
      }
    });
  for (std::uint32_t t = 0; t < 2; ++t)
    threads.emplace_back([&, t] {
      std::uint32_t d = t;
      for (std::uint64_t round = 1; !stop.load(); ++round) {
        d = (d + 1) % kDocuments;
        if (round % 16 == 0) {
          std::array<std::uint64_t, kDocuments> due{};
          for (std::uint32_t i = 0; i < kDocuments; ++i) due[i] = ++clears[i];
          cache.clear();
          for (std::uint32_t i = 0; i < kDocuments; ++i)
            raise(finished[i], due[i]);
        } else {
          const std::uint64_t due = ++clears[d];
          cache.clear(id(d));
          raise(finished[d], due);
        }
      }
    });
  std::this_thread::sleep_for(std::chrono::milliseconds(200));
  stop = true;
  for (auto& thread : threads) thread.join();
  CHECK(stale.load() == 0);
}
#endif

}  // namespace

#if !defined(__wasi__) || defined(_REENTRANT)
// Several threads insert the same keys in different orders while the table
// grows: no key is lost or stored twice, and each keeps its first value.
template <class V, class Make>
void a_table_keeps_every_key_through_growth(Make make) {
  constexpr int kThreads = 8;
  constexpr int kKeys = 20000;
  cursiv::detail::ConcurrentTable<int, V> table;
  std::atomic<int> wrong{0};
  std::vector<std::thread> threads;
  for (int t = 0; t < kThreads; ++t)
    threads.emplace_back([&, t] {
      for (int k = 0; k < kKeys; ++k) {
        const int key = (k * 7919 + t * 104729) % kKeys;
        V value = make(key);
        const std::optional<V> kept =
            table.insert(table.generation(), key, value);
        if (!kept || !(*kept == make(key))) ++wrong;
      }
    });
  for (auto& thread : threads) thread.join();
  CHECK(wrong.load() == 0);
  std::size_t stored = 0;
  table.forEach([&](const V&) { ++stored; });
  CHECK(stored == kKeys);
  int found = 0;
  for (int k = 0; k < kKeys; ++k)
    if (const std::optional<V> v = table.find(k); v && *v == make(k)) ++found;
  CHECK(found == kKeys);
}
#endif

// Copying a value into its slot picks the copy constructor, never one that
// takes a non-const reference and may throw: a reserved slot is always
// published, so a second insert of the key does not wait forever.
struct PickyValue {
  int payload = 0;
  PickyValue() = default;
  explicit PickyValue(int p) : payload(p) {}
  PickyValue(const PickyValue&) = default;
  PickyValue(PickyValue&&) = default;
  PickyValue& operator=(const PickyValue&) = default;
  PickyValue& operator=(PickyValue&&) = default;
  template <class T>
    requires std::is_same_v<T, PickyValue>
  PickyValue(T&) {
    throw std::runtime_error("picked");
  }
};

void a_slot_is_filled_by_the_copy_constructor() {
  cursiv::detail::ConcurrentTable<int, PickyValue> table;
  PickyValue first(1);
  PickyValue second(2);
  CHECK(table.insert(table.generation(), 7, first)->payload == 1);
  CHECK(table.insert(table.generation(), 7, second)->payload == 1);
}

// Nor one that takes the value as a temporary: the slot's members are built
// from the arguments themselves, not moved in from a pair built first.
struct MovedValue {
  int payload = 0;
  explicit MovedValue(int p) : payload(p) {}
  MovedValue(const MovedValue&) = default;
  MovedValue& operator=(const MovedValue&) = default;
  template <class U>
    requires std::is_same_v<std::remove_cvref_t<U>, MovedValue>
  MovedValue(U&&) {
    throw std::runtime_error("moved");
  }
};

void a_slot_is_not_filled_from_a_temporary() {
  cursiv::detail::ConcurrentTable<int, MovedValue> table;
  MovedValue first(1);
  MovedValue second(2);
  CHECK(table.insert(table.generation(), 7, first)->payload == 1);
  CHECK(table.insert(table.generation(), 7, second)->payload == 1);
}

// Keys of one hash fill one run of groups, and doubling would not part
// them: the table probes on instead of growing without end.
struct OneHash {
  std::size_t operator()(int) const noexcept { return 42; }
};

void keys_of_one_hash_still_fit() {
  cursiv::detail::ConcurrentTable<int, int, OneHash> table;
  constexpr int kKeys = 2000;
  for (int k = 0; k < kKeys; ++k) {
    int value = k * 2;
    CHECK(*table.insert(table.generation(), k, value) == k * 2);
  }
  int found = 0;
  for (int k = 0; k < kKeys; ++k)
    if (const std::optional<int> v = table.find(k); v && *v == k * 2) ++found;
  CHECK(found == kKeys);
}

// Counts the values alive, so a test sees when a table's are freed.
struct Live {
  static inline int count = 0;
  Live() { ++count; }
  Live(const Live&) { ++count; }
  Live& operator=(const Live&) = default;
  ~Live() { --count; }
};

// A slot's tag spares a lookup the keys it cannot hold: about one key
// compared per hit and almost none per miss, on a 32-bit target as well,
// where the hash a standard `std::hash` gives is half as wide.
struct CountingEqual {
  static inline std::size_t compared = 0;
  bool operator()(std::uintptr_t a, std::uintptr_t b) const noexcept {
    ++compared;
    return a == b;
  }
};

void a_lookup_compares_about_one_key() {
  constexpr std::size_t kKeys = 4096;
  const auto key = [](std::size_t k) -> std::uintptr_t {
    return 0x10000 + 48 * k;
  };
  cursiv::detail::ConcurrentTable<std::uintptr_t, int,
                                  std::hash<std::uintptr_t>, CountingEqual>
      table;
  for (std::size_t k = 0; k < kKeys; ++k) {
    int value = static_cast<int>(k);
    CHECK(table.insert(table.generation(), key(k), value).has_value());
  }
  CountingEqual::compared = 0;
  for (std::size_t k = 0; k < kKeys; ++k) CHECK(table.find(key(k)).has_value());
  CHECK(CountingEqual::compared < kKeys * 11 / 10);
  CountingEqual::compared = 0;
  for (std::size_t k = 0; k < kKeys; ++k)
    CHECK(!table.find(key(k) + 8).has_value());
  CHECK(CountingEqual::compared < kKeys / 10);
}

// A reader that stays holds back only what was released after it came: what
// was released before is freed meanwhile, and the rest once it leaves. A
// `forEach` still running is such a reader.
void a_guard_holds_back_only_what_follows_it() {
  support::collect_pending_tables();
  cursiv::detail::ConcurrentTable<int, Live> before;
  cursiv::detail::ConcurrentTable<int, Live> after;
  const auto fill = [](cursiv::detail::ConcurrentTable<int, Live>& table) {
    Live value;
    CHECK(table.insert(table.generation(), 1, value).has_value());
  };
  fill(before);
  fill(after);
  CHECK(Live::count == 2);
  cursiv::detail::ConcurrentTable<int, int> reader;
  int one = 1;
  CHECK(reader.insert(reader.generation(), 1, one).has_value());
  before.clear();
  reader.forEach([&](int) {
    after.clear();
    support::collect_pending_tables();
    CHECK(Live::count == 1);
  });
  support::collect_pending_tables();
  CHECK(Live::count == 0);
}

// A release that holds a lot is collected soon: a dozen large tables
// bring a collection that frees the small one released before them.
void a_large_pending_free_is_collected_soon() {
  support::collect_pending_tables();
  cursiv::detail::ConcurrentTable<int, Live> small;
  {
    Live value;
    CHECK(small.insert(small.generation(), 1, value).has_value());
  }
  small.clear();
  CHECK(Live::count == 1);
  support::collect_pending_tables();
  CHECK(Live::count == 0);
}

// A rare release does not wait for dozens more: the first release
// after a short pause collects.
void a_rare_pending_free_is_collected_after_a_pause() {
  support::collect_pending_tables();
  cursiv::SimpleCache<std::shared_ptr<int>> cache;
  std::weak_ptr<int> dropped;
  (void)cache.get([&] {
    auto value = std::make_shared<int>(1);
    dropped = value;
    return value;
  });
  cache.clear();
  CHECK(!dropped.expired());
  wait_past_the_collection_delay();
  cursiv::SimpleCache<int> other;
  other.clear();
  CHECK(dropped.expired());
}

#if !defined(__wasi__) || defined(_REENTRANT)
// A clear that comes while a growth is under way takes out the table the
// growth started from, and the successor it never published goes with it.
// The sanitizers' leak check reads this one.
void a_clear_during_a_growth_frees_its_successor() {
  cursiv::detail::ConcurrentTable<int, int> table;
  std::atomic<bool> done{false};
  std::thread clearer([&] {
    while (!done.load(std::memory_order_relaxed)) table.clear();
  });
  for (int round = 0; round < 2000; ++round)
    for (int k = 0; k < 64; ++k) {
      int value = k;
      (void)table.insert(table.generation(), k, value);
    }
  done = true;
  clearer.join();
  support::collect_pending_tables();
}
#endif

#if !defined(__wasi__) || defined(_REENTRANT)
// A thread gives its slot back as it ends, even when a thread-local object
// destroyed after that still reads through a guard: more threads than slots,
// one after the other, would otherwise run out.
struct ReadsAtExit {
  cursiv::detail::ConcurrentTable<int, int>* table = nullptr;
  ~ReadsAtExit() {
    if (table != nullptr) (void)table->find(1);
  }
};

void a_thread_gives_its_slot_back_even_from_a_late_guard() {
  cursiv::detail::ConcurrentTable<int, int> table;
  int value = 1;
  (void)table.insert(table.generation(), 1, value);
  for (int i = 0; i < 2000; ++i)
    std::thread([&] {
      static thread_local ReadsAtExit late;
      late.table = &table;
      CHECK(table.find(1).has_value());
    }).join();
}
#endif

#if !defined(__wasi__)
// More threads alive than the process has slots: each still reads without
// waiting for another to end. Those that find no slot hold back every
// collection while they read, so what they read stays alive until they leave.
void more_threads_than_slots_read_at_once() {
  constexpr int kHolders = 1024;  // one per slot: none is left after them
  constexpr int kLate = 4;
  support::collect_pending_tables();
  cursiv::detail::ConcurrentTable<int, int> table;
  int one = 1;
  (void)table.insert(table.generation(), 1, one);
  cursiv::detail::ConcurrentTable<int, Live> held;
  {
    Live value;
    CHECK(held.insert(held.generation(), 1, value).has_value());
  }
  std::atomic<int> read{0};
  std::atomic<int> wrong{0};
  std::atomic<int> inside{0};
  std::atomic<bool> cleared{false};
  std::atomic<bool> released{false};
  const auto until = [](const auto& done) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!done() && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return done();
  };
  std::vector<std::thread> threads;
  threads.reserve(kHolders + kLate);
  for (int t = 0; t < kHolders; ++t)
    threads.emplace_back([&] {
      if (table.find(1) != 1) ++wrong;
      ++read;
      while (!released.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
  const bool allRead = until([&] { return read.load() == kHolders; });
  CHECK(allRead);
  if (allRead)
    for (int t = 0; t < kLate; ++t)
      threads.emplace_back([&] {
        held.forEach([&](const Live&) {
          ++inside;
          while (!cleared.load()) std::this_thread::yield();
        });
      });
  const bool allInside =
      allRead && until([&] { return inside.load() == kLate; });
  CHECK(allInside);
  if (allInside) {
    held.clear();
    support::collect_pending_tables();
    CHECK(Live::count == 1);
  }
  cleared = true;
  released = true;
  for (auto& thread : threads) thread.join();
  CHECK(wrong.load() == 0);
  support::collect_pending_tables();
  CHECK(Live::count == 0);
}
#endif

void run() {
  a_hit_never_recomputes();
  filling_the_cache_is_linear();
  providers_can_reenter_the_cache();
#if !defined(__wasi__) || defined(_REENTRANT)
  concurrent_providers_do_not_wait_on_each_other();
  an_evicted_provider_does_not_replace_the_new_generation();
#endif
  a_failed_provider_can_be_retried();
  a_value_cache_keeps_null_and_borrows_without_copying();
  value_providers_and_consumers_can_reenter();
  a_cleared_value_can_reenter_the_cache_when_destroyed();
  a_cleared_value_can_read_a_growing_document_cache();
  a_stale_answer_holds_back_no_collection();
  a_workspace_value_uses_the_same_invalidation_lifecycle();
  single_value_caches_do_not_require_hashable_values();
  a_document_value_is_partitioned_and_can_be_read_without_computing();
  document_values_follow_updates_states_and_deletions();
#if !defined(__wasi__) || defined(_REENTRANT)
  document_value_providers_are_invalidated_only_by_their_partition();
  value_providers_do_not_wait_and_clear_rejects_stale_publication();
  a_value_consumer_keeps_its_answer_alive_during_clear();
#endif
  a_failed_subscription_leaves_no_listener();
  a_stateless_workspace_cache_empties_on_any_update();
  a_staged_workspace_cache_empties_at_its_state();
  a_document_cache_evicts_per_document();
  a_document_cache_hits_per_document();
  a_document_cache_clears_with_any_hash();
  a_document_cache_keeps_out_what_a_clear_overtakes();
  a_document_cache_keeps_out_what_a_deletion_overtakes();
  a_document_cache_keeps_what_a_neighbours_clear_does_not_touch();
#if !defined(__wasi__) || defined(_REENTRANT)
  a_document_cache_never_answers_from_before_a_finished_clear();
  a_table_keeps_every_key_through_growth<int>([](int k) { return k * 3; });
  a_table_keeps_every_key_through_growth<std::string>(
      [](int k) { return std::to_string(k); });
  a_thread_gives_its_slot_back_even_from_a_late_guard();
  a_clear_during_a_growth_frees_its_successor();
#endif
#if !defined(__wasi__)
  more_threads_than_slots_read_at_once();
#endif
  a_slot_is_filled_by_the_copy_constructor();
  a_slot_is_not_filled_from_a_temporary();
  keys_of_one_hash_still_fit();
  a_lookup_compares_about_one_key();
  a_guard_holds_back_only_what_follows_it();
  a_large_pending_free_is_collected_soon();
  a_rare_pending_free_is_collected_after_a_pause();
}

}  // namespace caching

namespace round_marks {

void run() {
  cursiv::RoundMarks marks(2);
  // Past its first size, and each id once a round.
  CHECK(marks.first(0) && !marks.first(0) && marks.first(9) && !marks.first(9));
  marks.next();
  CHECK(marks.first(0) && marks.first(9) && !marks.first(0));
}

}  // namespace round_marks

int main() {
  round_marks::run();
  case_fold::run();
  function_ref::run();
  uri_trie::run();
  uri_utils::run();
  caching::run();
  return cursiv::test::report("utils");
}
