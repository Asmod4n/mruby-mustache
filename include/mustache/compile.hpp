#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace mustache {

enum class Tag : uint32_t { text, var, raw, section, inverted, partial, block, parent };

struct Op {
  Tag      tag;
  uint32_t a;
  uint32_t b;
  uint32_t c;
  uint32_t d;
  uint32_t e;
};

inline constexpr size_t kSourceMax = std::numeric_limits<uint32_t>::max() / 2;

inline constexpr size_t kBlockNestingMax = 64;

inline constexpr size_t kDelimiterMax = 16;

enum class Problem : uint8_t {
  unclosed_triple,
  unclosed_tag,
  empty_tag,
  empty_key,
  empty_key_segment,
  empty_partial_name,
  close_without_open,
  close_mismatch,
  unclosed_section,
  set_delimiter,
  empty_block_name,
  empty_parent_name,
  blocks_too_deep,
  too_large,
  delimiter_too_long,
};

inline constexpr std::array<std::string_view, 15> kProblemTitles = {
    "unclosed {{{",
    "unclosed {{",
    "empty tag",
    "empty key",
    "a key has an empty segment",
    "empty partial name",
    "a closing tag without an opening tag",
    "a closing tag names another section than the one it closes",
    "an unclosed section",
    "a Set Delimiter tag needs two delimiters",
    "empty block name",
    "empty parent name",
    "blocks and parents nest deeper than 64",
    "the compiled template does not fit 32-bit offsets",
    "a delimiter is longer than 16 bytes",
};

struct Refusal {
  Problem  problem;
  uint32_t at;
};

struct Compiled {
  std::string              texts;
  std::vector<std::string> keys;
  std::vector<Op>          ops;
  std::vector<uint32_t>    arguments;
};

