#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#include "render.hpp"

namespace mustache {

enum class Request : uint8_t { find, kind, text, size, element, partial, new_string, grow, done, finished, failed };

template <class Value>
struct Ask {
  Request          request;
  Value            value{};
  std::string_view text{};
  size_t           size = 0;
  size_t           capacity = 0;
};

template <class Value, class Key, int kDepthMax = kMaxDepth, int kPartialDepthMax = kMaxPartialDepth>
class Steps {
public:
  size_t score_max = kMaxRenderScore;
  size_t initial_capacity = kInitialCapacity;
  size_t max_capacity = kMaxCapacity;

  void start(const Program<Key> &p, const Value root)
  {
    stack_.at(0) = root;
    depth_ = 1;
    frames_count_ = 0;
    score_ = 0;
    fault_ = Fault::none;
    what_ = {};
    asked_ = 0;
    allowed_ = 0;
    bytes_ = {};
    open_ = false;
    size_ = 0;
    lookup_ = Lookup::idle;
    state_ = State::opening;
    waiting_ = false;
    entered(p, 0, (uint32_t)p.ops.size(), 0, nullptr, {}, nullptr);
  }

  Ask<Value> next()
  {
    switch (state_) {
      case State::opening:
        if (!waiting_) {
          waiting_ = true;
          requested_ = initial_capacity + kEscapeSlack;
          return {Request::new_string, {}, {}, 0, requested_};
        }
        waiting_ = false;
        state_ = open_ ? State::running : State::failed;
        return next();
      case State::running: {
        const std::optional<Ask<Value>> ask = ran();
        if (ask) return *ask;
        state_ = State::closing;
        return {Request::done, {}, {}, size_, 0};
      }
      case State::closing:
        state_ = fault_ == Fault::none ? State::idle : State::failed;
        return next();
      case State::failed:
        state_ = State::idle;
        return {Request::failed};
      case State::idle:
        return {Request::finished};
    }
    return {Request::finished};
  }

  void found(const std::optional<Value> v) { answer_value_ = v; }

  void kind_is(const Kind k) { answer_kind_ = k; }

  void text_is(const std::string_view s) { answer_text_ = s; }

  void size_is(const size_t n) { answer_size_ = n; }

  void partial_is(const Program<Key> *const p) { answer_partial_ = p; }

  void string_is(const std::span<char> bytes)
  {
    if (bytes.data() != nullptr) open_ = true;
    if (bytes.size() < requested_) [[unlikely]] {
      failed(Fault::no_memory, "render output", requested_ - kEscapeSlack, 0);
      return;
    }
    bytes_ = bytes;
  }

  Fault fault() const { return fault_; }

  std::string_view what() const { return what_; }

  bool is_running() const { return state_ != State::idle; }

private:
  enum class State : uint8_t { idle, opening, running, closing, failed };

  enum class Lookup : uint8_t { idle, searching, chaining };

  enum class Phase : uint8_t { start, kind_known, text_known, writing, size_known, element_known, body_ran, partial_known };

  struct Indent {
    Indent          *outer;
    std::string_view text;
    bool             pending;
  };

  struct Args {
    const Program<Key> *program;
    uint32_t            arguments;
    const Args         *outer;
  };

  struct Argument {
    const Program<Key> *program;
    uint32_t            at;
  };

  struct Frame {
    const Program<Key> *program;
    uint32_t            pc;
    uint32_t            stop;
    int                 pd;
    Indent             *ind;
    const Args         *args;
    Indent              inner;
    Args                frame_args;
    bool                owns_inner;
    bool                pushed;
    Phase               phase;
    Kind                kind;
    std::optional<Value> value;
    size_t              count;
    size_t              index;
    std::string_view    text;
  };

  std::array<Value, kDepthMax>                             stack_{};
  std::array<Frame, kDepthMax + kPartialDepthMax + 1>      frames_{};
  int                                                      depth_ = 0;
  size_t                                                   frames_count_ = 0;
  size_t                                                   score_ = 0;
  Fault                                                    fault_ = Fault::none;
  std::string_view                                         what_;
  size_t                                                   asked_ = 0;
  size_t                                                   allowed_ = 0;
  std::span<char>                                          bytes_;
  bool                                                     open_ = false;
  size_t                                                   size_ = 0;
  State                                                    state_ = State::idle;
  bool                                                     waiting_ = false;
  Lookup                                                   lookup_ = Lookup::idle;
  int                                                      lookup_at_ = 0;
  uint32_t                                                 lookup_segment_ = 0;
  std::optional<Value>                                     lookup_base_;
  std::optional<Value>                                     answer_value_;
  Kind                                                     answer_kind_ = Kind::falsy;
  std::string_view                                         answer_text_;
  size_t                                                   answer_size_ = 0;
  const Program<Key>                                      *answer_partial_ = nullptr;
  size_t                                                   requested_ = 0;

