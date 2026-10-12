set -u
out=result-$(date -u +%Y%m%dT%H%M%SZ)-one-container.txt
steal() { awk '/^cpu /{print $9}' /proc/stat; }
{ echo "one container for all C++ runs (podman exec), JVM on the host"
  s0=$(steal)
  echo "sysbench cpu events/s, 10 x 1 s:"; podman exec kxb sh -c 'for i in 1 2 3 4 5 6 7 8 9 10; do sysbench cpu --threads=1 --time=1 run | awk "/events per second/{printf \"%s \",\$4}"; done'; echo
  for r in 1 2 3 4 5; do
    for b in kx-g++-Os-a kx-g++-O2-a kx-g++-Os-b kx-g++-O2-b; do
      printf "round $r $b "; podman exec kxb nice -n -15 ./$b --benchmark_repetitions=5 --benchmark_min_time=0.3s --benchmark_report_aggregates_only=true 2>&1 | awk '/_median/{printf "%s %s ns ",$1,$2} /_cv/{printf "cv %s ",$2}'; echo
    done
    printf "round $r kotlinx "; nice -n -15 sh jvm.sh
  done
  echo "steal ticks during run: $(( $(steal) - s0 ))"; } > $out 2>&1
echo $out
