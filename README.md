# mustache

Mustache templates for C++, with a C API for every other language.

The library compiles a template once and renders it many times. It
writes the answer directly into a string of the caller, and it escapes
HTML by default. It passes 163 cases of the Mustache specification. It
never runs code of the data it renders.

- C++20, header only: `include/mustache/`.
- A reflection render for C++26 compilers with reflection: the
  template is compiled at compile time, and a key that names no member
  is a compile error.
- A C API for other languages: `include/mustache-c/mustache.h` and the
  library `mustache-c`.

## Contents

1. [Install](#install)
2. [A first render](#a-first-render)
3. [Sections, lists, partials](#sections-lists-partials)
4. [Render a C++ struct with reflection](#render-a-c-struct-with-reflection)
5. [Errors and limits](#errors-and-limits)
6. [Render your own data types](#render-your-own-data-types)
7. [The C API](#the-c-api)
8. [What the library does not do](#what-the-library-does-not-do)
9. [Tests and benchmarks](#tests-and-benchmarks)

## Install

The library needs a C++20 compiler and CMake 3.25 or later. The
reflection render needs a compiler with C++26 reflection, for example
g++ 16 with `-std=c++26 -freflection`.

CMake exports two targets:

| target | what it is |
|---|---|
| `mustache::mustache` | the C++ headers, an INTERFACE library |
| `mustache::mustache-c` | the C API, a compiled library |

### With FetchContent

```cmake
include(FetchContent)
FetchContent_Declare(mustache
  GIT_REPOSITORY https://github.com/Asmod4n/mruby-mustache.git
  GIT_TAG mustache-cpp)
FetchContent_MakeAvailable(mustache)

target_link_libraries(app PRIVATE mustache::mustache)
```

### As an installed package

```sh
git clone --recursive -b mustache-cpp https://github.com/Asmod4n/mruby-mustache.git mustache
cmake -S mustache -B build -DBUILD_TESTING=OFF
cmake --build build
cmake --install build --prefix /usr/local
```

```cmake
find_package(mustache REQUIRED)
target_link_libraries(app PRIVATE mustache::mustache)      # C++
target_link_libraries(app-c PRIVATE mustache::mustache-c)  # C
```

## A first render

`std_host` is the host for data that C++ holds: text, numbers, true,
false, null, lists and maps.

```cpp
#include <mustache/std.hpp>

#include <cstdio>

int main()
{
  using namespace mustache::std_host;

  Host host;
  const auto program = mustache::program_of<Key>(host, "Hello, {{name}}! You have {{count}} new {{what}}.",
                                                 mustache::kSourceMax);
  if (!program) {
    std::printf("the template does not compile: %.*s\n", (int)host.what.size(), host.what.data());
    return 1;
  }

  Map data;
  data.insert_or_assign("name", Value{std::string("Ada <admin>")});
  data.insert_or_assign("count", Value{std::int64_t{3}});
  data.insert_or_assign("what", Value{std::string("messages")});
  const Value root{data};

  const mustache::Fault fault = mustache::Walk<Host, const Value *, Key>(host).run(*program, &root);
  if (fault != mustache::Fault::none) {
    std::printf("the render failed: %.*s\n", (int)host.what.size(), host.what.data());
    return 1;
  }
  std::printf("%s\n", host.answer.c_str());
}
```

Output:

```
Hello, Ada &lt;admin&gt;! You have 3 new messages.
```

- `program_of` compiles the template once. Keep the program and render
  it as often as you need.
- `{{name}}` escapes `&`, `<`, `>`, `"` and `'`. `{{{name}}}` and
  `{{&name}}` write the value as it is.
- A number is written with `std::to_chars`, in its shortest form.
- The answer is in `host.answer`, a `std::string`.

## Sections, lists, partials

```cpp
#include <mustache/std.hpp>

#include <cstdio>
#include <string>
#include <unordered_map>

int main()
{
  using namespace mustache::std_host;

  Host host;
  const auto page = mustache::program_of<Key>(host,
                                              "<ul>\n"
                                              "{{#items}}\n"
                                              "  {{> item}}\n"
                                              "{{/items}}\n"
                                              "{{^items}}\n"
                                              "  <li>Nothing here.</li>\n"
                                              "{{/items}}\n"
                                              "</ul>\n"
                                              "{{#admin}}<a href=\"/edit\">Edit</a>{{/admin}}\n",
                                              mustache::kSourceMax);
  const auto item = mustache::program_of<Key>(host, "<li>{{name}}: {{price}} EUR</li>\n", mustache::kSourceMax);

  const std::unordered_map<std::string, const mustache::Program<Key> *, KeyHash, KeyEqual> partials = {
      {"item", &*item}};
  host.partials = &partials;

  List items;
  for (const auto &[name, price] : {std::pair{"Tea", 3.5}, std::pair{"Cake", 4.25}}) {
    Map row;
    row.insert_or_assign("name", Value{std::string(name)});
    row.insert_or_assign("price", Value{price});
    items.push_back(Value{row});
  }
  Map data;
  data.insert_or_assign("items", Value{items});
  data.insert_or_assign("admin", Value{true});
  const Value root{data};

  if (mustache::Walk<Host, const Value *, Key>(host).run(*page, &root) != mustache::Fault::none) return 1;
  std::printf("%s", host.answer.c_str());
}
```

Output:

```
<ul>
  <li>Tea: 3.5 EUR</li>
  <li>Cake: 4.25 EUR</li>
</ul>
<a href="/edit">Edit</a>
```

| tag | what it does |
|---|---|
| `{{#items}}…{{/items}}` | renders the body once for each element of a list, or once for a map, for true or for text |
| `{{^items}}…{{/items}}` | renders the body when the value is missing, false, null or an empty list |
| `{{> item}}` | renders the partial `item` in the current context, with the indent of the tag |
| `{{< layout}}{{$block}}…{{/block}}{{/layout}}` | renders the parent `layout` and replaces its block `block` |
| `{{a.b.c}}` | looks up `a`, then `b` in it, then `c` |
| `{{.}}` | the current value |
| `{{! comment }}` | writes nothing |
| `{{=<% %>=}}` | sets new delimiters |

true, false and null are conditions. A section renders for true and not
for false or null. `{{x}}` writes nothing for false and null.

## Render a C++ struct with reflection

With a C++26 compiler that has reflection, a template renders a struct
directly. The template is a template argument, so the compiler compiles
it, and a render is plain C++ code with no lookup at run time.

```cpp
#include <mustache/reflect.hpp>
#include <mustache/std.hpp>

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

struct Item {
  std::string name;
  std::string price;
};

struct Page {
  std::string                title;
  std::optional<std::string> subtitle;
  bool                       admin;
  std::vector<Item>          items;
};

int main()
{
  const Page page{"Menu", std::nullopt, true, {{"Tea", "3.50"}, {"Cake", "4.25"}}};

  mustache::std_host::Host host;
  const mustache::Fault fault =
      mustache::render<"<h1>{{title}}</h1>{{#subtitle}}<h2>{{.}}</h2>{{/subtitle}}\n"
                       "{{#items}}{{> item}}{{/items}}"
                       "{{#admin}}<a href=\"/edit\">Edit</a>{{/admin}}\n",
                       mustache::static_partial<"item", "<p>{{name}}: {{price}} EUR</p>\n">>(host, page);
  if (fault != mustache::Fault::none) return 1;
  std::printf("%s", host.answer.c_str());
}
```

Output:

```
<h1>Menu</h1>
<p>Tea: 3.50 EUR</p>
<p>Cake: 4.25 EUR</p>
<a href="/edit">Edit</a>
```

- A member is text: anything that converts to `std::string_view`.
- `bool` and `std::optional` are conditions. A range is a list.
- A key that names no member is a compile error that names the key:

```
error: use of deleted function 'names_no_member() [with Key = fixed_string<6>{"titel"}]':
a key names no member of any context
```

The other compile errors are `does_not_compile`, `nests_too_deep`,
`names_a_cxx_keyword` and `is_not_text`.

## Errors and limits

A render gives back a `mustache::Fault`. The host keeps the message in
`host.what`.

| Fault | when |
|---|---|
| `none` | the render succeeded |
| `parse` | the template does not compile |
| `not_text` | `{{x}}` names a list or a map |
| `too_deep` | sections nest deeper than 32, or partials deeper than 64 |
| `over_work` | the render does more work than its limit |
| `over_limit` | the answer is larger than its limit |
| `no_memory` | an allocation failed |

Every limit has a default, and a render can lower it:

```cpp
mustache::Walk<Host, const Value *, Key> walk(host,
                                             1 << 20,   // work limit
                                             4096,      // first capacity of the answer
                                             1 << 20);  // largest answer
```

The work limit counts each run of a template body. It stops a small
template with partials that call each other before it takes the
machine.

## Render your own data types

The core does not know your types. It asks a host through
`tag_invoke`, so your values are rendered where they are, with no copy
into a map of ours. A host gives answers to these tags:

| tag | the question |
|---|---|
| `key_of(host, name)` | make the key for a name of the template, once, when the template compiles |
| `find(host, value, key)` | the value under `key` in `value`, or `std::nullopt` |
| `kind_of(host, value)` | `Kind::falsy`, `text`, `list`, `map` or `truthy` |
| `text_of(host, value)` | the text of a value, as `std::string_view` |
| `size_of(host, value)` | the number of elements of a list |
| `element(host, value, i)` | the element `i` of a list |
| `partial(host, key)` | the program of a partial, or `nullptr` |
| `new_string(host, capacity)` | a fresh string for the answer, as `std::span<char>` |
| `grow(host, size, capacity)` | a larger string that keeps the first `size` bytes |
| `done(host, size)` | the answer has `size` bytes; comes after every render that opened a string |
| `fail(host, fault, what, asked, allowed)` | why the render stopped |

`include/mustache/std.hpp` is a complete host in 163 lines.

## The C API

A function gives 0 or a code, or -1 with `errno` set. The template is a
handle of an incomplete type, and each callback is set one by one, so a
foreign function interface builds no struct. Python ctypes, Ruby
Fiddle, LuaJIT and Java Panama can call it as it is.

```c
#include <mustache-c/mustache.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The data of the host: one map, the root, with two text values. A
 * value is any pointer the host chooses; the core only passes it back. */
struct pair {
  const char *key;
  const char *text;
};

static const struct pair data[] = {{"name", "Ada"}, {"city", "London"}};
static const char root[] = "the map";

static const void *
find(void *user, const void *value, const char *key, size_t size)
{
  size_t i;
  (void)user;
  if (value != root) return NULL;
  for (i = 0; i < 2; i++)
    if (strlen(data[i].key) == size && memcmp(data[i].key, key, size) == 0) return &data[i];
  return NULL;
}

static int
kind(void *user, const void *value)
{
  (void)user;
  return value == root ? MUSTACHE_KIND_MAP : MUSTACHE_KIND_TEXT;
}

static const char *
text(void *user, const void *value, size_t *size)
{
  const struct pair *p = value;
  (void)user;
  *size = strlen(p->text);
  return p->text;
}

static size_t
size_of(void *user, const void *value)
{
  (void)user;
  (void)value;
  return 0;
}

static const void *
element(void *user, const void *value, size_t index)
{
  (void)user;
  (void)index;
  return value;
}

static const mustache_template *
partial(void *user, const char *name, size_t size)
{
  (void)user;
  (void)name;
  (void)size;
  return NULL;
}

/* The string is the host's. The render writes into it and asks for
 * more room when it needs it. */
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
  (void)user;
  ((char *)string)[size] = '\0';
  return 0;
}

int
main(void)
{
  const char *source = "{{name}} lives in {{city}}.";
  mustache_template *tpl = NULL;
  void *string = NULL;
  int next;

  if (mustache_compile(source, strlen(source), &tpl) != 0) {
    const char *message;
    size_t size;
    mustache_message(tpl, &message, &size);
    fprintf(stderr, "%.*s\n", (int)size, message);
    mustache_dispose_template(tpl);
    return 1;
  }

  /* mustache_next names the next setter to call, until every callback
   * is set. */
  while ((next = mustache_next(tpl)) != MUSTACHE_RENDER) {
    switch (next) {
      case MUSTACHE_SET_FIND:       mustache_set_find(tpl, find); break;
      case MUSTACHE_SET_KIND:       mustache_set_kind(tpl, kind); break;
      case MUSTACHE_SET_TEXT:       mustache_set_text(tpl, text); break;
      case MUSTACHE_SET_SIZE:       mustache_set_size(tpl, size_of); break;
      case MUSTACHE_SET_ELEMENT:    mustache_set_element(tpl, element); break;
      case MUSTACHE_SET_PARTIAL:    mustache_set_partial(tpl, partial); break;
      case MUSTACHE_SET_NEW_STRING: mustache_set_new_string(tpl, new_string); break;
      case MUSTACHE_SET_GROW:       mustache_set_grow(tpl, grow); break;
      case MUSTACHE_SET_DONE:       mustache_set_done(tpl, done); break;
      default:                      return 1;
    }
  }

  if (mustache_render(tpl, root, NULL, NULL, &string) != 0) {
    fprintf(stderr, "the render failed: errno %d\n", errno);
    mustache_dispose_template(tpl);
    return 1;
  }
  printf("%s\n", (char *)string);
  free(string);
  mustache_dispose_template(tpl);
  return 0;
}
```

Output:

```
Ada lives in London.
```

- `mustache_next` names the setter that is still missing, or gives
  `MUSTACHE_RENDER`. A render with a missing callback does not start:
  it gives the same code.
- A value is any pointer of the host. The core only passes it back.
- The host owns the string. `done` comes after every render that got a
  string, also after a failure.
- `mustache_render` owns the root it is given. It calls `release` when
  the render ends, whether it succeeded or not.
- `mustache_message` gives the message of the last failure.

| errno | why |
|---|---|
| `EILSEQ` | the template does not compile |
| `EINVAL` | `{{x}}` names a list or a map, or an argument is wrong |
| `ELOOP` | nesting too deep |
| `ETIME` | the render does more work than its limit |
| `E2BIG` | the answer is larger than its limit |
| `ENOMEM` | an allocation failed |
| `EBADF` | the handle is not a template |
| `EBUSY` | the template renders already, for example from a callback |

## What the library does not do

- Lambdas (`~lambdas` in the specification). Data never runs code in a
  render.
- Dynamic names (`~dynamic-names`). The data never picks a template.

The specification is in `refs/mustache-spec/`, byte for byte.

## Tests and benchmarks

```sh
cmake -S . -B build -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined"
cmake --build build
ctest --test-dir build
```

The tests need simdjson. They run every case of the specification
through the C++ host and through the C API.

`bench/build.sh` builds one benchmark binary for each workload and each
compiler, and `bench/run.sh` runs them. They need Google Benchmark.

## License

Apache-2.0.
