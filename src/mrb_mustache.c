/*
 * mruby-mustache — logic-less template engine for mruby
 */

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/error.h>
#include <mruby/presym.h>
#include <mruby/internal.h>
#include <string.h>

#ifndef MUSTACHE_OUTBUF_STACK
#  define MUSTACHE_OUTBUF_STACK (64 * 1024)
#endif
#ifndef MUSTACHE_MAX_DEPTH
#  define MUSTACHE_MAX_DEPTH 32
#endif
#ifndef MUSTACHE_MAX_PARTIAL_DEPTH
#  define MUSTACHE_MAX_PARTIAL_DEPTH 64
#endif

#define OP_TEXT     0
#define OP_VAR      1
#define OP_RAW      2
#define OP_SECTION  3
#define OP_INVERTED 4
#define OP_CLOSE    5
#define OP_COMMENT  6
#define OP_PARTIAL  7

/* ===================================================================== */

static struct RClass *
mustache_module(mrb_state *mrb)
{
  return mrb_module_get_id(mrb, MRB_SYM(Mustache));
}

static struct RClass *
mustache_err_class(mrb_state *mrb, mrb_sym name)
{
  return mrb_class_get_under_id(mrb, mustache_module(mrb), name);
}

static struct RClass *
template_class(mrb_state *mrb)
{
  return mrb_class_get_under_id(mrb, mustache_module(mrb), MRB_SYM(Template));
}

#define PARSE_ERR(mrb)  mustache_err_class((mrb), MRB_SYM(ParseError))
#define RENDER_ERR(mrb) mustache_err_class((mrb), MRB_SYM(RenderError))

/* ===================================================================== */
/* outbuf                                                                 */
/* ===================================================================== */

typedef struct {
  char     *stack_ptr;
  mrb_int   stack_len;
  mrb_int   stack_capa;
  mrb_value heap_str;
  char     *heap_ptr;
  mrb_int   heap_len;
  mrb_int   heap_capa;
  int       arena_index;
} outbuf_t;

static mrb_int
next_pow2_int(mrb_int x)
{
  if (x < 64) return 64;
  mrb_int r = 1;
  while (r < x) {
    if (r > MRB_INT_MAX / 2) return MRB_INT_MAX;
    r <<= 1;
  }
  return r;
}

static void
outbuf_init(outbuf_t *o, char *initial, mrb_int initial_capa)
{
  o->stack_ptr   = initial;
  o->stack_len   = 0;
  o->stack_capa  = initial_capa;
  o->heap_str    = mrb_undef_value();
  o->heap_ptr    = NULL;
  o->heap_len    = 0;
  o->heap_capa   = 0;
  o->arena_index = 0;
}

static void
outbuf_promote(mrb_state *mrb, outbuf_t *o, mrb_int need)
{
  if (need > MRB_INT_MAX - o->stack_len) {
    mrb_raise(mrb, RENDER_ERR(mrb), "render output too large");
  }
  mrb_int capa = next_pow2_int(o->stack_len + need);
  o->heap_str = mrb_str_new_capa(mrb, capa);
  mrb_gc_register(mrb, o->heap_str);
  o->arena_index = mrb_gc_arena_save(mrb);

  struct RString *s = RSTRING(o->heap_str);
  o->heap_ptr  = RSTR_PTR(s);
  o->heap_capa = RSTR_CAPA(s);

  if (o->stack_len > 0) {
    memcpy(o->heap_ptr, o->stack_ptr, (size_t)o->stack_len);
    o->heap_len = o->stack_len;
  }
}

static void
outbuf_ensure_heap(mrb_state *mrb, outbuf_t *o, mrb_int add)
{
  if (add <= o->heap_capa - o->heap_len) return;

  if (add > MRB_INT_MAX - o->heap_len) {
    mrb_raise(mrb, RENDER_ERR(mrb), "render output too large");
  }
  mrb_int capa = next_pow2_int(o->heap_len + add);
  mrb_str_resize(mrb, o->heap_str, capa);

  struct RString *s = RSTRING(o->heap_str);
  o->heap_ptr  = RSTR_PTR(s);
  o->heap_capa = RSTR_CAPA(s);
}

static inline void
outbuf_append(mrb_state *mrb, outbuf_t *o, const char *src, mrb_int n)
{
  if (n <= 0) return;

  if (mrb_undef_p(o->heap_str)) {
    if (n <= o->stack_capa - o->stack_len) {
      memcpy(o->stack_ptr + o->stack_len, src, (size_t)n);
      o->stack_len += n;
      return;
    }
    outbuf_promote(mrb, o, n);
  }

  outbuf_ensure_heap(mrb, o, n);
  memcpy(o->heap_ptr + o->heap_len, src, (size_t)n);
  o->heap_len += n;
  mrb_gc_arena_restore(mrb, o->arena_index);
}

static mrb_value
outbuf_finalize(mrb_state *mrb, outbuf_t *o)
{
  if (mrb_undef_p(o->heap_str)) {
    return mrb_str_new(mrb, o->stack_ptr, o->stack_len);
  }

  struct RString *s = RSTRING(o->heap_str);
  RSTR_SET_LEN(s, o->heap_len);
  o->heap_ptr[o->heap_len] = '\0';
  mrb_gc_unregister(mrb, o->heap_str);
  return o->heap_str;
}

