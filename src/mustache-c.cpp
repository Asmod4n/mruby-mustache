#include <mustache-c/mustache.h>

#include <mustache/render.hpp>

#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <memory_resource>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mustache::c_host {

struct Key {
  std::string name;
};

struct Callbacks {
  const void *(*find)(void *, const void *, const char *, size_t);
  int (*kind)(void *, const void *);
  const char *(*text)(void *, const void *, size_t *);
  size_t (*size)(void *, const void *);
  const void *(*element)(void *, const void *, size_t);
  const mustache_template *(*partial)(void *, const char *, size_t);
  char *(*new_string)(void *, size_t, void **, size_t *);
  char *(*grow)(void *, void **, size_t, size_t, size_t *);
  int (*done)(void *, void *, size_t);
};

struct Host {
  const Callbacks &callbacks;
  void            *user;
  size_t           capacity_hint;
  void            *string = nullptr;
  size_t           size = 0;
  int              done_errno = 0;
  Fault            fault = Fault::none;
  std::string_view what{};
  size_t           asked = 0;
  size_t           allowed = 0;
};

inline Key
tag_invoke(key_of_tag, Host &, const std::string_view name)
{
  return {std::string(name)};
}

inline std::optional<const void *>
tag_invoke(find_tag, Host &h, const void *const v, const Key &key)
{
  const void *const found = h.callbacks.find(h.user, v, key.name.data(), key.name.size());
  if (found == nullptr) return std::nullopt;
  return found;
}

inline Kind
tag_invoke(kind_of_tag, Host &h, const void *const v)
{
  const int kind = h.callbacks.kind(h.user, v);
  if (kind < MUSTACHE_KIND_FALSY || kind > MUSTACHE_KIND_TRUTHY) [[unlikely]] return Kind::falsy;
  return static_cast<Kind>(kind);
}

inline std::string_view
tag_invoke(text_of_tag, Host &h, const void *const v)
{
  size_t size = 0;
  const char *const text = h.callbacks.text(h.user, v, &size);
  if (text == nullptr) [[unlikely]] return {};
  return {text, size};
}

inline size_t
tag_invoke(size_of_tag, Host &h, const void *const v)
{
  return h.callbacks.size(h.user, v);
}

inline const void *
tag_invoke(element_tag, Host &h, const void *const v, const size_t i)
{
  return h.callbacks.element(h.user, v, i);
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

inline std::span<char>
tag_invoke(new_string_tag, Host &h, const size_t capacity)
{
  size_t real_capacity = 0;
  char *const bytes = h.callbacks.new_string(h.user, std::max(capacity, h.capacity_hint + kEscapeSlack), &h.string, &real_capacity);
  if (bytes == nullptr) [[unlikely]] return {};
  return {bytes, real_capacity};
}

inline std::span<char>
tag_invoke(grow_tag, Host &h, const size_t size, const size_t capacity)
{
  size_t real_capacity = 0;
  char *const bytes = h.callbacks.grow(h.user, &h.string, size, capacity, &real_capacity);
  if (bytes == nullptr) [[unlikely]] return {};
  return {bytes, real_capacity};
}

inline Fault
tag_invoke(done_tag, Host &h, const size_t size)
{
  h.size = size;
  if (h.callbacks.done(h.user, h.string, size) != 0) [[unlikely]] h.done_errno = errno;
  return Fault::none;
}

constexpr int
errno_of(const Fault fault)
{
  switch (fault) {
    case Fault::none:       return 0;
    case Fault::over_limit: return E2BIG;
    case Fault::not_text:   return EINVAL;
    case Fault::too_deep:   return ELOOP;
    case Fault::over_work:  return ETIME;
    case Fault::parse:      return EILSEQ;
    case Fault::no_memory:  return ENOMEM;
  }
  return EINVAL;
}

constexpr int
setter_missing_from(const Callbacks &c)
{
  if (c.find == nullptr) return MUSTACHE_SET_FIND;
  if (c.kind == nullptr) return MUSTACHE_SET_KIND;
  if (c.text == nullptr) return MUSTACHE_SET_TEXT;
  if (c.size == nullptr) return MUSTACHE_SET_SIZE;
  if (c.element == nullptr) return MUSTACHE_SET_ELEMENT;
  if (c.partial == nullptr) return MUSTACHE_SET_PARTIAL;
  if (c.new_string == nullptr) return MUSTACHE_SET_NEW_STRING;
  if (c.grow == nullptr) return MUSTACHE_SET_GROW;
  if (c.done == nullptr) return MUSTACHE_SET_DONE;
  return MUSTACHE_RENDER;
}

inline constexpr uint64_t kMagic = std::bit_cast<uint64_t>(std::array<char, 8>{'m', 'u', 's', 't', 'a', 'c', 'h', 'e'});

}

