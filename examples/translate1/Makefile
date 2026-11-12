CXX ?= ../../build/bin/clang++
CXXFLAGS = -std=c++26 -stdlib=libc++ -freflection -Wall -Wextra
LDFLAGS ?= -Wl,-rpath,$(dir $(CXX))../lib/x86_64-unknown-linux-gnu

main: main.cpp translate.h
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ main.cpp

run: main
	./main

clean:
	rm -f main core.main.*

.PHONY: run clean
