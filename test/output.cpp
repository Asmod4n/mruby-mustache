#include <mustache/std.hpp>

#include "pull.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace {

using mustache::std_host::Host;
using mustache::std_host::Key;
using mustache::std_host::Map;
using mustache::std_host::Value;
using Program = mustache::Program<Key>;

struct Rendered {
  mustache::Fault fault;
  std::string     answer;
};

Rendered
rendered(const std::string_view source, const Map &data, const size_t initial_capacity, const size_t max_capacity)
{
  Host h;
  const std::optional<Program> p = mustache::program_of<Key>(h, source, mustache::kSourceMax);
  if (!p) return {mustache::Fault::parse, {}};
  const Value root{data};
  const mustache::Fault f =
      mustache::Walk<Host, const Value *, Key>(h, mustache::kMaxRenderScore, initial_capacity, max_capacity)
          .run(*p, &root);
  return {f, h.answer};
}

// The same render through the C API. The string grows by requests
// there, from the size of the last output of the template.
Rendered
rendered_through_c(mustache_template *const tpl, const Map &data)
{
  const Value root{data};
  const Pulled r = pulled(tpl, root, {});
  return {r.result == 0 && r.done_seen ? mustache::Fault::none : mustache::Fault::no_memory, r.answer};
}

std::string
repeated(const std::string_view s, const size_t n)
{
  std::string r;
  for (size_t i = 0; i < n; i++) r += s;
  return r;
}

int failed = 0;

void
expect(const bool ok, const char *const what, const size_t n)
{
  if (ok) return;
  failed++;
  std::printf("FAIL %s at %zu\n", what, n);
}

}

int
main()
{
  // The string of the host starts at the initial capacity and doubles.
  // A value that does not fit makes it grow before the copy. Every
  // length around a doubled capacity has to come back whole, from an
  // initial capacity of one byte as well as from the default.
  for (const size_t initial : {size_t{1}, mustache::kInitialCapacity}) {
    for (const size_t n : {4000, 4090, 4095, 4096, 4097, 8191, 8192, 8193, 20000}) {
      Map m;
      m.insert_or_assign("a", Value{std::string(n, 'x')});
      m.insert_or_assign("b", Value{repeated("<&", 3000)});
      m.insert_or_assign("c", Value{std::string(n, 'y')});
      const Rendered r = rendered("{{{a}}}{{b}}{{{c}}}", m, initial, mustache::kMaxCapacity);
      const std::string expected = std::string(n, 'x') + repeated("&lt;&amp;", 3000) + std::string(n, 'y');
      expect(r.fault == mustache::Fault::none && r.answer == expected, "growth", n);
    }
  }

  // The same lengths through the C API. One template renders them all,
  // so each render starts from the size of the one before it, and a
  // smaller answer after a larger one has to come back whole too.
  {
    mustache_template *tpl = nullptr;
    const std::string_view source = "{{{a}}}{{b}}{{{c}}}";
    mustache_compile(source.data(), source.size(), &tpl);
    for (const size_t n : {4000, 20000, 4090, 4095, 4096, 4097, 8191, 8192, 8193, 10}) {
      Map m;
      m.insert_or_assign("a", Value{std::string(n, 'x')});
      m.insert_or_assign("b", Value{repeated("<&", 3000)});
      m.insert_or_assign("c", Value{std::string(n, 'y')});
      const Rendered r = rendered_through_c(tpl, m);
      const std::string expected = std::string(n, 'x') + repeated("&lt;&amp;", 3000) + std::string(n, 'y');
      expect(r.fault == mustache::Fault::none && r.answer == expected, "growth through C", n);
    }
    mustache_dispose_template(tpl);
  }

  // An escaped value grows up to six times. The string grows to the
  // exact escaped size before the escape, and the escape writes whole
  // 32-byte blocks, so the end of the escape near a doubled capacity is
  // where a write past the end would show under ASan.
  for (size_t n = 4080; n <= 4100; n++) {
    Map m;
    m.insert_or_assign("a", Value{std::string(n, 'x')});
    m.insert_or_assign("b", Value{std::string(700, '"')});
    const Rendered r = rendered("{{{a}}}{{b}}", m, mustache::kInitialCapacity, mustache::kMaxCapacity);
    expect(r.fault == mustache::Fault::none && r.answer == std::string(n, 'x') + repeated("&quot;", 700),
           "escape near the end", n);
    mustache_template *tpl = nullptr;
    const std::string_view source = "{{{a}}}{{b}}";
    mustache_compile(source.data(), source.size(), &tpl);
    const Rendered c = rendered_through_c(tpl, m);
    mustache_dispose_template(tpl);
    expect(c.fault == mustache::Fault::none && c.answer == std::string(n, 'x') + repeated("&quot;", 700),
           "escape near the end through C", n);
  }

  // A render that needs more than max_capacity stops with over_limit
  // and hands the host a string it can still read.
  {
    Map m;
    m.insert_or_assign("a", Value{std::string(5000, 'x')});
    const Rendered r = rendered("{{{a}}}", m, mustache::kInitialCapacity, 4096);
    expect(r.fault == mustache::Fault::over_limit, "over_limit", 5000);
  }

  std::printf("output: %s\n", failed == 0 ? "passed" : "failed");
  return failed == 0 ? 0 : 1;
}