struct mustache_template {
  uint64_t                                                   magic = mustache::c_host::kMagic;
  bool                                                       rendering = false;
  std::optional<mustache::Program<mustache::c_host::Key>> program;
  mustache::c_host::Callbacks                                callbacks{};
  std::string_view                                           message;
  size_t                                                     last_size = mustache::kInitialCapacity;
  std::pmr::synchronized_pool_resource                       pool;
};

namespace {

int
failed(const int error)
{
  errno = error;
  return -1;
}

bool
is_template(const void *const handle)
{
  if (handle == nullptr) [[unlikely]] return false;
  uint64_t magic = 0;
  std::memcpy(&magic, handle, sizeof magic);
  return magic == mustache::c_host::kMagic;
}

template <class Callback>
int
set_callback(mustache_template *const tpl, Callback mustache::c_host::Callbacks::*const member, const Callback callback)
{
  if (!is_template(tpl)) [[unlikely]] return failed(EBADF);
  if (tpl->rendering) [[unlikely]] return failed(EBUSY);
  if (callback == nullptr) [[unlikely]] return failed(EINVAL);
  tpl->callbacks.*member = callback;
  return 0;
}

}

namespace mustache::c_host {

inline const Program<Key> *
tag_invoke(partial_tag, Host &h, const Key &name)
{
  const mustache_template *const sub = h.callbacks.partial(h.user, name.name.data(), name.name.size());
  if (!is_template(sub) || !sub->program) [[unlikely]] return nullptr;
  return &*sub->program;
}

}

extern "C" int
mustache_compile(const char *const source, const size_t size, mustache_template **const tpl)
{
  if (tpl == nullptr || (source == nullptr && size != 0)) [[unlikely]] return failed(EINVAL);
  *tpl = new (std::nothrow) mustache_template();
  if (*tpl == nullptr) [[unlikely]] return failed(ENOMEM);
  const mustache::c_host::Callbacks none{};
  mustache::c_host::Host host{none, nullptr, 0};
  (*tpl)->program =
      mustache::program_of<mustache::c_host::Key>(host, std::string_view(source, size), mustache::kSourceMax);
  if (!(*tpl)->program) [[unlikely]] {
    (*tpl)->message = host.what;
    return failed(mustache::c_host::errno_of(host.fault));
  }
  return 0;
}

extern "C" int
mustache_dispose_template(mustache_template *const tpl)
{
  if (!is_template(tpl)) [[unlikely]] return failed(EBADF);
  if (tpl->rendering) [[unlikely]] return failed(EBUSY);
  tpl->magic = 0;
  delete tpl;
  return 0;
}

extern "C" int
mustache_message(const mustache_template *const tpl, const char **const message, size_t *const size)
{
  if (!is_template(tpl)) [[unlikely]] return failed(EBADF);
  if (message == nullptr || size == nullptr) [[unlikely]] return failed(EINVAL);
  *message = tpl->message.data();
  *size = tpl->message.size();
  return 0;
}