  void failed(const Fault fault, const std::string_view what, const size_t asked, const size_t allowed)
  {
    fault_ = fault;
    what_ = what;
    asked_ = asked;
    allowed_ = allowed;
  }

  size_t capacity() const { return bytes_.size() - kEscapeSlack; }

  std::optional<Ask<Value>> grown(const size_t needed)
  {
    if (needed > max_capacity) [[unlikely]] {
      failed(Fault::over_limit, "render output", needed, max_capacity);
      return std::nullopt;
    }
    const size_t target = std::min(std::max(capacity() * 2, needed), max_capacity);
    requested_ = target + kEscapeSlack;
    return Ask<Value>{Request::grow, {}, {}, size_, requested_};
  }

  bool entered(const Program<Key> &p, const uint32_t from, const uint32_t stop, const int pd, Indent *const ind,
               const std::string_view indent, const Args *const args)
  {
    const size_t work = size_t{stop - from} + 1;
    if (score_max - score_ < work) [[unlikely]] {
      score_ += work;
      failed(Fault::over_work, "the render does more work than max_render_score", score_, score_max);
      return false;
    }
    score_ += work;
    Frame &f = frames_.at(frames_count_++);
    f = Frame{&p, from, stop, pd, ind, args, {}, {}, false, false, Phase::start, Kind::falsy, {}, 0, 0, {}};
    if (!indent.empty()) {
      f.inner = Indent{ind, indent, true};
      f.ind = &f.inner;
      f.owns_inner = true;
    }
    return true;
  }

  bool pushed(const Program<Key> &p, const Value v, const uint32_t from, const uint32_t stop, const int pd,
              Indent *const ind, const Args *const args)
  {
    if (depth_ >= kDepthMax) [[unlikely]] {
      failed(Fault::too_deep, "nesting too deep", 0, 0);
      return false;
    }
    stack_.at(depth_++) = v;
    if (!entered(p, from, stop, pd, ind, {}, args)) [[unlikely]] return false;
    frames_.at(frames_count_ - 1).pushed = true;
    return true;
  }

  void left()
  {
    Frame &f = frames_.at(--frames_count_);
    if (f.owns_inner && f.inner.outer != nullptr) f.inner.outer->pending = f.inner.pending;
    if (f.pushed) depth_--;
  }

  static size_t indent_size_of(const Indent *const ind)
  {
    size_t n = 0;
    for (const Indent *i = ind; i != nullptr; i = i->outer) n += i->text.size();
    return n;
  }

  static size_t pending_size_of(const Indent *const ind)
  {
    if (ind == nullptr || !ind->pending) return 0;
    return indent_size_of(ind);
  }

  void raw(const std::string_view s)
  {
    std::ranges::copy(s, bytes_.subspan(size_).begin());
    size_ += s.size();
  }

