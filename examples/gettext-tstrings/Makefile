CXX ?= clang++
CXXFLAGS = -std=c++26 -stdlib=libc++ -freflection -Wall -Wextra
LDFLAGS ?=

main: main.cpp
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $<

run: main
	./main

clean:
	rm -f main

.PHONY: run clean