/* ===================================================================== */
/* indent state                                                           */
/* ===================================================================== */

typedef struct {
  const char *bytes;
  mrb_int     len;
  int         pending;
} indent_t;

static inline void
emit_indent_if_pending(mrb_state *mrb, outbuf_t *out, indent_t *ind)
{
  if (ind && ind->pending && ind->len > 0) {
    outbuf_append(mrb, out, ind->bytes, ind->len);
    ind->pending = 0;
  } else if (ind) {
    ind->pending = 0;
  }
}

static inline void
emit_value_bytes(mrb_state *mrb, outbuf_t *out, indent_t *ind,
                 const char *src, mrb_int n)
{
  if (n <= 0) return;
  emit_indent_if_pending(mrb, out, ind);
  outbuf_append(mrb, out, src, n);
}

static void
emit_text_bytes(mrb_state *mrb, outbuf_t *out, indent_t *ind,
                const char *src, mrb_int n)
{
  if (n <= 0) return;

  if (!ind || ind->len == 0) {
    outbuf_append(mrb, out, src, n);
    return;
  }

  mrb_int start = 0;
  for (mrb_int i = 0; i < n; i++) {
    if (src[i] == '\n') {
      if (ind->pending) {
        outbuf_append(mrb, out, ind->bytes, ind->len);
        ind->pending = 0;
      }
      outbuf_append(mrb, out, src + start, i + 1 - start);
      start = i + 1;
      ind->pending = 1;
    }
  }
  if (start < n) {
    if (ind->pending) {
      outbuf_append(mrb, out, ind->bytes, ind->len);
      ind->pending = 0;
    }
    outbuf_append(mrb, out, src + start, n - start);
  }
}

/* ===================================================================== */
/* helpers                                                                */
/* ===================================================================== */

static inline int
is_space_byte(unsigned char b)
{
  return b == 32 || b == 9 || b == 10 || b == 13 || b == 11 || b == 12;
}

static inline int
is_ws_inline(unsigned char b)
{
  return b == 32 || b == 9 || b == 13;
}

static int
ws_only(const char *p, mrb_int lo, mrb_int hi)
{
  for (mrb_int i = lo; i < hi; i++) {
    if (!is_ws_inline((unsigned char)p[i])) return 0;
  }
  return 1;
}

static mrb_int
byteindex_str(const char *hay, mrb_int hlen,
              const char *needle, mrb_int nlen, mrb_int from)
{
  if (nlen == 0) return from;
  if (from < 0) from = 0;
  if (from + nlen > hlen) return -1;

  const char *start = hay + from;
  const char *limit = hay + hlen - nlen + 1;
  char first = needle[0];
  while (start < limit) {
    const char *q = (const char *)memchr(start, (unsigned char)first,
                                          (size_t)(limit - start));
    if (!q) return -1;
    if (memcmp(q, needle, (size_t)nlen) == 0) return (mrb_int)(q - hay);
    start = q + 1;
  }
  return -1;
}

static mrb_int
byteindex_char(const char *hay, mrb_int hlen, char ch, mrb_int from)
{
  if (from < 0) from = 0;
  if (from >= hlen) return -1;
  const char *q = (const char *)memchr(hay + from, (unsigned char)ch,
                                        (size_t)(hlen - from));
  return q ? (mrb_int)(q - hay) : -1;
}

/* ===================================================================== */
/* hash lookup                                                            */
/* ===================================================================== */

struct hlk_ctx {
  const char *name;
  mrb_int     name_len;
  mrb_value   result;
  int         found;
};

static int
hlk_match(mrb_state *mrb, mrb_value key, mrb_value val, void *data)
{
  struct hlk_ctx *c = (struct hlk_ctx *)data;
  mrb_value s = mrb_obj_as_string(mrb, key);
  if (RSTRING_LEN(s) == c->name_len &&
      memcmp(RSTRING_PTR(s), c->name, (size_t)c->name_len) == 0) {
    c->result = val;
    c->found = 1;
    return 1;
  }
  return 0;
}

static mrb_value
hash_lookup_str(mrb_state *mrb, mrb_value hash, mrb_value name)
{
  if (!mrb_hash_p(hash)) return mrb_nil_value();
  struct hlk_ctx c = {
    RSTRING_PTR(name), RSTRING_LEN(name), mrb_nil_value(), 0
  };
  mrb_hash_foreach(mrb, mrb_hash_ptr(hash), hlk_match, &c);
  return c.found ? c.result : mrb_nil_value();
}

