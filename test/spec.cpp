#include <mustache/std.hpp>

#include "c_host.hpp"

#include <simdjson.h>

#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>

namespace {

using mustache::std_host::Host;
using mustache::std_host::Key;
using mustache::std_host::KeyEqual;
using mustache::std_host::KeyHash;
using mustache::std_host::List;
using mustache::std_host::Map;
using mustache::std_host::Value;
using Program = mustache::Program<Key>;
using Partials = std::unordered_map<std::string, const Program *, KeyHash, KeyEqual>;

// Each JSON type becomes the C++ type that holds it. A number stays a
// number, so the render writes it with std::to_chars, and null stays
// null, a value of its own that is falsy.
Value
value_of(simdjson::ondemand::value v)
{
  switch (v.type().value()) {
    case simdjson::ondemand::json_type::object: {
      Map m;
      for (auto field : v.get_object()) {
        const std::string key(field.unescaped_key().value());
        m.insert_or_assign(key, value_of(field.value()));
      }
      return Value{std::move(m)};
    }
    case simdjson::ondemand::json_type::array: {
      List l;
      for (auto e : v.get_array()) l.push_back(value_of(e.value()));
      return Value{std::move(l)};
    }
    case simdjson::ondemand::json_type::string:
      return Value{std::string(v.get_string().value())};
    case simdjson::ondemand::json_type::number:
      if (v.get_number_type().value() == simdjson::ondemand::number_type::floating_point_number) {
        return Value{v.get_double().value()};
      }
      return Value{v.get_int64().value()};
    case simdjson::ondemand::json_type::boolean:
      return Value{v.get_bool().value()};
    default:
      return Value{nullptr};
  }
}

struct Case {
  std::string                                  name;
  std::string                                  source;
  std::string                                  expected;
  Value                                        data;
  std::unordered_map<std::string, std::string> partials;
};

Case
case_of(simdjson::ondemand::object t)
{
  Case c{};
  for (auto field : t) {
    const std::string_view key = field.unescaped_key().value();
    if (key == "name") c.name = std::string(field.value().get_string().value());
    else if (key == "template") c.source = std::string(field.value().get_string().value());
    else if (key == "expected") c.expected = std::string(field.value().get_string().value());
    else if (key == "data") c.data = value_of(field.value());
    else if (key == "partials") {
      for (auto p : field.value().get_object()) {
        c.partials.emplace(std::string(p.unescaped_key().value()), std::string(p.value().get_string().value()));
      }
    }
  }
  return c;
}

// Each case compiles its partials first, the way a caller hands them to
// a render. A template or a partial that does not compile is a failure
// of the case, because every source in the spec is valid Mustache.
bool
renders_as_expected(const Case &c)
{
  Host h;
  std::unordered_map<std::string, Program> compiled;
  Partials partials;
  for (const auto &[name, source] : c.partials) {
    std::optional<Program> p = mustache::program_of<Key>(h, source, mustache::kSourceMax);
    if (!p) return false;
    partials.emplace(name, &compiled.emplace(name, std::move(*p)).first->second);
  }
  h.partials = &partials;
  const std::optional<Program> p = mustache::program_of<Key>(h, c.source, mustache::kSourceMax);
  if (!p) return false;
  const mustache::Fault f = mustache::Walk<Host, const Value *, Key>(h).run(*p, &c.data);
  return f == mustache::Fault::none && h.answer == c.expected;
}

// The same cases run through the C API, partials included: the partial
// callback gives the handle of a compiled partial.
bool
renders_as_expected_through_c(const Case &c)
{
  std::unordered_map<std::string, mustache_template *> partials;
  bool compiled = true;
  for (const auto &[name, source] : c.partials) {
    mustache_template *p = nullptr;
    compiled = mustache_compile(source.data(), source.size(), &p) == 0 && compiled;
    partials.emplace(name, p);
  }
  mustache_template *tpl = nullptr;
  compiled = mustache_compile(c.source.data(), c.source.size(), &tpl) == 0 && compiled;
  CUser user;
  user.partials = &partials;
  void *string = nullptr;
  int rendered = -1;
  if (compiled) {
    c_set_callbacks(tpl);
    rendered = mustache_render(tpl, &c.data, c_release, &user, &string);
  }
  mustache_dispose_template(tpl);
  for (const auto &[name, p] : partials) mustache_dispose_template(p);
  return rendered == 0 && string == &user && user.answer == c.expected;
}

}

// One run reads one file of the spec and renders every case in it. A
// case that fails prints its name. The file holds the template, the
// data and the expected answer of that name.
int
main(const int argc, const char *const argv[])
{
  const bool through_c = argc == 3 && std::string_view(argv[2]) == "c";
  if (argc != 2 && !through_c) [[unlikely]] {
    std::fprintf(stderr, "usage: %s spec.json [c]\n", argv[0]);
    return 2;
  }
  simdjson::padded_string json;
  if (simdjson::padded_string::load(argv[1]).get(json) != simdjson::SUCCESS) [[unlikely]] {
    std::fprintf(stderr, "%s: cannot read\n", argv[1]);
    return 2;
  }
  simdjson::ondemand::parser parser;
  simdjson::ondemand::document doc = parser.iterate(json);
  int failed = 0;
  int passed = 0;
  for (auto t : doc["tests"].get_array()) {
    const Case c = case_of(t.get_object());
    if (through_c ? renders_as_expected_through_c(c) : renders_as_expected(c)) {
      passed++;
      continue;
    }
    failed++;
    std::printf("FAIL %s: %s\n", argv[1], c.name.c_str());
  }
  std::printf("%s%s: %d passed, %d failed\n", argv[1], through_c ? " through C" : "", passed, failed);
  return failed == 0 ? 0 : 1;
}