  void escaped(const std::string_view s)
  {
    char *const w = bytes_.subspan(size_).data();
    size_ += (size_t)std::distance(w, escaped_into(w, s));
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

  static size_t text_size_of(const std::string_view all, const Indent *const ind)
  {
    if (ind == nullptr) return all.size();
    const size_t indent = indent_size_of(ind);
    bool pending = ind->pending;
    size_t n = 0;
    std::string_view s = all;
    while (!s.empty()) {
      const size_t nl = s.find('\n');
      const size_t line = nl == std::string_view::npos ? s.size() : nl + 1;
      if (pending) n += indent;
      pending = false;
      n += line;
      if (nl != std::string_view::npos) pending = true;
      s.remove_prefix(line);
    }
    return n;
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

  static std::string_view block_name_of(const Program<Key> &p, const Op &op)
  {
    return std::string_view(p.texts).substr(op.a, op.b);
  }

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

  std::optional<Ask<Value>> looked_up(const Program<Key> &p, const Op &op, std::optional<Value> &result)
  {
    if (op.b == 0) {
      result = stack_.at(depth_ - 1);
      return std::nullopt;
    }
    switch (lookup_) {
      case Lookup::idle:
        lookup_at_ = depth_ - 1;
        lookup_ = Lookup::searching;
        return Ask<Value>{Request::find, stack_.at(lookup_at_), p.keys.at(op.a).name};
      case Lookup::searching:
        if (!answer_value_) {
          if (--lookup_at_ >= 0) return Ask<Value>{Request::find, stack_.at(lookup_at_), p.keys.at(op.a).name};
          lookup_ = Lookup::idle;
          result = std::nullopt;
          return std::nullopt;
        }
        lookup_base_ = answer_value_;
        lookup_segment_ = 1;
        lookup_ = Lookup::chaining;
        break;
      case Lookup::chaining:
        lookup_base_ = answer_value_;
        break;
    }
    if (lookup_segment_ < op.b && lookup_base_) {
      return Ask<Value>{Request::find, *lookup_base_, p.keys.at(op.a + lookup_segment_++).name};
    }
    lookup_ = Lookup::idle;
    result = lookup_base_;
    return std::nullopt;
  }

  std::optional<Ask<Value>> ran()
  {
    while (frames_count_ > 0) {
      if (fault_ != Fault::none) [[unlikely]] return std::nullopt;
      Frame &f = frames_.at(frames_count_ - 1);
      if (f.pc >= f.stop) {
        left();
        continue;
      }
      const Program<Key> &p = *f.program;
      const Op &op = p.ops.at(f.pc);
      const std::optional<Ask<Value>> ask = stepped(f, p, op);
      if (ask) return ask;
    }
    return std::nullopt;
  }

  std::optional<Ask<Value>> finished_op(Frame &f, const uint32_t pc)
  {
    f.pc = pc;
    f.phase = Phase::start;
    return std::nullopt;
  }

  std::optional<Ask<Value>> stepped(Frame &f, const Program<Key> &p, const Op &op)
  {
    switch (op.tag) {
      case Tag::text: {
        const std::string_view all = std::string_view(p.texts).substr(op.a, op.b);
        const size_t n = text_size_of(all, f.ind);
        if (size_ + n > capacity()) [[unlikely]] return grown(size_ + n);
        text(all, f.ind);
        return finished_op(f, f.pc + 1);
      }
      case Tag::var:
      case Tag::raw:
        return interpolated(f, p, op);
      case Tag::section:
        return sectioned(f, p, op);
      case Tag::inverted:
        return inverted(f, p, op);
      case Tag::partial:
        return included(f, p, op);
      case Tag::parent:
        return extended(f, p, op);
      case Tag::block:
        return blocked(f, p, op);
    }
    return finished_op(f, f.pc + 1);
  }

  std::optional<Ask<Value>> looked_up_into(Frame &f, const Program<Key> &p, const Op &op)
  {
    std::optional<Value> v;
    const std::optional<Ask<Value>> ask = looked_up(p, op, v);
    if (ask) return ask;
    f.value = v;
    return std::nullopt;
  }

  std::optional<Ask<Value>> interpolated(Frame &f, const Program<Key> &p, const Op &op)
  {
    switch (f.phase) {
      case Phase::start: {
        const std::optional<Ask<Value>> ask = looked_up_into(f, p, op);
        if (ask) return ask;
        if (!f.value) return finished_op(f, f.pc + 1);
        f.phase = Phase::kind_known;
        return Ask<Value>{Request::kind, *f.value};
      }
      case Phase::kind_known:
        if (answer_kind_ == Kind::falsy) return finished_op(f, f.pc + 1);
        if (answer_kind_ != Kind::text) [[unlikely]] {
          failed(Fault::not_text, "a value is not text", 0, 0);
          return std::nullopt;
        }
        f.phase = Phase::text_known;
        return Ask<Value>{Request::text, *f.value};
      case Phase::text_known:
        f.text = answer_text_;
        f.phase = Phase::writing;
        [[fallthrough]];
      case Phase::writing: {
        const std::string_view s = f.text;
        if (s.empty()) return finished_op(f, f.pc + 1);
        const size_t indent = pending_size_of(f.ind);
        const size_t room = capacity() - size_;
        const bool fits = indent <= room && (op.tag == Tag::var ? escaped_fits(s, room - indent) : s.size() <= room - indent);
        if (!fits) [[unlikely]] return grown(size_ + indent + (op.tag == Tag::var ? escaped_size_of(s) : s.size()));
        indent_if_pending(f.ind);
        if (op.tag == Tag::var) escaped(s);
        else raw(s);
        return finished_op(f, f.pc + 1);
      }
      default:
        return finished_op(f, f.pc + 1);
    }
  }

  std::optional<Ask<Value>> sectioned(Frame &f, const Program<Key> &p, const Op &op)
  {
    switch (f.phase) {
      case Phase::start: {
        const std::optional<Ask<Value>> ask = looked_up_into(f, p, op);
        if (ask) return ask;
        if (!f.value) return finished_op(f, op.c);
        f.phase = Phase::kind_known;
        return Ask<Value>{Request::kind, *f.value};
      }
      case Phase::kind_known:
        f.kind = answer_kind_;
        if (f.kind == Kind::falsy) return finished_op(f, op.c);
        if (f.kind == Kind::list) {
          f.phase = Phase::size_known;
          return Ask<Value>{Request::size, *f.value};
        }
        f.phase = Phase::body_ran;
        pushed(p, *f.value, f.pc + 1, op.c, f.pd, f.ind, f.args);
        return std::nullopt;
      case Phase::size_known:
        f.count = answer_size_;
        f.index = 0;
        break;
      case Phase::element_known:
        f.phase = Phase::body_ran;
        pushed(p, answer_value_.value_or(Value{}), f.pc + 1, op.c, f.pd, f.ind, f.args);
        return std::nullopt;
      case Phase::body_ran:
        if (f.kind != Kind::list) return finished_op(f, op.c);
        f.index++;
        break;
      default:
        return finished_op(f, op.c);
    }
    if (f.index >= f.count) return finished_op(f, op.c);
    f.phase = Phase::element_known;
    return Ask<Value>{Request::element, *f.value, {}, f.index};
  }

  std::optional<Ask<Value>> inverted(Frame &f, const Program<Key> &p, const Op &op)
  {
    bool empty = true;
    switch (f.phase) {
      case Phase::start: {
        const std::optional<Ask<Value>> ask = looked_up_into(f, p, op);
        if (ask) return ask;
        if (!f.value) break;
        f.phase = Phase::kind_known;
        return Ask<Value>{Request::kind, *f.value};
      }
      case Phase::kind_known:
        f.kind = answer_kind_;
        if (f.kind == Kind::list) {
          f.phase = Phase::size_known;
          return Ask<Value>{Request::size, *f.value};
        }
        empty = f.kind == Kind::falsy;
        break;
      case Phase::size_known:
        empty = answer_size_ == 0;
        break;
      default:
        return finished_op(f, op.c);
    }
    if (!empty) return finished_op(f, op.c);
    f.phase = Phase::body_ran;
    pushed(p, stack_.at(depth_ - 1), f.pc + 1, op.c, f.pd, f.ind, f.args);
    return std::nullopt;
  }

  std::optional<Ask<Value>> ran_indented(Frame &f, const Program<Key> &p, const uint32_t from, const uint32_t stop,
                                         const std::string_view indent, const Args *const args)
  {
    if (indent.empty()) {
      const size_t n = pending_size_of(f.ind);
      if (size_ + n > capacity()) [[unlikely]] return grown(size_ + n);
      indent_if_pending(f.ind);
    }
    f.phase = Phase::body_ran;
    if (!entered(p, from, stop, f.pd + 1, f.ind, indent, args)) [[unlikely]] return std::nullopt;
    return std::nullopt;
  }

  std::optional<Ask<Value>> included(Frame &f, const Program<Key> &p, const Op &op)
  {
    switch (f.phase) {
      case Phase::start:
        if (f.pd >= kPartialDepthMax) [[unlikely]] {
          failed(Fault::too_deep, "nesting too deep", 0, 0);
          return std::nullopt;
        }
        f.phase = Phase::partial_known;
        return Ask<Value>{Request::partial, {}, p.keys.at(op.a).name};
      case Phase::partial_known:
        if (answer_partial_ == nullptr) [[unlikely]] return finished_op(f, f.pc + 1);
        return ran_indented(f, *answer_partial_, 0, (uint32_t)answer_partial_->ops.size(),
                            std::string_view(p.texts).substr(op.c, op.b), nullptr);
      default:
        return finished_op(f, f.pc + 1);
    }
  }

  std::optional<Ask<Value>> extended(Frame &f, const Program<Key> &p, const Op &op)
  {
    switch (f.phase) {
      case Phase::start:
        if (f.pd >= kPartialDepthMax) [[unlikely]] {
          failed(Fault::too_deep, "nesting too deep", 0, 0);
          return std::nullopt;
        }
        f.phase = Phase::partial_known;
        return Ask<Value>{Request::partial, {}, p.keys.at(op.a).name};
      case Phase::partial_known: {
        const Program<Key> *const sub = answer_partial_;
        if (sub == nullptr) return finished_op(f, op.c);
        f.frame_args = Args{&p, op.e, f.args};
        return ran_indented(f, *sub, 0, (uint32_t)sub->ops.size(), std::string_view(p.texts).substr(op.d, op.b),
                            &f.frame_args);
      }
      default:
        return finished_op(f, op.c);
    }
  }

  std::optional<Ask<Value>> blocked(Frame &f, const Program<Key> &p, const Op &op)
  {
    switch (f.phase) {
      case Phase::start: {
        if (f.pd >= kPartialDepthMax) [[unlikely]] {
          failed(Fault::too_deep, "nesting too deep", 0, 0);
          return std::nullopt;
        }
        const std::optional<Argument> found = argument_of(f.args, block_name_of(p, op));
        const Program<Key> &body = found ? *found->program : p;
        const uint32_t first = found ? found->at + 1 : f.pc + 1;
        const uint32_t last = found ? body.ops.at(found->at).c : op.c;
        return ran_indented(f, body, first, last, std::string_view(p.texts).substr(op.e, op.d), f.args);
      }
      default:
        return finished_op(f, op.c);
    }
  }
};

}
