#include <mustache-c/mustache.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This file is C, so it also shows that the header is C: a handle of an
 * incomplete type, no struct a caller must build, and every function
 * gives 0 or more, or -1 with errno. */

struct value {
  int                 kind;
  const char         *text;
  const struct value *child;
};

struct rendered {
  int    result;
  int    error;
  int    done_seen;
  char   answer[4096];
  char  *string;
  size_t capacity;
};

static int failed = 0;

static void
expect(int ok, const char *what)
{
  if (ok) return;
  failed++;
  printf("FAIL %s\n", what);
}

/* A host answers every request with a call. It owns the string: it
 * allocates it at MUSTACHE_NEW_STRING, grows it, and frees it at
 * MUSTACHE_DONE, which comes after a failure too. */
static struct rendered
rendered(mustache_template *tpl, const struct value *root)
{
  struct rendered out;
  memset(&out, 0, sizeof out);
  if (mustache_render(tpl, root) != 0) {
    out.result = -1;
    out.error = errno;
    return out;
  }
  for (;;) {
    const void *value = NULL;
    const char *text = NULL;
    size_t size = 0;
    size_t capacity = 0;
    const struct value *v;
    char *grown;
    int r = mustache_next(tpl, &value, &text, &size, &capacity);
    v = value;
    switch (r) {
      case MUSTACHE_RENDER:
        return out;
      case MUSTACHE_FIND:
        mustache_answer(tpl, v->kind == MUSTACHE_KIND_MAP && size == 1 && text[0] == 'a' ? v->child : NULL, NULL, 0);
        break;
      case MUSTACHE_KIND:
        mustache_answer(tpl, NULL, NULL, (size_t)v->kind);
        break;
      case MUSTACHE_TEXT:
        mustache_answer(tpl, NULL, v->text, strlen(v->text));
        break;
      case MUSTACHE_SIZE:
        mustache_answer(tpl, NULL, NULL, 1);
        break;
      case MUSTACHE_ELEMENT:
        mustache_answer(tpl, v->child, NULL, 0);
        break;
      case MUSTACHE_PARTIAL:
        mustache_answer(tpl, NULL, NULL, 0);
        break;
      case MUSTACHE_NEW_STRING:
      case MUSTACHE_GROW:
        grown = realloc(out.string, capacity);
        if (grown != NULL) out.string = grown;
        mustache_answer_string(tpl, grown, capacity);
        break;
      case MUSTACHE_DONE:
        memcpy(out.answer, out.string, size < sizeof out.answer - 1 ? size : sizeof out.answer - 1);
        free(out.string);
        out.string = NULL;
        out.done_seen = 1;
        break;
      default:
        out.result = -1;
        out.error = errno;
        break;
    }
  }
}

static int
message_is(const mustache_template *tpl, const char *expected)
{
  const char *message = NULL;
  size_t size = 0;
  if (mustache_message(tpl, &message, &size) != 0) return 0;
  return size == strlen(expected) && memcmp(message, expected, size) == 0;
}

static mustache_template *
compiled(const char *source)
{
  mustache_template *tpl = NULL;
  mustache_compile(source, strlen(source), &tpl);
  return tpl;
}

