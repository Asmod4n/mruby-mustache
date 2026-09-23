#include <mustache/render.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/cpp_helpers.hpp>
#include <mruby/data.h>
#include <mruby/hash.h>
#include <mruby/numeric.h>
#include <mruby/presym.h>
#include <mruby/string.h>
#include <mruby/variable.h>

#ifndef MUSTACHE_MAX_DEPTH
#if defined(MRB_HIGH_PROFILE)
#define MUSTACHE_MAX_DEPTH 128
#elif defined(MRB_MAIN_PROFILE)
#define MUSTACHE_MAX_DEPTH 64
#elif defined(MRB_BASELINE_PROFILE)
#define MUSTACHE_MAX_DEPTH 32
#else
#define MUSTACHE_MAX_DEPTH 16
#endif
#endif

#ifndef MUSTACHE_MAX_PARTIAL_DEPTH
#define MUSTACHE_MAX_PARTIAL_DEPTH MUSTACHE_MAX_DEPTH
#endif

#ifndef MUSTACHE_MAX_RENDER_SCORE
#if defined(MRB_HIGH_PROFILE)
#define MUSTACHE_MAX_RENDER_SCORE (size_t{1} << 30)
#elif defined(MRB_MAIN_PROFILE)
#define MUSTACHE_MAX_RENDER_SCORE (size_t{1} << 27)
#else
#define MUSTACHE_MAX_RENDER_SCORE (size_t{1} << 24)
#endif
#endif

struct MrubyHost {
  mrb_state *mrb;
  mrb_value  partials;
};

struct Template {
  mustache::Program<mrb_sym> program;
  mustache::SizeHint         hint;
  size_t                     answer_max;
  size_t                     score_max;
};

MRB_CPP_DEFINE_TYPE(Template, mustache_template)

namespace {

struct RClass *
error_class(mrb_state *mrb, const mrb_sym name)
{
  return mrb_class_get_under_id(mrb, mrb_module_get_id(mrb, MRB_SYM(Mustache)), name);
}

} // namespace

namespace mustache {

inline mrb_sym
tag_invoke(key_of_tag, MrubyHost &h, const std::string_view name)
{
  return mrb_intern(h.mrb, name.data(), name.size());
}

inline std::optional<mrb_value>
tag_invoke(find_tag, MrubyHost &h, const mrb_value map, const mrb_sym key)
{
  if (!mrb_hash_p(map)) return std::nullopt;
  const mrb_value v = mrb_hash_fetch(h.mrb, map, mrb_symbol_value(key), mrb_undef_value());
  if (mrb_undef_p(v)) return std::nullopt;
  return v;
}

inline Kind
tag_invoke(kind_of_tag, MrubyHost &h, const mrb_value v)
{
  switch (mrb_type(v)) {
    case MRB_TT_FALSE:  return Kind::falsy;
    case MRB_TT_STRING: return Kind::text;
    case MRB_TT_ARRAY:  return Kind::list;
    case MRB_TT_HASH:   return mrb_hash_empty_p(h.mrb, v) ? Kind::falsy : Kind::map;
    default:            return Kind::truthy;
  }
}

inline std::string_view
tag_invoke(text_of_tag, MrubyHost &, const mrb_value v)
{
  return {RSTRING_PTR(v), (size_t)RSTRING_LEN(v)};
}

inline size_t
tag_invoke(size_of_tag, MrubyHost &, const mrb_value v)
{
  return (size_t)RARRAY_LEN(v);
}

inline mrb_value
tag_invoke(element_tag, MrubyHost &, const mrb_value v, const size_t i)
{
  return RARRAY_PTR(v)[i];
}

inline const Program<mrb_sym> *
tag_invoke(partial_tag, MrubyHost &h, const mrb_sym name)
{
  if (!mrb_hash_p(h.partials)) return nullptr;
  const mrb_value t = mrb_hash_fetch(h.mrb, h.partials, mrb_symbol_value(name), mrb_undef_value());
  if (!mrb_data_p(t) || DATA_TYPE(t) != mrb_data_type_traits<Template>::get() || DATA_PTR(t) == nullptr) {
    return nullptr;
  }
  return &static_cast<const Template *>(DATA_PTR(t))->program;
}

inline void
tag_invoke(fail_tag, MrubyHost &h, const Fault fault, const std::string_view what, const size_t asked,
           const size_t allowed)
{
  mrb_state *mrb = h.mrb;
  switch (fault) {
    case Fault::none:
      return;
    case Fault::over_limit:
      mrb_raisef(mrb, E_ARGUMENT_ERROR, "%l too long (len=%v max=%v)", what.data(), what.size(),
                 mrb_value_from_size_t(mrb, asked), mrb_value_from_size_t(mrb, allowed));
      break;
    case Fault::no_memory:
      mrb_raisef(mrb, mrb_exc_get_id(mrb, MRB_SYM(NoMemoryError)), "%l: out of memory (len=%v)", what.data(),
                 what.size(), mrb_value_from_size_t(mrb, asked));
      break;
    case Fault::parse:
      mrb_raisef(mrb, error_class(mrb, MRB_SYM(ParseError)), "%l at byte %v", what.data(), what.size(),
                 mrb_value_from_size_t(mrb, asked));
      break;
    case Fault::not_text:
      mrb_exc_raise(mrb, mrb_exc_new(mrb, E_TYPE_ERROR, what.data(), (mrb_int)what.size()));
      break;
    case Fault::over_capacity:
    case Fault::too_deep:
    case Fault::over_work:
      mrb_exc_raise(mrb, mrb_exc_new(mrb, error_class(mrb, MRB_SYM(RenderError)), what.data(), (mrb_int)what.size()));
      break;
  }
}

} // namespace mustache

