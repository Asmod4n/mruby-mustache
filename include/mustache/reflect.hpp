#pragma once

#ifdef __cpp_impl_reflection

#include <meta>
#include <array>
#include <optional>
#include <tuple>
#include <ranges>
#include <string_view>
#include <variant>
#include <type_traits>

#include "compile.hpp"
#include "out.hpp"

namespace mustache {

template <size_t N>
struct fixed_string {
  char s[N]{};
  consteval fixed_string() = default;
  consteval fixed_string(const char (&a)[N]) { for (size_t i = 0; i < N; i++) s[i] = a[i]; }
  constexpr std::string_view view() const { return {s, N - 1}; }
};

template <size_t N>
consteval fixed_string<N + 1>
fixed_of(const std::string_view text)
{
  fixed_string<N + 1> f;
  for (size_t i = 0; i < N; i++) f.s[i] = text.at(i);
  return f;
}

struct KeyRef {
  uint32_t off;
  uint32_t len;
};

struct Sizes {
  size_t ops;
  size_t texts;
  size_t keys;
  size_t key_refs;
  bool   refused;
};

consteval Sizes
sizes_of(const std::string_view src)
{
  const std::variant<Compiled, Refusal> r = compile(src);
  if (std::holds_alternative<Refusal>(r)) return {0, 0, 0, 0, true};
  const Compiled &c = std::get<Compiled>(r);
  size_t keys = 0;
  for (const std::string &k : c.keys) keys += k.size();
  return {c.ops.size(), c.texts.size(), keys, c.keys.size(), false};
}

template <Sizes S>
struct StaticProgram {
  std::array<Op, S.ops>          ops{};
  std::array<char, S.texts + 1>  texts{};
  std::array<char, S.keys + 1>   keys{};
  std::array<KeyRef, S.key_refs> key_refs{};
  bool                           refused = S.refused;
};

template <Sizes S>
consteval StaticProgram<S>
make_static(const std::string_view src)
{
  StaticProgram<S> p;
  const std::variant<Compiled, Refusal> r = compile(src);
  if (std::holds_alternative<Refusal>(r)) return p;
  const Compiled &c = std::get<Compiled>(r);
  for (size_t i = 0; i < S.ops; i++) p.ops.at(i) = c.ops.at(i);
  for (size_t i = 0; i < S.texts; i++) p.texts.at(i) = c.texts.at(i);
  size_t at = 0;
  for (size_t i = 0; i < S.key_refs; i++) {
    p.key_refs.at(i) = {(uint32_t)at, (uint32_t)c.keys.at(i).size()};
    for (const char ch : c.keys.at(i)) p.keys.at(at++) = ch;
  }
  return p;
}

template <fixed_string Src>
inline constexpr auto static_program_of = make_static<sizes_of(Src.view())>(Src.view());

namespace detail {

consteval std::meta::info
member_named(std::meta::info type, std::string_view name)
{
  if (!std::meta::is_class_type(type)) return std::meta::info{};
  for (std::meta::info m : std::meta::nonstatic_data_members_of(type, std::meta::access_context::unchecked())) {
    if (std::meta::has_identifier(m) && std::meta::identifier_of(m) == name) return m;
  }
  return std::meta::info{};
}

inline constexpr auto kCxxKeywords = std::to_array<std::string_view>({
    "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
    "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr",
    "constinit", "const_cast", "continue", "contract_assert", "co_await", "co_return", "co_yield", "decltype",
    "default", "delete", "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false",
    "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable", "namespace", "new", "noexcept",
    "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private", "protected", "public", "register",
    "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static", "static_assert",
    "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true", "try", "typedef",
    "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while", "xor",
    "xor_eq", "atomic_cancel", "atomic_commit", "atomic_noexcept", "reflexpr", "synchronized", "transaction_safe"});

consteval bool
is_cxx_keyword(const std::string_view name)
{
  for (const std::string_view k : kCxxKeywords) {
    if (k == name) return true;
  }
  return false;
}

template <class T>
concept text_like = std::is_convertible_v<const T &, std::string_view>;

template <class T>
concept list_like = std::ranges::range<T> && !text_like<T>;

template <class T>
struct is_optional : std::false_type {};
template <class T>
struct is_optional<std::optional<T>> : std::true_type {};

}

template <fixed_string Name, fixed_string Source>
struct static_partial {
  static constexpr auto name = Name;
  static constexpr auto source = Source;
};

template <class... Partials>
struct static_partial_list {};

struct no_frame {};

template <fixed_string Source, uint32_t From, uint32_t Stop, class Outer>
struct args_frame {
  static constexpr auto     source = Source;
  static constexpr uint32_t from = From;
  static constexpr uint32_t stop = Stop;
  using outer = Outer;
};

struct no_indent {};

template <fixed_string Text, class Outer>
struct indent_frame {};

namespace detail {

template <class Indent>
struct indent_text {
  static constexpr std::string_view view{};
};

template <fixed_string Text, class Outer>
struct indent_text<indent_frame<Text, Outer>> {
  static constexpr std::array<char, indent_text<Outer>::view.size() + Text.view().size()> storage = [] {
    std::array<char, indent_text<Outer>::view.size() + Text.view().size()> all{};
    size_t i = 0;
    for (const char c : indent_text<Outer>::view) all.at(i++) = c;
    for (const char c : Text.view()) all.at(i++) = c;
    return all;
  }();
  static constexpr std::string_view view{storage.data(), storage.size()};
};

template <class Args, size_t K>
struct frame_at {
  using type = typename frame_at<typename Args::outer, K - 1>::type;
};

template <class Args>
struct frame_at<Args, 0> {
  using type = Args;
};

template <class List>
struct partials_of;

template <class... Partials>
struct partials_of<static_partial_list<Partials...>> {
  static consteval int index_of(const std::string_view name)
  {
    int found = -1;
    int i = 0;
    ((found = (found < 0 && Partials::name.view() == name) ? i : found, i++), ...);
    return found;
  }