static mrb_value
ctx_lookup(mrb_state *mrb, mrb_value *stack, mrb_int depth, mrb_value key)
{
  mrb_int klen = RARRAY_LEN(key);
  if (klen == 0) return stack[depth - 1];

  mrb_value first = mrb_ary_ref(mrb, key, 0);

  mrb_value base = mrb_nil_value();
  int found = 0;
  for (mrb_int i = depth - 1; i >= 0; i--) {
    if (!mrb_hash_p(stack[i])) continue;
    mrb_value v = hash_lookup_str(mrb, stack[i], first);
    if (!mrb_nil_p(v)) { base = v; found = 1; break; }
  }
  if (!found) return mrb_nil_value();

  for (mrb_int j = 1; j < klen; j++) {
    if (mrb_nil_p(base) || !mrb_hash_p(base)) return mrb_nil_value();
    mrb_value seg = mrb_ary_ref(mrb, key, j);
    base = hash_lookup_str(mrb, base, seg);
    if (mrb_nil_p(base)) return mrb_nil_value();
  }
  return base;
}

/* ===================================================================== */
/* HTML escape                                                            */
/* ===================================================================== */

static void
escape_html_value(mrb_state *mrb, outbuf_t *o, indent_t *ind,
                  const char *src, mrb_int len)
{
  mrb_int i = 0, run_start = 0;
  while (i < len) {
    char c = src[i];
    const char *ent = NULL;
    int el = 0;
    switch (c) {
      case '&':  ent = "&amp;";  el = 5; break;
      case '<':  ent = "&lt;";   el = 4; break;
      case '>':  ent = "&gt;";   el = 4; break;
      case '"':  ent = "&quot;"; el = 6; break;
      case '\'': ent = "&#39;";  el = 5; break;
      default: break;
    }
    if (ent) {
      if (i > run_start) emit_value_bytes(mrb, o, ind, src + run_start, i - run_start);
      emit_value_bytes(mrb, o, ind, ent, el);
      run_start = i + 1;
    }
    i++;
  }
  if (run_start < len) emit_value_bytes(mrb, o, ind, src + run_start, len - run_start);
}

/* ===================================================================== */
/* render                                                                 */
/* ===================================================================== */

static int
section_truthy(mrb_state *mrb, mrb_value v)
{
  if (mrb_nil_p(v) || mrb_false_p(v)) return 0;
  if (mrb_array_p(v) && RARRAY_LEN(v) == 0) return 0;
  if (mrb_hash_p(v)  && mrb_hash_empty_p(mrb, v)) return 0;
  return 1;
}

static void run_ops(mrb_state *mrb, mrb_value ops,
                    mrb_int pc, mrb_int stop,
                    mrb_value *stack, mrb_int *depth,
                    mrb_value partials, int partial_depth,
                    indent_t *ind, outbuf_t *out);

static void
push_or_raise(mrb_state *mrb, mrb_value *stack, mrb_int *depth, mrb_value v)
{
  if (*depth >= MUSTACHE_MAX_DEPTH) {
    mrb_raise(mrb, RENDER_ERR(mrb), "max nesting depth exceeded");
  }
  stack[*depth] = v;
  (*depth)++;
}

static void
render_section(mrb_state *mrb, mrb_value ops,
               mrb_int pc, mrb_int stop,
               mrb_value *stack, mrb_int *depth,
               mrb_value partials, int partial_depth,
               indent_t *ind, outbuf_t *out, mrb_value v)
{
  if (mrb_array_p(v)) {
    mrb_int n = RARRAY_LEN(v);
    if (n == 0) return;
    for (mrb_int i = 0; i < n; i++) {
      mrb_value elem = mrb_ary_ref(mrb, v, i);
      push_or_raise(mrb, stack, depth, elem);
      run_ops(mrb, ops, pc, stop, stack, depth, partials, partial_depth, ind, out);
      (*depth)--;
    }
  }
  else if (mrb_hash_p(v)) {
    if (mrb_hash_empty_p(mrb, v)) return;
    push_or_raise(mrb, stack, depth, v);
    run_ops(mrb, ops, pc, stop, stack, depth, partials, partial_depth, ind, out);
    (*depth)--;
  }
  else if (mrb_nil_p(v) || mrb_false_p(v)) {
    /* skip */
  }
  else {
    push_or_raise(mrb, stack, depth, v);
    run_ops(mrb, ops, pc, stop, stack, depth, partials, partial_depth, ind, out);
    (*depth)--;
  }
}

static void
render_partial(mrb_state *mrb, mrb_value name, mrb_value indent_str,
               mrb_value *stack, mrb_int *depth,
               mrb_value partials, int partial_depth,
               indent_t *outer_ind, outbuf_t *out)
{
  if (!mrb_hash_p(partials)) return;
  if (partial_depth >= MUSTACHE_MAX_PARTIAL_DEPTH) {
    mrb_raise(mrb, RENDER_ERR(mrb), "max partial recursion depth exceeded");
  }
  mrb_value tmpl = hash_lookup_str(mrb, partials, name);
  if (!mrb_obj_is_kind_of(mrb, tmpl, template_class(mrb))) return;
  mrb_value sub = mrb_iv_get(mrb, tmpl, MRB_IVSYM(ops));

  if (outer_ind) emit_indent_if_pending(mrb, out, outer_ind);

  if (RSTRING_LEN(indent_str) == 0) {
    run_ops(mrb, sub, 0, RARRAY_LEN(sub), stack, depth,
            partials, partial_depth + 1, outer_ind, out);
    return;
  }

  indent_t ind = { RSTRING_PTR(indent_str), RSTRING_LEN(indent_str), 1 };
  run_ops(mrb, sub, 0, RARRAY_LEN(sub), stack, depth,
          partials, partial_depth + 1, &ind, out);
}