namespace {

inline constexpr size_t kEscapeCountedPiece = 32;

#if defined(MRB_STR_LENGTH_MAX) && MRB_STR_LENGTH_MAX != 0
inline constexpr size_t kStringMax = (size_t)MRB_STR_LENGTH_MAX - 1;
#else
inline constexpr size_t kStringMax = std::min<size_t>(MRB_FIXNUM_MAX, MRB_SSIZE_MAX) - 1;
#endif
inline constexpr size_t kSourceCeiling = std::min(mustache::kSourceMax, kStringMax);
inline constexpr size_t kAnswerCeiling = kStringMax - mustache::kEscapeSlack;

struct Answer {
  mrb_state *mrb;
  mrb_value  string;
  size_t     max;
  char      *begin = RSTRING_PTR(string);
  char      *w = begin;
  char      *end = begin + RSTRING_CAPA(string);
  size_t     peak = 0;
  bool       full = false;

  size_t length() const { return (size_t)(w - begin); }

  void grow(const size_t more)
  {
    const size_t length_now = length();
    RSTR_SET_LEN(RSTRING(string), (mrb_int)length_now);
    const size_t doubled = std::min(2 * (size_t)(end - begin), max + mustache::kEscapeSlack);
    mrb_str_resize(mrb, string, (mrb_int)std::max(doubled, length_now + more));
    RSTR_SET_LEN(RSTRING(string), (mrb_int)length_now);
    begin = RSTRING_PTR(string);
    w = begin + length_now;
    end = begin + RSTRING_CAPA(string);
  }

  void raw(const std::string_view s)
  {
    if (max - length() < s.size()) [[unlikely]] {
      full = true;
      return;
    }
    if ((size_t)(end - w) < s.size()) [[unlikely]] grow(s.size());
    w = std::copy_n(s.begin(), s.size(), w);
  }

  size_t escape_headroom() const
  {
    return w + mustache::kEscapeSlack < end ? (size_t)(end - w) - mustache::kEscapeSlack : 0;
  }

