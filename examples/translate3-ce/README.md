# translate3 on Compiler Explorer

A Compiler Explorer-ready variant of `examples/translate3`: same library,
adapted to CE's sandbox. Everything lives in one flat directory because CE's
project tree is flat.

## Differences from examples/translate3

- **Catalogs are `#embed`ed at build time** instead of loaded from disk at
  runtime. They are still separate `.po` files in the project, which makes
  for a good live demo: edit a Polish plural variant (or break a placeholder
  name and watch validation catch it), hit compile, output changes.
- **`.pot` extraction happens twice, on both of CE's surfaces** (verified
  working on godbolt.org: the build-time `.pot` appears in the CMake build
  log between the link line and "Built target"). At run
  time, `extracted_pot()` walks the program's own `i18n_messages` section
  between the linker-provided `__start_i18n_messages` /
  `__stop_i18n_messages` symbols ("the binary hands you its own translation
  template" -- shows up in the executor pane). And at build time, a CMake
  `POST_BUILD` step runs the same `objcopy | tr | awk` pipeline as
  `examples/translate3` and prints the `.pot` into the build log -- which is
  the honest version of the "extraction requires no execution" claim.
- Output is prints, not asserts; there's an `en.po` so the English plural
  table reads correctly on screen.

## Setting it up on godbolt.org

1. On [godbolt.org](https://godbolt.org), open the **Add...** menu (top
   left) and choose **Tree (IDE mode)**. A project-tree pane appears.
2. In the tree pane, enable the **CMake** toggle. CE now expects a
   `CMakeLists.txt` at the project root (it seeds a default one you can
   overwrite).
3. Create each file in the tree (`CMakeLists.txt`, `main.cpp`,
   `translate3.h`, `en.po`, `fr.po`, `pl.po`, `de.po`) and paste the
   contents from this directory. Files open as editor tabs attached to the
   tree.
4. From the tree pane, add a **compiler** and select the template-strings
   clang ("Barry prototypes"). All required flags are in `CMakeLists.txt`
   (`-std=c++26 -freflection -stdlib=libc++ -Wno-c23-extensions`), so the
   compiler options field can stay empty.
5. From the tree pane, add an **executor** to build and run the project.
   The tree's *CMake arguments* / *Custom output filename* fields can stay
   at their defaults: there is a single executable target (`demo`).
6. **Share** -> short link, as usual. The whole tree state is captured.

## Caveats

- The CE compiler must be built from a branch that includes the phase-4
  template string implementation (the name clause `{expr;"name"}`, `-E`
  round-tripping) *and* the `std::rebind_format` / `std::rebound_format`
  libc++ additions, which this demo now uses. An older deployment of the
  fork will not compile it.
- `#embed` resolves relative to the including file, which is why the `.po`
  files sit next to `main.cpp`.
- `__start_`/`__stop_` section symbols require the section name to be a
  valid C identifier (hence `i18n_messages`, no dots) and are an ELF/GNU-ld
  (and lld) feature -- fine on CE's Linux builders.

## Testing locally

```sh
mkdir build && cd build
cmake -G Ninja -DCMAKE_CXX_COMPILER=path/to/build/bin/clang++ ..
ninja
LD_LIBRARY_PATH=path/to/build/lib/x86_64-unknown-linux-gnu ./demo
```