static void
run_ops(mrb_state *mrb, mrb_value ops,
        mrb_int pc, mrb_int stop,
        mrb_value *stack, mrb_int *depth,
        mrb_value partials, int partial_depth,
        indent_t *ind, outbuf_t *out)
{
  while (pc < stop) {
    mrb_value op = mrb_ary_ref(mrb, ops, pc);
    mrb_int tag = mrb_integer(mrb_ary_ref(mrb, op, 0));

    switch (tag) {
      case OP_TEXT: {
        mrb_value t = mrb_ary_ref(mrb, op, 1);
        emit_text_bytes(mrb, out, ind, RSTRING_PTR(t), RSTRING_LEN(t));
        pc++;
        break;
      }
      case OP_VAR: {
        mrb_value k = mrb_ary_ref(mrb, op, 1);
        mrb_value v = ctx_lookup(mrb, stack, *depth, k);
        if (!mrb_nil_p(v)) {
          mrb_value s = mrb_obj_as_string(mrb, v);
          escape_html_value(mrb, out, ind, RSTRING_PTR(s), RSTRING_LEN(s));
        }
        pc++;
        break;
      }
      case OP_RAW: {
        mrb_value k = mrb_ary_ref(mrb, op, 1);
        mrb_value v = ctx_lookup(mrb, stack, *depth, k);
        if (!mrb_nil_p(v)) {
          mrb_value s = mrb_obj_as_string(mrb, v);
          emit_value_bytes(mrb, out, ind, RSTRING_PTR(s), RSTRING_LEN(s));
        }
        pc++;
        break;
      }
      case OP_SECTION: {
        mrb_value k = mrb_ary_ref(mrb, op, 1);
        mrb_int end_pc = mrb_integer(mrb_ary_ref(mrb, op, 2));
        mrb_value v = ctx_lookup(mrb, stack, *depth, k);
        render_section(mrb, ops, pc + 1, end_pc, stack, depth,
                       partials, partial_depth, ind, out, v);
        pc = end_pc;
        break;
      }
      case OP_INVERTED: {
        mrb_value k = mrb_ary_ref(mrb, op, 1);
        mrb_int end_pc = mrb_integer(mrb_ary_ref(mrb, op, 2));
        mrb_value v = ctx_lookup(mrb, stack, *depth, k);
        if (!section_truthy(mrb, v)) {
          run_ops(mrb, ops, pc + 1, end_pc, stack, depth,
                  partials, partial_depth, ind, out);
        }
        pc = end_pc;
        break;
      }
      case OP_PARTIAL: {
        mrb_value name   = mrb_ary_ref(mrb, op, 1);
        mrb_value indent = mrb_ary_ref(mrb, op, 2);
        render_partial(mrb, name, indent, stack, depth,
                       partials, partial_depth, ind, out);
        pc++;
        break;
      }
      default:
        mrb_raisef(mrb, RENDER_ERR(mrb),
                   "internal: unexpected op tag %d at pc=%d",
                   (int)tag, (int)pc);
    }
  }
}

/* ===================================================================== */
/* compile-time: parse_key, strip_name, keys_equal                        */
/* ===================================================================== */

static mrb_value
parse_key(mrb_state *mrb, mrb_value src, mrb_int off, mrb_int len)
{
  const char *p = RSTRING_PTR(src);
  while (len > 0 && is_space_byte((unsigned char)p[off])) { off++; len--; }
  while (len > 0 && is_space_byte((unsigned char)p[off + len - 1])) len--;

  mrb_value parts = mrb_ary_new(mrb);
  if (len == 1 && p[off] == '.') return parts;
  if (len == 0) mrb_raise(mrb, PARSE_ERR(mrb), "empty key");

  mrb_int seg_start = off;
  mrb_int end = off + len;
  for (mrb_int i = off; i <= end; i++) {
    if (i == end || p[i] == '.') {
      mrb_int seg_len = i - seg_start;
      if (seg_len == 0) mrb_raise(mrb, PARSE_ERR(mrb), "bad key (empty segment)");
      mrb_ary_push(mrb, parts, mrb_str_byte_subseq(mrb, src, seg_start, seg_len));
      seg_start = i + 1;
    }
  }
  return parts;
}

static mrb_value
strip_name(mrb_state *mrb, mrb_value src, const char *p,
           mrb_int off, mrb_int len, mrb_int err_pos)
{
  mrb_int s = off, e = off + len;
  while (s < e && is_space_byte((unsigned char)p[s])) s++;
  while (e > s && is_space_byte((unsigned char)p[e - 1])) e--;
  if (s == e) {
    mrb_raisef(mrb, PARSE_ERR(mrb),
               "empty partial name at byte %d", (int)err_pos);
  }
  return mrb_str_byte_subseq(mrb, src, s, e - s);
}

