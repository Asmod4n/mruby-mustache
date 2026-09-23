#pragma once

#include <cstddef>
#include <functional>
#include <optional>
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
struct Entry;
using List = std::vector<Value>;

class Map {
public:
  void set(std::string key, Value value);
  const Value *find(const Key &key) const;
  bool empty() const;
  size_t size() const;

private:
  std::vector<Entry> entries_;
};

struct Value {
  std::variant<std::string, List, Map> v;
};

struct Entry {
  size_t      hash;
  std::string key;
  Value       value;
};

inline bool
Map::empty() const
{
  return entries_.empty();
}

inline size_t
Map::size() const
{
  return entries_.size();
}

inline void
Map::set(std::string key, Value value)
{
  const size_t hash = KeyHash{}(std::string_view(key));
  for (Entry &e : entries_) {
    if (e.hash == hash && e.key == key) {
      e.value = std::move(value);
      return;
    }
  }
  entries_.push_back(Entry{hash, std::move(key), std::move(value)});
}

inline const Value *
Map::find(const Key &key) const
{
  for (const Entry &e : entries_) {
    if (e.hash == key.hash && e.key == key.name) return &e.value;
  }
  return nullptr;
}

struct Host {
  const std::unordered_map<std::string, const Program<Key> *, KeyHash, KeyEqual> *partials = nullptr;
  Fault            fault = Fault::none;
  std::string_view what;
  size_t           asked = 0;
  size_t           allowed = 0;
};

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
  const Value *const found = map->find(key);
  if (found == nullptr) return std::nullopt;
  return found;
}

inline Kind
tag_invoke(kind_of_tag, Host &, const Value *const v)
{
  switch (v->v.index()) {
    case 0:  return Kind::text;
    case 1:  return Kind::list;
    default: return std::get<Map>(v->v).empty() ? Kind::falsy : Kind::map;
  }
}

inline std::string_view
tag_invoke(text_of_tag, Host &, const Value *const v)
{
  return std::get<std::string>(v->v);
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
