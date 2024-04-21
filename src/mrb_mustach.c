#include "mrb_mustach.h"

static void
mrb_mustach_parse_json(mrb_state *mrb, const mrb_value data, struct mrb_mustach_userdata *userdata)
{
#if defined(HAVE_CJSON)
  userdata->root = cJSON_ParseWithLength(RSTRING_PTR(data), RSTRING_LEN(data));
#elif defined(HAVE_JANSSON)
  userdata->root = json_loadb(RSTRING_PTR(data), RSTRING_LEN(data), 0, &userdata->error);
#elif defined(HAVE_JSON_C)
  userdata->tok = json_tokener_new();
  userdata->root = json_tokener_parse_ex(userdata->tok, RSTRING_PTR(data), RSTRING_LEN(data));
  if (unlikely(userdata->root == NULL)) {
    json_tokener_free(userdata->tok);
  }
#endif
  if (unlikely(userdata->root == NULL)) {
    mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Cannot parse JSON");
  }
}

static void
mrb_mustach_free_userdata(struct mrb_mustach_userdata *userdata)
{
#if defined(HAVE_CJSON)
  cJSON_Delete(userdata->root);
#elif defined(HAVE_JANSSON)
  json_decref(userdata->root);
#elif defined(HAVE_JSON_C)
  json_object_put(userdata->root);
  json_tokener_free(userdata->tok);
#endif
  free(userdata->result);
  memset(userdata, '\0', sizeof(*userdata));
}

static void
mrb_mustach_raise_error(mrb_state *mrb, const int rc)
{
  switch (rc) {
    case MUSTACH_ERROR_SYSTEM:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "System");
    case MUSTACH_ERROR_UNEXPECTED_END:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Unexpected end");
    case MUSTACH_ERROR_EMPTY_TAG:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Empty Tag");
    case MUSTACH_ERROR_TAG_TOO_LONG:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Tag too long");
    case MUSTACH_ERROR_BAD_SEPARATORS:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Bad Separators");
    case MUSTACH_ERROR_TOO_DEEP:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Too deep");
    case MUSTACH_ERROR_CLOSING:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Closing");
    case MUSTACH_ERROR_BAD_UNESCAPE_TAG:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Bad unescape Tag");
    case MUSTACH_ERROR_INVALID_ITF:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Invalid itf");
    case MUSTACH_ERROR_ITEM_NOT_FOUND:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Item not found");
    case MUSTACH_ERROR_PARTIAL_NOT_FOUND:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Partial not found");
    case MUSTACH_ERROR_UNDEFINED_TAG:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Undefined Tag");
    case MUSTACH_ERROR_TOO_MUCH_NESTING:
      mrb_raise(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Too much nesting");
    default:
      mrb_raisef(mrb, mrb_class_get_under(mrb, mrb_class_get(mrb, "Mustache"), "Error"), "Unknown Error: %S", mrb_int_value(mrb, rc));
  }
}

static mrb_value
mrb_mustach_json_mem(mrb_state *mrb, const mrb_value template, const int flags, struct mrb_mustach_userdata *userdata)
{
  size_t size;
#if defined(HAVE_CJSON)
  const int rc = mustach_cJSON_mem  (RSTRING_PTR(template), RSTRING_LEN(template), userdata->root, flags, &userdata->result, &size);
#elif defined(HAVE_JANSSON)
  const int rc = mustach_jansson_mem(RSTRING_PTR(template), RSTRING_LEN(template), userdata->root, flags, &userdata->result, &size);
#elif defined(HAVE_JSON_C)
  const int rc = mustach_json_c_mem (RSTRING_PTR(template), RSTRING_LEN(template), userdata->root, flags, &userdata->result, &size);
#endif
  mrb_value res = mrb_undef_value();
  if (likely(rc == 0)) {
    res = mrb_str_new(mrb, userdata->result, size);
  } else {
    mrb_mustach_free_userdata(userdata);
    mrb_mustach_raise_error(mrb, rc);
  }
  return res;
}