  template <int I>
  using at = std::tuple_element_t<I, std::tuple<Partials...>>;
};

struct Argument {
  int      depth;
  uint32_t at;
};

}

inline constexpr int kMaxStaticDepth = 64;

template <fixed_string Src, class Partials = static_partial_list<>, class Args = no_frame, class Indent = no_indent,
          int Depth = 0>
struct Renderer {
  static_assert(Depth <= kMaxStaticDepth, "partials, parents or blocks nest deeper than 64");
  static_assert(!static_program_of<Src>.refused, "a template does not compile");

  static constexpr const auto &P = static_program_of<Src>;
  static constexpr bool kIndented = !std::is_same_v<Indent, no_indent>;

  static consteval std::string_view key(uint32_t i)
  {
    return {P.keys.data() + P.key_refs.at(i).off, P.key_refs.at(i).len};
  }

  static consteval std::string_view text_at(const uint32_t at, const uint32_t length)
  {
    return {P.texts.data() + at, length};
  }

  template <class... Ctx>
  static consteval int innermost(std::string_view name)
  {
    constexpr std::array<std::meta::info, sizeof...(Ctx)> types = {^^std::remove_cvref_t<Ctx>...};
    for (int i = (int)types.size() - 1; i >= 0; i--) {
      if (detail::member_named(types.at(i), name) != std::meta::info{}) return i;
    }
    return -1;
  }

  template <uint32_t first, uint32_t count, uint32_t j, class T>
  static constexpr const auto &follow(const T &v)
  {
    if constexpr (j == count) {
      return v;
    }
    else {
      static_assert(!detail::is_cxx_keyword(key(first + j)), "a key is a C++ keyword, which no member can be named");
      constexpr std::meta::info m = detail::member_named(^^T, key(first + j));
      static_assert(m != std::meta::info{}, "a key segment names no member");
      return follow<first, count, j + 1>(v.[:m:]);
    }
  }

  template <uint32_t first, uint32_t count, class... Ctx>
  static constexpr const auto &lookup(const Ctx &...ctx)
  {
    if constexpr (count == 0) {
      return std::get<sizeof...(Ctx) - 1>(std::tie(ctx...));
    }
    else {
      static_assert(!detail::is_cxx_keyword(key(first)), "a key is a C++ keyword, which no member can be named");
      constexpr int i = innermost<Ctx...>(key(first));
      static_assert(i >= 0, "a key names no member of any context");
      const auto &base = std::get<i>(std::tie(ctx...));
      constexpr std::meta::info m = detail::member_named(^^std::remove_cvref_t<decltype(base)>, key(first));
      return follow<first, count, 1>(base.[:m:]);
    }
  }

