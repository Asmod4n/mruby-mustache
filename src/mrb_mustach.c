/*
 * mruby-mustache: mustache templates for mruby.
 *
 * Walks mruby Hash/Array values directly through mustach-wrap's callback
 * interface. No JSON parsing at render time, no system JSON library
 * dependency. Hash keys are matched by their stringified form, so
 * String, Symbol, Integer, or any to_s-able key works uniformly.
 */

#include <mruby.h>
#include <mruby/array.h>
#include <mruby/hash.h>
#include <mruby/string.h>
#include <mruby/variable.h>
#include <mruby/error.h>
#include <mruby/numeric.h>
#include <mruby/presym.h>
#include <mruby/class.h>

#include <mustach.h>
#include <mustach-wrap.h>

#include <stdlib.h>
#include <string.h>

#if (__GNUC__ >= 3) || (__INTEL_COMPILER >= 800) || defined(__clang__)
# define likely(x)   __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x)   (x)
# define unlikely(x) (x)
#endif

#define MRB_MUSTACH_MAX_DEPTH 256

struct frame {
  mrb_value container;     /* what we're iterating over */
  mrb_value keys;          /* hash keys for objiter, nil otherwise */
  mrb_value obj;           /* current item — stable across tag renders in this frame */
  mrb_int   index;
  mrb_int   length;
  unsigned  is_iter   : 1;
  unsigned  is_objiter: 1;
};

struct ctx {
  mrb_state *mrb;
  mrb_value  root;
  mrb_value  selection;     /* transient: result of the last sel/subsel */
  struct frame stack[MRB_MUSTACH_MAX_DEPTH];
  int depth;
};

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static inline int
m_truthy(mrb_state *mrb, mrb_value v) {
  if (mrb_nil_p(v) || mrb_false_p(v)) return 0;
  if (mrb_array_p(v)  && RARRAY_LEN(v) == 0)         return 0;
  if (mrb_hash_p(v)   && mrb_hash_size(mrb, v) == 0) return 0;
  if (mrb_string_p(v) && RSTRING_LEN(v) == 0)        return 0;
  return 1;
}

/* Walk every key, stringify it, compare to the tag name. First match wins. */
struct lookup_ctx { const char *name; size_t len; mrb_value out; };

static int
match_by_str(mrb_state *mrb, mrb_value key, mrb_value val, void *data) {
  struct lookup_ctx *lc = (struct lookup_ctx *) data;
  mrb_value s = mrb_obj_as_string(mrb, key);
  if ((size_t) RSTRING_LEN(s) == lc->len &&
      memcmp(RSTRING_PTR(s), lc->name, lc->len) == 0) {
    lc->out = val;
    return 1;  /* stop — found */
  }
  return 0;
}

static mrb_value
m_lookup(mrb_state *mrb, mrb_value h, const char *name) {
  if (mrb_hash_size(mrb, h) == 0) return mrb_nil_value();
  struct lookup_ctx lc = { name, strlen(name), mrb_nil_value() };
  mrb_hash_foreach(mrb, mrb_hash_ptr(h), match_by_str, &lc);
  return lc.out;
}

/* "Current item" at depth d: root at depth 0, frame[d-1].obj otherwise. */
static inline mrb_value
frame_obj(struct ctx *c, int d) {
  return d == 0 ? c->root : c->stack[d - 1].obj;
}

/* Walk from innermost current-item up to root, looking for `name` in
 * each hash along the way. */
static int
m_resolve(struct ctx *c, const char *name) {
  mrb_state *mrb = c->mrb;
  for (int d = c->depth; d >= 0; d--) {
    mrb_value here = frame_obj(c, d);
    if (mrb_hash_p(here)) {
      mrb_value v = m_lookup(mrb, here, name);
      if (!mrb_nil_p(v)) { c->selection = v; return 1; }
    }
  }
  return 0;
}

/* ------------------------------------------------------------------ */
/* mustach_wrap_itf callbacks                                         */
/* ------------------------------------------------------------------ */

static int
cb_start(void *closure) {
  struct ctx *c = (struct ctx *) closure;
  c->selection = c->root;
  c->depth = 0;
  return MUSTACH_OK;
}

