#include "cursiv/core/utils/UriUtils.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/core/utils/ctstring.hpp"

namespace cursiv {

namespace {

bool has_file_scheme(std::string_view uri) {
  if (uri.size() < 5) return false;
  for (std::size_t i = 0; i < 5; ++i)
    if (to_lower(uri[i]) != "file:"[i]) return false;
  return true;
}

// RFC 3986 unreserved plus `/`: what vscode-uri leaves bare in a path, so a
// drive letter's `:` travels as `%3A`. A fixed ASCII table, never `isalnum`:
// an active 8-bit locale would otherwise call a UTF-8 path byte alphanumeric
// and leave it unescaped.
inline constexpr CharSet kUnreserved = [] {
  CharSet s;
  for (int c = 'a'; c <= 'z'; ++c) s.set(static_cast<unsigned char>(c));
  for (int c = 'A'; c <= 'Z'; ++c) s.set(static_cast<unsigned char>(c));
  for (int c = '0'; c <= '9'; ++c) s.set(static_cast<unsigned char>(c));
  for (const char c : std::string_view("-._~/"))
    s.set(static_cast<unsigned char>(c));
  return s;
}();

#ifdef _WIN32
inline constexpr CharSet kDriveLetter = [] {
  CharSet s;
  for (int c = 'a'; c <= 'z'; ++c) s.set(static_cast<unsigned char>(c));
  for (int c = 'A'; c <= 'Z'; ++c) s.set(static_cast<unsigned char>(c));
  return s;
}();
#endif

char to_hex(unsigned char value) noexcept {
  return static_cast<char>(value < 10 ? '0' + value : 'A' + (value - 10));
}

std::string percent_encode(std::string_view text) {
  if (std::ranges::all_of(text, [](char c) {
        return kUnreserved.test(static_cast<unsigned char>(c));
      }))
    return std::string(text);

  std::string encoded;
  encoded.reserve(text.size());
  for (const char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    if (kUnreserved.test(c)) {
      encoded.push_back(raw);
      continue;
    }
    encoded.push_back('%');
    encoded.push_back(to_hex(static_cast<unsigned char>((c >> 4U) & 0x0FU)));
    encoded.push_back(to_hex(static_cast<unsigned char>(c & 0x0FU)));
  }
  return encoded;
}

int from_hex(char c) noexcept {
  const char l = to_lower(c);
  if (l >= '0' && l <= '9') return l - '0';
  if (l >= 'a' && l <= 'f') return 10 + (l - 'a');
  return -1;
}

std::optional<unsigned char> decode_percent_escape(std::string_view text,
                                                   std::size_t index) {
  if (text[index] != '%' || index + 2 >= text.size()) return std::nullopt;
  const int high = from_hex(text[index + 1]);
  const int low = from_hex(text[index + 2]);
  if (high < 0 || low < 0) return std::nullopt;
  return static_cast<unsigned char>((static_cast<unsigned int>(high) << 4U) |
                                    static_cast<unsigned int>(low));
}

std::string percent_decode(std::string_view text) {
  std::string decoded;
  decoded.reserve(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (const auto byte = decode_percent_escape(text, i); byte.has_value()) {
      decoded.push_back(static_cast<char>(*byte));
      i += 2;
      continue;
    }
    decoded.push_back(text[i]);
  }
  return decoded;
}

// Scheme, authority and path of a URI; query and fragment are not a place.
struct UriParts {
  std::string_view scheme;
  std::string_view authority;
  std::string_view path;
};

UriParts split_uri(std::string_view uri) {
  uri = uri.substr(0, uri.find_first_of("?#"));
  UriParts parts;
  const std::size_t colon = uri.find(':');
  if (colon != 0 && colon < uri.find('/')) {
    parts.scheme = uri.substr(0, colon);
    uri.remove_prefix(colon + 1);
  }
  if (uri.starts_with("//")) {
    uri.remove_prefix(2);
    const std::size_t slash = uri.find('/');
    parts.authority = uri.substr(0, slash);
    uri = slash == std::string_view::npos ? std::string_view{}
                                          : uri.substr(slash);
  }
  parts.path = uri;
  return parts;
}

// Both normalized: the scheme and the authority are already in lower case.
bool same_place(const UriParts& a, const UriParts& b) {
  return a.scheme == b.scheme && a.authority == b.authority;
}

// The scheme and the authority in lower case, the userinfo apart: RFC 3986
// makes both case-insensitive, and gives the scheme lower case as its
// canonical form.
std::string with_lower_scheme_and_authority(std::string_view uri) {
  std::string out(uri);
  const std::size_t scheme = split_uri(uri).scheme.size();
  for (std::size_t i = 0; i < scheme; ++i) out[i] = to_lower(out[i]);
  const std::size_t colon = out.find(':');
  if (colon == std::string::npos || !uri.substr(colon + 1).starts_with("//"))
    return out;
  const std::size_t begin = colon + 3;
  const std::size_t end = std::min(out.find_first_of("/?#", begin), out.size());
  const std::size_t at = uri.substr(begin, end - begin).rfind('@');
  for (std::size_t i = at == std::string_view::npos ? begin : begin + at + 1;
       i < end; ++i)
    out[i] = to_lower(out[i]);
  return out;
}

std::string_view without_trailing_slash(std::string_view path) {
  if (path.ends_with('/')) path.remove_suffix(1);
  return path;
}

std::vector<std::string_view> path_segments(std::string_view path) {
  std::vector<std::string_view> segments;
  for (const auto segment : std::views::split(path, '/'))
    if (!segment.empty())
      segments.emplace_back(std::ranges::data(segment), segment.size());
  return segments;
}

}  // namespace

std::string normalize_uri(std::string_view uri) {
  if (const auto path = file_uri_to_path(uri); path.has_value()) {
    std::string out = path_to_file_uri(*path);
    // A Windows drive letter is a syntactic fact of the URI, not of the host:
    // the same document arrives as `/c:/`, `/C:/` or `/c%3A/` and must land
    // on one spelling whatever platform the server runs on.
    constexpr std::size_t at = 8;  // past "file:///"
    if (out.size() > at + 3 && out.compare(at + 1, 3, "%3A") == 0)
      out[at] = to_lower(out[at]);
    if (const std::size_t suffix = uri.find_first_of("?#");
        suffix != std::string_view::npos)
      out.append(uri.substr(suffix));
    return out;
  }
  return with_lower_scheme_and_authority(uri);
}

bool is_file_uri(std::string_view uri) { return has_file_scheme(uri); }

std::optional<std::string> file_uri_to_path(std::string_view uri) {
  if (!has_file_scheme(uri)) return std::nullopt;
  // URI components are separated before decoding: an escaped '?' or '#'
  // belongs to the file name, a literal one starts a query or fragment.
  uri = uri.substr(0, uri.find_first_of("?#"));
  // An authority with no path names no file, which is the rule the localhost
  // test below already applies; an empty path would reach `absolute("")`,
  // whose failure is a throw.

  // `file:/path` has no authority; `file://authority/path` has one.
  const std::string_view rest = uri.substr(5);
  std::string_view path = rest;
  if (rest.starts_with("//")) {
    const std::string_view remainder = rest.substr(2);
    path = remainder;
    // An authority is allowed only if it is `localhost`: any other host names a
    // file this process cannot open, so it is not a path at all.
    if (!remainder.empty() && remainder.front() != '/') {
      const auto slash = remainder.find('/');
      const std::string_view authority = slash == std::string_view::npos
                                             ? remainder
                                             : remainder.substr(0, slash);
      if (!authority.empty() &&
          !std::ranges::equal(authority, std::string_view("localhost"), {},
                              [](char c) { return to_lower(c); }))
        return std::nullopt;
      path = slash == std::string_view::npos ? std::string_view{}
                                             : remainder.substr(slash);
    }
  } else if (!path.starts_with('/')) {
    return std::nullopt;
  }

  std::string decoded = percent_decode(path);
  // An escaped NUL would end the path where the system call stops reading
  // and open another file than the URI names.
  if (decoded.empty() || decoded.find('\0') != std::string::npos)
    return std::nullopt;
#ifdef _WIN32
  if (decoded.size() >= 3 && decoded[0] == '/' &&
      kDriveLetter.test(static_cast<unsigned char>(decoded[1])) &&
      decoded[2] == ':')
    decoded.erase(decoded.begin());
  std::ranges::replace(decoded, '/', '\\');
#endif
  return decoded;
}

std::string path_to_file_uri(std::string_view path) {
  if (path.empty()) throw std::invalid_argument("an empty path names no file");
  auto normalized = std::filesystem::absolute(std::filesystem::path(path))
                        .lexically_normal()
                        .generic_string();
#ifdef _WIN32
  if (normalized.size() >= 2 &&
      kDriveLetter.test(static_cast<unsigned char>(normalized[0])) &&
      normalized[1] == ':')
    normalized.insert(normalized.begin(), '/');
#endif
  if (!normalized.starts_with('/')) normalized.insert(normalized.begin(), '/');
  return "file://" + percent_encode(normalized);
}

std::string normalize_extension(std::string_view extension) {
  if (extension.empty()) return {};
  std::string normalized(extension);
  if (!normalized.starts_with('.')) normalized.insert(normalized.begin(), '.');
  for (char& c : normalized) c = to_lower(c);
  return normalized;
}

std::string uri_file_name(std::string_view uri) {
  std::string path = percent_decode(split_uri(uri).path);
  while (path.ends_with('/')) path.pop_back();
  const std::size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string uri_extension(std::string_view uri) {
  const std::string name = uri_file_name(uri);
  const std::size_t dot = name.find_last_of('.');
  if (dot == std::string::npos || dot == 0 || name == "..") return {};
  // Keep the registry's documented case-insensitive extension convention.
  std::string ext = name.substr(dot);
  for (char& c : ext) c = to_lower(c);
  return ext;
}

bool equals_uri(std::string_view left, std::string_view right) {
  return normalize_uri(left) == normalize_uri(right);
}

bool contains_uri(std::string_view parent, std::string_view child) {
  const std::string normalizedParent = normalize_uri(parent);
  const std::string normalizedChild = normalize_uri(child);
  const UriParts p = split_uri(normalizedParent);
  const UriParts c = split_uri(normalizedChild);
  if (!same_place(p, c)) return false;
  const std::string_view parentPath = without_trailing_slash(p.path);
  const std::string_view childPath = without_trailing_slash(c.path);
  if (childPath == parentPath) return true;
  // Segment by segment, so `/a/bc` is not read as living under `/a/b`.
  return childPath.size() > parentPath.size() &&
         childPath.starts_with(parentPath) &&
         childPath[parentPath.size()] == '/';
}

std::string relative_uri(std::string_view from, std::string_view to) {
  const std::string normalizedFrom = normalize_uri(from);
  std::string normalizedTo = normalize_uri(to);
  const UriParts f = split_uri(normalizedFrom);
  const UriParts t = split_uri(normalizedTo);
  if (!same_place(f, t)) return normalizedTo;

  const std::vector<std::string_view> fromParts = path_segments(f.path);
  const std::vector<std::string_view> toParts = path_segments(t.path);
  std::size_t common = 0;
  while (common < fromParts.size() && common < toParts.size() &&
         fromParts[common] == toParts[common])
    ++common;
  std::string relative;
  for (std::size_t i = common; i < fromParts.size(); ++i) relative += "../";
  for (std::size_t i = common; i < toParts.size(); ++i) {
    if (i > common) relative += '/';
    relative += toParts[i];
  }
  return relative;
}

}  // namespace cursiv
