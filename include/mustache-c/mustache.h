#ifndef MUSTACHE_C_MUSTACHE_H
#define MUSTACHE_C_MUSTACHE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mustache_template mustache_template;

enum {
  MUSTACHE_KIND_FALSY = 0,
  MUSTACHE_KIND_TEXT = 1,
  MUSTACHE_KIND_LIST = 2,
  MUSTACHE_KIND_MAP = 3,
  MUSTACHE_KIND_TRUTHY = 4
};

enum {
  MUSTACHE_RENDER = 0,
  MUSTACHE_SET_FIND = 1,
  MUSTACHE_SET_KIND = 2,
  MUSTACHE_SET_TEXT = 3,
  MUSTACHE_SET_SIZE = 4,
  MUSTACHE_SET_ELEMENT = 5,
  MUSTACHE_SET_PARTIAL = 6,
  MUSTACHE_SET_NEW_STRING = 7,
  MUSTACHE_SET_GROW = 8,
  MUSTACHE_SET_DONE = 9
};

int mustache_compile(const char *source, size_t size, mustache_template **tpl);

int mustache_dispose_template(mustache_template *tpl);

int mustache_message(const mustache_template *tpl, const char **message, size_t *size);

int mustache_next(const mustache_template *tpl);

int mustache_set_find(mustache_template *tpl,
                      const void *(*find)(void *user, const void *value, const char *key, size_t size));

int mustache_set_kind(mustache_template *tpl, int (*kind)(void *user, const void *value));

int mustache_set_text(mustache_template *tpl, const char *(*text)(void *user, const void *value, size_t *size));

int mustache_set_size(mustache_template *tpl, size_t (*size)(void *user, const void *value));

int mustache_set_element(mustache_template *tpl, const void *(*element)(void *user, const void *value, size_t index));

int mustache_set_partial(mustache_template *tpl,
                         const mustache_template *(*partial)(void *user, const char *name, size_t size));

int mustache_set_new_string(mustache_template *tpl,
                            char *(*new_string)(void *user, size_t capacity, void **string, size_t *real_capacity));

int mustache_set_grow(mustache_template *tpl,
                      char *(*grow)(void *user, void **string, size_t size, size_t capacity, size_t *real_capacity));

int mustache_set_done(mustache_template *tpl, int (*done)(void *user, void *string, size_t size));

int mustache_render(mustache_template *tpl, const void *root, void (*release)(const void *root), void *user,
                    void **string);

#ifdef __cplusplus
}
#endif

#endif
