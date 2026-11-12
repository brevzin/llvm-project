#!/usr/bin/env python3
# Copyright 2026 Jump Trading LLC
#
# Writes build/clangd-reflection/compile_commands.json: a compile command for
# each libc++ reflection test, as lit compiles it (the libc++ test flags plus
# the test's own ADDITIONAL_COMPILE_FLAGS), so that clangd sees what the test
# needs (-fconsteval-operations, -foperator-for, ...). Re-run after adding a
# test or changing a test's flags. (.clangd points at the output directory.)

import json
import os
import re
import shlex

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(ROOT, "build")
TESTS = os.path.join(ROOT, "libcxx/test/std/experimental/reflection")
OUT = os.path.join(BUILD, "clangd-reflection")

# From `llvm-lit -a` on one of the tests, without -Werror (in the editor,
# warnings are warnings) or anything for linking.
BASE = [
    os.path.join(BUILD, "bin/clang++"),
    "--target=x86_64-unknown-linux-gnu",
    "-nostdinc++",
    "-isystem", os.path.join(BUILD, "include/x86_64-unknown-linux-gnu/c++/v1"),
    "-isystem", os.path.join(BUILD, "include/c++/v1"),
    "-I", os.path.join(ROOT, "libcxx/test/support"),
    "-std=c++26",
    "-Wall", "-Wextra", "-Wshadow", "-Wundef", "-Wunused-template",
    "-Wno-unused-command-line-argument", "-Wno-attributes",
    "-Wno-pessimizing-move", "-Wno-noexcept-type", "-Wno-atomic-alignment",
    "-Wno-reserved-module-identifier", "-Wno-user-defined-literals",
    "-Wno-tautological-compare", "-Wno-unused-local-typedef",
    "-Wno-local-type-template-args", "-Wno-c++11-extensions",
    "-Wno-unknown-pragmas", "-Wno-mismatched-new-delete", "-Wno-redundant-move",
    "-Wno-self-move", "-Wno-nullability-completeness",
    "-D_LIBCPP_HAS_NO_PRAGMA_SYSTEM_HEADER",
    "-D_LIBCPP_ENABLE_EXPERIMENTAL",
]

FLAGS = re.compile(r"^//\s*ADDITIONAL_COMPILE_FLAGS(?:\([^)]*\))?:(.*)$")


def test_flags(path):
    flags = []
    with open(path, errors="replace") as f:
        for line in f:
            m = FLAGS.match(line)
            if m:
                # Linker inputs don't matter to clangd.
                flags += [a for a in shlex.split(m.group(1))
                          if not a.startswith(("-l", "-L", "-Wl,"))]
    return flags


def main():
    entries = []
    for name in sorted(os.listdir(TESTS)):
        if not name.endswith((".cpp", ".cppm")):
            continue
        path = os.path.join(TESTS, name)
        entries.append({
            "directory": TESTS,
            "file": path,
            "arguments": BASE + test_flags(path) + ["-c", path],
        })
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "compile_commands.json"), "w") as f:
        json.dump(entries, f, indent=1)
    print(f"wrote {len(entries)} entries to {OUT}/compile_commands.json")


if __name__ == "__main__":
    main()
