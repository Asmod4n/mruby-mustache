#include <mruby.h>
#include <mruby/string.h>
#include <mruby/hash.h>
#include <mruby/array.h>
#include <mruby/class.h>
#include <mustach.h>
#include <stdlib.h>
#include <string.h>
#include <mruby/numeric.h>

#if (__GNUC__ >= 3) || (__INTEL_COMPILER >= 800) || defined(__clang__)
# define likely(x) __builtin_expect(!!(x), 1)
# define unlikely(x) __builtin_expect(!!(x), 0)
#else
# define likely(x) (x)
# define unlikely(x) (x)
#endif

#if defined(HAVE_CJSON)
  #include <mustach-cjson.h>
  struct mrb_mustach_userdata {
    cJSON *root;
    char *result;
  };
#elif defined(HAVE_JANSSON)
  #include <mustach-jansson.h>
  struct mrb_mustach_userdata {
    json_error_t error;
    json_t *root;
    char *result;
  };
#elif defined(HAVE_JSON_C)
  #include <mustach-json-c.h>
  struct mrb_mustach_userdata {
    struct json_tokener *tok;
    struct json_object *root;
    char *result;
  };
#endif