static int
keys_equal(mrb_state *mrb, mrb_value a, mrb_value b)
{
  if (mrb_obj_equal(mrb, a, b)) return 1;
  mrb_int la = RARRAY_LEN(a), lb = RARRAY_LEN(b);
  if (la != lb) return 0;
  for (mrb_int i = 0; i < la; i++) {
    mrb_value xa = mrb_ary_ref(mrb, a, i);
    mrb_value xb = mrb_ary_ref(mrb, b, i);
    if (!mrb_string_p(xa) || !mrb_string_p(xb)) return 0;
    if (RSTRING_LEN(xa) != RSTRING_LEN(xb)) return 0;
    if (memcmp(RSTRING_PTR(xa), RSTRING_PTR(xb),
               (size_t)RSTRING_LEN(xa)) != 0) return 0;
  }
  return 1;
}

/* ===================================================================== */
/* op constructors                                                        */
/* ===================================================================== */

static void
push_op1(mrb_state *mrb, mrb_value ops, mrb_int tag)
{
  mrb_value op = mrb_ary_new_capa(mrb, 1);
  mrb_ary_push(mrb, op, mrb_int_value(mrb, tag));
  mrb_ary_push(mrb, ops, op);
}

static void
push_op2(mrb_state *mrb, mrb_value ops, mrb_int tag, mrb_value a)
{
  mrb_value op = mrb_ary_new_capa(mrb, 2);
  mrb_ary_push(mrb, op, mrb_int_value(mrb, tag));
  mrb_ary_push(mrb, op, a);
  mrb_ary_push(mrb, ops, op);
}

static void
push_op3(mrb_state *mrb, mrb_value ops, mrb_int tag, mrb_value a, mrb_value b)
{
  mrb_value op = mrb_ary_new_capa(mrb, 3);
  mrb_ary_push(mrb, op, mrb_int_value(mrb, tag));
  mrb_ary_push(mrb, op, a);
  mrb_ary_push(mrb, op, b);
  mrb_ary_push(mrb, ops, op);
}

/* ===================================================================== */
/* tokenize                                                               */
/* ===================================================================== */

static mrb_value
tokenize(mrb_state *mrb, mrb_value src)
{
  mrb_value ops = mrb_ary_new(mrb);
  const char *p = RSTRING_PTR(src);
  mrb_int sz = RSTRING_LEN(src);
  mrb_int pos = 0;

  while (pos < sz) {
    mrb_int open_at = byteindex_str(p, sz, "{{", 2, pos);
    if (open_at < 0) {
      push_op2(mrb, ops, OP_TEXT, mrb_str_byte_subseq(mrb, src, pos, sz - pos));
      break;
    }
    if (open_at > pos) {
      push_op2(mrb, ops, OP_TEXT, mrb_str_byte_subseq(mrb, src, pos, open_at - pos));
    }

    if (open_at + 2 < sz && p[open_at + 2] == '{') {
      mrb_int close_at = byteindex_str(p, sz, "}}}", 3, open_at + 3);
      if (close_at < 0) {
        mrb_raisef(mrb, PARSE_ERR(mrb), "unclosed {{{ at byte %d", (int)open_at);
      }
      push_op2(mrb, ops, OP_RAW,
               parse_key(mrb, src, open_at + 3, close_at - open_at - 3));
      pos = close_at + 3;
      continue;
    }

    mrb_int close_at = byteindex_str(p, sz, "}}", 2, open_at + 2);
    if (close_at < 0) {
      mrb_raisef(mrb, PARSE_ERR(mrb), "unclosed {{ at byte %d", (int)open_at);
    }

    mrb_int body_off = open_at + 2;
    mrb_int body_len = close_at - body_off;
    pos = close_at + 2;

    mrb_int i = 0;
    while (i < body_len && is_space_byte((unsigned char)p[body_off + i])) i++;
    if (i == body_len) {
      mrb_raisef(mrb, PARSE_ERR(mrb), "empty tag at byte %d", (int)open_at);
    }

    char sigil = p[body_off + i];
    mrb_int kstart = body_off + i + 1;
    mrb_int klen = body_len - i - 1;

    switch (sigil) {
    case '!':
      push_op1(mrb, ops, OP_COMMENT);
      break;
    case '#':
      push_op3(mrb, ops, OP_SECTION,  parse_key(mrb, src, kstart, klen), mrb_nil_value());
      break;
    case '^':
      push_op3(mrb, ops, OP_INVERTED, parse_key(mrb, src, kstart, klen), mrb_nil_value());
      break;
    case '>':
      push_op3(mrb, ops, OP_PARTIAL,  strip_name(mrb, src, p, kstart, klen, open_at), mrb_str_new_lit(mrb, ""));
      break;
    case '/':
      push_op2(mrb, ops, OP_CLOSE,    parse_key(mrb, src, kstart, klen));
      break;
    case '&':
      push_op2(mrb, ops, OP_RAW,      parse_key(mrb, src, kstart, klen));
      break;
    default:
      push_op2(mrb, ops, OP_VAR,      parse_key(mrb, src, body_off + i, body_len - i));
    }
  }
  return ops;
}

/* ===================================================================== */
/* strip standalone-line whitespace                                       */
/* ===================================================================== */

