#pragma once

#include <cstddef>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <variant>

#include "render.hpp"

namespace mustache::container_host {

template <class T>
concept text_like = std::is_convertible_v<const T &, std::string_view>;

template <class T>
concept map_like = requires(const T &m, const std::string &k) {
  typename T::key_type;
  typename T::mapped_type;
  { m.find(k) } -> std::same_as<typename T::const_iterator>;
  { m.end() } -> std::same_as<typename T::const_iterator>;
  { m.empty() } -> std::convertible_to<bool>;
} && std::is_convertible_v<const typename T::key_type &, std::string_view> &&
                   std::is_lvalue_reference_v<std::iter_reference_t<typename T::const_iterator>>;

template <class T>
concept list_like = std::ranges::random_access_range<const T> &&
                    std::is_lvalue_reference_v<std::ranges::range_reference_t<const T>> && !text_like<T> && !map_like<T>;

template <class T>
inline constexpr bool is_variant_v = false;
template <class... A>
inline constexpr bool is_variant_v<std::variant<A...>> = true;

struct Key {
  std::string name;
};

struct Table;

struct Value {
  const void  *data;
  const Table *table;
};

struct Table {
  Kind (*kind_of)(const void *);
  std::string_view (*text_of)(const void *);
  size_t (*size_of)(const void *);
  Value (*element)(const void *, size_t);
  std::optional<Value> (*find)(const void *, const Key &);
};

template <class T>
Value value_of(const T &v);

template <class T>
struct TableOf {
  static Kind kind_of(const void *const p)
  {
    const T &v = *static_cast<const T *>(p);
    if constexpr (is_variant_v<T>) {
      return std::visit([](const auto &x) { return value_of(x).table->kind_of(&x); }, v);
    }
    else if constexpr (text_like<T>) {
      return Kind::text;
    }
    else if constexpr (map_like<T>) {
      return v.empty() ? Kind::falsy : Kind::map;
    }
    else {
      static_assert(list_like<T>, "a value is a map, a list or a string");
      return Kind::list;
    }
  }

  static std::string_view text_of(const void *const p)
  {
    const T &v = *static_cast<const T *>(p);
    if constexpr (is_variant_v<T>) {
      return std::visit([](const auto &x) { return value_of(x).table->text_of(&x); }, v);
    }
    else if constexpr (text_like<T>) {
      return std::string_view(v);
    }
    else {
      return {};
    }
  }

  static size_t size_of(const void *const p)
  {
    const T &v = *static_cast<const T *>(p);
    if constexpr (is_variant_v<T>) {
      return std::visit([](const auto &x) { return value_of(x).table->size_of(&x); }, v);
    }
    else if constexpr (list_like<T>) {
      return (size_t)std::ranges::size(v);
    }
    else {
      return 0;
    }
  }

  static Value element(const void *const p, const size_t i)
  {
    const T &v = *static_cast<const T *>(p);
    if constexpr (is_variant_v<T>) {
      return std::visit([i](const auto &x) { return value_of(x).table->element(&x, i); }, v);
    }
    else if constexpr (list_like<T>) {
      return value_of(*std::ranges::next(std::ranges::begin(v), (std::ptrdiff_t)i));
    }
    else {
      return value_of(v);
    }
  }

  static std::optional<Value> find(const void *const p, const Key &key)
  {
    const T &v = *static_cast<const T *>(p);
    if constexpr (is_variant_v<T>) {
      return std::visit([&key](const auto &x) { return value_of(x).table->find(&x, key); }, v);
    }
    else if constexpr (map_like<T>) {
      const auto it = v.find(key.name);
      if (it == v.end()) return std::nullopt;
      return value_of(it->second);
    }
    else {
      return std::nullopt;
    }
  }

  static constexpr Table kTable{kind_of, text_of, size_of, element, find};
};

template <class T>
Value
value_of(const T &v)
{
  return Value{&v, &TableOf<T>::kTable};
}

struct Host {
  const std::unordered_map<std::string, const Program<Key> *> *partials = nullptr;
  Fault                                                          fault = Fault::none;
  std::string_view                                               what;
  size_t                                                         asked = 0;
  size_t                                                         allowed = 0;
};

inline Key
tag_invoke(key_of_tag, Host &, const std::string_view name)
{
  return {std::string(name)};
}

inline std::optional<Value>
tag_invoke(find_tag, Host &, const Value v, const Key &key)
{
  return v.table->find(v.data, key);
}

inline Kind
tag_invoke(kind_of_tag, Host &, const Value v)
{
  return v.table->kind_of(v.data);
}

inline std::string_view
tag_invoke(text_of_tag, Host &, const Value v)
{
  return v.table->text_of(v.data);
}

inline size_t
tag_invoke(size_of_tag, Host &, const Value v)
{
  return v.table->size_of(v.data);
}

inline Value
tag_invoke(element_tag, Host &, const Value v, const size_t i)
{
  return v.table->element(v.data, i);
}

inline const Program<Key> *
tag_invoke(partial_tag, Host &h, const Key &name)
{
  if (h.partials == nullptr) return nullptr;
  const auto it = h.partials->find(name.name);
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