static int
cb_sel(void *closure, const char *name) {
  struct ctx *c = (struct ctx *) closure;
  if (name == NULL) {
    /* Current item — i.e. {{.}} inside an iteration. */
    c->selection = frame_obj(c, c->depth);
    return 1;
  }
  return m_resolve(c, name);
}

static int
cb_subsel(void *closure, const char *name) {
  struct ctx *c = (struct ctx *) closure;
  if (mrb_hash_p(c->selection)) {
    mrb_value v = m_lookup(c->mrb, c->selection, name);
    if (!mrb_nil_p(v)) { c->selection = v; return 1; }
  }
  return 0;
}

static int
cb_enter(void *closure, int objiter) {
  struct ctx *c = (struct ctx *) closure;
  if (unlikely(c->depth >= MRB_MUSTACH_MAX_DEPTH)) return MUSTACH_ERROR_TOO_DEEP;

  mrb_value sel = c->selection;
  struct frame *f = &c->stack[c->depth];

  if (objiter) {
    if (!mrb_hash_p(sel)) return 0;
    mrb_value keys = mrb_hash_keys(c->mrb, sel);
    if (RARRAY_LEN(keys) == 0) return 0;
    f->container  = sel;
    f->keys       = keys;
    f->index      = 0;
    f->length     = RARRAY_LEN(keys);
    f->is_iter    = 1;
    f->is_objiter = 1;
    f->obj        = mrb_hash_get(c->mrb, sel, mrb_ary_ref(c->mrb, keys, 0));
    c->depth++;
    return 1;
  }

  if (mrb_array_p(sel)) {
    if (RARRAY_LEN(sel) == 0) return 0;
    f->container  = sel;
    f->keys       = mrb_nil_value();
    f->index      = 0;
    f->length     = RARRAY_LEN(sel);
    f->is_iter    = 1;
    f->is_objiter = 0;
    f->obj        = mrb_ary_ref(c->mrb, sel, 0);
    c->depth++;
    return 1;
  }

  /* Single-shot if truthy. */
  if (!m_truthy(c->mrb, sel)) return 0;
  f->container  = sel;
  f->keys       = mrb_nil_value();
  f->index      = 0;
  f->length     = 1;
  f->is_iter    = 0;
  f->is_objiter = 0;
  f->obj        = sel;
  c->depth++;
  return 1;
}

static int
cb_next(void *closure) {
  struct ctx *c = (struct ctx *) closure;
  if (c->depth == 0) return 0;
  struct frame *f = &c->stack[c->depth - 1];
  if (!f->is_iter) return 0;

  f->index++;
  if (f->index >= f->length) return 0;

  if (f->is_objiter) {
    mrb_value k = mrb_ary_ref(c->mrb, f->keys, f->index);
    f->obj = mrb_hash_get(c->mrb, f->container, k);
  } else {
    f->obj = mrb_ary_ref(c->mrb, f->container, f->index);
  }
  return 1;
}

static int
cb_leave(void *closure) {
  struct ctx *c = (struct ctx *) closure;
  if (c->depth > 0) c->depth--;
  /* No restoration needed — the next sel/subsel sets c->selection fresh. */
  return MUSTACH_OK;
}

static int
cb_get(void *closure, struct mustach_sbuf *sbuf, int key) {
  struct ctx *c = (struct ctx *) closure;
  mrb_value v;

  /* {{*}} during objiter: render the current key, not the value. */
  if (key && c->depth > 0) {
    struct frame *f = &c->stack[c->depth - 1];
    if (f->is_objiter) {
      v = mrb_ary_ref(c->mrb, f->keys, f->index);
      goto stringify;
    }
  }
  v = c->selection;

stringify: {
  mrb_value s = mrb_obj_as_string(c->mrb, v);
  sbuf->value = mrb_string_value_cstr(c->mrb, &s);
  /* No length, no freecb — bytes are owned by mruby's GC; mustach
   * copies them out during emit (synchronous, no GC trigger), so it's
   * safe to just lend the pointer. */
  return 1;  /* wrap convention: 1 = ok, NOT MUSTACH_OK */
}
}