  template <class A>
  static consteval detail::Argument argument_of(const std::string_view name)
  {
    if constexpr (std::is_same_v<A, no_frame>) {
      return {-1, 0};
    }
    else {
      const detail::Argument outer = argument_of<typename A::outer>(name);
      if (outer.depth >= 0) return {outer.depth + 1, outer.at};
      constexpr const auto &F = static_program_of<A::source>;
      detail::Argument found{-1, 0};
      for (uint32_t q = A::from; q < A::stop; q = F.ops.at(q).c) {
        if (std::string_view(F.texts.data() + F.ops.at(q).a, F.ops.at(q).b) == name) found = {0, q};
      }
      return found;
    }
  }

  template <class Sink>
  static constexpr void indent_if_pending(Sink &out, bool &pending)
  {
    if constexpr (kIndented) {
      if (pending) {
        pending = false;
        out.raw(detail::indent_text<Indent>::view);
      }
    }
  }

  template <class Sink>
  static constexpr void text(Sink &out, bool &pending, const std::string_view all)
  {
    if constexpr (!kIndented) {
      out.raw(all);
    }
    else {
      std::string_view s = all;
      while (!s.empty()) {
        const size_t nl = s.find('\n');
        const size_t line = nl == std::string_view::npos ? s.size() : nl + 1;
        indent_if_pending(out, pending);
        out.raw(s.substr(0, line));
        if (nl != std::string_view::npos) pending = true;
        s.remove_prefix(line);
      }
    }
  }

  template <Tag tag, class Sink>
  static constexpr void value(Sink &out, bool &pending, const std::string_view s)
  {
    if (s.empty()) return;
    indent_if_pending(out, pending);
    if constexpr (tag == Tag::var) out.escaped(s);
    else out.raw(s);
  }

  template <fixed_string Source, class InnerArgs, uint32_t IndentLength, uint32_t IndentAt, uint32_t From, uint32_t Stop,
            class Sink, class... Ctx>
  static constexpr void run_nested(Sink &out, bool &pending, const Ctx &...ctx)
  {
    if constexpr (IndentLength == 0) {
      indent_if_pending(out, pending);
      Renderer<Source, Partials, InnerArgs, Indent, Depth + 1>::template run<From, Stop>(out, pending, ctx...);
    }
    else {
      using Inner = indent_frame<fixed_of<IndentLength>(text_at(IndentAt, IndentLength)), Indent>;
      bool inner = true;
      Renderer<Source, Partials, InnerArgs, Inner, Depth + 1>::template run<From, Stop>(out, inner, ctx...);
      if constexpr (kIndented) pending = inner;
    }
  }