  void escaped(const std::string_view s)
  {
    if (!mustache::escaped_fits(s, max - length())) [[unlikely]] {
      full = true;
      return;
    }
    std::string_view rest = s;
    while (!rest.empty()) {
      const size_t take = std::min(rest.size(), escape_headroom() / mustache::kEntityMax);
      if (take > 0) [[likely]] {
        w = mustache::escaped_into(w, rest.substr(0, take));
        rest.remove_prefix(take);
        continue;
      }
      const std::string_view piece = rest.substr(0, kEscapeCountedPiece);
      if (mustache::escaped_size_of(piece) > escape_headroom()) {
        grow(rest.size() + mustache::kEscapeSlack);
        continue;
      }
      w = mustache::escaped_into(w, piece);
      rest.remove_prefix(piece.size());
    }
    peak = std::max(peak, length() + mustache::kEscapeSlack);
  }

  mrb_value finished()
  {
    const size_t length_now = length();
    peak = std::max(peak, length_now);
    if ((size_t)(end - w) > std::max(length_now, size_t{256})) [[unlikely]] {
      RSTR_SET_LEN(RSTRING(string), RSTRING_CAPA(string));
      mrb_str_resize(mrb, string, (mrb_int)length_now);
    }
    RSTR_SET_LEN(RSTRING(string), (mrb_int)length_now);
    RSTRING_PTR(string)[length_now] = '\0';
    return string;
  }
};

size_t
size_of_argument(mrb_state *mrb, const mrb_value v)
{
  const mrb_int n = mrb_as_int(mrb, v);
  if (n < 0) mrb_raise(mrb, E_ARGUMENT_ERROR, "a size cannot be negative");
  return (size_t)n;
}

std::optional<size_t>
limit_set_on(mrb_state *mrb, const mrb_value holder, const mrb_sym name)
{
  const mrb_value v = mrb_iv_get(mrb, holder, name);
  if (mrb_nil_p(v)) return std::nullopt;
  return (size_t)mrb_integer(v);
}

size_t
vm_limit(mrb_state *mrb, const mrb_sym name, const size_t ceiling)
{
  const mrb_value module = mrb_obj_value(mrb_module_get_id(mrb, MRB_SYM(Mustache)));
  return std::min(ceiling, limit_set_on(mrb, module, name).value_or(ceiling));
}

size_t
class_limit(mrb_state *mrb, struct RClass *const klass, const mrb_sym name, const size_t ceiling)
{
  size_t limit = vm_limit(mrb, name, ceiling);
  const struct RClass *const base = mrb_class_get_under_id(mrb, mrb_module_get_id(mrb, MRB_SYM(Mustache)), MRB_SYM(Template));
  for (struct RClass *c = klass; c != nullptr; c = c->super) {
    if (c->tt == MRB_TT_ICLASS) continue;
    limit = std::min(limit, limit_set_on(mrb, mrb_obj_value(c), name).value_or(limit));
    if (c == base) break;
  }
  return limit;
}

mrb_value
limit_lowered(mrb_state *mrb, const mrb_value holder, const mrb_sym name, const size_t above)
{
  const mrb_value arg = mrb_get_arg1(mrb);
  const size_t lowered = size_of_argument(mrb, arg);
  MrubyHost host{mrb, mrb_nil_value()};
  mustache::within_limit(host, mrb_sym_name(mrb, name), lowered, above);
  mrb_iv_set(mrb, holder, name, mrb_value_from_size_t(mrb, lowered));
  return arg;
}

size_t
instance_limit(mrb_state *mrb, const mrb_value arg, const mrb_sym name, const size_t above)
{
  if (mrb_nil_p(arg)) return above;
  const size_t lowered = size_of_argument(mrb, arg);
  MrubyHost host{mrb, mrb_nil_value()};
  mustache::within_limit(host, mrb_sym_name(mrb, name), lowered, above);
  return lowered;
}

mrb_value
mustache_source_max(mrb_state *mrb, mrb_value)
{
  return mrb_value_from_size_t(mrb, vm_limit(mrb, MRB_SYM(max_source_bytes), kSourceCeiling));
}

mrb_value
mustache_lower_source_max(mrb_state *mrb, mrb_value self)
{
  return limit_lowered(mrb, self, MRB_SYM(max_source_bytes), vm_limit(mrb, MRB_SYM(max_source_bytes), kSourceCeiling));
}

mrb_value
mustache_score_max(mrb_state *mrb, mrb_value)
{
  return mrb_value_from_size_t(mrb, vm_limit(mrb, MRB_SYM(max_render_score), MUSTACHE_MAX_RENDER_SCORE));
}

mrb_value
mustache_lower_score_max(mrb_state *mrb, mrb_value self)
{
  return limit_lowered(mrb, self, MRB_SYM(max_render_score),
                       vm_limit(mrb, MRB_SYM(max_render_score), MUSTACHE_MAX_RENDER_SCORE));
}

mrb_value
template_class_source_max(mrb_state *mrb, mrb_value self)
{
  return mrb_value_from_size_t(mrb, class_limit(mrb, mrb_class_ptr(self), MRB_SYM(max_source_bytes), kSourceCeiling));
}

mrb_value
template_class_lower_source_max(mrb_state *mrb, mrb_value self)
{
  return limit_lowered(mrb, self, MRB_SYM(max_source_bytes),
                       class_limit(mrb, mrb_class_ptr(self), MRB_SYM(max_source_bytes), kSourceCeiling));
}

mrb_value
template_class_score_max(mrb_state *mrb, mrb_value self)
{
  return mrb_value_from_size_t(mrb,
                               class_limit(mrb, mrb_class_ptr(self), MRB_SYM(max_render_score), MUSTACHE_MAX_RENDER_SCORE));
}

mrb_value
template_class_lower_score_max(mrb_state *mrb, mrb_value self)
{
  return limit_lowered(mrb, self, MRB_SYM(max_render_score),
                       class_limit(mrb, mrb_class_ptr(self), MRB_SYM(max_render_score), MUSTACHE_MAX_RENDER_SCORE));
}

mrb_value
template_initialize(mrb_state *mrb, mrb_value self)
{
  const char *src;
  mrb_int src_len;
  mrb_value bytes_arg = mrb_nil_value();
  mrb_value source_max_arg = mrb_nil_value();
  mrb_value score_max_arg = mrb_nil_value();
  mrb_get_args(mrb, "s|ooo", &src, &src_len, &bytes_arg, &source_max_arg, &score_max_arg);
  struct RClass *const klass = mrb_obj_class(mrb, self);
  const size_t bytes = instance_limit(mrb, bytes_arg, MRB_SYM(bytes), kAnswerCeiling);
  const size_t source_max =
      instance_limit(mrb, source_max_arg, MRB_SYM(max_source_bytes), class_limit(mrb, klass, MRB_SYM(max_source_bytes), kSourceCeiling));
  const size_t score_max = instance_limit(mrb, score_max_arg, MRB_SYM(max_render_score),
                                          class_limit(mrb, klass, MRB_SYM(max_render_score), MUSTACHE_MAX_RENDER_SCORE));
  MrubyHost host{mrb, mrb_nil_value()};
  std::optional<mustache::Program<mrb_sym>> program =
      mustache::program_of<mrb_sym>(host, std::string_view(src, (size_t)src_len), source_max);
  if (!program) [[unlikely]] return self;
  if (DATA_PTR(self) != nullptr) mrb_cpp_delete(mrb, static_cast<Template *>(DATA_PTR(self)));
  DATA_PTR(self) = nullptr;
  mrb_cpp_new<Template>(mrb, self, std::move(*program), mustache::SizeHint{}, bytes, score_max);
  return self;
}

mrb_value
template_compile(mrb_state *mrb, mrb_value self)
{
  return mrb_obj_new(mrb, mrb_class_ptr(self), mrb_get_argc(mrb), mrb_get_argv(mrb));
}

mrb_value
template_render(mrb_state *mrb, mrb_value self)
{
  mrb_value ctx = mrb_nil_value();
  mrb_value partials = mrb_nil_value();
  mrb_get_args(mrb, "|oo", &ctx, &partials);
  if (!mrb_nil_p(partials) && !mrb_hash_p(partials)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "partials must be a Hash or nil");
  }
  Template *const t = mrb_cpp_get<Template>(mrb, self);
  if (t == nullptr) mrb_raise(mrb, error_class(mrb, MRB_SYM(RenderError)), "uninitialized Mustache::Template");
  MrubyHost host{mrb, partials};
  Answer answer{mrb, mrb_str_new_capa(mrb, (mrb_int)std::min(t->hint.get(), t->answer_max + mustache::kEscapeSlack)),
                t->answer_max};
  mustache::Walk<MrubyHost, mrb_value, mrb_sym, Answer, MUSTACHE_MAX_DEPTH, MUSTACHE_MAX_PARTIAL_DEPTH> walk(host, answer,
                                                                                                  t->score_max);
  walk.run(t->program, ctx);
  const mrb_value finished = answer.finished();
  t->hint.update(answer.peak);
  return finished;
}

} // namespace