extern "C" int
mustache_next(const mustache_template *const tpl)
{
  if (!is_template(tpl)) [[unlikely]] return failed(EBADF);
  return mustache::c_host::setter_missing_from(tpl->callbacks);
}

extern "C" int
mustache_set_find(mustache_template *const tpl, const void *(*const find)(void *, const void *, const char *, size_t))
{
  return set_callback(tpl, &mustache::c_host::Callbacks::find, find);
}

extern "C" int
mustache_set_kind(mustache_template *const tpl, int (*const kind)(void *, const void *))
{
  return set_callback(tpl, &mustache::c_host::Callbacks::kind, kind);
}

extern "C" int
mustache_set_text(mustache_template *const tpl, const char *(*const text)(void *, const void *, size_t *))
{
  return set_callback(tpl, &mustache::c_host::Callbacks::text, text);
}

extern "C" int
mustache_set_size(mustache_template *const tpl, size_t (*const size)(void *, const void *))
{
  return set_callback(tpl, &mustache::c_host::Callbacks::size, size);
}

extern "C" int
mustache_set_element(mustache_template *const tpl, const void *(*const element)(void *, const void *, size_t))
{
  return set_callback(tpl, &mustache::c_host::Callbacks::element, element);
}

extern "C" int
mustache_set_partial(mustache_template *const tpl, const mustache_template *(*const partial)(void *, const char *, size_t))
{
  return set_callback(tpl, &mustache::c_host::Callbacks::partial, partial);
}

extern "C" int
mustache_set_new_string(mustache_template *const tpl, char *(*const new_string)(void *, size_t, void **, size_t *))
{
  return set_callback(tpl, &mustache::c_host::Callbacks::new_string, new_string);
}

extern "C" int
mustache_set_grow(mustache_template *const tpl, char *(*const grow)(void *, void **, size_t, size_t, size_t *))
{
  return set_callback(tpl, &mustache::c_host::Callbacks::grow, grow);
}

extern "C" int
mustache_set_done(mustache_template *const tpl, int (*const done)(void *, void *, size_t))
{
  return set_callback(tpl, &mustache::c_host::Callbacks::done, done);
}

extern "C" int
mustache_render(mustache_template *const tpl, const void *const root, void (*const release)(const void *),
                void *const user, void **const string)
{
  const auto refused = [root, release](const int result) {
    if (release != nullptr) release(root);
    return result;
  };
  if (!is_template(tpl)) [[unlikely]] return refused(failed(EBADF));
  if (tpl->rendering) [[unlikely]] return refused(failed(EBUSY));
  if (string == nullptr) [[unlikely]] return refused(failed(EINVAL));
  const int missing = mustache::c_host::setter_missing_from(tpl->callbacks);
  if (missing != MUSTACHE_RENDER) [[unlikely]] return refused(missing);
  if (!tpl->program) [[unlikely]] return refused(failed(EILSEQ));
  std::shared_ptr<const void> held;
  try {
    held = std::shared_ptr<const void>(
        root,
        [release](const void *const r) {
          if (release != nullptr) release(r);
        },
        std::pmr::polymorphic_allocator<std::byte>(&tpl->pool));
  }
  catch (const std::bad_alloc &) {
    return failed(ENOMEM);
  }
  mustache::c_host::Host host{tpl->callbacks, user, tpl->last_size};
  tpl->rendering = true;
  const mustache::Fault fault =
      mustache::Walk<mustache::c_host::Host, const void *, mustache::c_host::Key>(host).run(*tpl->program, held.get());
  tpl->rendering = false;
  *string = host.string;
  tpl->message = host.what;
  if (fault != mustache::Fault::none) [[unlikely]] return failed(mustache::c_host::errno_of(fault));
  if (host.done_errno != 0) [[unlikely]] return failed(host.done_errno);
  tpl->last_size = host.size;
  return 0;
}