static int
cb_compare(void *closure, const char *value) {
  struct ctx *c = (struct ctx *) closure;
  mrb_value s = mrb_obj_as_string(c->mrb, c->selection);
  return strcmp(mrb_string_value_cstr(c->mrb, &s), value);
}

static const struct mustach_wrap_itf wrap_itf = {
  .start   = cb_start,
  .stop    = NULL,
  .compare = cb_compare,
  .sel     = cb_sel,
  .subsel  = cb_subsel,
  .enter   = cb_enter,
  .next    = cb_next,
  .leave   = cb_leave,
  .get     = cb_get,
};

/* ------------------------------------------------------------------ */
/* errors                                                             */
/* ------------------------------------------------------------------ */

static void
mustache_raise(mrb_state *mrb, int rc) {
  struct RClass *base = mrb_class_get_under_id(mrb,
    mrb_class_get_id(mrb, MRB_SYM(Mustache)), MRB_SYM(Error));

  mrb_sym     cls;
  const char *msg;
  switch (rc) {
    case MUSTACH_ERROR_SYSTEM:            cls = MRB_SYM(System);          msg = "system error"; break;
    case MUSTACH_ERROR_UNEXPECTED_END:    cls = MRB_SYM(UnexpectedEnd);   msg = "unexpected end of template"; break;
    case MUSTACH_ERROR_EMPTY_TAG:         cls = MRB_SYM(EmptyTag);        msg = "empty tag"; break;
    case MUSTACH_ERROR_TAG_TOO_LONG:      cls = MRB_SYM(TagTooLong);      msg = "tag too long"; break;
    case MUSTACH_ERROR_BAD_SEPARATORS:    cls = MRB_SYM(BadSeparators);   msg = "bad separators"; break;
    case MUSTACH_ERROR_TOO_DEEP:          cls = MRB_SYM(TooDeep);         msg = "nesting too deep"; break;
    case MUSTACH_ERROR_CLOSING:           cls = MRB_SYM(Closing);         msg = "closing tag mismatch"; break;
    case MUSTACH_ERROR_BAD_UNESCAPE_TAG:  cls = MRB_SYM(BadUnescapeTag);  msg = "bad unescape tag"; break;
    case MUSTACH_ERROR_INVALID_ITF:       cls = MRB_SYM(InvalidItf);      msg = "invalid interface"; break;
    case MUSTACH_ERROR_ITEM_NOT_FOUND:    cls = MRB_SYM(ItemNotFound);    msg = "item not found"; break;
    case MUSTACH_ERROR_PARTIAL_NOT_FOUND: cls = MRB_SYM(PartialNotFound); msg = "partial not found"; break;
    case MUSTACH_ERROR_UNDEFINED_TAG:     cls = MRB_SYM(UndefinedTag);    msg = "undefined tag"; break;
    case MUSTACH_ERROR_TOO_MUCH_NESTING:  cls = MRB_SYM(TooMuchNesting);  msg = "too much nesting"; break;
    default:                              cls = MRB_SYM(Unknown);         msg = NULL; break;
  }

  struct RClass *err = mrb_class_get_under_id(mrb, base, cls);
  if (msg) {
    mrb_raise(mrb, err, msg);
  } else {
    mrb_raisef(mrb, err, "unknown mustach error: %d", rc);
  }
}

/* ------------------------------------------------------------------ */
/* Ruby surface                                                       */
/* ------------------------------------------------------------------ */

struct render_args {
  const char *tmpl;
  size_t tmpl_len;
  int flags;
  struct ctx *ctx;
  char **result;
  size_t *result_size;
  int rc;
};

static mrb_value
do_render(mrb_state *mrb, void *data) {
  (void)mrb;
  struct render_args *a = (struct render_args *) data;
  a->rc = mustach_wrap_mem(a->tmpl, a->tmpl_len,
                           &wrap_itf, a->ctx,
                           a->flags, a->result, a->result_size);
  return mrb_nil_value();
}

struct copy_args {
  const char *src;
  size_t      len;
  mrb_value   out;
};

static mrb_value
do_copy(mrb_state *mrb, void *data) {
  struct copy_args *a = (struct copy_args *) data;
  a->out = mrb_str_new(mrb, a->src, (mrb_int) a->len);
  return mrb_nil_value();
}

