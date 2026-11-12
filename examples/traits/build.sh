#!/bin/bash
set -euo pipefail
# usage: ./build.sh range.cpp && ./range
B=$(cd "$(dirname "$0")/../../build" && pwd)
exec "$B/bin/clang++" -std=c++2d -freflection -stdlib=libc++ -nostdinc++ \
  -isystem "$B/include/x86_64-unknown-linux-gnu/c++/v1" -isystem "$B/include/c++/v1" \
  -L "$B/lib/x86_64-unknown-linux-gnu" -Wl,-rpath,"$B/lib/x86_64-unknown-linux-gnu" \
  "$1" -o "${1%.cpp}"
