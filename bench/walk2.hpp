#pragma once

#include <mustache/render.hpp>

// A copy of Walk for one measurement: find gives a Value, and found says
// whether that Value names a value. Walk asks find for a
// std::optional<Value> instead.
namespace mustache {
struct found_tag {
  template <class... Args>
    requires tag_invocable<found_tag, Args...>
  constexpr decltype(auto) operator()(Args &&...args) const { return tag_invoke(*this, std::forward<Args>(args)...); }
};
inline constexpr found_tag found{};

template <class Host, class Value, class Key, int kDepthMax = kMaxDepth, int kPartialDepthMax = kMaxPartialDepth>
class Walk2 {
public:
  Walk2(Host &host, const size_t score_max = kMaxRenderScore, const size_t initial_capacity = kInitialCapacity,
       const size_t max_capacity = kMaxCapacity)
      : host_(host), out_{host, {}, 0, max_capacity}, score_max_(score_max), initial_capacity_(initial_capacity)
  {
  }

  Fault run(const Program<Key> &p, const Value &root)
  {
    stack_.at(0) = root;
    depth_ = 1;
    score_ = 0;
    fault_ = Fault::none;
    out_.size = 0;
    out_.fault = Fault::none;
    if (!out_.opened(initial_capacity_)) [[unlikely]] return failed(out_.fault);
    run(p, 0, (uint32_t)p.ops.size(), 0, nullptr, nullptr);
    const Fault closed = done(host_, out_.size);
    if (fault_ != Fault::none) [[unlikely]] return failed(fault_);
    if (closed != Fault::none) [[unlikely]] return failed(closed);
    return Fault::none;
  }

private:
  Fault failed(const Fault fault)
  {
    switch (fault) {
      case Fault::not_text: [[unlikely]]
        fail(host_, fault_, std::string_view("a value is not text"), size_t{0}, size_t{0});
        break;
      case Fault::too_deep: [[unlikely]]
        fail(host_, fault_, std::string_view("nesting too deep"), size_t{0}, size_t{0});
        break;
      case Fault::over_work: [[unlikely]]
        fail(host_, fault_, std::string_view("the render does more work than max_render_score"), score_, score_max_);
        break;
      case Fault::over_limit:
      case Fault::no_memory: [[unlikely]]
        if (out_.fault != Fault::none)
          fail(host_, fault, std::string_view("render output"), out_.asked, out_.allowed);
        break;
      default:
        break;
    }
    return fault;
  }

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
  HostString<Host> out_;
  std::array<Value, kDepthMax> stack_{};
  int   depth_ = 0;
  size_t score_ = 0;
  size_t score_max_;
  size_t initial_capacity_;
  Fault fault_ = Fault::none;

  void raw(const std::string_view s)
  {
    out_.raw(s);
    if (out_.fault != Fault::none) [[unlikely]] fault_ = out_.fault;
  }

  void escaped(const std::string_view s)
  {
    out_.escaped(s);
    if (out_.fault != Fault::none) [[unlikely]] fault_ = out_.fault;
  }

  void indent_of(const Indent *const ind)
  {
    if (ind == nullptr) return;
    indent_of(ind->outer);
    raw(ind->text);
  }

  void indent_if_pending(Indent *const ind)
  {
    if (ind != nullptr && ind->pending) [[unlikely]] {
      ind->pending = false;
      indent_of(ind);
    }
  }

  void text(const std::string_view all, Indent *const ind)
  {
    if (ind != nullptr) [[unlikely]] {
      std::string_view s = all;
      while (!s.empty()) {
        const size_t nl = s.find('\n');
        const size_t line = nl == std::string_view::npos ? s.size() : nl + 1;
        indent_if_pending(ind);
        raw(s.substr(0, line));
        if (nl != std::string_view::npos) ind->pending = true;
        s.remove_prefix(line);
      }
      return;
    }
    raw(all);
  }

  Value lookup(const Program<Key> &p, const Op &op)
  {
    if (op.b == 0) return stack_.at(depth_ - 1);
    const Key &first = p.keys.at(op.a);
    Value base{};
    for (int i = depth_ - 1; i >= 0; i--) {
      base = find(host_, stack_.at(i), first);
      if (found(host_, base)) break;
    }
    for (uint32_t j = 1; j < op.b && found(host_, base); j++) base = find(host_, base, p.keys.at(op.a + j));
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
      if (fault_ != Fault::none) [[unlikely]] return false;
      const Op &op = p.ops.at(pc);
      switch (op.tag) {
        case Tag::text:
          text(std::string_view(p.texts).substr(op.a, op.b), ind);
          pc++;
          break;
        case Tag::var:
        case Tag::raw: {
          pc++;
          const Value v = lookup(p, op);
          if (!found(host_, v)) break;
          const Kind k = kind_of(host_, v);
          if (k == Kind::falsy) break;
          if (k != Kind::text) [[unlikely]] {
            fault_ = Fault::not_text;
            return false;
          }
          const std::string_view s = text_of(host_, v);
          if (s.empty()) break;
          indent_if_pending(ind);
          if (op.tag == Tag::var) escaped(s);
          else raw(s);
          break;
        }
        case Tag::section: {
          const Value v = lookup(p, op);
          const Kind k = found(host_, v) ? kind_of(host_, v) : Kind::falsy;
          if (k == Kind::list) {
            const size_t n = size_of(host_, v);
            for (size_t i = 0; i < n; i++) {
              if (!push_run(p, element(host_, v, i), pc + 1, op.c, pd, ind, args)) [[unlikely]] return false;
            }
          }
          else if (k != Kind::falsy) {
            if (!push_run(p, v, pc + 1, op.c, pd, ind, args)) [[unlikely]] return false;
          }
          pc = op.c;
          break;
        }
        case Tag::inverted: {
          const Value v = lookup(p, op);
          const Kind k = found(host_, v) ? kind_of(host_, v) : Kind::falsy;
          const bool empty = k == Kind::falsy || (k == Kind::list && size_of(host_, v) == 0);
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
          if (sub == nullptr) [[unlikely]] break;
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