static mrb_int
op_tag(mrb_state *mrb, mrb_value op)
{
  return mrb_integer(mrb_ary_ref(mrb, op, 0));
}

static int
is_standalone_eligible_tag(mrb_int tag)
{
  return tag == OP_SECTION || tag == OP_INVERTED ||
         tag == OP_CLOSE   || tag == OP_COMMENT  ||
         tag == OP_PARTIAL;
}

static mrb_value
strip_standalone(mrb_state *mrb, mrb_value ops)
{
  if (RARRAY_LEN(ops) == 0) return ops;

  mrb_int line_start_op = 0;
  mrb_int line_start_inset = 0;

  while (line_start_op < RARRAY_LEN(ops)) {
    mrb_int n = RARRAY_LEN(ops);

    mrb_int end_op = -1, end_inset = -1;
    for (mrb_int j = line_start_op; j < n; j++) {
      mrb_value op = mrb_ary_ref(mrb, ops, j);
      if (op_tag(mrb, op) != OP_TEXT) continue;
      mrb_value t = mrb_ary_ref(mrb, op, 1);
      mrb_int start = (j == line_start_op) ? line_start_inset : 0;
      mrb_int nl = byteindex_char(RSTRING_PTR(t), RSTRING_LEN(t), '\n', start);
      if (nl >= 0) { end_op = j; end_inset = nl; break; }
    }

    mrb_int last_op = (end_op >= 0) ? end_op : (n - 1);
    int eligible = 1, has_standalone = 0;
    mrb_int partial_op_idx = -1;

    for (mrb_int k = line_start_op; k <= last_op; k++) {
      mrb_value op = mrb_ary_ref(mrb, ops, k);
      mrb_int t = op_tag(mrb, op);
      if (t == OP_TEXT) {
        mrb_value tx = mrb_ary_ref(mrb, op, 1);
        mrb_int lo = (k == line_start_op) ? line_start_inset : 0;
        mrb_int hi = (k == end_op && end_inset >= 0) ? end_inset : RSTRING_LEN(tx);
        if (!ws_only(RSTRING_PTR(tx), lo, hi)) { eligible = 0; break; }
      }
      else if (is_standalone_eligible_tag(t)) {
        has_standalone = 1;
        if (t == OP_PARTIAL) partial_op_idx = k;
      }
      else {
        eligible = 0; break;
      }
    }

    if (eligible && has_standalone) {
      if (partial_op_idx >= 0) {
        mrb_value indent = mrb_str_new_lit(mrb, "");
        if (line_start_op < partial_op_idx) {
          mrb_int total_indent = 0;
          for (mrb_int k = line_start_op; k < partial_op_idx; k++) {
            mrb_value op = mrb_ary_ref(mrb, ops, k);
            if (op_tag(mrb, op) == OP_TEXT) {
              mrb_value tx = mrb_ary_ref(mrb, op, 1);
              mrb_int lo = (k == line_start_op) ? line_start_inset : 0;
              mrb_int seg = RSTRING_LEN(tx) - lo;
              if (seg > MRB_INT_MAX - total_indent) {
                mrb_raise(mrb, PARSE_ERR(mrb), "indent too large");
              }
              total_indent += seg;
            }
          }
          if (total_indent > 0) {
            mrb_value buf = mrb_str_new_capa(mrb, total_indent);
            for (mrb_int k = line_start_op; k < partial_op_idx; k++) {
              mrb_value op = mrb_ary_ref(mrb, ops, k);
              if (op_tag(mrb, op) == OP_TEXT) {
                mrb_value tx = mrb_ary_ref(mrb, op, 1);
                mrb_int lo = (k == line_start_op) ? line_start_inset : 0;
                mrb_str_cat(mrb, buf, RSTRING_PTR(tx) + lo, RSTRING_LEN(tx) - lo);
              }
            }
            indent = buf;
          }
        }
        mrb_value pop = mrb_ary_ref(mrb, ops, partial_op_idx);
        mrb_ary_set(mrb, pop, 2, indent);
      }

      for (mrb_int k = line_start_op; k <= last_op; k++) {
        mrb_value op = mrb_ary_ref(mrb, ops, k);
        if (op_tag(mrb, op) != OP_TEXT) continue;
        mrb_value tx = mrb_ary_ref(mrb, op, 1);
        mrb_int lo = (k == line_start_op) ? line_start_inset : 0;
        mrb_value new_text;
        if (k == end_op && end_inset >= 0) {
          mrb_int drop_end = end_inset + 1;
          mrb_int tail_len = RSTRING_LEN(tx) - drop_end;
          new_text = mrb_str_new_capa(mrb, lo + tail_len);
          if (lo > 0) mrb_str_cat(mrb, new_text, RSTRING_PTR(tx), lo);
          if (tail_len > 0) mrb_str_cat(mrb, new_text, RSTRING_PTR(tx) + drop_end, tail_len);
        } else {
          new_text = mrb_str_byte_subseq(mrb, tx, 0, lo);
        }
        mrb_value new_op = mrb_ary_new_capa(mrb, 2);
        mrb_ary_push(mrb, new_op, mrb_int_value(mrb, OP_TEXT));
        mrb_ary_push(mrb, new_op, new_text);
        mrb_ary_set(mrb, ops, k, new_op);
      }
    }

    if (end_op >= 0) {
      if (eligible && has_standalone) {
        mrb_int head_len = (end_op == line_start_op) ? line_start_inset : 0;
        line_start_op = end_op;
        line_start_inset = head_len;
        mrb_value t = mrb_ary_ref(mrb, mrb_ary_ref(mrb, ops, end_op), 1);
        if (RSTRING_LEN(t) <= line_start_inset) {
          line_start_op = end_op + 1;
          line_start_inset = 0;
        }
      } else {
        line_start_op = end_op;
        line_start_inset = end_inset + 1;
        mrb_value t = mrb_ary_ref(mrb, mrb_ary_ref(mrb, ops, end_op), 1);
        if (line_start_inset >= RSTRING_LEN(t)) {
          line_start_op = end_op + 1;
          line_start_inset = 0;
        }
      }
    } else break;
  }

  return ops;
}

