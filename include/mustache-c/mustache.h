#ifndef MUSTACHE_C_MUSTACHE_H
#define MUSTACHE_C_MUSTACHE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mustache_template mustache_template;

enum {
  MUSTACHE_RENDER = 0,
  MUSTACHE_FIND = 1,
  MUSTACHE_KIND = 2,
  MUSTACHE_TEXT = 3,
  MUSTACHE_SIZE = 4,
  MUSTACHE_ELEMENT = 5,
  MUSTACHE_PARTIAL = 6,
  MUSTACHE_NEW_STRING = 7,
  MUSTACHE_GROW = 8,
  MUSTACHE_DONE = 9
};

enum {
  MUSTACHE_KIND_FALSY = 0,
  MUSTACHE_KIND_TEXT = 1,
  MUSTACHE_KIND_LIST = 2,
  MUSTACHE_KIND_MAP = 3,
  MUSTACHE_KIND_TRUTHY = 4
};

int mustache_compile(const char *source, size_t size, mustache_template **tpl);

int mustache_dispose_template(mustache_template *tpl);

int mustache_message(const mustache_template *tpl, const char **message, size_t *size);

int mustache_render(mustache_template *tpl, const void *root);

int mustache_next(mustache_template *tpl, const void **value, const char **text, size_t *size, size_t *capacity);

int mustache_answer(mustache_template *tpl, const void *value, const char *text, size_t size);

int mustache_answer_string(mustache_template *tpl, char *bytes, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
