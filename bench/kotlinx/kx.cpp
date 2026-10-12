#include <mustache/reflect.hpp>
#include <mustache/std.hpp>
#include <benchmark/benchmark.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include "source.inc"

namespace {
struct Stock { std::string symbol, url, name, price, change, ratio; };
struct Item { std::string rowClass, index, negativeClass; Stock value; };
struct Data { std::vector<Item> items; };

std::vector<Stock> stocks_of()
{
  const std::vector<Stock> stocks = {
      {"ADBE", "http://www.adobe.com", "Adobe Systems", "39.26", "0.13", "0.33"},
      {"AMD", "http://www.amd.com", "Advanced Micro Devices", "16.22", "0.17", "1.06"},
      {"AMZN", "http://www.amazon.com", "Amazon.com", "36.85", "-0.23", "-0.62"},
      {"AAPL", "http://www.apple.com", "Apple", "85.38", "-0.87", "-1.01"},
      {"BEAS", "http://www.bea.com", "BEA Systems", "12.46", "0.09", "0.73"},
      {"CA", "http://www.ca.com", "CA", "24.66", "0.38", "1.57"},
      {"CSCO", "http://www.cisco.com", "Cisco Systems", "26.35", "0.13", "0.5"},
      {"DELL", "http://www.dell.com/", "Dell", "23.73", "-0.42", "-1.74"},
      {"EBAY", "http://www.ebay.com", "eBay", "31.65", "-0.8", "-2.47"},
      {"GOOG", "http://www.google.com", "Google", "495.84", "7.75", "1.59"},
      {"HPQ", "http://www.hp.com", "Hewlett-Packard", "41.69", "-0.02", "-0.05"},
      {"IBM", "http://www.ibm.com", "IBM", "97.45", "-0.06", "-0.06"},
      {"INTC", "http://www.intel.com", "Intel", "20.53", "-0.07", "-0.34"},
      {"JNPR", "http://www.juniper.net/", "Juniper Networks", "18.96", "0.5", "2.71"},
      {"MSFT", "http://www.microsoft.com", "Microsoft", "30.6", "0.15", "0.49"},
      {"ORCL", "http://www.oracle.com", "Oracle", "17.15", "0.17", "1.1"},
      {"SAP", "http://www.sap.com", "SAP", "46.2", "-0.16", "-0.35"},
      {"STX", "http://www.seagate.com/", "Seagate Technology", "27.35", "-0.36", "-1.3"},
      {"SUNW", "http://www.sun.com", "Sun Microsystems", "6.33", "-0.01", "-0.16"},
      {"YHOO", "http://www.yahoo.com", "Yahoo", "28.04", "-0.17", "-0.6"},
  };
  return stocks;
}
const std::vector<Stock> kStocks = stocks_of();

Data data(const std::vector<Stock> &stocks)
{
  Data d;
  d.items.reserve(stocks.size());
  for (size_t i = 0; i < stocks.size(); i++) {
    const bool minus = stocks.at(i).change.front() == '-' || stocks.at(i).change == "0";
    d.items.push_back(Item{i % 2 == 0 ? "even" : "odd", std::to_string(i + 1), minus ? " class=\"minus\"" : "", stocks.at(i)});
  }
  return d;
}
const Data kData = data(kStocks);

std::string rendered(mustache::std_host::Host &h, size_t capacity)
{
  if (mustache::render<SOURCE>(h, kData, capacity) != mustache::Fault::none) std::abort();
  return std::move(h.answer);
}

void per_render(benchmark::State &state)
{
  mustache::std_host::Host h;
  size_t last = 64;
  for (auto _ : state) {
    const Data d = data(kStocks);
    if (mustache::render<SOURCE>(h, d, last) != mustache::Fault::none) std::abort();
    std::string a = std::move(h.answer);
    last = a.size();
    benchmark::DoNotOptimize(a.data());
    benchmark::ClobberMemory();
  }
}

void finished(benchmark::State &state)
{
  mustache::std_host::Host h;
  size_t last = 64;
  for (auto _ : state) {
    std::string a = rendered(h, last);
    last = a.size();
    benchmark::DoNotOptimize(a.data());
    benchmark::ClobberMemory();
  }
}
}
BENCHMARK(finished);
BENCHMARK(per_render);

int main(int argc, char **argv)
{
  std::ifstream f("kx.html");
  const std::string want{std::istreambuf_iterator<char>(f), {}};
  mustache::std_host::Host h;
  const std::string got = rendered(h, 64);
  if (got != want) { std::fprintf(stderr, "differs: %zu vs %zu\n", got.size(), want.size()); std::fwrite(got.data(),1,got.size(),stdout); return 1; }
  std::fprintf(stderr, "same bytes: %zu\n", got.size());
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
