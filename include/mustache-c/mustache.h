#ifndef MUSTACHE_C_MUSTACHE_H
#define MUSTACHE_C_MUSTACHE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mustache_template mustache_template;

enum {
  MUSTACHE_FALSY = 0,
  MUSTACHE_TEXT = 1,
  MUSTACHE_LIST = 2,
  MUSTACHE_MAP = 3,
  MUSTACHE_TRUTHY = 4
};

int mustache_compile(const char *source, size_t size, mustache_template **tpl);

int mustache_dispose_template(mustache_template *tpl);

int mustache_message(const mustache_template *tpl, const char **message, size_t *size);

int mustache_set_find(mustache_template *tpl,
                      const void *(*find)(void *user, const void *value, const char *key, size_t size));

int mustache_set_kind(mustache_template *tpl, int (*kind)(void *user, const void *value));

int mustache_set_text(mustache_template *tpl, const char *(*text)(void *user, const void *value, size_t *size));

int mustache_set_size(mustache_template *tpl, size_t (*size)(void *user, const void *value));

int mustache_set_element(mustache_template *tpl, const void *(*element)(void *user, const void *value, size_t index));

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
