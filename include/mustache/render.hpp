#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <new>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "compile.hpp"
#include "out.hpp"

namespace mustache {

template <class Tag, class... Args>
concept tag_invocable = requires(Tag tag, Args &&...args) {
  tag_invoke(tag, std::forward<Args>(args)...);
};

#define MUSTACHE_TAG(name)                                                     \
  struct name##_tag {                                                          \
    template <class... Args>                                                   \
      requires tag_invocable<name##_tag, Args...>                              \
    constexpr decltype(auto) operator()(Args &&...args) const                  \
    {                                                                          \
      return tag_invoke(*this, std::forward<Args>(args)...);                   \
    }                                                                          \
  };                                                                           \
  inline constexpr name##_tag name{};

MUSTACHE_TAG(key_of)
MUSTACHE_TAG(find)
MUSTACHE_TAG(kind_of)
MUSTACHE_TAG(text_of)
MUSTACHE_TAG(size_of)
MUSTACHE_TAG(element)
MUSTACHE_TAG(partial)
MUSTACHE_TAG(fail)

#undef MUSTACHE_TAG

enum class Kind : uint8_t { falsy, text, list, map, truthy };

enum class Fault : uint8_t { none, over_capacity, over_limit, not_text, too_deep, over_work, parse, no_memory };

template <class Key>
struct Program {
  std::string           texts;
  std::vector<Key>      keys;
  std::vector<Op>       ops;
  std::vector<uint32_t> arguments;
};

template <class Host, class Allocate>
auto
allocated_or_failed(Host &host, const std::string_view what, const size_t asked, const Allocate allocate)
    -> std::optional<decltype(allocate())>
{
  try {
    return allocate();
  }
  catch (const std::bad_alloc &) {
    fail(host, Fault::no_memory, what, asked, size_t{0});
  }
  catch (const std::length_error &) {
    fail(host, Fault::no_memory, what, asked, size_t{0});
  }
  return std::nullopt;
}

template <class Host>
bool
within_limit(Host &host, const std::string_view what, const size_t asked, const size_t allowed)
{
  if (asked > allowed) [[unlikely]] {
    fail(host, Fault::over_limit, what, asked, allowed);
    return false;
  }
  return true;
}

template <class Key, class Host>
std::optional<Program<Key>>
program_of(Host &host, const std::string_view source, const size_t source_max)
{
  if (!within_limit(host, "template", source.size(), std::min(source_max, kSourceMax))) [[unlikely]] return std::nullopt;
  std::optional<std::variant<Compiled, Refusal>> compiled =
      allocated_or_failed(host, "template", source.size(), [&] { return compile(source); });
  if (!compiled) [[unlikely]] return std::nullopt;
  if (std::holds_alternative<Refusal>(*compiled)) [[unlikely]] {
    const Refusal r = std::get<Refusal>(*compiled);
    fail(host, Fault::parse, kProblemTitles.at((size_t)r.problem), size_t{r.at}, size_t{0});
    return std::nullopt;
  }
  const Compiled &c = std::get<Compiled>(*compiled);
  return allocated_or_failed(host, "template", source.size(), [&] {
    Program<Key> p{c.texts, {}, c.ops, c.arguments};
    p.keys.reserve(c.keys.size());
    for (const std::string &k : c.keys) p.keys.push_back(key_of(host, std::string_view(k)));
    return p;
  });
}

inline constexpr int kMaxDepth = 32;
inline constexpr int kMaxPartialDepth = 64;
inline constexpr size_t kMaxRenderScore = size_t{1} << 24;

template <class Host, class Value, class Key, class Sink = Out, int kDepthMax = kMaxDepth,
          int kPartialDepthMax = kMaxPartialDepth>
class Walk {
public:
  Walk(Host &host, Sink &out, const size_t score_max = kMaxRenderScore) : host_(host), out_(out), score_max_(score_max)
  {
  }

