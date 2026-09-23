#pragma once

#if defined(__AVX2__)
#include <immintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mustache {

struct Entities {
  std::array<uint64_t, 256> word;
};

consteval Entities
entities_of()
{
  Entities e{};
  for (size_t c = 0; c < 256; c++) e.word.at(c) = (uint64_t)c | (uint64_t{1} << 56);
  auto put = [&e](const unsigned char c, const std::string_view s) {
    uint64_t v = (uint64_t)s.size() << 56;
    for (size_t i = 0; i < s.size(); i++) v |= (uint64_t)(unsigned char)s.at(i) << (8 * i);
    e.word.at(c) = v;
  };
  put('&', "&amp;");
  put('<', "&lt;");
  put('>', "&gt;");
  put('"', "&quot;");
  put('\'', "&#39;");
  return e;
}

inline constexpr Entities kEntities = entities_of();

inline constexpr size_t kEntityMax = 6;

inline constexpr size_t kEscapeSlack = 32;

constexpr size_t
entity_length(const uint64_t word)
{
  return (size_t)(word >> 56);
}

constexpr bool
is_marked(const char c)
{
  return entity_length(kEntities.word.at((unsigned char)c)) != 1;
}

constexpr char *
entity_into(char *const w, const uint64_t word)
{
  const std::array<char, 8> bytes = std::bit_cast<std::array<char, 8>>(word);
  std::copy_n(bytes.begin(), bytes.size(), w);
  return w + entity_length(word);
}

constexpr char *
escape_bytes(char *const out, const std::string_view s)
{
  char *w = out;
  for (const char c : s) w = entity_into(w, kEntities.word.at((unsigned char)c));
  return w;
}

#if defined(__AVX2__)

inline uint32_t
marks_of(const __m256i b)
{
  const __m256i hit = _mm256_or_si256(
      _mm256_or_si256(_mm256_cmpeq_epi8(b, _mm256_set1_epi8('&')), _mm256_cmpeq_epi8(b, _mm256_set1_epi8('<'))),
      _mm256_or_si256(_mm256_cmpeq_epi8(b, _mm256_set1_epi8('>')),
                      _mm256_or_si256(_mm256_cmpeq_epi8(b, _mm256_set1_epi8('"')),
                                      _mm256_cmpeq_epi8(b, _mm256_set1_epi8('\'')))));
  return (uint32_t)_mm256_movemask_epi8(hit);
}

inline char *
escape_into(char *const out, const std::string_view s)
{
  if (s.size() < 32) return escape_bytes(out, s);
  char *w = out;
  const auto *src = reinterpret_cast<const unsigned char *>(s.data());
  alignas(32) unsigned char block[64];
  size_t at = 0;
  for (; at + 32 <= s.size(); at += 32) {
    const __m256i b = _mm256_loadu_si256((const __m256i *)(src + at));
    uint32_t mask = marks_of(b);
    _mm256_storeu_si256((__m256i *)w, b);
    if (mask == 0) {
      w += 32;
      continue;
    }
    if (__builtin_popcount(mask) > 8) {
      w = escape_bytes(w, s.substr(at, 32));
      continue;
    }
    const unsigned char *from = src + at;
    if (at + 64 > s.size()) {
      _mm256_store_si256((__m256i *)block, b);
      _mm256_store_si256((__m256i *)(block + 32), _mm256_setzero_si256());
      from = block;
    }
    size_t done = 0;
    while (mask != 0) {
      const size_t i = (size_t)__builtin_ctz(mask);
      w += i - done;
      w = entity_into(w, kEntities.word.at(from[i]));
      done = i + 1;
      _mm256_storeu_si256((__m256i *)w, _mm256_loadu_si256((const __m256i *)(from + done)));
      mask &= mask - 1;
    }
    w += 32 - done;
  }
  return escape_bytes(w, s.substr(at));
}

#elif defined(__ARM_NEON)

inline uint64_t
marks_of(const uint8x16_t b)
{
  const uint8x16_t hit =
      vorrq_u8(vorrq_u8(vceqq_u8(b, vdupq_n_u8('&')), vceqq_u8(b, vdupq_n_u8('<'))),
               vorrq_u8(vceqq_u8(b, vdupq_n_u8('>')),
                        vorrq_u8(vceqq_u8(b, vdupq_n_u8('"')), vceqq_u8(b, vdupq_n_u8('\'')))));
  return vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(hit), 4)), 0);
}

inline char *
escape_into(char *const out, const std::string_view s)
{
  if (s.size() < 16) return escape_bytes(out, s);
  char *w = out;
  const auto *src = reinterpret_cast<const unsigned char *>(s.data());
  alignas(16) unsigned char block[32];
  size_t at = 0;
  for (; at + 16 <= s.size(); at += 16) {
    const uint8x16_t b = vld1q_u8(src + at);
    uint64_t mask = marks_of(b) & 0x8888888888888888ull;
    vst1q_u8((uint8_t *)w, b);
    if (mask == 0) {
      w += 16;
      continue;
    }
    if (__builtin_popcountll(mask) > 4) {
      w = escape_bytes(w, s.substr(at, 16));
      continue;
    }
    const unsigned char *from = src + at;
    if (at + 32 > s.size()) {
      vst1q_u8(block, b);
      vst1q_u8(block + 16, vdupq_n_u8(0));
      from = block;
    }
    size_t done = 0;
    while (mask != 0) {
      const size_t i = (size_t)__builtin_ctzll(mask) / 4;
      w += i - done;
      w = entity_into(w, kEntities.word.at(from[i]));
      done = i + 1;
      vst1q_u8((uint8_t *)w, vld1q_u8(from + done));
      mask &= mask - 1;
    }
    w += 16 - done;
  }
  return escape_bytes(w, s.substr(at));
}

#else

constexpr char *
escape_into(char *const out, const std::string_view s)
{
  return escape_bytes(out, s);
}

#endif

}
