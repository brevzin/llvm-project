#!/bin/bash
set -euo pipefail
# Build the native consteval plugin once, then compile & run the <meta>-free consumer.
DIR=$(cd "$(dirname "$0")" && pwd)
B=$(cd "$DIR/../../build" && pwd)
FLAGS=(-std=c++2d -freflection -stdlib=libc++ -nostdinc++
  -isystem "$B/include/x86_64-unknown-linux-gnu/c++/v1"
  -isystem "$B/include/c++/v1"
  -L "$B/lib/x86_64-unknown-linux-gnu"
  -Wl,-rpath,"$B/lib/x86_64-unknown-linux-gnu")

"$B/bin/clang++" "${FLAGS[@]}" -fconsteval-plugin -fPIC -shared -O2 \
  "$DIR/iter_iface_plugin.cpp" -o "$DIR/libiter_iface.so"

"$B/bin/clang++" "${FLAGS[@]}" -fconsteval-plugin-load="$DIR/libiter_iface.so" \
  "$DIR/iter_example.cpp" -o "$DIR/iter_example"

"$DIR/iter_example"
