#include <mustache/compile.hpp>
#include <mustache/out.hpp>

#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include <mruby.h>
#include <mruby/string.h>

namespace {

// The size of the texts a source compiles to, or nil when the compiler
// refuses it. The texts hold every run of the source and every indent a
// standalone tag carries; a test holds them to the size of the source.
mrb_value
compiled_texts_size(mrb_state *mrb, mrb_value)
{
  const char *src;
  mrb_int len;
  mrb_get_args(mrb, "s", &src, &len);
  const std::variant<mustache::Compiled, mustache::Refusal> c = mustache::compile(std::string_view(src, (size_t)len));
  if (std::holds_alternative<mustache::Refusal>(c)) return mrb_nil_value();
  return mrb_int_value(mrb, (mrb_int)std::get<mustache::Compiled>(c).texts.size());
}

// Escapes a value through an Out made by out_over on a heap buffer of
// exactly the given size, and gives back what it wrote, or nil when the
// Out said it is full. The buffer has no bytes after it that ASan would
// let the escape write into.
mrb_value
escaped_through_out_over(mrb_state *mrb, mrb_value)
{
  const char *s;
  mrb_int len;
  mrb_int size;
  mrb_get_args(mrb, "si", &s, &len, &size);
  std::vector<char> buffer((size_t)size);
  mustache::Out out = mustache::out_over(buffer);
  out.escaped(std::string_view(s, (size_t)len));
  if (out.full) return mrb_nil_value();
  return mrb_str_new(mrb, buffer.data(), (mrb_int)(out.w - buffer.data()));
}

}

extern "C" void
mrb_mruby_mustache_gem_test(mrb_state *mrb)
{
  struct RClass *t = mrb_define_module(mrb, "MustacheTest");
  mrb_define_module_function(mrb, t, "compiled_texts_size", compiled_texts_size, MRB_ARGS_REQ(1));
  mrb_define_module_function(mrb, t, "escaped_through_out_over", escaped_through_out_over, MRB_ARGS_REQ(2));
}