/* ===================================================================== */
/* link sections                                                          */
/* ===================================================================== */

static mrb_value
key_to_str(mrb_state *mrb, mrb_value k)
{
  if (RARRAY_LEN(k) == 0) return mrb_str_new_lit(mrb, ".");
  return mrb_ary_join(mrb, k, mrb_str_new_lit(mrb, "."));
}

static mrb_value
link_ops(mrb_state *mrb, mrb_value tokens)
{
  mrb_value out = mrb_ary_new(mrb);
  mrb_value stk = mrb_ary_new(mrb);
  mrb_int n = RARRAY_LEN(tokens);

  for (mrb_int i = 0; i < n; i++) {
    mrb_value op = mrb_ary_ref(mrb, tokens, i);
    mrb_int tag = op_tag(mrb, op);

    if (tag == OP_SECTION || tag == OP_INVERTED) {
      mrb_value entry = mrb_ary_new_capa(mrb, 2);
      mrb_ary_push(mrb, entry, mrb_int_value(mrb, RARRAY_LEN(out)));
      mrb_ary_push(mrb, entry, mrb_ary_ref(mrb, op, 1));
      mrb_ary_push(mrb, stk, entry);
      mrb_ary_push(mrb, out, op);
    }
    else if (tag == OP_CLOSE) {
      if (RARRAY_LEN(stk) == 0) {
        mrb_value k = mrb_ary_ref(mrb, op, 1);
        mrb_raisef(mrb, PARSE_ERR(mrb), "closing tag without opener: %S",
                   key_to_str(mrb, k));
      }
      mrb_value entry = mrb_ary_pop(mrb, stk);
      mrb_int   opener_idx = mrb_integer(mrb_ary_ref(mrb, entry, 0));
      mrb_value opener_key = mrb_ary_ref(mrb, entry, 1);
      mrb_value close_key  = mrb_ary_ref(mrb, op, 1);
      if (!keys_equal(mrb, opener_key, close_key)) {
        mrb_raisef(mrb, PARSE_ERR(mrb),
                   "mismatched: opened %S, closed %S",
                   key_to_str(mrb, opener_key),
                   key_to_str(mrb, close_key));
      }
      mrb_value opener = mrb_ary_ref(mrb, out, opener_idx);
      mrb_ary_set(mrb, opener, 2, mrb_int_value(mrb, RARRAY_LEN(out)));
    }
    else if (tag == OP_COMMENT) {
      /* drop */
    }
    else if (tag == OP_TEXT) {
      mrb_value t = mrb_ary_ref(mrb, op, 1);
      if (RSTRING_LEN(t) == 0) {
        /* drop empty text from standalone strip */
      } else {
        mrb_ary_push(mrb, out, op);
      }
    }
    else {
      mrb_ary_push(mrb, out, op);
    }
  }

  if (RARRAY_LEN(stk) > 0) {
    mrb_value entry = mrb_ary_ref(mrb, stk, RARRAY_LEN(stk) - 1);
    mrb_value k = mrb_ary_ref(mrb, entry, 1);
    mrb_raisef(mrb, PARSE_ERR(mrb), "unclosed section: %S", key_to_str(mrb, k));
  }
  return out;
}

/* ===================================================================== */
/* Template public API                                                    */
/* ===================================================================== */

static mrb_value
template_initialize(mrb_state *mrb, mrb_value self)
{
  mrb_value src;
  mrb_get_args(mrb, "S", &src);
  mrb_value ops = tokenize(mrb, src);
  ops = strip_standalone(mrb, ops);
  ops = link_ops(mrb, ops);
  mrb_iv_set(mrb, self, MRB_IVSYM(ops), ops);
  return self;
}

static mrb_value
template_compile(mrb_state *mrb, mrb_value self)
{
  mrb_value src;
  mrb_get_args(mrb, "S", &src);
  return mrb_obj_new(mrb, mrb_class_ptr(self), 1, &src);
}