  template <uint32_t pc, uint32_t stop, class Sink, class... Ctx>
  static constexpr void run(Sink &out, bool &pending, const Ctx &...ctx)
  {
    if constexpr (pc < stop) {
      constexpr Op op = P.ops.at(pc);
      if constexpr (op.tag == Tag::text) {
        text(out, pending, text_at(op.a, op.b));
        run<pc + 1, stop>(out, pending, ctx...);
      }
      else if constexpr (op.tag == Tag::var || op.tag == Tag::raw) {
        const auto &v = lookup<op.a, op.b>(ctx...);
        using V = std::remove_cvref_t<decltype(v)>;
        if constexpr (detail::is_optional<V>::value) {
          static_assert(detail::text_like<typename V::value_type>, "a value is not text");
          if (v) value<op.tag>(out, pending, std::string_view(*v));
        }
        else {
          static_assert(detail::text_like<V>, "a value is not text");
          value<op.tag>(out, pending, std::string_view(v));
        }
        run<pc + 1, stop>(out, pending, ctx...);
      }
      else if constexpr (op.tag == Tag::section) {
        const auto &v = lookup<op.a, op.b>(ctx...);
        using V = std::remove_cvref_t<decltype(v)>;
        if constexpr (std::is_same_v<V, bool>) {
          if (v) run<pc + 1, op.c>(out, pending, ctx...);
        }
        else if constexpr (detail::is_optional<V>::value) {
          if (v) run<pc + 1, op.c>(out, pending, ctx..., *v);
        }
        else if constexpr (detail::list_like<V>) {
          for (const auto &e : v) run<pc + 1, op.c>(out, pending, ctx..., e);
        }
        else {
          run<pc + 1, op.c>(out, pending, ctx..., v);
        }
        run<op.c, stop>(out, pending, ctx...);
      }
      else if constexpr (op.tag == Tag::inverted) {
        const auto &v = lookup<op.a, op.b>(ctx...);
        using V = std::remove_cvref_t<decltype(v)>;
        bool empty;
        if constexpr (std::is_same_v<V, bool>) empty = !v;
        else if constexpr (detail::is_optional<V>::value) empty = !v;
        else if constexpr (detail::list_like<V>) empty = std::ranges::empty(v);
        else empty = false;
        if (empty) run<pc + 1, op.c>(out, pending, ctx...);
        run<op.c, stop>(out, pending, ctx...);
      }
      else if constexpr (op.tag == Tag::partial) {
        constexpr int i = detail::partials_of<Partials>::index_of(key(op.a));
        if constexpr (i >= 0) {
          using Found = typename detail::partials_of<Partials>::template at<i>;
          run_nested<Found::source, no_frame, op.b, op.c, 0, (uint32_t)static_program_of<Found::source>.ops.size()>(
              out, pending, ctx...);
        }
        run<pc + 1, stop>(out, pending, ctx...);
      }
      else if constexpr (op.tag == Tag::parent) {
        constexpr int i = detail::partials_of<Partials>::index_of(key(op.a));
        if constexpr (i >= 0) {
          using Found = typename detail::partials_of<Partials>::template at<i>;
          run_nested<Found::source, args_frame<Src, pc + 1, op.c, Args>, op.b, op.d, 0,
                     (uint32_t)static_program_of<Found::source>.ops.size()>(out, pending, ctx...);
        }
        run<op.c, stop>(out, pending, ctx...);
      }
      else if constexpr (op.tag == Tag::block) {
        constexpr detail::Argument found = argument_of<Args>(text_at(op.a, op.b));
        if constexpr (found.depth >= 0) {
          using Frame = typename detail::frame_at<Args, (size_t)found.depth>::type;
          run_nested<Frame::source, Args, op.d, op.e, found.at + 1,
                     static_program_of<Frame::source>.ops.at(found.at).c>(out, pending, ctx...);
        }
        else {
          run_nested<Src, Args, op.d, op.e, pc + 1, op.c>(out, pending, ctx...);
        }
        run<op.c, stop>(out, pending, ctx...);
      }
    }
  }
};

template <fixed_string Src, class... Partials, class Sink, class T>
constexpr void
render(Sink &out, const T &data)
{
  bool pending = false;
  Renderer<Src, static_partial_list<Partials...>>::template run<0, (uint32_t)static_program_of<Src>.ops.size()>(out, pending,
                                                                                                           data);
}

}

namespace mustache {

template <fixed_string Src, auto Make, class... Partials>
consteval size_t
static_length()
{
  for (size_t capacity = 4096;; capacity *= 2) {
    char *buffer = new char[capacity + kSlack];
    Out out{buffer, buffer + capacity};
    render<Src, Partials...>(out, Make());
    const size_t n = (size_t)(out.w - buffer);
    const bool full = out.full;
    delete[] buffer;
    if (!full) return n;
  }
}

template <fixed_string Src, auto Make, class... Partials>
consteval auto
static_page()
{
  constexpr size_t n = static_length<Src, Make, Partials...>();
  std::array<char, n + kSlack> page{};
  Out out{page.data(), page.data() + n};
  render<Src, Partials...>(out, Make());
  return page;
}

template <fixed_string Src, auto Make, class... Partials>
inline constexpr auto static_page_of = static_page<Src, Make, Partials...>();

}

#endif
