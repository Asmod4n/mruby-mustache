#pragma once

#include <mustache-c/mustache.h>
#include <mustache/std.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

// Drives one render through the C API the way a host in another
// language does: it calls mustache_next until the render ends and
// answers every request from the std_host values. The same answers the
// C++ Walk gets through its tags come back here as calls, so a case
// that passes one way and fails the other shows a fault of the steps.
struct Pulled {
  int         result;
  int         error;
  std::string answer;
  bool        done_seen;
};

inline Pulled
pulled(mustache_template *const tpl, const mustache::std_host::Value &root,
       const std::unordered_map<std::string, mustache_template *> &partials)
{
  using mustache::std_host::Map;
  using mustache::std_host::Value;
  mustache::std_host::Host host;
  Pulled out{0, 0, {}, false};
  // The string is a heap block of exactly the capacity the render asked
  // for, so a write past it is a write past the block that ASan sees.
  std::unique_ptr<char[]> string;
  size_t string_capacity = 0;
  if (mustache_render(tpl, &root) != 0) return {-1, errno, {}, false};
  for (;;) {
    const void *value = nullptr;
    const char *text = nullptr;
    size_t size = 0;
    size_t capacity = 0;
    const int r = mustache_next(tpl, &value, &text, &size, &capacity);
    const Value *const v = static_cast<const Value *>(value);
    switch (r) {
      case MUSTACHE_RENDER:
        return out;
      case MUSTACHE_FIND: {
        const Map *const map = std::get_if<Map>(&v->v);
        const auto found = map == nullptr ? Map::const_iterator() : map->find(std::string_view(text, size));
        const void *const answer = map == nullptr || found == map->end() ? nullptr : &found->second;
        mustache_answer(tpl, answer, nullptr, 0);
        break;
      }
      case MUSTACHE_KIND:
        mustache_answer(tpl, nullptr, nullptr, (size_t)mustache::kind_of(host, v));
        break;
      case MUSTACHE_TEXT: {
        const std::string_view s = mustache::text_of(host, v);
        mustache_answer(tpl, nullptr, s.data(), s.size());
        break;
      }
      case MUSTACHE_SIZE:
        mustache_answer(tpl, nullptr, nullptr, mustache::size_of(host, v));
        break;
      case MUSTACHE_ELEMENT:
        mustache_answer(tpl, mustache::element(host, v, size), nullptr, 0);
        break;
      case MUSTACHE_PARTIAL: {
        const auto found = partials.find(std::string(text, size));
        mustache_answer(tpl, found == partials.end() ? nullptr : found->second, nullptr, 0);
        break;
      }
      case MUSTACHE_NEW_STRING:
      case MUSTACHE_GROW: {
        std::unique_ptr<char[]> grown = std::make_unique_for_overwrite<char[]>(capacity);
        if (string) std::copy_n(string.get(), std::min(size, string_capacity), grown.get());
        string = std::move(grown);
        string_capacity = capacity;
        mustache_answer_string(tpl, string.get(), capacity);
        break;
      }
      case MUSTACHE_DONE:
        out.answer.assign(string.get(), size);
        out.done_seen = true;
        break;
      default:
        out.result = -1;
        out.error = errno;
        break;
    }
  }
}
