#!/bin/sh
# Builds one binary for each workload and each build: g++ and clang,
# -Os and -O2, -march=x86-64-v4, with the alignment flags on. Four
# builds run at a time.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$here/build-bench}
mkdir -p "$out"
jobs=""
for cxx in g++ clang++; do
  for level in -Os -O2; do
    for workload in 1 2 3; do
      jobs="$jobs $cxx:$level:$workload"
    done
  done
done
echo $jobs | tr ' ' '\n' | xargs -P 4 -I{} sh -c '
  cxx=$(echo {} | cut -d: -f1); level=$(echo {} | cut -d: -f2); workload=$(echo {} | cut -d: -f3)
  case $cxx in
    g++) std="-std=c++26 -freflection"; align="-falign-functions=64 -falign-loops=64 -falign-jumps=64";;
    *)   std="-std=c++26"; align="-falign-functions=64 -falign-loops=64";;
  esac
  name='"$out"'/render-$workload-$cxx$level
  $cxx $std $level -march=x86-64-v4 $align -DNDEBUG -DWORKLOAD=$workload -I'"$here"'/include \
    '"$here"'/bench/render.cpp '"$here"'/src/mustache-c.cpp -lbenchmark -lpthread -o $name 2> $name.log \
    || echo "$name: $(grep -m1 error $name.log)"
'
ls "$out" | grep -v log
