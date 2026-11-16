CXX ?= clang++
CXXFLAGS = -std=c++17 -O3 -march=native -Wall -Wno-unused-function

all: bin/fighter bin/arena bin/coach

bin/fighter: src/fighter.cpp src/board.hpp src/search.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ src/fighter.cpp

bin/arena: src/arena.cpp src/board.hpp src/search.hpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -pthread -o $@ src/arena.cpp

bin/coach: coach/coach.swift
	@mkdir -p bin
	swiftc -O -o $@ coach/coach.swift

perft: bin/fighter
	./bin/fighter perft 5

clean:
	rm -rf bin

.PHONY: all perft clean