  Fault run(const Program<Key> &p, const Value &root)
  {
    stack_.at(0) = root;
    depth_ = 1;
    score_ = 0;
    fault_ = Fault::none;
    run(p, 0, (uint32_t)p.ops.size(), 0, nullptr, nullptr);
    if (fault_ == Fault::none && out_.full) [[unlikely]] fault_ = Fault::over_capacity;
    switch (fault_) {
      case Fault::over_capacity:
        fail(host_, fault_, std::string_view("the answer is larger than the buffer"), size_t{0}, size_t{0});
        break;
      case Fault::not_text:
        fail(host_, fault_, std::string_view("a value is not text"), size_t{0}, size_t{0});
        break;
      case Fault::too_deep:
        fail(host_, fault_, std::string_view("nesting too deep"), size_t{0}, size_t{0});
        break;
      case Fault::over_work:
        fail(host_, fault_, std::string_view("the render does more work than max_render_score"), score_, score_max_);
        break;
      default:
        break;
    }
    return fault_;
  }

private:
  struct Indent {
    const Indent    *outer;
    std::string_view text;
    bool             pending;
  };

  struct Args {
    const Program<Key> *program;
    uint32_t            arguments;
    const Args         *outer;
  };

  Host &host_;
  Sink &out_;
  std::array<Value, kDepthMax> stack_{};
  int   depth_ = 0;
  size_t score_ = 0;
  size_t score_max_;
  Fault fault_ = Fault::none;

  void indent_of(const Indent *const ind)
  {
    if (ind == nullptr) return;
    indent_of(ind->outer);
    out_.raw(ind->text);
  }

  void indent_if_pending(Indent *const ind)
  {
    if (ind == nullptr || !ind->pending) [[likely]] return;
    ind->pending = false;
    indent_of(ind);
  }

  void text(const std::string_view all, Indent *const ind)
  {
    if (ind == nullptr) [[likely]] {
      out_.raw(all);
      return;
    }
    std::string_view s = all;
    while (!s.empty()) {
      const size_t nl = s.find('\n');
      const size_t line = nl == std::string_view::npos ? s.size() : nl + 1;
      indent_if_pending(ind);
      out_.raw(s.substr(0, line));
      if (nl != std::string_view::npos) ind->pending = true;
      s.remove_prefix(line);
    }
  }

  std::optional<Value> lookup(const Program<Key> &p, const Op &op)
  {
    if (op.b == 0) return stack_.at(depth_ - 1);
    const Key &first = p.keys.at(op.a);
    std::optional<Value> base;
    for (int i = depth_ - 1; i >= 0; i--) {
      base = find(host_, stack_.at(i), first);
      if (base) break;
    }
    for (uint32_t j = 1; j < op.b && base; j++) base = find(host_, *base, p.keys.at(op.a + j));
    return base;
  }

  bool push_run(const Program<Key> &p, const Value &v, const uint32_t pc, const uint32_t stop, const int pd,
                Indent *const ind, const Args *const args)
  {
    if (depth_ >= kDepthMax) [[unlikely]] {
      fault_ = Fault::too_deep;
      return false;
    }
    stack_.at(depth_++) = v;
    const bool ok = run(p, pc, stop, pd, ind, args);
    depth_--;
    return ok;
  }

  bool run_indented(const Program<Key> &p, const uint32_t from, const uint32_t stop, const int pd, Indent *const ind,
                    const std::string_view indent, const Args *const args)
  {
    if (indent.empty()) {
      indent_if_pending(ind);
      return run(p, from, stop, pd, ind, args);
    }
    Indent inner{ind, indent, true};
    const bool ok = run(p, from, stop, pd, &inner, args);
    if (ind != nullptr) ind->pending = inner.pending;
    return ok;
  }

  static std::string_view block_name_of(const Program<Key> &p, const Op &op)
  {
    return std::string_view(p.texts).substr(op.a, op.b);
  }

  struct Argument {
    const Program<Key> *program;
    uint32_t            at;
  };

  static std::optional<Argument> argument_of(const Args *const args, const std::string_view name)
  {
    std::optional<Argument> found;
    for (const Args *f = args; f != nullptr; f = f->outer) {
      const std::vector<uint32_t> &all = f->program->arguments;
      const std::span<const uint32_t> sorted = std::span(all).subspan(f->arguments + 1, all.at(f->arguments));
      const auto name_at = [f](const uint32_t q) { return block_name_of(*f->program, f->program->ops.at(q)); };
      const auto after = std::ranges::upper_bound(sorted, name, {}, name_at);
      if (after != sorted.begin() && name_at(*std::prev(after)) == name) found = Argument{f->program, *std::prev(after)};
    }
    return found;
  }

