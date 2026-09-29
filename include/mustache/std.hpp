#pragma once

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "render.hpp"

namespace mustache::std_host {

struct Key {
  std::string name;
  size_t      hash;
};

struct KeyHash {
  using is_transparent = void;
  size_t operator()(const std::string_view s) const { return std::hash<std::string_view>{}(s); }
  size_t operator()(const std::string &s) const { return std::hash<std::string_view>{}(s); }
  size_t operator()(const Key &k) const { return k.hash; }
};

struct KeyEqual {
  using is_transparent = void;
  bool operator()(const std::string &a, const std::string &b) const { return a == b; }
  bool operator()(const std::string &a, const Key &b) const { return a == b.name; }
  bool operator()(const Key &a, const std::string &b) const { return a.name == b; }
  bool operator()(const std::string &a, const std::string_view b) const { return a == b; }
  bool operator()(const std::string_view a, const std::string &b) const { return a == b; }
};

struct Value;
using List = std::vector<Value>;
using Map = std::unordered_map<std::string, Value, KeyHash, KeyEqual>;

struct Value {
  std::variant<std::string, List, Map, bool, std::int64_t, double, std::nullptr_t> v;
};

struct Host {
  const std::unordered_map<std::string, const Program<Key> *, KeyHash, KeyEqual> *partials = nullptr;
  Fault            fault = Fault::none;
  std::string_view what;
  size_t           asked = 0;
  size_t           allowed = 0;
  std::string      answer;
  std::array<char, 32> text{};
};

inline std::span<char>
tag_invoke(new_string_tag, Host &h, const size_t capacity)
{
  h.answer = std::string();
  return allocated_or_failed(h, "render output", capacity, [&h, capacity] {
           h.answer.resize(capacity);
           return std::span<char>(h.answer);
         }).value_or(std::span<char>());
}

inline std::span<char>
tag_invoke(grow_tag, Host &h, const size_t, const size_t capacity)
{
  return allocated_or_failed(h, "render output", capacity, [&h, capacity] {
           h.answer.resize(capacity);
           return std::span<char>(h.answer);
         }).value_or(std::span<char>());
}

inline Fault
tag_invoke(done_tag, Host &h, const size_t size)
{
  h.answer.resize(size);
  return Fault::none;
}

inline Key
tag_invoke(key_of_tag, Host &, const std::string_view name)
{
  return {std::string(name), KeyHash{}(name)};
}

inline std::optional<const Value *>
tag_invoke(find_tag, Host &, const Value *const v, const Key &key)
{
  const Map *const map = std::get_if<Map>(&v->v);
  if (map == nullptr) return std::nullopt;
  const auto found = map->find(key);
  if (found == map->end()) return std::nullopt;
  return &found->second;
}

inline Kind
tag_invoke(kind_of_tag, Host &, const Value *const v)
{
  switch (v->v.index()) {
    case 0:  return Kind::text;
    case 1:  return Kind::list;
    case 2:  return std::get<Map>(v->v).empty() ? Kind::falsy : Kind::map;
    case 3:  return std::get<bool>(v->v) ? Kind::truthy : Kind::falsy;
    case 4:
    case 5:  return Kind::text;
    default: return Kind::falsy;
  }
}

inline std::string_view
tag_invoke(text_of_tag, Host &h, const Value *const v)
{
  switch (v->v.index()) {
    case 4: {
      const auto [end, ec] = std::to_chars(h.text.data(), std::to_address(h.text.end()), std::get<std::int64_t>(v->v));
      return {h.text.data(), end};
    }
    case 5: {
      const auto [end, ec] = std::to_chars(h.text.data(), std::to_address(h.text.end()), std::get<double>(v->v));
      return {h.text.data(), end};
    }
    default: return std::get<std::string>(v->v);
  }
}

inline size_t
tag_invoke(size_of_tag, Host &, const Value *const v)
{
  return std::get<List>(v->v).size();
}

inline const Value *
tag_invoke(element_tag, Host &, const Value *const v, const size_t i)
{
  return &std::get<List>(v->v).at(i);
}

inline const Program<Key> *
tag_invoke(partial_tag, Host &h, const Key &name)
{
  if (h.partials == nullptr) return nullptr;
  const auto it = h.partials->find(name);
  return it == h.partials->end() ? nullptr : it->second;
}

inline void
tag_invoke(fail_tag, Host &h, const Fault fault, const std::string_view what, const size_t asked,
           const size_t allowed)
{
  h.fault = fault;
  h.what = what;
  h.asked = asked;
  h.allowed = allowed;
}

}