static mrb_value
template_render(mrb_state *mrb, mrb_value self)
{
  mrb_value ctx = mrb_nil_value(), partials = mrb_nil_value();
  mrb_get_args(mrb, "|oo", &ctx, &partials);
  if (!mrb_nil_p(partials) && !mrb_hash_p(partials)) {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "partials must be a Hash or nil");
  }

  mrb_value ops = mrb_iv_get(mrb, self, MRB_IVSYM(ops));
  mrb_value stack[MUSTACHE_MAX_DEPTH];
  mrb_int depth = 0;
  stack[depth++] = ctx;

  char stackbuf[MUSTACHE_OUTBUF_STACK];
  outbuf_t out;
  outbuf_init(&out, stackbuf, sizeof(stackbuf));

  run_ops(mrb, ops, 0, RARRAY_LEN(ops), stack, &depth, partials, 0, NULL, &out);
  return outbuf_finalize(mrb, &out);
}

static mrb_value
template_tags(mrb_state *mrb, mrb_value self)
{
  if (mrb_iv_defined(mrb, self, MRB_IVSYM(tags))) {
    return mrb_iv_get(mrb, self, MRB_IVSYM(tags));
  }
  mrb_value ops = mrb_iv_get(mrb, self, MRB_IVSYM(ops));
  mrb_value tags = mrb_ary_new(mrb);
  mrb_value seen = mrb_hash_new(mrb);
  mrb_int n = RARRAY_LEN(ops);
  for (mrb_int i = 0; i < n; i++) {
    mrb_value op = mrb_ary_ref(mrb, ops, i);
    mrb_int t = op_tag(mrb, op);
    if (t == OP_VAR || t == OP_RAW || t == OP_SECTION || t == OP_INVERTED) {
      mrb_value key = mrb_ary_ref(mrb, op, 1);
      mrb_value label = key_to_str(mrb, key);
      if (!mrb_hash_key_p(mrb, seen, label)) {
        mrb_hash_set(mrb, seen, label, mrb_true_value());
        mrb_ary_push(mrb, tags, label);
      }
    }
  }
  mrb_iv_set(mrb, self, MRB_IVSYM(tags), tags);
  return tags;
}

static mrb_value
mustache_one_shot(mrb_state *mrb, mrb_value self)
{
  mrb_value src, ctx = mrb_nil_value(), _flags = mrb_nil_value();
  mrb_get_args(mrb, "S|oo", &src, &ctx, &_flags);
  (void)_flags;
  mrb_value t = mrb_obj_new(mrb, template_class(mrb), 1, &src);
  return mrb_funcall_id(mrb, t, MRB_SYM(render), 1, ctx);
}

/* ===================================================================== */
/* gem init                                                               */
/* ===================================================================== */

void
mrb_mruby_mustache_gem_init(mrb_state *mrb)
{
  struct RClass *m = mrb_define_module_id(mrb, MRB_SYM(Mustache));
  struct RClass *err = mrb_define_class_under_id(mrb, m, MRB_SYM(Error), E_RUNTIME_ERROR);
  mrb_define_class_under_id(mrb, m, MRB_SYM(ParseError),  err);
  mrb_define_class_under_id(mrb, m, MRB_SYM(RenderError), err);

  mrb_define_const_id(mrb, m, MRB_SYM(OP_TEXT),     mrb_int_value(mrb, OP_TEXT));
  mrb_define_const_id(mrb, m, MRB_SYM(OP_VAR),      mrb_int_value(mrb, OP_VAR));
  mrb_define_const_id(mrb, m, MRB_SYM(OP_RAW),      mrb_int_value(mrb, OP_RAW));
  mrb_define_const_id(mrb, m, MRB_SYM(OP_SECTION),  mrb_int_value(mrb, OP_SECTION));
  mrb_define_const_id(mrb, m, MRB_SYM(OP_INVERTED), mrb_int_value(mrb, OP_INVERTED));
  mrb_define_const_id(mrb, m, MRB_SYM(OP_CLOSE),    mrb_int_value(mrb, OP_CLOSE));
  mrb_define_const_id(mrb, m, MRB_SYM(OP_COMMENT),  mrb_int_value(mrb, OP_COMMENT));
  mrb_define_const_id(mrb, m, MRB_SYM(OP_PARTIAL),  mrb_int_value(mrb, OP_PARTIAL));

  struct RClass *tmpl = mrb_define_class_under_id(mrb, m, MRB_SYM(Template), mrb->object_class);
  mrb_define_method_id(mrb, tmpl, MRB_SYM(initialize), template_initialize, MRB_ARGS_REQ(1));
  mrb_define_class_method_id(mrb, tmpl, MRB_SYM(compile), template_compile, MRB_ARGS_REQ(1));
  mrb_define_method_id(mrb, tmpl, MRB_SYM(render), template_render, MRB_ARGS_OPT(2));
  mrb_define_method_id(mrb, tmpl, MRB_SYM(tags),   template_tags,   MRB_ARGS_NONE());
  mrb_define_module_function_id(mrb, m, MRB_SYM(mustache), mustache_one_shot, MRB_ARGS_ARG(1, 2));
}

void
mrb_mruby_mustache_gem_final(mrb_state *mrb)
{
  (void)mrb;
}