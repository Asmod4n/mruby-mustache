# One JMH run shaped like one Google Benchmark run with
# --benchmark_repetitions=5 --benchmark_min_time=0.3s:
# 5 forks = 5 repetitions, one 0.3 s measured iteration each,
# mean per repetition, then median and cv over the 5.
# The JIT warmup (5 x 1 s per fork) has no C++ counterpart and is not timed.
java -XX:ActiveProcessorCount=3 -jar build/libs/kx-jmh.jar -f 5 -wi 5 -w 1s -i 1 -r 300ms -bm avgt -tu ns -rf json -rff jmh.json >/dev/null 2>&1
python3 -I -c '
import json,statistics as s
for b in json.load(open("jmh.json")):
  r=[x[0] for x in b["primaryMetric"]["rawData"]]
  print(b["benchmark"].split(".")[-1], f"{s.median(r):.0f} ns median cv {100*s.stdev(r)/s.mean(r):.2f}", end="  ")
print()'
