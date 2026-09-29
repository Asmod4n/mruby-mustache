#pragma once

#include <mustache-c/mustache.h>
#include <mustache/std.hpp>

#include <algorithm>
#include <cerrno>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

// A host of the C API whose callbacks read the std_host values. The C++
// Walk reads the same values through its tags, so a case that passes one
// way and fails the other shows a fault of the C API.
//
// The string is a heap block of exactly the capacity the render asks
// for, so a write past it is a write past the block that ASan sees.
struct CUser {
  mustache::std_host::Host                                    host;
  const std::unordered_map<std::string, mustache_template *> *partials = nullptr;
  std::unique_ptr<char[]>                                     block;
  size_t                                                      block_size = 0;
  std::string                                                 answer;
  int                                                         released = 0;
};

inline const void *
c_find(void *, const void *const v, const char *const key, const size_t size)
{
  using mustache::std_host::Map;
  const Map *const map = std::get_if<Map>(&static_cast<const mustache::std_host::Value *>(v)->v);
  if (map == nullptr) return nullptr;
  const auto found = map->find(std::string_view(key, size));
  return found == map->end() ? nullptr : &found->second;
}

inline int
c_kind(void *const user, const void *const v)
{
  return (int)mustache::kind_of(static_cast<CUser *>(user)->host, static_cast<const mustache::std_host::Value *>(v));
}

inline const char *
c_text(void *const user, const void *const v, size_t *const size)
{
  const std::string_view text =
      mustache::text_of(static_cast<CUser *>(user)->host, static_cast<const mustache::std_host::Value *>(v));
  *size = text.size();
  return text.data();
}

inline size_t
c_size(void *const user, const void *const v)
{
  return mustache::size_of(static_cast<CUser *>(user)->host, static_cast<const mustache::std_host::Value *>(v));
}

inline const void *
c_element(void *const user, const void *const v, const size_t i)
{
  return mustache::element(static_cast<CUser *>(user)->host, static_cast<const mustache::std_host::Value *>(v), i);
}

inline const mustache_template *
c_partial(void *const user, const char *const name, const size_t size)
{
  const CUser *const u = static_cast<CUser *>(user);
  if (u->partials == nullptr) return nullptr;
  const auto found = u->partials->find(std::string(name, size));
  return found == u->partials->end() ? nullptr : found->second;
}

inline char *
c_new_string(void *const user, const size_t capacity, void **const string, size_t *const real_capacity)
{
  CUser *const u = static_cast<CUser *>(user);
  u->block = std::make_unique_for_overwrite<char[]>(capacity);
  u->block_size = capacity;
  *string = u;
  *real_capacity = capacity;
  return u->block.get();
}

inline char *
c_grow(void *, void **const string, const size_t size, const size_t capacity, size_t *const real_capacity)
{
  CUser *const u = static_cast<CUser *>(*string);
  std::unique_ptr<char[]> grown = std::make_unique_for_overwrite<char[]>(capacity);
  std::copy_n(u->block.get(), std::min(size, u->block_size), grown.get());
  u->block = std::move(grown);
  u->block_size = capacity;
  *real_capacity = capacity;
  return u->block.get();
}

inline int
c_done(void *, void *const string, const size_t size)
{
  CUser *const u = static_cast<CUser *>(string);
  u->answer.assign(u->block.get(), size);
  u->block.reset();
  return 0;
}

inline void
c_release(const void *)
{
}

inline void
c_set_callbacks(mustache_template *const tpl)
{
  mustache_set_find(tpl, c_find);
  mustache_set_kind(tpl, c_kind);
  mustache_set_text(tpl, c_text);
  mustache_set_size(tpl, c_size);
  mustache_set_element(tpl, c_element);
  mustache_set_partial(tpl, c_partial);
  mustache_set_new_string(tpl, c_new_string);
  mustache_set_grow(tpl, c_grow);
  mustache_set_done(tpl, c_done);
}
