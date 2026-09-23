#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/error.h>
#include <mruby/hash.h>
#include <mruby/presym.h>
#include <mruby/string.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string_view>

namespace {

mrb_state *mrb = nullptr;

mrb_value
data_of(const std::string_view bytes)
{
  const mrb_value h = mrb_hash_new(mrb);
  const mrb_value text = mrb_str_new(mrb, bytes.data(), (mrb_int)bytes.size());
  mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "name")), text);
  mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "value")), text);
  const mrb_value nested = mrb_hash_new(mrb);
  mrb_hash_set(mrb, nested, mrb_symbol_value(mrb_intern_lit(mrb, "n")), text);
  mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "obj")), nested);
  const mrb_value items = mrb_ary_new(mrb);
  for (size_t i = 0; i < std::min(bytes.size(), size_t{8}); i++) {
    const mrb_value entry = mrb_hash_new(mrb);
    mrb_hash_set(mrb, entry, mrb_symbol_value(mrb_intern_lit(mrb, "i")), mrb_str_new(mrb, bytes.data() + i, 1));
    mrb_ary_push(mrb, items, entry);
  }
  mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "items")), items);
  mrb_hash_set(mrb, h, mrb_symbol_value(mrb_intern_lit(mrb, "flag")),
               mrb_bool_value(!bytes.empty() && (bytes.front() & 1)));
  return h;
}

mrb_value
partials_of(const mrb_value template_class)
{
  struct Seed {
    const char *name;
    const char *source;
  };
  constexpr std::array<Seed, 5> seeds{{
      {"p", "[{{name}}]"},
      {"deep", "{{#items}}{{i}}{{/items}}"},
      {"self", "{{#flag}}{{>self}}{{/flag}}"},
      {"indent", "line1\nline2\n"},
      {"layout", "<{{$body}}default{{/body}}>"},
  }};
  const mrb_value partials = mrb_hash_new(mrb);
  for (const Seed &seed : seeds) {
    const mrb_value source = mrb_str_new_cstr(mrb, seed.source);
    const mrb_value t = mrb_obj_new(mrb, mrb_class_ptr(template_class), 1, &source);
    mrb_hash_set(mrb, partials, mrb_symbol_value(mrb_intern_cstr(mrb, seed.name)), t);
  }
  return partials;
}

}

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *const data, const size_t size)
{
  if (mrb == nullptr) mrb = mrb_open();
  if (mrb == nullptr) std::abort();
  const int arena = mrb_gc_arena_save(mrb);

  const std::string_view all(reinterpret_cast<const char *>(data), size);
  const size_t header = std::min(all.size(), size_t{2});
  const size_t template_length =
      header < 2 ? all.size() : std::min<size_t>(size_t{data[0]} | size_t{data[1]} << 8, all.size() - 2);
  const std::string_view source = all.substr(header == 2 ? 2 : 0, template_length);
  const std::string_view rest = all.substr(std::min(all.size(), (header == 2 ? 2 : 0) + template_length));

  const mrb_value mustache = mrb_obj_value(mrb_module_get_id(mrb, MRB_SYM(Mustache)));
  const mrb_value template_class = mrb_obj_value(mrb_class_get_under_id(mrb, mrb_class_ptr(mustache), MRB_SYM(Template)));
  const mrb_value src = mrb_str_new(mrb, source.data(), (mrb_int)source.size());
  const mrb_value ctx = data_of(rest);
  const mrb_value partials = partials_of(template_class);
  mrb_clear_error(mrb);

  const mrb_value t = mrb_funcall_argv(mrb, template_class, MRB_SYM(new), 1, &src);
  if (!mrb_check_error(mrb)) {
    const std::array<mrb_value, 2> args{ctx, partials};
    mrb_funcall_argv(mrb, t, MRB_SYM(render), 2, args.data());
    mrb_clear_error(mrb);
    mrb_funcall_argv(mrb, t, MRB_SYM(render), 1, &ctx);
    mrb_clear_error(mrb);
  }
  mrb_clear_error(mrb);

  const std::array<mrb_value, 4> limited_args{src, mrb_fixnum_value(256), mrb_nil_value(), mrb_fixnum_value(64)};
  const mrb_value limited = mrb_funcall_argv(mrb, template_class, MRB_SYM(new), 4, limited_args.data());
  if (!mrb_check_error(mrb)) {
    const std::array<mrb_value, 2> args{ctx, partials};
    mrb_funcall_argv(mrb, limited, MRB_SYM(render), 2, args.data());
  }
  mrb_clear_error(mrb);

  mrb_gc_arena_restore(mrb, arena);
  return 0;
}