static mrb_value
mrb_mustach_mem(mrb_state *mrb, mrb_value self) {
  (void)self;
  mrb_value template, data;
  mrb_int flags = Mustach_With_AllExtensions;
  mrb_get_args(mrb, "So|i", &template, &data, &flags);

  struct ctx c;
  memset(&c, 0, sizeof(c));
  c.mrb       = mrb;
  c.root      = data;
  c.selection = data;

  char  *result = NULL;
  size_t result_size = 0;
  struct render_args args = {
    .tmpl        = RSTRING_PTR(template),
    .tmpl_len    = (size_t) RSTRING_LEN(template),
    .flags       = (int) flags,
    .ctx         = &c,
    .result      = &result,
    .result_size = &result_size,
    .rc          = 0,
  };

  mrb_bool error = FALSE;
  mrb_value err = mrb_protect_error(mrb, do_render, &args, &error);

  if (error) {
    free(result);
    mrb_exc_raise(mrb, err);
  }
  if (args.rc != 0) {
    free(result);
    mustache_raise(mrb, args.rc);
  }

  struct copy_args cp = { result, result_size, mrb_nil_value() };
  mrb_bool copy_err = FALSE;
  mrb_value copy_exc = mrb_protect_error(mrb, do_copy, &cp, &copy_err);
  free(result);

  if (copy_err) mrb_exc_raise(mrb, copy_exc);
  return cp.out;
}

void
mrb_mruby_mustache_gem_init(mrb_state *mrb) {
  struct RClass *mc = mrb_define_class_id(mrb, MRB_SYM(Mustache), mrb->object_class);

  /* Error hierarchy: Mustache::Error < RuntimeError, with one subclass
   * per MUSTACH_ERROR_* code so callers can rescue at any granularity. */
  struct RClass *err = mrb_define_class_under_id(mrb, mc, MRB_SYM(Error), E_RUNTIME_ERROR);
  mrb_define_class_under_id(mrb, err, MRB_SYM(System),          err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(UnexpectedEnd),   err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(EmptyTag),        err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(TagTooLong),      err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(BadSeparators),   err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(TooDeep),         err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(Closing),         err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(BadUnescapeTag),  err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(InvalidItf),      err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(ItemNotFound),    err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(PartialNotFound), err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(UndefinedTag),    err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(TooMuchNesting),  err);
  mrb_define_class_under_id(mrb, err, MRB_SYM(Unknown),         err);

  mrb_define_const_id(mrb, mc, MRB_SYM(With_Equal),            mrb_fixnum_value(Mustach_With_Equal));
  mrb_define_const_id(mrb, mc, MRB_SYM(With_Compare),          mrb_fixnum_value(Mustach_With_Compare));
  mrb_define_const_id(mrb, mc, MRB_SYM(With_JsonPointer),      mrb_fixnum_value(Mustach_With_JsonPointer));
  mrb_define_const_id(mrb, mc, MRB_SYM(With_ObjectIter),       mrb_fixnum_value(Mustach_With_ObjectIter));
  mrb_define_const_id(mrb, mc, MRB_SYM(With_EscFirstCmp),      mrb_fixnum_value(Mustach_With_EscFirstCmp));
  mrb_define_const_id(mrb, mc, MRB_SYM(With_PartialDataFirst), mrb_fixnum_value(Mustach_With_PartialDataFirst));
  mrb_define_const_id(mrb, mc, MRB_SYM(With_ErrorUndefined),   mrb_fixnum_value(Mustach_With_ErrorUndefined));
  mrb_define_const_id(mrb, mc, MRB_SYM(With_AllExtensions),    mrb_fixnum_value(Mustach_With_AllExtensions));
  mrb_define_const_id(mrb, mc, MRB_SYM(With_NoExtensions),     mrb_fixnum_value(Mustach_With_NoExtensions));

  mrb_define_class_method_id(mrb, mc, MRB_SYM(mustache),
    mrb_mustach_mem, MRB_ARGS_ARG(2, 1));
}

void
mrb_mruby_mustache_gem_final(mrb_state *mrb) { (void)mrb; }