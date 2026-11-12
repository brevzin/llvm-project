CXX ?= ../../build/bin/clang++
OBJCOPY ?= objcopy
CXXFLAGS = -std=c++26 -stdlib=libc++ -freflection -Wall -Wextra
LDFLAGS ?= -Wl,-rpath,$(dir $(CXX))../lib/x86_64-unknown-linux-gnu

main: main.cpp translate3.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ main.cpp

run: main
	./main

# Build-time extraction: the .pot template comes out of the binary's
# i18n_messages section. No execution required.
extract: main
	$(OBJCOPY) -O binary --only-section=i18n_messages main messages.bin
	tr -s '\0' '\n' < messages.bin | awk \
	  '{ rec = rec $$0 "\n" } /^msgstr ""/ { if (!(rec in seen)) { seen[rec]; printf "%s\n", rec } rec = "" }' \
	  > messages.pot
	@echo "--- messages.pot ---"
	@cat messages.pot

clean:
	rm -f main messages.bin messages.pot core.main.*

.PHONY: run extract clean
