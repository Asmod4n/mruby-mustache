#include <mustache-c/mustache.h>
#include <mustache/std.hpp>

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

// The same cases run through the C API, with a host whose callbacks
// read the std_host values. The C API resolves no partials, so a case
// with partials is left to the C++ run.
struct CUser {
  Host        host;
  std::string answer;
};

const void *
c_find(void *, const void *const v, const char *const key, const size_t size)
{
  const Map *const map = std::get_if<Map>(&static_cast<const Value *>(v)->v);
  if (map == nullptr) return nullptr;
  const auto found = map->find(std::string_view(key, size));
  return found == map->end() ? nullptr : &found->second;
}

int
c_kind(void *const user, const void *const v)
{
  return (int)mustache::kind_of(static_cast<CUser *>(user)->host, static_cast<const Value *>(v));
}

const char *
c_text(void *const user, const void *const v, size_t *const size)
{
  const std::string_view text = mustache::text_of(static_cast<CUser *>(user)->host, static_cast<const Value *>(v));
  *size = text.size();
  return text.data();
}

size_t
c_size(void *const user, const void *const v)
{
  return mustache::size_of(static_cast<CUser *>(user)->host, static_cast<const Value *>(v));
}

const void *
c_element(void *const user, const void *const v, const size_t i)
{
  return mustache::element(static_cast<CUser *>(user)->host, static_cast<const Value *>(v), i);
}

char *
c_new_string(void *const user, const size_t capacity, void **const string, size_t *const real_capacity)
{
  std::string &answer = static_cast<CUser *>(user)->answer;
  answer.resize(capacity);
  *string = &answer;
  *real_capacity = answer.size();
  return answer.data();
}

char *
c_grow(void *, void **const string, const size_t, const size_t capacity, size_t *const real_capacity)
{
  std::string &answer = *static_cast<std::string *>(*string);
  answer.resize(capacity);
  *real_capacity = answer.size();
  return answer.data();
}

int
c_done(void *, void *const string, const size_t size)
{
  static_cast<std::string *>(string)->resize(size);
  return 0;
}

bool
renders_as_expected_through_c(const Case &c)
{
  mustache_template *tpl = nullptr;
  if (mustache_compile(c.source.data(), c.source.size(), &tpl) != 0) {
    mustache_dispose_template(tpl);
    return false;
  }
  mustache_set_find(tpl, c_find);
  mustache_set_kind(tpl, c_kind);
  mustache_set_text(tpl, c_text);
  mustache_set_size(tpl, c_size);
  mustache_set_element(tpl, c_element);
  mustache_set_new_string(tpl, c_new_string);
  mustache_set_grow(tpl, c_grow);
  mustache_set_done(tpl, c_done);
  CUser user;
  void *string = nullptr;
  const int rendered = mustache_render(tpl, &c.data, nullptr, &user, &string);
  mustache_dispose_template(tpl);
  return rendered == 0 && string == &user.answer && user.answer == c.expected;
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
  int skipped = 0;
  for (auto t : doc["tests"].get_array()) {
    const Case c = case_of(t.get_object());
    if (through_c && !c.partials.empty()) {
      skipped++;
      continue;
    }
    if (through_c ? renders_as_expected_through_c(c) : renders_as_expected(c)) {
      passed++;
      continue;
    }
    failed++;
    std::printf("FAIL %s: %s\n", argv[1], c.name.c_str());
  }
  std::printf("%s%s: %d passed, %d failed, %d skipped\n", argv[1], through_c ? " through C" : "", passed, failed,
              skipped);
  return failed == 0 ? 0 : 1;
}