  bool run(const Program<Key> &p, const uint32_t from, const uint32_t stop, const int pd, Indent *const ind,
           const Args *const args)
  {
    const size_t work = size_t{stop - from} + 1;
    if (score_max_ - score_ < work) [[unlikely]] {
      score_ += work;
      fault_ = Fault::over_work;
      return false;
    }
    score_ += work;
    uint32_t pc = from;
    while (pc < stop) {
      if (out_.full) [[unlikely]] return false;
      const Op &op = p.ops.at(pc);
      switch (op.tag) {
        case Tag::text:
          text(std::string_view(p.texts).substr(op.a, op.b), ind);
          pc++;
          break;
        case Tag::var:
        case Tag::raw: {
          pc++;
          const std::optional<Value> v = lookup(p, op);
          if (!v) break;
          const Kind k = kind_of(host_, *v);
          if (k == Kind::falsy) break;
          if (k != Kind::text) [[unlikely]] {
            fault_ = Fault::not_text;
            return false;
          }
          const std::string_view s = text_of(host_, *v);
          if (s.empty()) break;
          indent_if_pending(ind);
          if (op.tag == Tag::var) out_.escaped(s);
          else out_.raw(s);
          break;
        }
        case Tag::section: {
          const std::optional<Value> v = lookup(p, op);
          const Kind k = v ? kind_of(host_, *v) : Kind::falsy;
          if (k == Kind::list) {
            const size_t n = size_of(host_, *v);
            for (size_t i = 0; i < n; i++) {
              if (!push_run(p, element(host_, *v, i), pc + 1, op.c, pd, ind, args)) [[unlikely]] return false;
            }
          }
          else if (k != Kind::falsy) {
            if (!push_run(p, *v, pc + 1, op.c, pd, ind, args)) [[unlikely]] return false;
          }
          pc = op.c;
          break;
        }
        case Tag::inverted: {
          const std::optional<Value> v = lookup(p, op);
          const Kind k = v ? kind_of(host_, *v) : Kind::falsy;
          const bool empty = k == Kind::falsy || (k == Kind::list && size_of(host_, *v) == 0);
          if (empty && !push_run(p, stack_.at(depth_ - 1), pc + 1, op.c, pd, ind, args)) [[unlikely]] return false;
          pc = op.c;
          break;
        }
        case Tag::partial: {
          pc++;
          if (pd >= kPartialDepthMax) [[unlikely]] {
            fault_ = Fault::too_deep;
            return false;
          }
          const Program<Key> *const sub = partial(host_, p.keys.at(op.a));
          if (sub == nullptr) break;
          if (!run_indented(*sub, 0, (uint32_t)sub->ops.size(), pd + 1, ind,
                            std::string_view(p.texts).substr(op.c, op.b), nullptr)) [[unlikely]] {
            return false;
          }
          break;
        }
        case Tag::parent: {
          if (pd >= kPartialDepthMax) [[unlikely]] {
            fault_ = Fault::too_deep;
            return false;
          }
          const Program<Key> *const sub = partial(host_, p.keys.at(op.a));
          if (sub != nullptr) {
            const Args frame{&p, op.e, args};
            if (!run_indented(*sub, 0, (uint32_t)sub->ops.size(), pd + 1, ind,
                              std::string_view(p.texts).substr(op.d, op.b), &frame)) [[unlikely]] {
              return false;
            }
          }
          pc = op.c;
          break;
        }
        case Tag::block: {
          if (pd >= kPartialDepthMax) [[unlikely]] {
            fault_ = Fault::too_deep;
            return false;
          }
          const std::optional<Argument> found = argument_of(args, block_name_of(p, op));
          const Program<Key> &body = found ? *found->program : p;
          const uint32_t first = found ? found->at + 1 : pc + 1;
          const uint32_t last = found ? body.ops.at(found->at).c : op.c;
          if (!run_indented(body, first, last, pd + 1, ind, std::string_view(p.texts).substr(op.e, op.d),
                            args)) [[unlikely]] {
            return false;
          }
          pc = op.c;
          break;
        }
      }
    }
    return true;
  }
};

}