static mrb_value
mrb_json_dump_mrb_obj(mrb_state *mrb, const mrb_value obj);

static mrb_value
mrb_mustach_mem(mrb_state *mrb, mrb_value self)
{
  mrb_value template, data;
  const mrb_sym ruby = mrb_intern_lit(mrb, "ruby");
  mrb_sym data_type = ruby;
  mrb_int flags = Mustach_With_AllExtensions;
  mrb_get_args(mrb, "So|ni", &template, &data, &data_type, &flags);

  struct mrb_mustach_userdata userdata;
  memset(&userdata, '\0', sizeof(struct mrb_mustach_userdata));
  if (ruby == data_type) {
    data = mrb_json_dump_mrb_obj(mrb, data);
    mrb_mustach_parse_json(mrb, data, &userdata);
  } else if (mrb_intern_lit(mrb, "json") == data_type) {
    if (unlikely(!mrb_string_p(data))) {
      mrb_raise(mrb, E_TYPE_ERROR, "data must be a String if data_type is :json");
    }
    mrb_mustach_parse_json(mrb, data, &userdata);
  } else {
    mrb_raise(mrb, E_ARGUMENT_ERROR, "data_type must be :ruby or :json");
  }

  mrb_value res = mrb_mustach_json_mem(mrb, template, (int) flags, &userdata);
  mrb_mustach_free_userdata(&userdata);

  return res;
}

static mrb_value
mrb_json_dump_mrb_array(mrb_state *mrb, const mrb_value array)
{
  const mrb_int len = RARRAY_LEN(array);
  mrb_value result = mrb_str_new_capa(mrb, 4095 - sizeof(struct RString));
  const int arena_index = mrb_gc_arena_save(mrb);
  mrb_str_cat_lit(mrb, result, "[");
  for (mrb_int i = 0; i < len; i++) {
    const mrb_value val = mrb_ary_ref(mrb, array, i);
    mrb_str_append(mrb, result, mrb_json_dump_mrb_obj(mrb, val));
    if (i < len - 1) {
      mrb_str_cat_lit(mrb, result, ",");
    }
    mrb_gc_arena_restore(mrb, arena_index);
  }
  mrb_str_cat_lit(mrb, result, "]");

  return mrb_str_resize(mrb, result, RSTRING_LEN(result));
}

static mrb_value
mrb_json_dump_mrb_hash(mrb_state *mrb, const mrb_value hash)
{
  const mrb_value keys = mrb_hash_keys(mrb, hash);
  const mrb_int len = RARRAY_LEN(keys);
  mrb_value result = mrb_str_new_capa(mrb, 4095 - sizeof(struct RString));
  const int arena_index = mrb_gc_arena_save(mrb);
  mrb_str_cat_lit(mrb, result, "{");
  for (mrb_int i = 0; i < len; i++) {
    const mrb_value key = mrb_ary_ref(mrb, keys, i);
    mrb_str_append(mrb, result, mrb_json_dump_mrb_obj(mrb, key));
    mrb_str_cat_lit(mrb, result, ":");
    const mrb_value val = mrb_hash_get(mrb, hash, key);
    mrb_str_append(mrb, result, mrb_json_dump_mrb_obj(mrb, val));
    if (i < len - 1) {
      mrb_str_cat_lit(mrb, result, ",");
    }
    mrb_gc_arena_restore(mrb, arena_index);
  }
  mrb_str_cat_lit(mrb, result, "}");

  return mrb_str_resize(mrb, result, RSTRING_LEN(result));
}

