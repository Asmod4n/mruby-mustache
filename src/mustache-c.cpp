#include <mustache-c/mustache.h>

#include <mustache/steps.hpp>

#include <array>
#include <bit>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace mustache::c_host {

struct Key {
  std::string name;
};

struct Host {
  Fault            fault = Fault::none;
  std::string_view what{};
};

inline Key
tag_invoke(key_of_tag, Host &, const std::string_view name)
{
  return {std::string(name)};
}

inline void
tag_invoke(fail_tag, Host &h, const Fault fault, const std::string_view what, const size_t, const size_t)
{
  h.fault = fault;
  h.what = what;
}

inline constexpr uint64_t kMagic = std::bit_cast<uint64_t>(std::array<char, 8>{'m', 'u', 's', 't', 'a', 'c', 'h', 'e'});

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

}

struct mustache_template {
  uint64_t                                                     magic = mustache::c_host::kMagic;
  std::optional<mustache::Program<mustache::c_host::Key>>      program;
  mustache::Steps<const void *, mustache::c_host::Key>         steps;
  std::optional<mustache::Request>                             awaiting;
  std::string_view                                             message;
  size_t                                                       last_size = mustache::kInitialCapacity;
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

}

extern "C" int
mustache_compile(const char *const source, const size_t size, mustache_template **const tpl)
{
  if (tpl == nullptr || (source == nullptr && size != 0)) [[unlikely]] return failed(EINVAL);
  *tpl = new (std::nothrow) mustache_template();
  if (*tpl == nullptr) [[unlikely]] return failed(ENOMEM);
  mustache::c_host::Host host;
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
mustache_render(mustache_template *const tpl, const void *const root)
{
  if (!is_template(tpl)) [[unlikely]] return failed(EBADF);
  if (tpl->steps.is_running()) [[unlikely]] return failed(EBUSY);
  if (!tpl->program) [[unlikely]] return failed(EILSEQ);
  tpl->message = {};
  tpl->awaiting = std::nullopt;
  tpl->steps.initial_capacity = tpl->last_size;
  tpl->steps.start(*tpl->program, root);
  return 0;
}

extern "C" int
mustache_next(mustache_template *const tpl, const void **const value, const char **const text, size_t *const size,
              size_t *const capacity)
{
  if (!is_template(tpl)) [[unlikely]] return failed(EBADF);
  if (value == nullptr || text == nullptr || size == nullptr || capacity == nullptr) [[unlikely]] return failed(EINVAL);
  if (tpl->awaiting) [[unlikely]] return failed(EINVAL);
  const mustache::Ask<const void *> ask = tpl->steps.next();
  *value = ask.value;
  *text = ask.text.data();
  *size = ask.size;
  *capacity = ask.capacity;
  switch (ask.request) {
    case mustache::Request::find:
      *size = ask.text.size();
      tpl->awaiting = ask.request;
      return MUSTACHE_FIND;
    case mustache::Request::kind:       tpl->awaiting = ask.request; return MUSTACHE_KIND;
    case mustache::Request::text:       tpl->awaiting = ask.request; return MUSTACHE_TEXT;
    case mustache::Request::size:       tpl->awaiting = ask.request; return MUSTACHE_SIZE;
    case mustache::Request::element:    tpl->awaiting = ask.request; return MUSTACHE_ELEMENT;
    case mustache::Request::partial:
      *size = ask.text.size();
      tpl->awaiting = ask.request;
      return MUSTACHE_PARTIAL;
    case mustache::Request::new_string: tpl->awaiting = ask.request; return MUSTACHE_NEW_STRING;
    case mustache::Request::grow:       tpl->awaiting = ask.request; return MUSTACHE_GROW;
    case mustache::Request::done:
      tpl->last_size = ask.size;
      return MUSTACHE_DONE;
    case mustache::Request::finished:   return MUSTACHE_RENDER;
    case mustache::Request::failed:
      tpl->message = tpl->steps.what();
      return failed(mustache::c_host::errno_of(tpl->steps.fault()));
  }
  return MUSTACHE_RENDER;
}

extern "C" int
mustache_answer(mustache_template *const tpl, const void *const value, const char *const text, const size_t size)
{
  if (!is_template(tpl)) [[unlikely]] return failed(EBADF);
  if (!tpl->awaiting) [[unlikely]] return failed(EINVAL);
  switch (*tpl->awaiting) {
    case mustache::Request::find:
      tpl->steps.found(value == nullptr ? std::nullopt : std::optional<const void *>(value));
      break;
    case mustache::Request::element:
      tpl->steps.found(value);
      break;
    case mustache::Request::kind:
      if (size > MUSTACHE_KIND_TRUTHY) [[unlikely]] return failed(EINVAL);
      tpl->steps.kind_is(static_cast<mustache::Kind>(size));
      break;
    case mustache::Request::text:
      if (text == nullptr && size != 0) [[unlikely]] return failed(EINVAL);
      tpl->steps.text_is(std::string_view(text, size));
      break;
    case mustache::Request::size:
      tpl->steps.size_is(size);
      break;
    case mustache::Request::partial:
      if (value == nullptr) {
        tpl->steps.partial_is(nullptr);
        break;
      }
      if (!is_template(value)) [[unlikely]] return failed(EBADF);
      tpl->steps.partial_is(static_cast<const mustache_template *>(value)->program
                                ? &*static_cast<const mustache_template *>(value)->program
                                : nullptr);
      break;
    default:
      return failed(EINVAL);
  }
  tpl->awaiting = std::nullopt;
  return 0;
}

extern "C" int
mustache_answer_string(mustache_template *const tpl, char *const bytes, const size_t capacity)
{
  if (!is_template(tpl)) [[unlikely]] return failed(EBADF);
  if (tpl->awaiting != mustache::Request::new_string && tpl->awaiting != mustache::Request::grow) [[unlikely]] {
    return failed(EINVAL);
  }
  tpl->steps.string_is(bytes == nullptr ? std::span<char>() : std::span<char>(bytes, capacity));
  tpl->awaiting = std::nullopt;
  return 0;
}
