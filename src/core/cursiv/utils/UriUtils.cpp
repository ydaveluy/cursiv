#include "cursiv/utils/UriUtils.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cursiv/utils/ctstring.hpp"

namespace cursiv {

namespace {

// ASCII case folding: the scheme, the host and a drive letter are folded, and
// a byte past ASCII is left as it is.
constexpr auto kLowerTable = [] {
  std::array<char, 256> t{};
  for (int i = 0; i < 256; ++i)
    t[static_cast<std::size_t>(i)] = (i >= 'A' && i <= 'Z')
                                         ? static_cast<char>(i - 'A' + 'a')
                                         : static_cast<char>(i);
  return t;
}();

constexpr char to_lower(char c) {
  return kLowerTable[static_cast<unsigned char>(c)];
}

bool has_file_scheme(std::string_view uri) {
  if (uri.size() < 5) return false;
  for (std::size_t i = 0; i < 5; ++i)
    if (to_lower(uri[i]) != "file:"[i]) return false;
  return true;
}

// Whether the host writes its paths with drive letters and `\`. A WASI module
// never does, whatever it runs on.
inline constexpr bool kWindowsPaths =
    std::filesystem::path::preferred_separator == '\\';

// RFC 3986 unreserved, all that stays bare in a component: a drive letter's `:`
// travels as `%3A`. A fixed ASCII table, never `isalnum`: an active 8-bit locale
// would otherwise call a UTF-8 path byte alphanumeric and leave it unescaped.
inline constexpr CharSet kUnreserved = [] {
  CharSet s;
  for (int c = 'a'; c <= 'z'; ++c) s.set(static_cast<unsigned char>(c));
  for (int c = 'A'; c <= 'Z'; ++c) s.set(static_cast<unsigned char>(c));
  for (int c = '0'; c <= '9'; ++c) s.set(static_cast<unsigned char>(c));
  for (const char c : std::string_view("-._~"))
    s.set(static_cast<unsigned char>(c));
  return s;
}();

// A path keeps its separators, and an IP literal its brackets and colons.
inline constexpr CharSet kPathBare = [] {
  CharSet s = kUnreserved;
  s.set(static_cast<unsigned char>('/'));
  return s;
}();
inline constexpr CharSet kAuthorityBare = [] {
  CharSet s = kUnreserved;
  for (const char c : std::string_view("[]:"))
    s.set(static_cast<unsigned char>(c));
  return s;
}();

bool is_ascii_letter(char c) noexcept {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char to_hex(unsigned char value) noexcept {
  return static_cast<char>(value < 10 ? '0' + value : 'A' + (value - 10));
}

std::string percent_encode(std::string_view text, const CharSet& bare) {
  if (std::ranges::all_of(text, [&](char c) {
        return bare.test(static_cast<unsigned char>(c));
      }))
    return std::string(text);

  std::string encoded;
  encoded.reserve(text.size());
  for (const char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    if (bare.test(c)) {
      encoded.push_back(raw);
      continue;
    }
    encoded.push_back('%');
    encoded.push_back(to_hex(static_cast<unsigned char>((c >> 4U) & 0x0FU)));
    encoded.push_back(to_hex(static_cast<unsigned char>(c & 0x0FU)));
  }
  return encoded;
}

char to_upper_hex(char c) noexcept {
  return c >= 'a' && c <= 'f' ? static_cast<char>(c - 'a' + 'A') : c;
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

// The components of a URI, as written and in that order.
struct UriParts {
  std::string_view scheme;
  std::string_view authority;
  std::string_view path;
  std::string_view query;
  std::string_view fragment;
  bool slashes = false;
  bool marked = false;
  bool hashed = false;
};

UriParts split_uri(std::string_view uri) {
  UriParts parts;
  if (const std::size_t hash = uri.find('#'); hash != std::string_view::npos) {
    parts.fragment = uri.substr(hash + 1);
    parts.hashed = true;
    uri = uri.substr(0, hash);
  }
  if (const std::size_t mark = uri.find('?'); mark != std::string_view::npos) {
    parts.query = uri.substr(mark + 1);
    parts.marked = true;
    uri = uri.substr(0, mark);
  }
  if (const std::size_t colon = uri.find(':');
      colon != 0 && colon < uri.find('/')) {
    parts.scheme = uri.substr(0, colon);
    uri.remove_prefix(colon + 1);
  }
  if (uri.starts_with("//")) {
    parts.slashes = true;
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

std::string ascii_lower(std::string_view text) {
  std::string out(text);
  for (char& c : out) c = to_lower(c);
  return out;
}

// A drive letter is a syntactic fact of the URI, not of the host: the same
// document arrives as `/c:/`, `/C:/` or `/c%3A/` and must map to one spelling
// whatever platform the server runs on.
void lower_drive_letter(std::string& path) {
  if (path.size() >= 3 && path[0] == '/' && is_ascii_letter(path[1]) &&
      path[2] == ':')
    path[1] = to_lower(path[1]);
  else if (path.size() >= 2 && is_ascii_letter(path[0]) && path[1] == ':')
    path[0] = to_lower(path[0]);
}

// `authority` split into userinfo, host and port, each read by `read`: the
// host goes to lower case, the rest stays as written. The split comes before
// decoding, so an escaped '@' or ':' stays in its part.
template <class Read>
std::string format_authority(std::string_view authority, Read read) {
  std::string out;
  if (const std::size_t at = authority.find('@');
      at != std::string_view::npos) {
    const std::string_view userinfo = authority.substr(0, at);
    const std::size_t colon = userinfo.find(':');
    out += percent_encode(read(userinfo.substr(0, colon)), kUnreserved);
    if (colon != std::string_view::npos) {
      out += ':';
      out += percent_encode(read(userinfo.substr(colon + 1)), kUnreserved);
    }
    out += '@';
    authority.remove_prefix(at + 1);
  }
  std::string_view port;
  const std::size_t colon = authority.rfind(':');
  if (const std::size_t close = authority.rfind(']');
      colon != std::string_view::npos &&
      (close == std::string_view::npos || close < colon)) {
    port = authority.substr(colon);
    authority = authority.substr(0, colon);
  }
  const bool literal = authority.starts_with('[') && authority.ends_with(']');
  out += percent_encode(ascii_lower(read(authority)),
                        literal ? kAuthorityBare : kUnreserved);
  out += port;
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

std::string rooted_path_to_uri(const std::filesystem::path& path) {
  auto normalized = path.lexically_normal().generic_string();
  if (!normalized.starts_with('/')) normalized.insert(normalized.begin(), '/');
  lower_drive_letter(normalized);
  return "file://" + percent_encode(normalized, kPathBare);
}

// Whether `text` is one component as a URI writes it, escapes in upper case and
// only where `bare` asks for them.
bool is_encoded(std::string_view text, const CharSet& bare) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (bare.test(static_cast<unsigned char>(text[i]))) continue;
    if (text[i] != '%' || i + 2 >= text.size()) return false;
    const int high = from_hex(text[i + 1]);
    if (const int low = from_hex(text[i + 2]);
        high < 0 || low < 0 || to_upper_hex(text[i + 1]) != text[i + 1] ||
        to_upper_hex(text[i + 2]) != text[i + 2] ||
        bare.test(static_cast<unsigned char>(high * 16 + low)))
      return false;
    i += 2;
  }
  return true;
}

// Whether `normalize_uri` writes `uri` back as it stands. Nearly every URI comes
// from it, and telling so allocates nothing. It may say no to a URI that would
// be written back: a host beyond plain letters goes the long way.
bool is_normal_uri(const UriParts& parts) {
  const auto lower = [](char c) { return to_lower(c) == c; };
  if (!std::ranges::all_of(parts.scheme, lower) ||
      !std::ranges::all_of(parts.authority, lower) ||
      !std::ranges::all_of(parts.authority, [](char c) {
        return kUnreserved.test(static_cast<unsigned char>(c));
      }))
    return false;
  const bool file = parts.scheme == "file";
  if (parts.slashes != (file || !parts.authority.empty())) return false;
  if ((file || parts.scheme == "http" || parts.scheme == "https") &&
      !parts.path.starts_with('/'))
    return false;
  // The letter before an escaped colon, which lowers.
  if (const std::string_view drive =
          parts.path.substr(parts.path.starts_with('/') ? 1 : 0, 4);
      drive.size() == 4 && drive.ends_with("%3A") &&
      is_ascii_letter(drive[0]) && !lower(drive[0]))
    return false;
  if (parts.marked && parts.query.empty()) return false;
  if (parts.hashed && parts.fragment.empty()) return false;
  return is_encoded(parts.path, kPathBare) &&
         is_encoded(parts.query, kUnreserved) &&
         is_encoded(parts.fragment, kUnreserved);
}

// A URI in its one spelling, copied only where it is not in it already.
class NormalUri {
 public:
  explicit NormalUri(std::string_view uri) {
    const UriParts parts = split_uri(uri);
    if (parts.scheme.empty() || is_normal_uri(parts)) {
      _view = uri;
    } else {
      _owned = normalize_uri(uri);
      _view = _owned;
    }
  }
  NormalUri(const NormalUri&) = delete;
  NormalUri& operator=(const NormalUri&) = delete;

  [[nodiscard]] std::string_view view() const { return _view; }

 private:
  std::string _owned;
  std::string_view _view;
};

}  // namespace

std::string normalize_uri(std::string_view uri) {
  const UriParts parts = split_uri(uri);
  // A bare name is not a URI, and nothing here invents its scheme.
  if (parts.scheme.empty() || is_normal_uri(parts)) return std::string(uri);
  // Each component decoded, then written back one way: the spelling depends on
  // the text alone, never on the host.
  const std::string scheme = ascii_lower(parts.scheme);
  std::string out = scheme + ':';
  const std::string authority = format_authority(
      parts.authority,
      [](std::string_view part) { return percent_decode(part); });
  if (!authority.empty() || scheme == "file") out += "//";
  out += authority;
  std::string path = percent_decode(parts.path);
  // These schemes have no relative form: their paths are rooted.
  if ((scheme == "file" || scheme == "http" || scheme == "https") &&
      !path.starts_with('/'))
    path.insert(path.begin(), '/');
  lower_drive_letter(path);
  out += percent_encode(path, kPathBare);
  if (!parts.query.empty())
    out += '?' + percent_encode(percent_decode(parts.query), kUnreserved);
  if (!parts.fragment.empty())
    out += '#' + percent_encode(percent_decode(parts.fragment), kUnreserved);
  return out;
}

bool is_file_uri(std::string_view uri) { return has_file_scheme(uri); }

std::optional<std::string> file_uri_to_path(std::string_view uri) {
  if (!has_file_scheme(uri)) return std::nullopt;
  // URI components are separated before decoding: an escaped '?' or '#'
  // belongs to the file name, a literal one starts a query or fragment.
  const UriParts parts = split_uri(uri);
  std::string decoded = percent_decode(parts.path);
  // A host with no path names no file.
  if (decoded.empty() && !parts.authority.empty()) return std::nullopt;
  if (!decoded.starts_with('/')) decoded.insert(decoded.begin(), '/');
  // An escaped NUL would end the path where the system call stops reading and
  // open another file than the URI names.
  if (decoded.find('\0') != std::string::npos) return std::nullopt;
  // `localhost` names this machine. On Windows, before a share rather than a
  // drive, it is the share's host.
  const bool drive =
      decoded.size() >= 3 && is_ascii_letter(decoded[1]) && decoded[2] == ':';
  const bool local =
      parts.authority.empty() ||
      (std::ranges::equal(parts.authority, std::string_view("localhost"), {},
                          [](char c) { return to_lower(c); }) &&
       (!kWindowsPaths || drive));
  if (local) lower_drive_letter(decoded);
  // Elsewhere a drive keeps its root, where a WASI runner preopens it, and any
  // other host names a file this process cannot open.
  if (!kWindowsPaths) return local ? std::optional(std::move(decoded)) : std::nullopt;
  if (local) {
    if (drive) decoded.erase(decoded.begin());
  } else {
    // A share: the host is the path's first component.
    if (decoded == "/") return std::nullopt;
    decoded.insert(0, "//" + percent_decode(parts.authority));
  }
  std::ranges::replace(decoded, '/', '\\');
  return decoded;
}

std::string path_to_file_uri(std::string_view path) {
  if (path.empty()) throw std::invalid_argument("an empty path names no file");
  // A share: the host is the URI's authority. Read here, since a standard path
  // may fold the two separators that say it is one.
  const auto separator = [](char c) { return c == '\\' || c == '/'; };
  if (kWindowsPaths && path.size() > 2 && separator(path[0]) &&
      separator(path[1])) {
    const std::size_t end = path.find_first_of("\\/", 2);
    const std::string_view host = path.substr(2, end - 2);
    const std::string rest =
        end == std::string_view::npos
            ? "/"
            : std::filesystem::path(path.substr(end))
                  .lexically_normal()
                  .generic_string();
    const auto plain = [](std::string_view part) { return std::string(part); };
    return "file://" + format_authority(host, plain) +
           percent_encode(rest, kPathBare);
  }
  return rooted_path_to_uri(
      std::filesystem::absolute(std::filesystem::path(path)));
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
  // Extensions are case-insensitive, as the registry expects.
  std::string ext = name.substr(dot);
  for (char& c : ext) c = to_lower(c);
  return ext;
}

bool equals_uri(std::string_view left, std::string_view right) {
  return left == right ||
         NormalUri(left).view() == NormalUri(right).view();
}

bool contains_uri(std::string_view parent, std::string_view child) {
  const NormalUri normalizedParent(parent);
  const NormalUri normalizedChild(child);
  const UriParts p = split_uri(normalizedParent.view());
  const UriParts c = split_uri(normalizedChild.view());
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

  // On the decoded paths: what comes back is a path, not a part of a URI.
  const std::string fromPath = percent_decode(f.path);
  const std::string toPath = percent_decode(t.path);
  const std::vector<std::string_view> fromParts = path_segments(fromPath);
  const std::vector<std::string_view> toParts = path_segments(toPath);
  // No way leads from one drive to another: the path stands whole.
  const auto is_drive = [](std::string_view part) {
    return part.size() == 2 && is_ascii_letter(part[0]) && part[1] == ':';
  };
  if (!fromParts.empty() && !toParts.empty() && is_drive(fromParts[0]) &&
      is_drive(toParts[0]) && fromParts[0] != toParts[0])
    return toPath.substr(1);
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