namespace detail {

enum class Kind { text, var, raw, section, inverted, close, comment, partial, set_delimiter, block, parent };

using Key = std::vector<std::string>;

inline constexpr uint32_t kNoIndent = std::numeric_limits<uint32_t>::max();

struct IndentRef {
  uint32_t line = kNoIndent;
  uint32_t skip = 0;
};

struct Token {
  Kind        kind;
  uint32_t    at;
  std::string text;
  Key         key;
  IndentRef   indent;
  uint32_t    end = 0;
  bool        standalone = false;
  bool        clear_right = false;
  bool        indented = false;
};

struct Delimiters {
  std::string open;
  std::string close;
};

struct Stripped {
  std::vector<Token>       tokens;
  std::vector<std::string> indents;
};

constexpr std::string_view
indent_of(const std::vector<std::string> &indents, const Token &t)
{
  if (!t.indented) return {};
  return std::string_view(indents.at(t.indent.line)).substr(t.indent.skip);
}

constexpr bool
is_space_byte(const char b)
{
  return b == ' ' || b == '\t' || b == '\n' || b == '\r' || b == '\v' || b == '\f';
}

constexpr bool
is_ws_inline(const char b)
{
  return b == ' ' || b == '\t' || b == '\r';
}

constexpr std::string_view
trimmed(std::string_view s)
{
  while (!s.empty() && is_space_byte(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_space_byte(s.back())) s.remove_suffix(1);
  return s;
}

constexpr std::variant<Key, Refusal>
parse_key(const std::string_view body, const uint32_t at)
{
  const std::string_view k = trimmed(body);
  Key parts;
  if (k == ".") return parts;
  if (k.empty()) [[unlikely]] return Refusal{Problem::empty_key, at};
  size_t from = 0;
  for (size_t i = 0; i <= k.size(); i++) {
    if (i == k.size() || k.at(i) == '.') {
      if (i == from) [[unlikely]] return Refusal{Problem::empty_key_segment, at};
      parts.emplace_back(k.substr(from, i - from));
      from = i + 1;
    }
  }
  return parts;
}

constexpr std::variant<Token, Refusal>
token_of(const Kind kind, const std::string_view body, const uint32_t at)
{
  std::variant<Key, Refusal> key = parse_key(body, at);
  if (std::holds_alternative<Refusal>(key)) [[unlikely]] return std::get<Refusal>(key);
  return Token{kind, at, {}, std::move(std::get<Key>(key)), {}};
}

constexpr std::variant<Token, Refusal>
named_token_of(const Kind kind, const std::string_view body, const uint32_t at, const Problem empty)
{
  const std::string_view name = trimmed(body);
  if (name.empty()) [[unlikely]] return Refusal{empty, at};
  return Token{kind, at, {}, Key{std::string(name)}, {}};
}

constexpr std::optional<Delimiters>
parse_set_delimiter(const std::string_view body)
{
  const std::string_view inner = trimmed(body);
  const size_t gap = (size_t)std::distance(inner.begin(), std::ranges::find_if(inner, is_space_byte));
  const std::string_view open = inner.substr(0, gap);
  const std::string_view close = trimmed(inner.substr(gap));
  if (open.empty() || close.empty() || std::ranges::any_of(close, is_space_byte)) [[unlikely]] return std::nullopt;
  return Delimiters{std::string(open), std::string(close)};
}

constexpr bool
is_clear_right(const std::string_view src, const size_t from)
{
  const std::string_view rest = src.substr(from);
  const size_t i = (size_t)std::distance(rest.begin(), std::ranges::find_if_not(rest, is_ws_inline));
  return i == rest.size() || rest.at(i) == '\n';
}

constexpr std::variant<Token, Refusal>
tag_of(const std::string_view body, const uint32_t at)
{
  size_t i = 0;
  while (i < body.size() && is_space_byte(body.at(i))) i++;
  if (i == body.size()) [[unlikely]] return Refusal{Problem::empty_tag, at};
  const std::string_view rest = body.substr(i + 1);
  switch (body.at(i)) {
    case '!': return Token{Kind::comment, at, {}, {}, {}};
    case '#': return token_of(Kind::section, rest, at);
    case '^': return token_of(Kind::inverted, rest, at);
    case '/': return token_of(Kind::close, rest, at);
    case '&': return token_of(Kind::raw, rest, at);
    case '>': return named_token_of(Kind::partial, rest, at, Problem::empty_partial_name);
    case '$': return named_token_of(Kind::block, rest, at, Problem::empty_block_name);
    case '<': return named_token_of(Kind::parent, rest, at, Problem::empty_parent_name);
    default:
      return token_of(Kind::var, body.substr(i), at);
  }
}

constexpr std::variant<std::vector<Token>, Refusal>
tokenize(const std::string_view src)
{
  std::vector<Token> out;
  Delimiters d{"{{", "}}"};
  size_t pos = 0;
  while (pos < src.size()) {
    const size_t open = src.find(d.open, pos);
    if (open == std::string_view::npos) {
      out.push_back({Kind::text, (uint32_t)pos, std::string(src.substr(pos)), {}, {}});
      break;
    }
    if (open > pos) out.push_back({Kind::text, (uint32_t)pos, std::string(src.substr(pos, open - pos)), {}, {}});
    const uint32_t at = (uint32_t)open;
    const size_t body = open + d.open.size();
    if (body < src.size() && src.at(body) == '=') {
      const size_t close = src.find("=" + d.close, body + 1);
      if (close == std::string_view::npos) [[unlikely]] return Refusal{Problem::unclosed_tag, at};
      const std::optional<Delimiters> next = parse_set_delimiter(src.substr(body + 1, close - body - 1));
      if (!next) [[unlikely]] return Refusal{Problem::set_delimiter, at};
      if (next->open.size() > kDelimiterMax || next->close.size() > kDelimiterMax) [[unlikely]] {
        return Refusal{Problem::delimiter_too_long, at};
      }
      pos = close + 1 + d.close.size();
      out.push_back({Kind::set_delimiter, at, {}, {}, {}});
      out.back().clear_right = is_clear_right(src, pos);
      d = *next;
      continue;
    }
    const bool triple = body < src.size() && src.at(body) == '{';
    const size_t close = triple ? src.find("}" + d.close, body + 1) : src.find(d.close, body);
    if (close == std::string_view::npos) [[unlikely]] {
      return Refusal{triple ? Problem::unclosed_triple : Problem::unclosed_tag, at};
    }
    std::variant<Token, Refusal> t = triple ? token_of(Kind::raw, src.substr(body + 1, close - body - 1), at)
                                            : tag_of(src.substr(body, close - body), at);
    if (std::holds_alternative<Refusal>(t)) [[unlikely]] return std::get<Refusal>(t);
    pos = close + (triple ? 1 : 0) + d.close.size();
    out.push_back(std::move(std::get<Token>(t)));
    out.back().clear_right = is_clear_right(src, pos);
  }
  return out;
}

constexpr std::string
joined(const Key &key)
{
  std::string out;
  for (const std::string &part : key) {
    if (!out.empty()) out += '.';
    out += part;
  }
  return out;
}

constexpr bool
is_standalone_eligible(const Kind k)
{
  return k == Kind::section || k == Kind::inverted || k == Kind::close || k == Kind::comment ||
         k == Kind::partial || k == Kind::set_delimiter || k == Kind::block || k == Kind::parent;
}

constexpr bool
ws_only(const std::string_view s, const size_t lo, const size_t hi)
{
  for (size_t i = lo; i < hi; i++) {
    if (!is_ws_inline(s.at(i))) return false;
  }
  return true;
}

constexpr Stripped
standalone_stripped(std::vector<Token> ops)
{
  std::vector<std::string> indents;
  size_t line_op = 0;
  size_t line_inset = 0;
  while (line_op < ops.size()) {
    const size_t n = ops.size();
    bool has_end = false;
    size_t end_op = 0;
    size_t end_inset = 0;
    for (size_t j = line_op; j < n; j++) {
      if (ops.at(j).kind != Kind::text) continue;
      const size_t nl = ops.at(j).text.find('\n', j == line_op ? line_inset : 0);
      if (nl != std::string::npos) {
        has_end = true;
        end_op = j;
        end_inset = nl;
        break;
      }
    }
    const size_t last_op = has_end ? end_op : n - 1;
    bool eligible = true;
    bool has_standalone = false;
    for (size_t k = line_op; k <= last_op; k++) {
      const Token &op = ops.at(k);
      if (op.kind == Kind::text) {
        const size_t lo = k == line_op ? line_inset : 0;
        const size_t hi = (has_end && k == end_op) ? end_inset : op.text.size();
        if (!ws_only(op.text, lo, hi)) {
          eligible = false;
          break;
        }
      }
      else if (is_standalone_eligible(op.kind)) {
        has_standalone = true;
      }
      else {
        eligible = false;
        break;
      }
    }
    size_t kept_newline = 0;
    if (eligible && has_standalone) {
      std::string indent;
      bool before_tag = true;
      bool block_open = false;
      bool indent_kept = false;
      std::string block_name;
      for (size_t k = line_op; k <= last_op; k++) {
        Token &op = ops.at(k);
        if (op.kind == Kind::text) {
          if (before_tag) indent.append(op.text, k == line_op ? line_inset : 0, std::string::npos);
          continue;
        }
        before_tag = false;
        op.standalone = true;
        if (op.kind == Kind::partial || op.kind == Kind::block || op.kind == Kind::parent) {
          if (!indent_kept) {
            indents.push_back(indent);
            indent_kept = true;
          }
          op.indent = IndentRef{(uint32_t)(indents.size() - 1), 0};
          op.indented = true;
        }
        if (op.kind == Kind::block) {
          block_open = true;
          block_name = op.key.at(0);
        }
        if (op.kind == Kind::close && block_open && joined(op.key) == block_name && has_end) {
          const std::string &t = ops.at(end_op).text;
          kept_newline = end_inset > 0 && t.at(end_inset - 1) == '\r' ? 2 : 1;
        }
      }
      for (size_t k = line_op; k <= last_op; k++) {
        if (ops.at(k).kind != Kind::text) continue;
        const std::string &t = ops.at(k).text;
        std::string kept = t.substr(0, k == line_op ? line_inset : 0);
        if (has_end && k == end_op) kept.append(t, end_inset + 1 - kept_newline, std::string::npos);
        ops.at(k).text = kept;
      }
    }
    if (!has_end) break;
    if (eligible && has_standalone) {
      line_inset = (end_op == line_op ? line_inset : 0) + kept_newline;
    }
    else {
      line_inset = end_inset + 1;
    }
    line_op = end_op;
    if (ops.at(line_op).text.size() <= line_inset) {
      line_op = end_op + 1;
      line_inset = 0;
    }
  }
  return {std::move(ops), std::move(indents)};
}

constexpr std::variant<std::vector<Token>, Refusal>
linked(std::vector<Token> tokens)
{
  std::vector<Token> out;
  std::vector<size_t> open;
  size_t named_open = 0;
  for (Token &t : tokens) {
    switch (t.kind) {
      case Kind::block:
      case Kind::parent:
        if (++named_open > kBlockNestingMax) [[unlikely]] return Refusal{Problem::blocks_too_deep, t.at};
        open.push_back(out.size());
        out.push_back(std::move(t));
        break;
      case Kind::section:
      case Kind::inverted:
        open.push_back(out.size());
        out.push_back(std::move(t));
        break;
      case Kind::close: {
        if (open.empty()) [[unlikely]] return Refusal{Problem::close_without_open, t.at};
        Token &opener = out.at(open.back());
        open.pop_back();
        const bool named = opener.kind == Kind::block || opener.kind == Kind::parent;
        if (named) named_open--;
        if (named ? joined(t.key) != opener.key.at(0) : opener.key != t.key) [[unlikely]] {
          return Refusal{Problem::close_mismatch, t.at};
        }
        opener.end = (uint32_t)out.size();
        break;
      }
      case Kind::comment:
      case Kind::set_delimiter:
        break;
      case Kind::text:
        if (!t.text.empty()) out.push_back(std::move(t));
        break;
      default:
        out.push_back(std::move(t));
    }
  }
  if (!open.empty()) [[unlikely]] return Refusal{Problem::unclosed_section, out.at(open.back()).at};
  return out;
}

constexpr Tag
tag_of_kind(const Kind k)
{
  switch (k) {
    case Kind::var:      return Tag::var;
    case Kind::raw:      return Tag::raw;
    case Kind::section:  return Tag::section;
    case Kind::inverted: return Tag::inverted;
    case Kind::partial:  return Tag::partial;
    case Kind::block:    return Tag::block;
    case Kind::parent:   return Tag::parent;
    default:             return Tag::text;
  }
}

constexpr bool
is_opener(const Kind k)
{
  return k == Kind::section || k == Kind::inverted || k == Kind::block || k == Kind::parent;
}

constexpr std::vector<bool>
parent_args_of(const std::vector<Token> &tokens)
{
  std::vector<bool> in_args(tokens.size(), false);
  std::vector<size_t> open;
  for (size_t i = 0; i < tokens.size(); i++) {
    while (!open.empty() && tokens.at(open.back()).end <= i) open.pop_back();
    in_args.at(i) = !open.empty() && tokens.at(open.back()).kind == Kind::parent;
    if (is_opener(tokens.at(i).kind)) open.push_back(i);
  }
  return in_args;
}

constexpr std::string
leading_ws_of(const std::string_view s)
{
  return std::string(s.substr(0, (size_t)std::distance(s.begin(), std::ranges::find_if_not(s, is_ws_inline))));
}


constexpr std::string
dedented_text(const std::string_view text, const std::string_view indent, const bool at_line_start)
{
  std::string out;
  bool line_start = at_line_start;
  size_t i = 0;
  while (i < text.size()) {
    if (line_start && text.substr(i).starts_with(indent)) i += indent.size();
    if (i == text.size()) break;
    out += text.at(i);
    line_start = text.at(i) == '\n';
    i++;
  }
  return out;
}

constexpr Stripped
blocks_dedented(Stripped s)
{
  const std::vector<bool> in_args = parent_args_of(s.tokens);
  for (size_t i = 0; i < s.tokens.size(); i++) {
    if (s.tokens.at(i).kind != Kind::block) continue;
    const Token &block = s.tokens.at(i);
    const bool eligible = (in_args.at(i) || block.standalone) && block.clear_right && block.end > i + 1;
    if (!eligible) continue;
    if (!block.standalone && s.tokens.at(i + 1).kind == Kind::text) {
      std::string &t = s.tokens.at(i + 1).text;
      t.erase(0, t.find('\n') + 1);
    }
    const Token &first = s.tokens.at(i + 1);
    IndentRef ref = first.indent;
    if (first.kind == Kind::text || !first.indented) {
      s.indents.push_back(first.kind == Kind::text ? leading_ws_of(first.text) : std::string());
      ref = IndentRef{(uint32_t)(s.indents.size() - 1), 0};
    }
    const std::string indent(std::string_view(s.indents.at(ref.line)).substr(ref.skip));
    bool line_start = true;
    for (size_t k = i + 1; k < block.end; k++) {
      Token &t = s.tokens.at(k);
      if (t.kind == Kind::text) {
        t.text = dedented_text(t.text, indent, line_start);
        if (!t.text.empty()) line_start = t.text.back() == '\n';
        continue;
      }
      if (!t.standalone) {
        line_start = false;
        continue;
      }
      if (t.indented && indent_of(s.indents, t).starts_with(indent)) t.indent.skip += (uint32_t)indent.size();
    }
    s.tokens.at(i).indent = ref;
    s.tokens.at(i).indented = true;
  }
  return s;
}

} // namespace detail

constexpr std::variant<Compiled, Refusal>
compile(const std::string_view src)
{
  using namespace detail;
  std::variant<std::vector<Token>, Refusal> tokens = tokenize(src);
  if (std::holds_alternative<Refusal>(tokens)) [[unlikely]] return std::get<Refusal>(tokens);
  Stripped stripped = standalone_stripped(std::move(std::get<std::vector<Token>>(tokens)));
  std::variant<std::vector<Token>, Refusal> linked_tokens = linked(std::move(stripped.tokens));
  if (std::holds_alternative<Refusal>(linked_tokens)) [[unlikely]] return std::get<Refusal>(linked_tokens);
  const Stripped s =
      blocks_dedented(Stripped{std::move(std::get<std::vector<Token>>(linked_tokens)), std::move(stripped.indents)});
  const std::vector<Token> &ts = s.tokens;
  const std::vector<bool> in_args = parent_args_of(ts);
  std::vector<uint32_t> op_of(ts.size() + 1, 0);
  Compiled out;
  std::vector<std::optional<size_t>> placed(s.indents.size());
  auto indent_at = [&](const Token &t) {
    if (!t.indented) return size_t{0};
    std::optional<size_t> &line = placed.at(t.indent.line);
    if (!line) {
      line = out.texts.size();
      out.texts += s.indents.at(t.indent.line);
    }
    return *line + t.indent.skip;
  };
  size_t i = 0;
  while (i < ts.size()) {
    const Token &t = ts.at(i);
    op_of.at(i) = (uint32_t)out.ops.size();
    if (in_args.at(i) && t.kind != Kind::block) {
      i = is_opener(t.kind) ? t.end : i + 1;
      continue;
    }
    const uint32_t indent_length = (uint32_t)indent_of(s.indents, t).size();
    switch (t.kind) {
      case Kind::text:
        if (!t.text.empty()) {
          out.ops.push_back({Tag::text, (uint32_t)out.texts.size(), (uint32_t)t.text.size(), 0, 0, 0});
          out.texts += t.text;
        }
        break;
      case Kind::partial: {
        const size_t at = indent_at(t);
        out.ops.push_back({Tag::partial, (uint32_t)out.keys.size(), indent_length, (uint32_t)at, 0, 0});
        out.keys.push_back(t.key.at(0));
        break;
      }
      case Kind::parent: {
        const size_t at = indent_at(t);
        out.ops.push_back({Tag::parent, (uint32_t)out.keys.size(), indent_length, t.end, (uint32_t)at, 0});
        out.keys.push_back(t.key.at(0));
        break;
      }
      case Kind::block: {
        const size_t at = indent_at(t);
        out.ops.push_back({Tag::block, (uint32_t)out.texts.size(), (uint32_t)t.key.at(0).size(), t.end, indent_length,
                           (uint32_t)at});
        out.texts += t.key.at(0);
        break;
      }
      default:
        out.ops.push_back({tag_of_kind(t.kind), (uint32_t)out.keys.size(), (uint32_t)t.key.size(), t.end, 0, 0});
        for (const std::string &k : t.key) out.keys.push_back(k);
    }
    i++;
  }
  op_of.at(ts.size()) = (uint32_t)out.ops.size();
  for (Op &op : out.ops) {
    if (op.tag == Tag::section || op.tag == Tag::inverted || op.tag == Tag::block || op.tag == Tag::parent) {
      op.c = op_of.at(op.c);
    }
  }
  for (size_t p = 0; p < out.ops.size(); p++) {
    if (out.ops.at(p).tag != Tag::parent) continue;
    std::vector<uint32_t> blocks;
    for (uint32_t q = (uint32_t)p + 1; q < out.ops.at(p).c; q = out.ops.at(q).c) blocks.push_back(q);
    const auto name_of = [&out](const uint32_t q) {
      return std::string_view(out.texts).substr(out.ops.at(q).a, out.ops.at(q).b);
    };
    std::ranges::stable_sort(blocks, {}, name_of);
    out.ops.at(p).e = (uint32_t)out.arguments.size();
    out.arguments.push_back((uint32_t)blocks.size());
    out.arguments.insert(out.arguments.end(), blocks.begin(), blocks.end());
  }
  constexpr size_t kOffsetMax = std::numeric_limits<uint32_t>::max();
  if (out.texts.size() > kOffsetMax || out.keys.size() > kOffsetMax || out.ops.size() > kOffsetMax ||
      out.arguments.size() > kOffsetMax) [[unlikely]] {
    return Refusal{Problem::too_large, 0};
  }
  return out;
}

} // namespace mustache
