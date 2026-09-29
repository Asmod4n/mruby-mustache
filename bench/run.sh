#!/bin/sh
# Measures the noise floor, then runs every binary: five repetitions,
# the arms interleaved at random, medians reported. The run asks for
# nice -15 and records whether it got it and as whom it ran.
set -u
dir=${1:-build-bench}
echo "ran_as=$(id -un) bench_nice=$(nice -n -15 nice 2>/dev/null || echo refused) cpus=$(nproc)"
echo "noise floor, sysbench cpu, events per second:"
for i in 1 2 3 4 5 6 7 8 9 10; do
  nice -n -15 sysbench cpu --threads=1 --time=1 run 2>/dev/null | awk '/events per second/ {printf "%s ", $4}'
done
echo
for f in "$dir"/render-*; do
  case $f in *.log) continue;; esac
  echo "== $(basename "$f")"
  nice -n -15 "$f" --benchmark_repetitions=5 --benchmark_enable_random_interleaving=true \
    --benchmark_report_aggregates_only=true --benchmark_min_time=0.3s 2>&1 | grep -E "output:|_median|_cv|WARNING"
done
