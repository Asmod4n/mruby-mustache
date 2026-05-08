#include <mruby.h>
#include <mruby/array.h>
#include <mruby/error.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/throw.h>
#include <mruby/variable.h>
#include <mruby/presym.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

static mrb_value
build_data_hash(mrb_state *mrb, const uint8_t *bytes, size_t len)
{
  mrb_value h = mrb_hash_new(mrb);
  mrb_value text = mrb_str_new(mrb, (const char *)bytes, (mrb_int)len);

  mrb_hash_set(mrb, h, mrb_str_new_lit(mrb, "name"),  text);
  mrb_hash_set(mrb, h, mrb_str_new_lit(mrb, "value"), text);

  mrb_value nested = mrb_hash_new(mrb);
  mrb_hash_set(mrb, nested, mrb_str_new_lit(mrb, "n"), text);
  mrb_hash_set(mrb, h, mrb_str_new_lit(mrb, "obj"), nested);

  mrb_value arr = mrb_ary_new(mrb);
  if (len > 0) {
    size_t n = len < 8 ? len : 8;
    for (size_t i = 0; i < n; i++) {
      mrb_value entry = mrb_hash_new(mrb);
      mrb_hash_set(mrb, entry, mrb_str_new_lit(mrb, "i"),
                   mrb_str_new(mrb, (const char *)&bytes[i], 1));
      mrb_ary_push(mrb, arr, entry);
    }
  }
  mrb_hash_set(mrb, h, mrb_str_new_lit(mrb, "items"), arr);
  mrb_hash_set(mrb, h, mrb_str_new_lit(mrb, "flag"),
               (len > 0 && (bytes[0] & 1)) ? mrb_true_value() : mrb_false_value());
  return h;
}

static mrb_value
build_partials(mrb_state *mrb, struct RClass *tmpl)
{
  mrb_value partials = mrb_hash_new(mrb);
  static const struct { const char *name; const char *src; } seeds[] = {
    { "p",      "[{{name}}]" },
    { "deep",   "{{#items}}{{i}}{{/items}}" },
    { "self",   "{{#flag}}{{>self}}{{/flag}}" },
    { "indent", "line1\nline2\n" },
  };
  for (size_t i = 0; i < sizeof(seeds) / sizeof(seeds[0]); i++) {
    mrb_value src = mrb_str_new_cstr(mrb, seeds[i].src);
    mrb_value t = mrb_obj_new(mrb, tmpl, 1, &src);
    mrb_hash_set(mrb, partials, mrb_str_new_cstr(mrb, seeds[i].name), t);
  }
  return partials;
}

int
LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  mrb_state *mrb = mrb_open();
  if (mrb == NULL) return 0;

  size_t tmpl_len;
  const uint8_t *tmpl_bytes;
  const uint8_t *data_bytes;
  size_t data_len;

  if (size < 2) {
    tmpl_len = size; tmpl_bytes = data;
    data_bytes = NULL; data_len = 0;
  } else {
    uint16_t hdr = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    tmpl_len = hdr;
    if (tmpl_len > size - 2) tmpl_len = size - 2;
    tmpl_bytes = data + 2;
    data_bytes = data + 2 + tmpl_len;
    data_len = size - 2 - tmpl_len;
  }

  struct RClass *m = mrb_module_get_id(mrb, MRB_SYM(Mustache));
  if (m == NULL) { mrb_close(mrb); return 0; }
  struct RClass *tmpl_cls = mrb_class_get_under_id(mrb, m, MRB_SYM(Template));
  if (tmpl_cls == NULL) { mrb_close(mrb); return 0; }

  struct mrb_jmpbuf *prev_jmp = mrb->jmp;
  struct mrb_jmpbuf c_jmp;

  MRB_TRY(&c_jmp) {
    mrb->jmp = &c_jmp;
    mrb_value src = mrb_str_new(mrb, (const char *)tmpl_bytes, (mrb_int)tmpl_len);
    mrb_value tmpl = mrb_obj_new(mrb, tmpl_cls, 1, &src);
    mrb_value ctx = build_data_hash(mrb, data_bytes, data_len);
    mrb_value partials = build_partials(mrb, tmpl_cls);

    mrb_value args[2] = { ctx, partials };
    mrb_funcall_argv(mrb, tmpl, MRB_SYM(render), 2, args);

    if (data_len > 0 && (data_bytes[0] & 0x80)) {
      mrb_funcall_argv(mrb, tmpl, MRB_SYM(render), 1, &ctx);
    }

    mrb_value oneshot[2] = { src, ctx };
    mrb_funcall_argv(mrb, mrb_obj_value(m), MRB_SYM(mustache), 2, oneshot);
    mrb->jmp = prev_jmp;
  }
  MRB_CATCH(&c_jmp) { mrb->jmp = prev_jmp; }
  MRB_END_EXC(&c_jmp);

  mrb_close(mrb);
  return 0;
}
