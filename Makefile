CXX      := g++
CXXFLAGS := -std=c++20 -O2 -pthread -Wall -Wextra

COMMON   := ranker.cpp thread_pool.cpp

all: bench

bench: bench.cpp $(COMMON)
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -f bench main *.o

.PHONY: all clean
