#!/usr/bin/env bash
#
# fuzz/run.sh — set up and run libFuzzer for mruby-mustache.
#
# Usage:
#   ./fuzz/run.sh                  # build + fuzz forever
#   ./fuzz/run.sh 300              # build + fuzz for 300 seconds
#   ./fuzz/run.sh build            # build only, don't run
#   ./fuzz/run.sh clean            # remove all generated artifacts
#
# Run from the repo root or from inside fuzz/ — either works.

set -euo pipefail

# --- locate repo root ------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -d "$SCRIPT_DIR/../mruby" ] && [ -f "$SCRIPT_DIR/../mrbgem.rake" ]; then
  ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
elif [ -d "$SCRIPT_DIR/mruby" ] && [ -f "$SCRIPT_DIR/mrbgem.rake" ]; then
  ROOT="$SCRIPT_DIR"
else
  echo "error: can't find repo root (need mrbgem.rake and mruby/ nearby)" >&2
  exit 1
fi

FUZZ_DIR="$ROOT/fuzz"
mkdir -p "$FUZZ_DIR"

# --- clean -----------------------------------------------------------------

if [ "${1:-}" = "clean" ]; then
  rm -rf "$FUZZ_DIR/fuzz_mustache" "$FUZZ_DIR/corpus" "$FUZZ_DIR/build_config.rb" \
         "$FUZZ_DIR/mustache.dict" "$FUZZ_DIR/fuzz_mustache.c" \
         "$ROOT/mruby/build/fuzz" 2>/dev/null || true
  rm -f "$ROOT"/crash-* "$ROOT"/leak-* "$ROOT"/oom-* "$ROOT"/timeout-* 2>/dev/null || true
  echo "cleaned."
  exit 0
fi

# --- write harness ---------------------------------------------------------

cat > "$FUZZ_DIR/fuzz_mustache.c" <<'HARNESS_EOF'
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
HARNESS_EOF

# --- write dictionary ------------------------------------------------------

cat > "$FUZZ_DIR/mustache.dict" <<'DICT_EOF'
"{{"
"}}"
"{{{"
"}}}"
"{{#"
"{{^"
"{{/"
"{{!"
"{{>"
"{{&"
"{{."
"{{ "
" }}"
"\x0a"
"\x0d\x0a"
"."
"="
"&amp;"
"&lt;"
"&gt;"
"&quot;"
"&#39;"
DICT_EOF

# --- write seed corpus -----------------------------------------------------

mkdir -p "$FUZZ_DIR/corpus"

# Helper: seed name, template string, data string. Writes a 2-byte LE
# template length prefix, the template bytes, then the data bytes.
write_seed() {
  local name="$1" tmpl="$2" data="$3"
  local tlen=${#tmpl}
  {
    printf '%b' "$(printf '\\x%02x\\x%02x' $((tlen & 0xff)) $(((tlen >> 8) & 0xff)))"
    printf '%s%s' "$tmpl" "$data"
  } > "$FUZZ_DIR/corpus/$name"
}

write_seed plain         'hello world'                                ''
write_seed simple_var    'Hi {{name}}!'                               'alice'
write_seed triple        'raw: {{{name}}}'                            '<x>'
write_seed section_arr   '{{#items}}{{i}},{{/items}}'                 'abcdef'
write_seed inverted      '{{^flag}}no{{/flag}}'                       ''
write_seed dotted        '{{obj.n}}'                                  'deep'
write_seed comment       'a{{! ignore me }}b'                         ''
write_seed standalone    $'  {{! lone }}\nend'                        ''
write_seed nested_sec    '{{#items}}[{{#items}}.{{/items}}]{{/items}}' 'xx'
write_seed partial       '{{>p}}'                                     'foo'
write_seed deep_partial  '{{>deep}}'                                  'abc'
write_seed broken_open   '{{unclosed'                                 ''
write_seed broken_close  'no opener {{/x}}'                           ''
write_seed broken_triple '{{{not closed'                              ''
write_seed empty_tag     '{{   }}'                                    ''
write_seed empty_partial '{{>}}'                                      ''
write_seed deep_dotted   '{{a.b.c.d.e.f.g.h}}'                        ''
write_seed indent_part   $'  {{>p}}\n'                                'x'

# --- write build config ----------------------------------------------------

cat > "$FUZZ_DIR/build_config.rb" <<RUBY_EOF
MRuby::Build.new('fuzz') do |conf|
  toolchain :clang
  conf.gembox 'default'
  conf.gem '$ROOT'
  conf.cc.flags << ENV['CFLAGS'].to_s.split(' ')
  conf.linker.flags << ENV['LDFLAGS'].to_s.split(' ')
  conf.enable_debug
end
RUBY_EOF

# --- build -----------------------------------------------------------------

export CC="${CC:-clang}"
SAN="-fsanitize=address,undefined,fuzzer-no-link -fno-omit-frame-pointer -g -O1"
export CFLAGS="$SAN"
export LDFLAGS="$SAN"

echo "===> building mruby with libfuzzer + asan + ubsan"
( cd "$ROOT/mruby" && rake MRUBY_CONFIG="$FUZZ_DIR/build_config.rb" )

LIBMRUBY="$ROOT/mruby/build/fuzz/lib/libmruby.a"
if [ ! -f "$LIBMRUBY" ]; then
  echo "error: libmruby.a not found at $LIBMRUBY" >&2
  exit 1
fi

echo "===> linking harness"
"$CC" \
  -fsanitize=address,undefined,fuzzer \
  -fno-omit-frame-pointer -g -O1 \
  -I "$ROOT/mruby/include" \
  -I "$ROOT/mruby/build/fuzz/include" \
  -o "$FUZZ_DIR/fuzz_mustache" \
  "$FUZZ_DIR/fuzz_mustache.c" \
  "$LIBMRUBY" \
  -lm

echo "built: $FUZZ_DIR/fuzz_mustache"

# --- run -------------------------------------------------------------------

if [ "${1:-}" = "build" ]; then
  echo "(build only — skipping run)"
  exit 0
fi

cd "$ROOT"

if [ -n "${1:-}" ] && [ "$1" -eq "$1" ] 2>/dev/null; then
  TIME_LIMIT="$1"
  echo "===> fuzzing for ${TIME_LIMIT}s"
  exec "$FUZZ_DIR/fuzz_mustache" \
    "$FUZZ_DIR/corpus" \
    -dict="$FUZZ_DIR/mustache.dict" \
    -max_total_time="$TIME_LIMIT" \
    -print_final_stats=1 \
    -rss_limit_mb=2048
else
  echo "===> fuzzing (forever — Ctrl-C to stop)"
  exec "$FUZZ_DIR/fuzz_mustache" \
    "$FUZZ_DIR/corpus" \
    -dict="$FUZZ_DIR/mustache.dict" \
    -rss_limit_mb=2048
fi