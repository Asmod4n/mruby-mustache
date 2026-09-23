#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <span>
#include <string_view>
#include <type_traits>

#include "escape.hpp"

namespace mustache {

inline constexpr size_t kSlack = 4096;

constexpr size_t
escaped_size_of(const std::string_view s)
{
  size_t size = 0;
  for (const char c : s) size += entity_length(kEntities.word.at((unsigned char)c));
  return size;
}

constexpr bool
escaped_fits(const std::string_view s, const size_t room)
{
  if (s.size() <= room / kEntityMax) [[likely]] return true;
  return escaped_size_of(s) <= room;
}

struct SizeHint {
  size_t value = 0;

  constexpr size_t get() const { return value + value / 8 + 75; }

  constexpr void update(const size_t size)
  {
    const size_t old = value == 0 ? size : value;
    value = old - old / 4 + size / 4;
  }
};

constexpr char *
escaped_into(char *const w, const std::string_view s)
{
  if (std::is_constant_evaluated()) return escape_bytes(w, s);
  return escape_into(w, s);
}

struct Out {
  char *w;
  char *end;
  bool  full = false;

  constexpr size_t room() const { return (size_t)(end - w); }

  constexpr void raw(const std::string_view s)
  {
    if (room() < s.size()) [[unlikely]] {
      full = true;
      return;
    }
    w = std::copy_n(s.begin(), s.size(), w);
  }

  constexpr void escaped(const std::string_view s)
  {
    if (!escaped_fits(s, room())) [[unlikely]] {
      full = true;
      return;
    }
    w = escaped_into(w, s);
  }
};

constexpr Out
out_over(const std::span<char> buffer)
{
  if (buffer.size() <= kEscapeSlack) return Out{buffer.data(), buffer.data()};
  return Out{buffer.data(), buffer.data() + (buffer.size() - kEscapeSlack)};
}

inline constexpr size_t kWithoutCopyAbove = 64;

constexpr size_t
pieces_max_of(const size_t capacity, const size_t without_copy_above)
{
  return 2 * (capacity / (without_copy_above + 1)) + 1;
}

template <size_t kReferAbove>
struct Runs {
  std::span<std::string_view> parts;
  std::span<char>             buffer;
  size_t                      used = 0;
  size_t                      written = 0;
  size_t                      referred = 0;
  size_t                      run_from = 0;
  bool                        full = false;

  constexpr size_t room() const { return buffer.size() - written - referred; }

  constexpr void close_run()
  {
    if (written == run_from) return;
    if (used == parts.size()) [[unlikely]] {
      full = true;
      return;
    }
    const std::span<char> run = buffer.subspan(run_from, written - run_from);
    parts[used++] = std::string_view(run.data(), run.size());
    run_from = written;
  }

  constexpr void raw(const std::string_view s)
  {
    if (room() < s.size()) [[unlikely]] {
      full = true;
      return;
    }
    if (s.size() > kReferAbove) {
      close_run();
      if (used == parts.size()) [[unlikely]] {
        full = true;
        return;
      }
      parts[used++] = s;
      referred += s.size();
      return;
    }
    std::ranges::copy(s, buffer.subspan(written).begin());
    written += s.size();
  }

  constexpr void escaped(const std::string_view s)
  {
    if (!escaped_fits(s, room())) [[unlikely]] {
      full = true;
      return;
    }
    char *const from = buffer.subspan(written).data();
    written += (size_t)std::distance(from, escaped_into(from, s));
  }

  constexpr std::span<const std::string_view> closed()
  {
    close_run();
    return parts.first(used);
  }
};

template <size_t kReferAbove>
constexpr Runs<kReferAbove>
runs_over(const std::span<std::string_view> parts, const std::span<char> buffer)
{
  if (buffer.size() <= kEscapeSlack) return Runs<kReferAbove>{parts, buffer.first(0)};
  return Runs<kReferAbove>{parts, buffer.first(buffer.size() - kEscapeSlack)};
}

struct Spread {
  std::span<const std::span<char>> buffers;
  size_t                           at = 0;
  size_t                           written = 0;
  bool                             full = false;

  constexpr size_t room() const { return at < buffers.size() ? buffers[at].size() - written : 0; }

  constexpr void advance_if_filled()
  {
    if (at < buffers.size() && written == buffers[at].size()) {
      at++;
      written = 0;
    }
  }

  constexpr void raw(std::string_view s)
  {
    while (!s.empty()) {
      advance_if_filled();
      if (at == buffers.size()) [[unlikely]] {
        full = true;
        return;
      }
      const size_t take = std::min(s.size(), room());
      std::copy_n(s.begin(), take, buffers[at].data() + written);
      written += take;
      s.remove_prefix(take);
    }
    advance_if_filled();
  }

  constexpr void escaped(std::string_view s)
  {
    while (!s.empty()) {
      advance_if_filled();
      if (at == buffers.size()) [[unlikely]] {
        full = true;
        return;
      }
      const size_t fitting = std::min(s.size(), room() / kEntityMax);
      if (fitting > 0) {
        char *const from = buffers[at].data() + written;
        written += (size_t)std::distance(from, escaped_into(from, s.substr(0, fitting)));
        s.remove_prefix(fitting);
        continue;
      }
      std::array<char, 8> entity{};
      char *const end = entity_into(entity.data(), kEntities.word.at((unsigned char)s.front()));
      raw(std::string_view(entity.data(), (size_t)(end - entity.data())));
      s.remove_prefix(1);
    }
    advance_if_filled();
  }

  constexpr size_t filled_count() const
  {
    const size_t whole = std::min(at, buffers.size());
    return whole + (at < buffers.size() && written > 0 ? 1 : 0);
  }

  constexpr std::string_view filled(const size_t which) const
  {
    const std::span<char> buffer = buffers[which];
    return std::string_view(buffer.data(), which < at ? buffer.size() : written);
  }
};

}