extern "C" void
mrb_mruby_mustache_gem_init(mrb_state *mrb)
{
  struct RClass *m = mrb_define_module_id(mrb, MRB_SYM(Mustache));
  struct RClass *err = mrb_define_class_under_id(mrb, m, MRB_SYM(Error), E_RUNTIME_ERROR);
  mrb_define_class_under_id(mrb, m, MRB_SYM(ParseError), err);
  mrb_define_class_under_id(mrb, m, MRB_SYM(RenderError), err);
  mrb_define_const_id(mrb, m, MRB_SYM(MAX_DEPTH), mrb_fixnum_value(MUSTACHE_MAX_DEPTH));
  mrb_define_const_id(mrb, m, MRB_SYM(MAX_PARTIAL_DEPTH), mrb_fixnum_value(MUSTACHE_MAX_PARTIAL_DEPTH));
  mrb_define_module_function_id(mrb, m, MRB_SYM(max_source_bytes), mustache_source_max, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, m, MRB_SYM_E(max_source_bytes), mustache_lower_source_max, MRB_ARGS_REQ(1));
  mrb_define_module_function_id(mrb, m, MRB_SYM(max_render_score), mustache_score_max, MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, m, MRB_SYM_E(max_render_score), mustache_lower_score_max, MRB_ARGS_REQ(1));
  struct RClass *tmpl = mrb_define_class_under_id(mrb, m, MRB_SYM(Template), mrb->object_class);
  MRB_SET_INSTANCE_TT(tmpl, MRB_TT_DATA);
  mrb_define_class_method_id(mrb, tmpl, MRB_SYM(max_source_bytes), template_class_source_max, MRB_ARGS_NONE());
  mrb_define_class_method_id(mrb, tmpl, MRB_SYM_E(max_source_bytes), template_class_lower_source_max, MRB_ARGS_REQ(1));
  mrb_define_class_method_id(mrb, tmpl, MRB_SYM(max_render_score), template_class_score_max, MRB_ARGS_NONE());
  mrb_define_class_method_id(mrb, tmpl, MRB_SYM_E(max_render_score), template_class_lower_score_max, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, tmpl, MRB_SYM(initialize), template_initialize, MRB_ARGS_ARG(1, 3));
  mrb_define_class_method_id(mrb, tmpl, MRB_SYM(compile), template_compile, MRB_ARGS_ARG(1, 3));
  mrb_define_method_id(mrb, tmpl, MRB_SYM(render), template_render, MRB_ARGS_OPT(2));
}

extern "C" void
mrb_mruby_mustache_gem_final(mrb_state *)
{
}