int
main(void)
{
  struct rendered r;

  /* A source that does not compile gives EILSEQ. The template still
   * exists, so the caller reads the message and disposes it. */
  {
    mustache_template *tpl = NULL;
    int c = mustache_compile("{{#a}}", 6, &tpl);
    expect(c == -1 && errno == EILSEQ, "parse gives EILSEQ");
    expect(tpl != NULL && message_is(tpl, "an unclosed section"), "parse message");
    r = rendered(tpl, NULL);
    expect(r.result == -1 && r.error == EILSEQ, "a render of a refused template gives EILSEQ");
    mustache_dispose_template(tpl);
  }

  /* A render that succeeds ends with MUSTACHE_RENDER, and the string
   * holds the answer. */
  {
    const struct value world = {MUSTACHE_KIND_TEXT, "World", NULL};
    const struct value root = {MUSTACHE_KIND_MAP, NULL, &world};
    mustache_template *tpl = compiled("Hello {{a}}!");
    r = rendered(tpl, &root);
    expect(r.result == 0 && r.done_seen && strcmp(r.answer, "Hello World!") == 0, "render");
    r = rendered(tpl, &root);
    expect(r.result == 0 && strcmp(r.answer, "Hello World!") == 0, "a template renders again");
    mustache_dispose_template(tpl);
  }

  /* A value that is not text gives EINVAL. MUSTACHE_DONE still comes
   * first, so the host frees its string. */
  {
    const struct value list = {MUSTACHE_KIND_LIST, NULL, NULL};
    const struct value root = {MUSTACHE_KIND_MAP, NULL, &list};
    mustache_template *tpl = compiled("{{a}}");
    r = rendered(tpl, &root);
    expect(r.result == -1 && r.error == EINVAL && r.done_seen, "not_text gives EINVAL after DONE");
    expect(message_is(tpl, "a value is not text"), "not_text message");
    mustache_dispose_template(tpl);
  }

  /* A map that holds itself nests without end. The depth limit stops
   * it with ELOOP. */
  {
    struct value root = {MUSTACHE_KIND_MAP, NULL, NULL};
    char source[512] = "";
    int i;
    mustache_template *tpl;
    root.child = &root;
    for (i = 0; i < 40; i++) strcat(source, "{{#a}}");
    for (i = 0; i < 40; i++) strcat(source, "{{/a}}");
    tpl = compiled(source);
    r = rendered(tpl, &root);
    expect(r.result == -1 && r.error == ELOOP && r.done_seen, "nesting gives ELOOP");
    mustache_dispose_template(tpl);
  }

  /* Every handle starts with a magic value. A pointer to anything else
   * gives EBADF, and so does NULL. */
  {
    unsigned long long other = 0;
    const char *message = NULL;
    size_t size = 0;
    expect(mustache_render((mustache_template *)&other, NULL) == -1 && errno == EBADF, "a foreign pointer gives EBADF");
    expect(mustache_message(NULL, &message, &size) == -1 && errno == EBADF, "NULL gives EBADF");
  }

  /* A render starts only when the last one ended, and an answer comes
   * only after a request that needs one. */
  {
    const struct value world = {MUSTACHE_KIND_TEXT, "World", NULL};
    const struct value root = {MUSTACHE_KIND_MAP, NULL, &world};
    mustache_template *tpl = compiled("{{a}}");
    expect(mustache_answer(tpl, NULL, NULL, 0) == -1 && errno == EINVAL, "an answer without a request gives EINVAL");
    expect(mustache_render(tpl, &root) == 0, "render starts");
    expect(mustache_render(tpl, &root) == -1 && errno == EBUSY, "a second render gives EBUSY");
    mustache_dispose_template(tpl);
  }

  /* A host that cannot give the first string answers NULL. No string
   * exists then, so the render ends with ENOMEM and no MUSTACHE_DONE. */
  {
    mustache_template *tpl = compiled("x");
    const void *value = NULL;
    const char *text = NULL;
    size_t size = 0;
    size_t capacity = 0;
    int n;
    mustache_render(tpl, NULL);
    n = mustache_next(tpl, &value, &text, &size, &capacity);
    expect(n == MUSTACHE_NEW_STRING, "the first request is MUSTACHE_NEW_STRING");
    mustache_answer_string(tpl, NULL, 0);
    n = mustache_next(tpl, &value, &text, &size, &capacity);
    expect(n == -1 && errno == ENOMEM, "a NULL string gives ENOMEM");
    n = mustache_next(tpl, &value, &text, &size, &capacity);
    expect(n == MUSTACHE_RENDER, "after the failure the template waits for a render");
    mustache_dispose_template(tpl);
  }

  printf("c_api: %s\n", failed == 0 ? "passed" : "failed");
  return failed == 0 ? 0 : 1;
}
