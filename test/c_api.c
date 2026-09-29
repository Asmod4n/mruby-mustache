#include <mustache-c/mustache.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This file is C, so it also shows that the header is C: no C++ type,
 * no struct a caller must rebuild, and every function gives 0 or -1
 * with errno. */

struct value {
  int                 kind;
  const char         *text;
  const struct value *child;
};

struct user {
  char answer[4096];
};

static const void *
find(void *user, const void *value, const char *key, size_t size)
{
  const struct value *v = value;
  (void)user;
  if (v->kind != MUSTACHE_MAP || size != 1 || key[0] != 'a') return NULL;
  return v->child;
}

static int
kind(void *user, const void *value)
{
  (void)user;
  return ((const struct value *)value)->kind;
}

static const char *
text(void *user, const void *value, size_t *size)
{
  const struct value *v = value;
  (void)user;
  *size = strlen(v->text);
  return v->text;
}

static size_t
size_of(void *user, const void *value)
{
  (void)user;
  (void)value;
  return 1;
}

static const void *
element(void *user, const void *value, size_t index)
{
  (void)user;
  (void)index;
  return ((const struct value *)value)->child;
}

static char *
new_string(void *user, size_t capacity, void **string, size_t *real_capacity)
{
  char *bytes = malloc(capacity);
  (void)user;
  *string = bytes;
  *real_capacity = capacity;
  return bytes;
}

static char *
grow(void *user, void **string, size_t size, size_t capacity, size_t *real_capacity)
{
  char *bytes = realloc(*string, capacity);
  (void)user;
  (void)size;
  if (bytes == NULL) return NULL;
  *string = bytes;
  *real_capacity = capacity;
  return bytes;
}

static int
done(void *user, void *string, size_t size)
{
  struct user *u = user;
  memcpy(u->answer, string, size);
  u->answer[size] = '\0';
  free(string);
  return 0;
}

/* A render owns the root it is given and releases it once, whether the
 * render succeeds or not. The count shows that. */
static int released = 0;

static void
release(const void *root)
{
  (void)root;
  released++;
}

static int failed = 0;

static void
expect(int ok, const char *what)
{
  if (ok) return;
  failed++;
  printf("FAIL %s\n", what);
}

static mustache_template *
compiled(const char *source)
{
  mustache_template *tpl = NULL;
  if (mustache_compile(source, strlen(source), &tpl) != 0) return tpl;
  mustache_set_find(tpl, find);
  mustache_set_kind(tpl, kind);
  mustache_set_text(tpl, text);
  mustache_set_size(tpl, size_of);
  mustache_set_element(tpl, element);
  mustache_set_new_string(tpl, new_string);
  mustache_set_grow(tpl, grow);
  mustache_set_done(tpl, done);
  return tpl;
}

static int
message_is(const mustache_template *tpl, const char *expected)
{
  const char *message = NULL;
  size_t size = 0;
  if (mustache_message(tpl, &message, &size) != 0) return 0;
  return size == strlen(expected) && memcmp(message, expected, size) == 0;
}

int
main(void)
{
  struct user u = {{0}};
  void *string = NULL;
  int r;

  /* A source that does not compile gives EILSEQ. The template still
   * exists, so the caller reads the message and disposes it. */
  {
    mustache_template *tpl = NULL;
    r = mustache_compile("{{#a}}", 6, &tpl);
    expect(r == -1 && errno == EILSEQ, "parse gives EILSEQ");
    expect(tpl != NULL && message_is(tpl, "an unclosed section"), "parse message");
    mustache_dispose_template(tpl);
  }

  /* A render that succeeds hands back the string of the host. */
  {
    const struct value world = {MUSTACHE_TEXT, "World", NULL};
    const struct value root = {MUSTACHE_MAP, NULL, &world};
    mustache_template *tpl = compiled("Hello {{a}}!");
    r = mustache_render(tpl, &root, release, &u, &string);
    expect(r == 0 && strcmp(u.answer, "Hello World!") == 0, "render");
    mustache_dispose_template(tpl);
  }

  /* A value that is not text gives EINVAL, and done still runs, so the
   * host frees its string. */
  {
    const struct value list = {MUSTACHE_LIST, NULL, NULL};
    const struct value root = {MUSTACHE_MAP, NULL, &list};
    mustache_template *tpl = compiled("{{a}}");
    r = mustache_render(tpl, &root, release, &u, &string);
    expect(r == -1 && errno == EINVAL, "not_text gives EINVAL");
    expect(message_is(tpl, "a value is not text"), "not_text message");
    mustache_dispose_template(tpl);
  }

  /* A map that holds itself nests without end. The depth limit stops
   * it with ELOOP. */
  {
    struct value root = {MUSTACHE_MAP, NULL, NULL};
    char source[512] = "";
    int i;
    mustache_template *tpl;
    root.child = &root;
    for (i = 0; i < 40; i++) strcat(source, "{{#a}}");
    for (i = 0; i < 40; i++) strcat(source, "{{/a}}");
    tpl = compiled(source);
    r = mustache_render(tpl, &root, release, &u, &string);
    expect(r == -1 && errno == ELOOP, "nesting gives ELOOP");
    mustache_dispose_template(tpl);
  }

  /* A callback that is missing is an argument the caller got wrong. */
  {
    mustache_template *tpl = NULL;
    const struct value root = {MUSTACHE_MAP, NULL, NULL};
    mustache_compile("x", 1, &tpl);
    r = mustache_set_find(tpl, NULL);
    expect(r == -1 && errno == EINVAL, "a null callback gives EINVAL");
    r = mustache_render(tpl, &root, release, &u, &string);
    expect(r == -1 && errno == EINVAL, "a render without callbacks gives EINVAL");
    mustache_dispose_template(tpl);
  }

  expect(released == 4, "every render releases its root once");
  printf("c_api: %s\n", failed == 0 ? "passed" : "failed");
  return failed == 0 ? 0 : 1;
}