static mrb_value
mrb_json_escape_string(mrb_state *mrb, const mrb_value str)
{
  static const char * const JSON_ESCAPE_MAPPING[256] = {
    ['"']  = "\\\"",
    ['\\'] = "\\\\",
    ['/']  = "\\/",
    ['\b'] = "\\b",
    ['\f'] = "\\f",
    ['\n'] = "\\n",
    ['\r'] = "\\r",
    ['\t'] = "\\t",  
  };

  mrb_value result = mrb_str_new_capa(mrb, RSTRING_LEN(str) + 2);
  mrb_str_cat_lit(mrb, result, "\"");
  const mrb_int len = RSTRING_LEN(str);
  const char * const cstr = RSTRING_PTR(str);
  for (mrb_int i = 0; i < len; i++) {
    const char *escaped = JSON_ESCAPE_MAPPING[(unsigned char)cstr[i]];
    if (escaped) {
      mrb_str_cat(mrb, result, escaped, 2); // as of now, all values are two bytes long in JSON_ESCAPE_MAPPING.
    } else {
      mrb_str_cat(mrb, result, &cstr[i], 1);
    }
  }
  mrb_str_cat_lit(mrb, result, "\"");

  return mrb_str_resize(mrb, result, RSTRING_LEN(result));
}

static mrb_value
mrb_json_dump_mrb_obj(mrb_state *mrb, const mrb_value obj)
{
  switch (mrb_type(obj)) {
    case MRB_TT_FALSE:
      if (mrb_bool(obj)) {
        return mrb_str_new_lit(mrb, "false");
      } else {
        return mrb_str_new_lit(mrb, "null");
      }
    case MRB_TT_TRUE:
      return mrb_str_new_lit(mrb, "true");
    case MRB_TT_SYMBOL:
      return mrb_json_escape_string(mrb, mrb_sym_str(mrb, mrb_symbol(obj)));
    case MRB_TT_FLOAT:
      return mrb_float_to_str(mrb, obj, NULL);
    case MRB_TT_INTEGER:
      return mrb_integer_to_str(mrb, obj, 10);
    case MRB_TT_CLASS:
    case MRB_TT_MODULE:
      return mrb_json_escape_string(mrb, mrb_class_path(mrb, mrb_class_ptr(obj)));
    case MRB_TT_ARRAY:
      return mrb_json_dump_mrb_array(mrb, obj);
    case MRB_TT_HASH:
      return mrb_json_dump_mrb_hash(mrb, obj);
    case MRB_TT_STRING:
      return mrb_json_escape_string(mrb, obj);
    default:
      return mrb_json_escape_string(mrb, mrb_obj_as_string(mrb, obj));
  }
}

void
mrb_mruby_mustache_gem_init(mrb_state* mrb)
{
  struct RClass *mustache_class = mrb_define_class(mrb, "Mustache", mrb->object_class);
  mrb_define_const(mrb, mustache_class, "With_Equal",           mrb_fixnum_value(Mustach_With_Equal));
  mrb_define_const(mrb, mustache_class, "With_Compare",         mrb_fixnum_value(Mustach_With_Compare));
  mrb_define_const(mrb, mustache_class, "With_JsonPointer",     mrb_fixnum_value(Mustach_With_JsonPointer));
  mrb_define_const(mrb, mustache_class, "With_ObjectIter",      mrb_fixnum_value(Mustach_With_ObjectIter));
  mrb_define_const(mrb, mustache_class, "With_EscFirstCmp",     mrb_fixnum_value(Mustach_With_EscFirstCmp));
  mrb_define_const(mrb, mustache_class, "With_PartialDataFirst",mrb_fixnum_value(Mustach_With_PartialDataFirst));
  mrb_define_const(mrb, mustache_class, "With_ErrorUndefined",  mrb_fixnum_value(Mustach_With_ErrorUndefined));
  mrb_define_const(mrb, mustache_class, "With_AllExtensions",   mrb_fixnum_value(Mustach_With_AllExtensions));
  mrb_define_const(mrb, mustache_class, "With_NoExtensions",    mrb_fixnum_value(Mustach_With_NoExtensions));
  mrb_define_class_method(mrb, mustache_class, "mustache",      mrb_mustach_mem, MRB_ARGS_ARG(2, 2));
}

void mrb_mruby_mustache_gem_final(mrb_state* mrb) {}
