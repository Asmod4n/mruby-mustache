#include <mustache/render.hpp>
#include <mustache/std.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using namespace mustache;
using namespace mustache::std_host;

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t *const data, const size_t size)
{
  const std::string_view all(reinterpret_cast<const char *>(data), size);
  const size_t cut = all.find('\x01');
  const std::string_view src = cut == std::string_view::npos ? all : all.substr(0, cut);
  const std::string_view partial_src = cut == std::string_view::npos ? std::string_view{} : all.substr(cut + 1);
  Host host;
  const auto program = program_of<Key>(host, src, 1 << 20);
  if (!program) return 0;
  const auto partial_program = program_of<Key>(host, partial_src, 1 << 20);
  std::unordered_map<std::string, const Program<Key> *, KeyHash, KeyEqual> partials;
  for (const char *const name : {"p", "a", "b", "q", "c"}) partials[name] = &*program;
  if (partial_program) {
    partials["q"] = &*partial_program;
    partials["c"] = &*partial_program;
  }
  host.partials = &partials;
  Map inner;
  inner.set("c", Value{std::string("y")});
  inner.set("a", Value{std::string("z'")});
  Map m;
  m.set("a", Value{std::string("<x&>\"'")});
  m.set("b", Value{std::string(80, '<')});
  m.set("m", Value{inner});
  m.set("l", Value{List{Value{std::string("1")}, Value{inner}, Value{List{}}}});
  m.set("e", Value{Map{}});
  const Value root{m};
  const Value *const rp = &root;

  Walk<Host, const Value *, Key, 8, 8> walk(host);
  walk.run(*program, rp);

  for (const size_t score : {size_t{1}, size_t{7}, size_t{64}, size_t{1} << 12}) {
    host.answer.clear();
    Walk<Host, const Value *, Key, 6, 6> scored(host, score);
    scored.run(*program, rp);
  }
  return 0;
}
